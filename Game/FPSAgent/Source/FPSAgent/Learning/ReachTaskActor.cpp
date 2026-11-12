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
#include "LearningAgentsCommunicator.h"
#include "LearningAgentsCritic.h"
#include "LearningAgentsInteractor.h"
#include "LearningAgentsManager.h"
#include "LearningAgentsNeuralNetwork.h"
#include "LearningAgentsPolicy.h"
#include "LearningAgentsTrainer.h"
#include "LearningAgentsTrainingEnvironment.h"
#include "Async/Async.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include <atomic>

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
	if (!bInfer)
	{
		UE_LOG(LogFPSAgent, Display, TEXT("SpikeA: trainer settings: iterations=%d snapshotEvery=%d device=%s tensorboard=%d seed=%d"),
			TrainingSettings.NumberOfIterations, TrainingSettings.IterationsPerSnapshot,
			Settings.bTrainOnGpu ? TEXT("GPU") : TEXT("CPU"), Settings.bUseTensorboard ? 1 : 0, BaseSeed);
	}
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

	// The Make* helpers take UPARAM(ref) pointers, so they need plain local variables of the base types.
	ULearningAgentsManager* Mgr = Manager;
	ULearningAgentsInteractor* I = ULearningAgentsInteractor::MakeInteractor(Mgr, UReachInteractor::StaticClass(), TEXT("ReachInteractor"));
	ULearningAgentsPolicy* P = I ? ULearningAgentsPolicy::MakePolicy(Mgr, I, ULearningAgentsPolicy::StaticClass(), TEXT("Policy"),
		nullptr, nullptr, nullptr, true, true, true, FLearningAgentsPolicySettings(), BaseSeed) : nullptr;
	Interactor = I;
	Policy = P;
	if (!I || !P)
	{
		ExitWith(1, TEXT("Learning Agents interactor/policy setup failed"));
		return false;
	}

	ULearningAgentsCritic* C = nullptr;
	if (!bInfer)
	{
		C = ULearningAgentsCritic::MakeCritic(Mgr, I, P, ULearningAgentsCritic::StaticClass(), TEXT("Critic"),
			nullptr, true, FLearningAgentsCriticSettings(), BaseSeed);
		Critic = C;
	}

	if (!Args.ResumeFrom.IsEmpty() && !LoadSnapshots(Args.ResumeFrom))
	{
		ExitWith(2, TEXT("-ResumeFrom is missing snapshot files"));
		return false;
	}
	if (bInfer)
	{
		return true;
	}

	ULearningAgentsTrainingEnvironment* E = ULearningAgentsTrainingEnvironment::MakeTrainingEnvironment(Mgr, UReachTrainingEnv::StaticClass(), TEXT("ReachEnv"));
	Env = E;

	FLearningAgentsTrainerProcessSettings Process;
	Process.TaskName = TEXT("FPSAgent");
	FLearningAgentsSocketCommunicatorSettings Socket;
	Socket.bUseExternalTrainingProcess = true; // never spawn a local trainer; address/port come from -LearningAgents* flags

	// Watchdog. Learning Agents' connect loop counts only its 1 ms sleeps toward the timeout, not the time spent inside
	// each blocking connect(), so an unreachable trainer can block this thread far longer than -LearningAgentsTimeout
	// (hours on Windows, minutes per attempt behind a firewall). A real wall-clock deadline force-exits with code 1.
	// We must not probe the trainer port ourselves: the Python trainer wedges on a connection that closes before
	// sending its config.
	float ConnectTimeout = Socket.Timeout;
	FParse::Value(FCommandLine::Get(), TEXT("LearningAgentsTimeout="), ConnectTimeout);
	const TSharedRef<std::atomic<bool>> bConnectDone = MakeShared<std::atomic<bool>>(false);
	if (!GIsEditor)
	{
		const double Deadline = FMath::Max(1.0, static_cast<double>(ConnectTimeout)) + 15.0;
		Async(EAsyncExecution::Thread, [bConnectDone, Deadline]()
		{
			const double Start = FPlatformTime::Seconds();
			while (!bConnectDone->load())
			{
				if (FPlatformTime::Seconds() - Start > Deadline)
				{
					UE_LOG(LogFPSAgent, Error, TEXT("SpikeA: exiting (code 1): no trainer connection after %.0f s (wall clock). Start the trainer first and wait for 'Server listening on'."), Deadline);
					GLog->Flush();
					FPlatformMisc::RequestExitWithStatus(true, 1);
					return;
				}
				FPlatformProcess::Sleep(0.2f);
			}
		});
	}
	const FLearningAgentsCommunicator Communicator = ULearningAgentsCommunicatorLibrary::MakeSocketTrainingProcess(Process, Socket);
	bConnectDone->store(true);

	FLearningAgentsPPOTrainerSettings TrainerSettings;
	TrainerSettings.MaxEpisodeStepNum = Settings.MaxEpisodeSteps;
	Trainer = ULearningAgentsPPOTrainer::MakePPOTrainer(Mgr, I, E, P, C, Communicator,
		ULearningAgentsPPOTrainer::StaticClass(), TEXT("PPOTrainer"), TrainerSettings);
	if (!E || !C || !Trainer)
	{
		ExitWith(1, TEXT("Learning Agents trainer setup failed"));
		return false;
	}

	TrainingSettings.NumberOfIterations = Args.Iterations > 0 ? Args.Iterations : Settings.MaxIterations;
	TrainingSettings.bSaveSnapshots = true;
	TrainingSettings.IterationsPerSnapshot = FMath::Max(1, Settings.IterationsPerSnapshot);
	TrainingSettings.bUseTensorboard = Settings.bUseTensorboard;
	TrainingSettings.Device = Settings.bTrainOnGpu ? ELearningAgentsTrainingDevice::GPU : ELearningAgentsTrainingDevice::CPU;
	TrainingSettings.RandomSeed = BaseSeed;
	return true;
}

