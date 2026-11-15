#include "Learning/ReachTaskActor.h"
#include "Arena/ArenaBuilder.h"
#include "Character/BeanCharacter.h"
#include "Common/FPSAgentSettings.h"
#include "Common/FPSRunArgs.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FPSAgent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMisc.h"
#include "Learning/ReachLearningClasses.h"
#include "Learning/ReachTaskLogic.h"
#include "Learning/TrainingSession.h"
#include "LearningAgentsInteractor.h"
#include "LearningAgentsManager.h"
#include "LearningAgentsPolicy.h"
#include "LearningAgentsTrainer.h"
#include "LearningAgentsTrainingEnvironment.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace
{
	// Toy arena geometry (not a mechanic).
	constexpr float FloorThicknessCm = 50.f;
	constexpr float WallThicknessCm = 50.f;
	constexpr float WallHeightCm = 200.f;
}

AReachTaskActor::AReachTaskActor()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Manager = CreateDefaultSubobject<ULearningAgentsManager>(TEXT("LearningAgentsManager"));
}

FBox AReachTaskActor::ComputeBounds(int32 ArenaCount, const FReachTaskSettings& S, const FVector& Origin)
{
	FBox Box(ForceInit);
	const float Half = 0.5f * S.ArenaSizeCm + WallThicknessCm;
	for (const FVector& C : FPSReach::ArenaCenters(ArenaCount, S.ArenaSpacingCm, Origin))
	{
		Box += C + FVector(-Half, -Half, 0.f);
		Box += C + FVector(Half, Half, WallHeightCm);
	}
	return Box;
}

void AReachTaskActor::BeginPlay()
{
	Super::BeginPlay();
	const FFPSRunArgs& Args = FFPSRunArgs::Get();
	Settings = UFPSAgentSettings::Get().SpikeA;
	BaseSeed = Args.Seed;
	// No local training (hard rule 6): play-in-editor only ever plays the policy back.
	bInfer = Args.bInfer || GIsEditor;
	AgentCenterHeight = UFPSAgentSettings::Get().Body.StandingHalfHeight + 2.f;
	StepSeconds = 1.f / FMath::Max(1.f, FLearningAgentsTrainingGameSettings().FixedTimeStepFrequency);
	if (bInfer && !GIsEditor)
	{
		// Headless playback/eval: same fixed step as training, uncapped, so episodes mean the same thing.
		// (Training gets this from Learning Agents' BeginTraining.)
		FApp::SetUseFixedTimeStep(true);
		FApp::SetFixedDeltaTime(StepSeconds);
		if (IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))) { MaxFps->Set(0.f); }
	}
	Centers = FPSReach::ArenaCenters(Args.Arenas, Settings.ArenaSpacingCm, GetActorLocation() + FVector(0.f, 0.f, AgentCenterHeight));
	Agents.SetNum(Centers.Num());

	BuildArenas();
	if (!SetupLearning())
	{
		return;
	}
	SpawnAgents();
	bReady = true;
	NextStatsTime = GetWorld()->GetTimeSeconds() + Settings.StatsLogIntervalSeconds;
	WindowWallStart = FPlatformTime::Seconds();
	UE_LOG(LogFPSAgent, Display, TEXT("SpikeA: ready. arenas=%d mode=%s resume=%s maxEpisodeSteps=%d reachRadius=%.0f target=%.0f..%.0fcm"),
		Centers.Num(), Args.bScripted ? TEXT("scripted (no policy)") : bInfer ? TEXT("infer") : TEXT("train (external trainer)"),
		Args.ResumeFrom.IsEmpty() ? TEXT("(none)") : *Args.ResumeFrom, Settings.MaxEpisodeSteps, Settings.ReachRadiusCm,
		Settings.TargetMinDistanceCm, Settings.TargetMaxDistanceCm);
}

