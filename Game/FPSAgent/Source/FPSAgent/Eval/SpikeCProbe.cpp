#include "Eval/SpikeCProbe.h"
#include "Arena/FloorGenerator.h"
#include "Arena/MapBuilder.h"
#include "Character/BeanAIController.h"
#include "Character/BeanCharacter.h"
#include "Common/FPSAgentSettings.h"
#include "Engine/World.h"
#include "FPSAgent.h"
#include "HAL/IConsoleManager.h"
#include "Learning/TrainingSession.h"
#include "LearningAgentsTrainer.h"
#include "Misc/App.h"
#include "Weapons/RifleComponent.h"

namespace
{
	// The probe procedure (not mechanics): phase lengths, how many shots and taps are measured, and the pass tolerance.
	constexpr int32 ProbeFloorTiles = 20;        // 80 m floor
	constexpr float StartBackCm = 3000.f;        // start 30 m behind the centre; the three move phases cover about 31 m
	constexpr double SettleSeconds = 0.5;
	constexpr double MovePhaseSeconds = 3.0;
	constexpr double MeasureSeconds = 1.0;       // average speed over the last second of each move phase
	constexpr double StopSeconds = 1.0;
	constexpr double HoldSeconds = 3.0;
	constexpr int32 FireRateShots = 20;          // fewer than the 25-round magazine
	constexpr int32 TapCount = 8;
	constexpr double TapIntervalSeconds = 0.5;   // far slower than the fire rate: every tap should fire exactly one shot
	constexpr double ToleranceFraction = 0.02;   // 60 Hz tick quantisation is under 1% over 20 shots
}

ASpikeCProbe::ASpikeCProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASpikeCProbe::BeginPlay()
{
	Super::BeginPlay();
	TickSeconds = 1.f / FMath::Max(1.f, FLearningAgentsTrainingGameSettings().FixedTimeStepFrequency);
	if (!GIsEditor)
	{
		// The same fixed step as training and headless playback (D-019), uncapped.
		FApp::SetUseFixedTimeStep(true);
		FApp::SetFixedDeltaTime(TickSeconds);
		if (IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))) { MaxFps->Set(0.f); }
	}
	const FMapSpec Spec = FPSArena::MakeOpenFloorSpec(ProbeFloorTiles, TEXT("spikeC"));
	FPSArena::BuildMap(GetWorld(), Spec, GetActorLocation() - Spec.Center());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Start = GetActorLocation() + FVector(-StartBackCm, 0.0, UFPSAgentSettings::Get().Body.StandingHalfHeight + 2.f);
	Bean = GetWorld()->SpawnActor<ABeanCharacter>(Start, FRotator::ZeroRotator, Params);
	if (!Bean)
	{
		UTrainingSession::ExitProcess(TEXT("SpikeC"), 1, TEXT("could not spawn the probe bean"));
		Phase = EPhase::Done;
		return;
	}
	Bean->AIControllerClass = ABeanAIController::StaticClass();
	Bean->SpawnDefaultController(); // CharacterMovement only simulates controlled pawns
	Bean->GetRifle()->OnShotFired.AddUObject(this, &ASpikeCProbe::OnShot);
	Enter(EPhase::Settle);
	UE_LOG(LogFPSAgent, Display, TEXT("SpikeC probe: started (tick %.5f s, fixed step %d)"), TickSeconds, FApp::UseFixedTimeStep() ? 1 : 0);
}

void ASpikeCProbe::Enter(EPhase Next)
{
	Phase = Next;
	PhaseStart = GetWorld()->GetTimeSeconds();
	SpeedSum = 0.0;
	SpeedSamples = 0;
	URifleComponent* Rifle = Bean->GetRifle();
	switch (Next)
	{
	case EPhase::Walk:
		Bean->SetWalking(true);
		break;
	case EPhase::Crouch:
		Bean->SetWalking(false);
		Bean->Crouch();
		break;
	case EPhase::Stop:
		Bean->UnCrouch();
		break;
	case EPhase::Hold:
		HoldShotTimes.Reset();
		Rifle->ResetWeapon();
		Rifle->SetTriggerHeld(true);
		break;
	case EPhase::Taps:
		Rifle->ResetWeapon(); // full magazine, trigger released
		TapsRequested = 0;
		TapShots = 0;
		bTapHeld = false;
		NextTapTime = PhaseStart + TapIntervalSeconds;
		break;
	default:
		break;
	}
}