bool AReachTaskActor::LoadSnapshots(const FString& Dir)
{
	TArray<TPair<FString, ULearningAgentsNeuralNetwork*>> Items;
	Items.Emplace(TEXT("encoder.bin"), Policy->GetEncoderNetworkAsset());
	Items.Emplace(TEXT("policy.bin"), Policy->GetPolicyNetworkAsset());
	Items.Emplace(TEXT("decoder.bin"), Policy->GetDecoderNetworkAsset());
	if (Critic)
	{
		Items.Emplace(TEXT("critic.bin"), Critic->GetCriticNetworkAsset());
	}
	for (const TPair<FString, ULearningAgentsNeuralNetwork*>& Item : Items)
	{
		const FString Path = FPaths::Combine(Dir, Item.Key);
		if (!Item.Value || !FPaths::FileExists(Path))
		{
			UE_LOG(LogFPSAgent, Error, TEXT("SpikeA: snapshot not found: %s"), *Path);
			return false;
		}
	}
	for (const TPair<FString, ULearningAgentsNeuralNetwork*>& Item : Items)
	{
		FFilePath File;
		File.FilePath = FPaths::Combine(Dir, Item.Key);
		Item.Value->LoadNetworkFromSnapshot(File);
	}
	UE_LOG(LogFPSAgent, Display, TEXT("SpikeA: loaded %d snapshots from %s"), Items.Num(), *Dir);
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

void AReachTaskActor::NoteDecision(int32 AgentId)
{
	if (Agents.IsValidIndex(AgentId))
	{
		Agents[AgentId].DecisionDistance = Distance2D(AgentId);
	}
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
	FAgentState& A = Agents[AgentId];
	const float Distance = Distance2D(AgentId);
	A.bReached = FPSReach::IsReached(Distance, Settings);
	const float Reward = FPSReach::StepReward(A.DecisionDistance, Distance, A.bReached, StepSeconds, Settings);
	A.DecisionDistance = Distance; // progress is counted once, even when no observation follows (scripted mode)
	A.Return += Reward;
	++A.Steps;
	++WindowSteps;
	return Reward;
}

bool AReachTaskActor::IsAgentReached(int32 AgentId) const
{
	return Agents.IsValidIndex(AgentId) && Agents[AgentId].bReached;
}

void AReachTaskActor::ResetAgent(int32 AgentId)
{
	if (!Agents.IsValidIndex(AgentId) || Agents[AgentId].Arena == INDEX_NONE)
	{
		return;
	}
	FAgentState& A = Agents[AgentId];
	if (A.Steps > 0)
	{
		RecordEpisodeEnd(A);
	}
	if (TotalEpisodes <= 6)
	{
		UE_LOG(LogFPSAgent, Display, TEXT("SpikeA: reset agent %d (episode %d): steps=%d reached=%d distance=%.0f return=%.3f"),
			AgentId, A.Episode, A.Steps, A.bReached ? 1 : 0, Distance2D(AgentId), A.Return);
	}
	FRandomStream Rng(FPSReach::EpisodeSeed(BaseSeed, A.Arena, A.Episode));
	++A.Episode;
	const FVector Center = Centers[A.Arena];
	const FVector Start = FPSReach::SampleStart(Rng, Center, Settings);
	A.Target = FPSReach::SampleTarget(Rng, Center, Start, Settings);
	A.Steps = 0;
	A.Return = 0.f;
	A.bReached = false;
	A.LastMove = FVector::ZeroVector;
	A.DecisionDistance = FVector::Dist2D(Start, A.Target);

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
	WindowReached += A.bReached ? 1 : 0;
	WindowReturn += A.Return;
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
	if (Code == 0)
	{
		UE_LOG(LogFPSAgent, Display, TEXT("SpikeA: exiting (code 0): %s"), Reason);
	}
	else
	{
		UE_LOG(LogFPSAgent, Error, TEXT("SpikeA: exiting (code %d): %s"), Code, Reason);
	}
	if (!GIsEditor)
	{
		FPlatformMisc::RequestExitWithStatus(false, static_cast<uint8>(Code));
	}
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
					if (Agents[Id].bReached || Agents[Id].Steps >= Settings.MaxEpisodeSteps) { ResetAgent(Id); }
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
				Policy->RunInference(0.f);
			}
			bInferStepped = true;
		}
	}
	else
	{
		// RunTraining would start a NEW session if called after the trainer finished, so stop as soon as it ends.
		Trainer->RunTraining(TrainingSettings, FLearningAgentsTrainingGameSettings(), true, true);
		if (Trainer->HasTrainingFailed())
		{
			LogStats();
			ExitWith(1, TEXT("training failed: trainer unreachable, timed out or crashed (see LogLearning above)"));
			return;
		}
		if (Trainer->IsTraining())
		{
			bTrainingStarted = true;
		}
		else if (bTrainingStarted)
		{
			LogStats();
			ExitWith(0, TEXT("trainer finished"));
			return;
		}
	}

	if (GetWorld()->GetTimeSeconds() >= NextStatsTime)
	{
		LogStats();
		NextStatsTime += Settings.StatsLogIntervalSeconds;
	}
}