void AReachTaskActor::BuildArenas()
{
	UWorld* World = GetWorld();
	const float Size = Settings.ArenaSizeCm;
	const float Outer = Size + 2.f * WallThicknessCm;
	const float WallOffset = 0.5f * Size + 0.5f * WallThicknessCm;
	for (const FVector& Center : Centers)
	{
		const FVector FloorTop = Center - FVector(0.f, 0.f, AgentCenterHeight);
		const FVector WallUp(0.f, 0.f, 0.5f * WallHeightCm);
		FPSArena::SpawnBlock(World, FloorTop - FVector(0.f, 0.f, 0.5f * FloorThicknessCm), FVector(Outer, Outer, FloorThicknessCm));
		FPSArena::SpawnBlock(World, FloorTop + FVector(WallOffset, 0.f, 0.f) + WallUp, FVector(WallThicknessCm, Outer, WallHeightCm));
		FPSArena::SpawnBlock(World, FloorTop + FVector(-WallOffset, 0.f, 0.f) + WallUp, FVector(WallThicknessCm, Outer, WallHeightCm));
		FPSArena::SpawnBlock(World, FloorTop + FVector(0.f, WallOffset, 0.f) + WallUp, FVector(Outer, WallThicknessCm, WallHeightCm));
		FPSArena::SpawnBlock(World, FloorTop + FVector(0.f, -WallOffset, 0.f) + WallUp, FVector(Outer, WallThicknessCm, WallHeightCm));
	}

	if (!FApp::CanEverRender())
	{
		return; // markers are only for watching
	}
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	for (const FVector& Center : Centers)
	{
		UStaticMeshComponent* Marker = NewObject<UStaticMeshComponent>(this);
		Marker->SetMobility(EComponentMobility::Movable);
		Marker->SetStaticMesh(Sphere);
		Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Marker->SetupAttachment(RootComponent);
		Marker->RegisterComponent();
		Marker->SetWorldLocationAndRotation(Center, FRotator::ZeroRotator);
		Marker->SetWorldScale3D(FVector(0.6f));
		Markers.Add(Marker);
	}
}

bool AReachTaskActor::SetupLearning()
{
	const FFPSRunArgs& Args = FFPSRunArgs::Get();
	Manager->SetMaxAgentNum(Agents.Num());
	FTrainingSessionConfig Config;
	Config.LogPrefix = TEXT("SpikeA");
	Config.InteractorClass = UReachInteractor::StaticClass();
	Config.EnvClass = UReachTrainingEnv::StaticClass();
	Config.bInfer = bInfer;
	Config.Seed = BaseSeed;
	Config.ResumeFrom = Args.ResumeFrom;
	Config.bResetAgentsOnUpdate = true;
	Config.Trainer.MaxEpisodeStepNum = Settings.MaxEpisodeSteps;
	FLearningAgentsPPOTrainingSettings& T = Config.Training;
	// The trainer counts gradient iterations, IterationsPerGather (32) per experience batch (C-005); -Batches converts.
	T.NumberOfIterations = Args.GradientIterations(T.IterationsPerGather, Settings.MaxIterations);
	T.bSaveSnapshots = true;
	T.IterationsPerSnapshot = FMath::Max(1, Settings.IterationsPerSnapshot);
	T.bUseTensorboard = Settings.bUseTensorboard;
	T.Device = Settings.bTrainOnGpu ? ELearningAgentsTrainingDevice::GPU : ELearningAgentsTrainingDevice::CPU;
	T.RandomSeed = BaseSeed;
	Session = NewObject<UTrainingSession>(this);
	if (!Session->Setup(*Manager, Config))
	{
		bExiting = true;
		return false;
	}
	return true;
}

void AReachTaskActor::SpawnAgents()
{
	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Beans.SetNum(Centers.Num());
	for (int32 Arena = 0; Arena < Centers.Num(); ++Arena)
	{
		ABeanCharacter* Bean = World->SpawnActor<ABeanCharacter>(Centers[Arena], FRotator::ZeroRotator, Params);
		if (!Bean)
		{
			UE_LOG(LogFPSAgent, Error, TEXT("SpikeA: could not spawn the agent for arena %d"), Arena);
			continue;
		}
		Bean->SpawnDefaultController(); // an AI controller: CharacterMovement only simulates controlled pawns
		Beans[Arena] = Bean;
		const int32 Id = Manager->AddAgent(Bean);
		if (!Agents.IsValidIndex(Id))
		{
			UE_LOG(LogFPSAgent, Error, TEXT("SpikeA: manager returned agent id %d for arena %d"), Id, Arena);
			continue;
		}
		Agents[Id].Arena = Arena;
		ResetAgent(Id);
	}
}