void ASpikeCProbe::MoveAndMeasure(double PhaseTime, float& OutSpeed, EPhase Next)
{
	Bean->AddMovementInput(FVector::ForwardVector, 1.f);
	if (PhaseTime >= MovePhaseSeconds - MeasureSeconds)
	{
		SpeedSum += Bean->GetHorizontalSpeed();
		++SpeedSamples;
	}
	if (PhaseTime >= MovePhaseSeconds)
	{
		OutSpeed = SpeedSamples > 0 ? static_cast<float>(SpeedSum / SpeedSamples) : 0.f;
		Enter(Next);
	}
}

void ASpikeCProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Bean || Phase == EPhase::Done)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const double T = Now - PhaseStart;
	switch (Phase)
	{
	case EPhase::Settle: if (T >= SettleSeconds) { Enter(EPhase::Run); } break;
	case EPhase::Run:    MoveAndMeasure(T, RunSpeed, EPhase::Walk); break;
	case EPhase::Walk:   MoveAndMeasure(T, WalkSpeed, EPhase::Crouch); break;
	case EPhase::Crouch: MoveAndMeasure(T, CrouchSpeed, EPhase::Stop); break;
	case EPhase::Stop:   if (T >= StopSeconds) { Enter(EPhase::Hold); } break;
	case EPhase::Hold:   if (T >= HoldSeconds) { Enter(EPhase::Taps); } break;
	case EPhase::Taps:
		if (bTapHeld)
		{
			Bean->GetRifle()->SetTriggerHeld(false); // a tap is one tick of trigger
			bTapHeld = false;
		}
		else if (TapsRequested < TapCount && Now >= NextTapTime)
		{
			Bean->GetRifle()->SetTriggerHeld(true);
			bTapHeld = true;
			++TapsRequested;
			NextTapTime = Now + TapIntervalSeconds;
		}
		else if (TapsRequested >= TapCount && Now >= NextTapTime)
		{
			Finish();
		}
		break;
	default:
		break;
	}
}

void ASpikeCProbe::OnShot(const FShotResult& Shot)
{
	if (Phase == EPhase::Hold)
	{
		HoldShotTimes.Add(GetWorld()->GetTimeSeconds());
	}
	else if (Phase == EPhase::Taps)
	{
		++TapShots;
	}
}

void ASpikeCProbe::Finish()
{
	Phase = EPhase::Done;
	const UFPSAgentSettings& S = UFPSAgentSettings::Get();
	const int32 N = FMath::Min(HoldShotTimes.Num(), FireRateShots);
	const double Span = N >= 2 ? HoldShotTimes[N - 1] - HoldShotTimes[0] : 0.0;
	const double FireRate = Span > 0.0 ? (N - 1) / Span : 0.0;
	auto Within = [](double Measured, double Config) { return FMath::Abs(Measured - Config) <= ToleranceFraction * FMath::Max(1e-6, FMath::Abs(Config)); };
	const bool bPass = Within(RunSpeed, S.Movement.RunSpeed) && Within(WalkSpeed, S.Movement.WalkSpeed)
		&& Within(CrouchSpeed, S.Movement.CrouchSpeed) && Within(FireRate, S.Rifle.FireRate) && TapShots == TapsRequested;
	UE_LOG(LogFPSAgent, Display, TEXT("SpikeC probe: run=%.2f (cfg %.2f) walk=%.2f (cfg %.2f) crouch=%.2f (cfg %.2f) cm/s; fireRate=%.3f (cfg %.3f) rounds/s over %d shots; taps=%d/%d; tick=%.5f s; verdict=%s"),
		RunSpeed, S.Movement.RunSpeed, WalkSpeed, S.Movement.WalkSpeed, CrouchSpeed, S.Movement.CrouchSpeed,
		FireRate, S.Rifle.FireRate, N, TapShots, TapsRequested, TickSeconds, bPass ? TEXT("PASS") : TEXT("MISMATCH"));
	UTrainingSession::ExitProcess(TEXT("SpikeC"), 0, bPass ? TEXT("probe finished: PASS") : TEXT("probe finished: MISMATCH (see the probe line)"));
}