ABeanCharacter* AReachTaskActor::BeanOf(int32 AgentId) const
{
	if (!Agents.IsValidIndex(AgentId) || !Beans.IsValidIndex(Agents[AgentId].Arena))
	{
		return nullptr;
	}
	return Beans[Agents[AgentId].Arena];
}

FVector AReachTaskActor::GetAgentLocation(int32 AgentId) const
{
	const ABeanCharacter* Bean = BeanOf(AgentId);
	return Bean ? Bean->GetActorLocation() : FVector::ZeroVector;
}

FVector AReachTaskActor::GetAgentVelocity(int32 AgentId) const
{
	const ABeanCharacter* Bean = BeanOf(AgentId);
	return Bean ? Bean->GetVelocity() : FVector::ZeroVector;
}

FVector AReachTaskActor::GetTarget(int32 AgentId) const
{
	return Agents.IsValidIndex(AgentId) ? Agents[AgentId].Target : FVector::ZeroVector;
}

float AReachTaskActor::Distance2D(int32 AgentId) const
{
	return FVector::Dist2D(GetAgentLocation(AgentId), GetTarget(AgentId));
}

void AReachTaskActor::ApplyMove(int32 AgentId, float X, float Y)
{
	if (!Agents.IsValidIndex(AgentId))
	{
		return;
	}
	Agents[AgentId].LastMove = FVector(X, Y, 0.f).GetClampedToMaxSize(1.0);
	if (ABeanCharacter* Bean = BeanOf(AgentId))
	{
		Bean->AddMovementInput(Agents[AgentId].LastMove, 1.f);
	}
}

float AReachTaskActor::TakeStepReward(int32 AgentId)
{
	if (!Agents.IsValidIndex(AgentId) || Agents[AgentId].Arena == INDEX_NONE)
	{
		return 0.f;
	}
	++WindowSteps;
	return Agents[AgentId].Ep.Score(Distance2D(AgentId), StepSeconds, Settings);
}

bool AReachTaskActor::IsAgentAtTarget(int32 AgentId) const
{
	return Agents.IsValidIndex(AgentId) && Agents[AgentId].Arena != INDEX_NONE
		&& FPSReach::FReachEpisode::IsTerminal(Distance2D(AgentId), Settings);
}

void AReachTaskActor::ResetAgent(int32 AgentId)
{
	if (!Agents.IsValidIndex(AgentId) || Agents[AgentId].Arena == INDEX_NONE)
	{
		return;
	}
	FAgentState& A = Agents[AgentId];
	if (A.Ep.Steps > 0)
	{
		RecordEpisodeEnd(A);
	}
	if (TotalEpisodes <= 6)
	{
		UE_LOG(LogFPSAgent, Display, TEXT("SpikeA: reset agent %d (episode %d): steps=%d reached=%d distance=%.0f return=%.3f"),
			AgentId, A.EpisodeCount, A.Ep.Steps, A.Ep.bReached ? 1 : 0, Distance2D(AgentId), A.Ep.Return);
	}
	FRandomStream Rng(FPSReach::EpisodeSeed(BaseSeed, A.Arena, A.EpisodeCount));
	++A.EpisodeCount;
	const FVector Center = Centers[A.Arena];
	const FVector Start = FPSReach::SampleStart(Rng, Center, Settings);
	A.Target = FPSReach::SampleTarget(Rng, Center, Start, Settings);
	A.LastMove = FVector::ZeroVector;
	A.Ep.Begin(FVector::Dist2D(Start, A.Target));

	if (ABeanCharacter* Bean = BeanOf(AgentId))
	{
		Bean->SetActorLocationAndRotation(Start, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
		if (UCharacterMovementComponent* Move = Bean->GetCharacterMovement())
		{
			Move->StopMovementImmediately();
		}
	}
	if (Markers.IsValidIndex(A.Arena) && Markers[A.Arena])
	{
		Markers[A.Arena]->SetWorldLocation(A.Target);
	}
}

void AReachTaskActor::RecordEpisodeEnd(const FAgentState& A)
{
	++TotalEpisodes;
	++WindowEpisodes;
	WindowReached += A.Ep.bReached ? 1 : 0;
	WindowReturn += A.Ep.Return;
}

void AReachTaskActor::LogStats()
{
	const double ReachedPct = WindowEpisodes > 0 ? 100.0 * WindowReached / WindowEpisodes : 0.0;
	const double AvgReturn = WindowEpisodes > 0 ? WindowReturn / WindowEpisodes : 0.0;
	const double Now = FPlatformTime::Seconds();
	const double Wall = FMath::Max(1e-6, Now - WindowWallStart);
	UE_LOG(LogFPSAgent, Display, TEXT("SpikeA stats: gameTime=%.0fs mode=%s episodes=%d total=%d reached=%.1f%% avgReturn=%.3f agentSteps/s(wall)=%.0f"),
		GetWorld()->GetTimeSeconds(), bInfer ? TEXT("infer") : TEXT("train"), WindowEpisodes, TotalEpisodes, ReachedPct, AvgReturn,
		WindowSteps / Wall);
	WindowEpisodes = 0;
	WindowReached = 0;
	WindowReturn = 0.0;
	WindowSteps = 0;
	WindowWallStart = Now;
}

void AReachTaskActor::ExitWith(int32 Code, const TCHAR* Reason)
{
	bExiting = true;
	UTrainingSession::ExitProcess(TEXT("SpikeA"), Code, Reason);
}

void AReachTaskActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bReady || bExiting)
	{
		return;
	}

	if (bInfer)
	{
		// Policy steps at 60 Hz whatever the frame rate (headless it is exactly one step per fixed tick).
		InferClock += DeltaSeconds;
		if (bInferStepped && InferClock + 1e-6 < StepSeconds)
		{
			for (FAgentState& A : Agents)
			{
				const int32 Id = &A - Agents.GetData();
				if (ABeanCharacter* Bean = BeanOf(Id)) { Bean->AddMovementInput(A.LastMove, 1.f); } // hold the action
			}
		}
		else
		{
			InferClock = FMath::Min(InferClock - StepSeconds, static_cast<double>(StepSeconds));
			InferClock = FMath::Max(InferClock, 0.0);
			// Score the step that just ran, end finished episodes, then act again (mean action, no exploration noise).
			if (bInferStepped)
			{
				for (int32 Id = 0; Id < Agents.Num(); ++Id)
				{
					if (Agents[Id].Arena == INDEX_NONE) { continue; }
					TakeStepReward(Id);
					if (IsAgentAtTarget(Id) || Agents[Id].Ep.Steps >= Settings.MaxEpisodeSteps) { ResetAgent(Id); }
				}
			}
			if (FFPSRunArgs::Get().bScripted)
			{
				for (int32 Id = 0; Id < Agents.Num(); ++Id)
				{
					const FVector ToTarget = (GetTarget(Id) - GetAgentLocation(Id)).GetSafeNormal2D();
					ApplyMove(Id, ToTarget.X, ToTarget.Y);
				}
			}
			else
			{
				Session->GetPolicy()->RunInference(0.f);
			}
			bInferStepped = true;
		}
	}
	else
	{
		switch (Session->RunTrainingStep())
		{
		case UTrainingSession::EStatus::Failed:
			LogStats();
			ExitWith(1, TEXT("training failed: trainer unreachable, timed out or crashed (see LogLearning above)"));
			return;
		case UTrainingSession::EStatus::Finished:
			LogStats();
			ExitWith(0, TEXT("trainer finished"));
			return;
		default:
			break;
		}
	}

	if (GetWorld()->GetTimeSeconds() >= NextStatsTime)
	{
		LogStats();
		NextStatsTime += Settings.StatsLogIntervalSeconds;
	}
}
