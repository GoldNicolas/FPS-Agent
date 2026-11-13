#include "Learning/CombatTaskActor.h"
#include "Arena/ArenaGameMode.h"
#include "Arena/MapSpec.h"
#include "Character/BeanCharacter.h"
#include "Common/FPSAgentSettings.h"
#include "Common/FPSRunArgs.h"
#include "Engine/World.h"
#include "FPSAgent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Learning/CombatLearningClasses.h"
#include "Learning/ReachTaskLogic.h"
#include "Learning/TrainingSession.h"
#include "LearningAgentsManager.h"
#include "LearningAgentsPolicy.h"
#include "LearningAgentsTrainer.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"

ACombatTaskActor::ACombatTaskActor()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Manager = CreateDefaultSubobject<ULearningAgentsManager>(TEXT("LearningAgentsManager"));
}

FBox ACombatTaskActor::ComputeBounds(int32 ArenaCount, const FVector& Origin, int32 StageIndex)
{
	FBox Box(ForceInit);
	const float Half = UCombatArena::FloorHalfSizeCm(StageIndex);
	for (const FVector& C : FPSReach::ArenaCenters(ArenaCount, UFPSAgentSettings::Get().Combat.ArenaSpacingCm, Origin))
	{
		Box += C + FVector(-Half, -Half, 0.0);
		Box += C + FVector(Half, Half, FMapSpec().WallCm);
	}
	return Box;
}

double ACombatTaskActor::Now() const
{
	return GetWorld()->GetTimeSeconds();
}

void ACombatTaskActor::Exit(int32 Code, const FString& Reason)
{
	bExiting = true;
	UTrainingSession::ExitProcess(TEXT("Combat"), Code, Reason);
}

void ACombatTaskActor::BeginPlay()
{
	Super::BeginPlay();
	const FFPSRunArgs& Args = FFPSRunArgs::Get();
	const UFPSAgentSettings& S = UFPSAgentSettings::Get();
	StageIndex = AArenaGameMode::RequestedCombatStage();
	if (StageIndex < 0 || StageIndex > 1)
	{
		Exit(2, FString::Printf(TEXT("stage S%d is not in this build (build B has S0 and S1)"), StageIndex));
		return;
	}
	const FCombatStageSpec& Stage = S.Curriculum.Stage(StageIndex);
	if (S.Combat.ArenaSpacingCm < 2.f * UCombatArena::FloorHalfSizeCm(StageIndex) + 5000.f)
	{
		Exit(2, FString::Printf(TEXT("arenas would interact: ArenaSpacingCm %.0f < floor %.0f + 50 m (hard rule 3)"), S.Combat.ArenaSpacingCm, 2.f * UCombatArena::FloorHalfSizeCm(StageIndex)));
		return;
	}
	bScripted = Args.bScripted;
	bEval = Args.bEval;
	bInfer = Args.bInfer || GIsEditor; // no local training (hard rule 6): the editor only plays back
	if (bEval && Args.ResumeFrom.IsEmpty() && !bScripted)
	{
		Exit(2, TEXT("-Eval needs -ResumeFrom=<snapshots> (or -Scripted for the reference score)"));
		return;
	}
	const float TickSeconds = 1.f / FMath::Max(1.f, FLearningAgentsTrainingGameSettings().FixedTimeStepFrequency);
	DecisionSeconds = TickSeconds * FMath::Max(1, S.Combat.Ppo.TicksPerDecision);
	if (bInfer && !GIsEditor)
	{
		// Headless playback / eval: the same fixed step as training, uncapped (D-019); training gets this from Learning Agents.
		FApp::SetUseFixedTimeStep(true);
		FApp::SetFixedDeltaTime(TickSeconds);
		if (IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))) { MaxFps->Set(0.f); }
	}

	const TArray<FVector> Centers = FPSReach::ArenaCenters(Args.Arenas, S.Combat.ArenaSpacingCm, GetActorLocation());
	for (int32 i = 0; i < Centers.Num(); ++i)
	{
		UCombatArena* Arena = NewObject<UCombatArena>(this);
		Arena->Build(GetWorld(), i, Centers[i], Args.Seed, StageIndex);
		Arenas.Add(Arena);
		FPSCurriculum::FDifficultyController D;
		D.Init(Args.StartDifficulty);
		Difficulty.Add(D);
	}

	AgentToArena.Init(INDEX_NONE, Arenas.Num());
	if (bScripted)
	{
		for (int32 i = 0; i < Arenas.Num(); ++i) { AgentToArena[i] = i; }
	}
	else
	{
		Manager->SetMaxAgentNum(Arenas.Num());
		const FCombatPpoSettings& Ppo = S.Combat.Ppo;
		FTrainingSessionConfig Config;
		Config.LogPrefix = TEXT("Combat");
		Config.InteractorClass = UCombatInteractor::StaticClass();
		Config.EnvClass = UCombatTrainingEnv::StaticClass();
		Config.bInfer = bInfer;
		Config.Seed = Args.Seed;
		Config.ResumeFrom = Args.ResumeFrom;
		Config.bResetAgentsOnUpdate = false; // D-026: don't cut episodes (and the ramp's statistics) at every policy update
		Config.Policy.HiddenLayerNum = Ppo.HiddenLayers;
		Config.Policy.HiddenLayerSize = Ppo.HiddenSize;
		Config.Policy.MemoryStateSize = Ppo.MemorySize;
		Config.Critic.HiddenLayerNum = Ppo.HiddenLayers;
		Config.Critic.HiddenLayerSize = Ppo.HiddenSize;
		// Our own timeout (Truncation) fires first; this cap only guards against a stuck episode.
		Config.Trainer.MaxEpisodeStepNum = FMath::CeilToInt(Stage.TimeLimitSeconds / DecisionSeconds) + 2;
		Config.Trainer.MaximumRecordedStepsPerIteration = FMath::Max(1, Ppo.ReplayMaxSteps);
		Config.Trainer.MaximumRecordedEpisodesPerIteration = FMath::Max(1, Ppo.ReplayMaxEpisodes);
		FLearningAgentsPPOTrainingSettings& T = Config.Training;
		T.IterationsPerGather = FMath::Max(1, Ppo.IterationsPerGather); // before converting -Batches (C-007)
		T.PolicyBatchSize = FMath::Max(1, Ppo.PolicyBatchSize);
		T.CriticBatchSize = FMath::Max(1, Ppo.CriticBatchSize);
		T.NumberOfIterations = Args.GradientIterations(T.IterationsPerGather, Ppo.MaxIterations);
		T.bSaveSnapshots = true;
		T.IterationsPerSnapshot = FMath::Max(1, Ppo.IterationsPerSnapshot);
		T.bUseTensorboard = Ppo.bUseTensorboard;
		T.Device = Ppo.bTrainOnGpu ? ELearningAgentsTrainingDevice::GPU : ELearningAgentsTrainingDevice::CPU;
		T.RandomSeed = Args.Seed;
		T.DiscountFactor = Ppo.Discount;
		T.GaeLambda = Ppo.GaeLambda;
		T.ActionEntropyWeight = Ppo.EntropyWeight;
		T.LearningRatePolicy = Ppo.LearningRatePolicy;
		T.LearningRateCritic = Ppo.LearningRateCritic;
		Session = NewObject<UTrainingSession>(this);
		if (!Session->Setup(*Manager, Config))
		{
			bExiting = true;
			return;
		}
		for (int32 i = 0; i < Arenas.Num(); ++i)
		{
			const int32 Id = Manager->AddAgent(Arenas[i]->GetAgent());
			if (!AgentToArena.IsValidIndex(Id))
			{
				Exit(1, FString::Printf(TEXT("manager returned agent id %d for arena %d"), Id, i));
				return;
			}
			AgentToArena[Id] = i;
		}
		UE_LOG(LogFPSAgent, Display, TEXT("Combat: ppo discount=%.4f gae=%.2f entropy=%.4f lr=%g/%g network=%dx%d+mem%d ticksPerDecision=%d resetOnUpdate=0"),
			Ppo.Discount, Ppo.GaeLambda, Ppo.EntropyWeight, Ppo.LearningRatePolicy, Ppo.LearningRateCritic, Ppo.HiddenLayers, Ppo.HiddenSize,
			Ppo.MemorySize, Ppo.TicksPerDecision);
	}

	ArenaIdle.Init(false, Arenas.Num());
	if (bEval)
	{
		// Fixed per-arena quotas (arena i gets N/K, plus 1 for the first N%K): every started episode is counted.
		const int32 N = Args.EvalEpisodes;
		const int32 K = Arenas.Num();
		EvalStartsLeft.SetNum(K);
		for (int32 i = 0; i < K; ++i) { EvalStartsLeft[i] = N / K + (i < N % K ? 1 : 0); }
	}
	for (int32 i = 0; i < Arenas.Num(); ++i) { TryStartNextEpisode(i); }
	bReady = true;
	NextDecisionTime = Now();
	NextStatsTime = Now() + S.Combat.Ppo.StatsLogIntervalSeconds;
	WindowWallStart = FPlatformTime::Seconds();
	UE_LOG(LogFPSAgent, Display, TEXT("Combat: ready. stage=S%d arenas=%d mode=%s resume=%s startDifficulty=%.2f decisionsPerSecond=%.1f timeLimit=%.0fs bots=%d..%d agentMoves=%d"),
		StageIndex, Arenas.Num(), bScripted ? TEXT("scripted (no policy)") : bEval ? TEXT("eval") : bInfer ? TEXT("infer") : TEXT("train (external trainer)"),
		Args.ResumeFrom.IsEmpty() ? TEXT("(none)") : *Args.ResumeFrom, Args.StartDifficulty, 1.0 / DecisionSeconds,
		Stage.TimeLimitSeconds, Stage.BotsEasy, Stage.BotsHard, Stage.bAgentCanMove ? 1 : 0);
}

UCombatArena* ACombatTaskActor::ArenaOf(int32 AgentId) const
{
	const int32 Index = AgentToArena.IsValidIndex(AgentId) ? AgentToArena[AgentId] : INDEX_NONE;
	return Arenas.IsValidIndex(Index) ? Arenas[Index].Get() : nullptr;
}

void ACombatTaskActor::StartNextEpisode(int32 ArenaIndex)
{
	UCombatArena* Arena = Arenas[ArenaIndex];
	const int32 Seed = Arena->NextEpisodeSeed();
	FRandomStream Rng(Seed);
	if (StageIndex == 1)
	{
		const UFPSAgentSettings& S = UFPSAgentSettings::Get();
		Arena->StartEpisode(FPSCurriculum::SampleS1(Rng, Difficulty[ArenaIndex].Level, /*bAllowReview=*/!bEval, S.Curriculum, S.Bots), Seed, Now());
		return;
	}
	const FPSCurriculum::FS0Episode Config = FPSCurriculum::SampleS0(Rng, Difficulty[ArenaIndex].Level, /*bAllowReview=*/!bEval,
		UFPSAgentSettings::Get().Curriculum, Arena->GetBotRadiusCm());
	Arena->StartEpisode(Config, Seed, Now());
}

bool ACombatTaskActor::TryStartNextEpisode(int32 ArenaIndex)
{
	if (EvalStartsLeft.IsValidIndex(ArenaIndex))
	{
		if (EvalStartsLeft[ArenaIndex] <= 0)
		{
			ArenaIdle[ArenaIndex] = true;
			return false;
		}
		--EvalStartsLeft[ArenaIndex];
	}
	StartNextEpisode(ArenaIndex);
	return true;
}

void ACombatTaskActor::RecordEpisode(int32 ArenaIndex, ECombatEpisodeEnd End)
{
	const FPSCombatStats::FEpisodeRecord R = Arenas[ArenaIndex]->FinishEpisode(End, Now());
	Window.Add(R);
	if (bEval)
	{
		EvalTotals.Add(R);
		if (EvalTotals.Episodes >= FFPSRunArgs::Get().EvalEpisodes)
		{
			FinishEval();
		}
		return;
	}
	if (!R.bReview)
	{
		Difficulty[ArenaIndex].Record(R.bCleared, UFPSAgentSettings::Get().Curriculum);
	}
}

void ACombatTaskActor::BuildObservation(int32 AgentId, FPSCombatObs::FObservationData& Out) const
{
	if (const UCombatArena* Arena = ArenaOf(AgentId))
	{
		Arena->BuildObservation(Out, Now());
	}
}

void ACombatTaskActor::SetAction(int32 AgentId, const FPSCombatObs::FAgentAction& Action)
{
	if (UCombatArena* Arena = ArenaOf(AgentId)) { Arena->SetAction(Action); }
}

float ACombatTaskActor::TakeDecisionReward(int32 AgentId)
{
	UCombatArena* Arena = ArenaOf(AgentId);
	return Arena ? Arena->TakeDecisionReward() : 0.f;
}

ECombatEpisodeEnd ACombatTaskActor::GetEpisodeEnd(int32 AgentId) const
{
	const UCombatArena* Arena = ArenaOf(AgentId);
	return Arena ? Arena->GetEpisodeEnd(Now()) : ECombatEpisodeEnd::Running;
}

void ACombatTaskActor::ResetAgentEpisode(int32 AgentId)
{
	const int32 Index = AgentToArena.IsValidIndex(AgentId) ? AgentToArena[AgentId] : INDEX_NONE;
	if (!Arenas.IsValidIndex(Index))
	{
		return;
	}
	const ECombatEpisodeEnd End = Arenas[Index]->GetEpisodeEnd(Now());
	// Running = a reset Learning Agents started itself (at the start of training): not a real episode end. (Its step cap only clears its episode buffer and never calls this.)
	if (End != ECombatEpisodeEnd::Running)
	{
		RecordEpisode(Index, End);
	}
	if (!bExiting)
	{
		TryStartNextEpisode(Index);
	}
}

int32 ACombatTaskActor::AgentIdOf(int32 ArenaIndex) const
{
	return AgentToArena.IndexOfByKey(ArenaIndex);
}

void ACombatTaskActor::Decide()
{
	const double T = Now();
	for (UCombatArena* Arena : Arenas) { Arena->PrepareDecision(T); }
	WindowDecisions += Arenas.Num();

	if (!bInfer)
	{
		// Learning Agents: completions -> rewards -> resets -> observations -> actions (our callbacks above).
		switch (Session->RunTrainingStep())
		{
		case UTrainingSession::EStatus::Failed:
			LogStats();
			Exit(1, TEXT("training failed: trainer unreachable, timed out or crashed (see LogLearning above)"));
			return;
		case UTrainingSession::EStatus::Finished:
			LogStats();
			Exit(0, TEXT("trainer finished"));
			return;
		default:
			return;
		}
	}

	// Playback / eval / scripted: the same order as training, run by us.
	TArray<int32> RestartedAgents;
	for (int32 i = 0; i < Arenas.Num(); ++i)
	{
		if (ArenaIdle[i]) { continue; }
		Arenas[i]->TakeDecisionReward();
		const ECombatEpisodeEnd End = Arenas[i]->GetEpisodeEnd(T);
		if (End != ECombatEpisodeEnd::Running)
		{
			RecordEpisode(i, End);
			if (bExiting) { return; }
			if (TryStartNextEpisode(i))
			{
				const int32 Id = AgentIdOf(i);
				if (Id != INDEX_NONE) { RestartedAgents.Add(Id); }
			}
		}
	}
	if (bScripted)
	{
		for (int32 i = 0; i < Arenas.Num(); ++i)
		{
			if (!ArenaIdle[i]) { Arenas[i]->SetAction(Arenas[i]->ScriptedAction(static_cast<float>(DecisionSeconds))); }
		}
	}
	else
	{
		// Playback runs its own episode loop, so it must reset the policy memory like training does (Manager->ResetAgents zeroes it). Training never calls this: there the trainer resets agents itself and calls ResetAgentEpisode.
		if (RestartedAgents.Num() > 0)
		{
			Manager->ResetAgents(RestartedAgents);
		}
		Session->GetPolicy()->RunInference(0.f); // mean actions, no exploration noise
	}
}

void ACombatTaskActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bReady || bExiting)
	{
		return;
	}
	if (Now() + 1e-6 >= NextDecisionTime)
	{
		NextDecisionTime += DecisionSeconds;
		if (NextDecisionTime < Now()) { NextDecisionTime = Now() + DecisionSeconds; } // editor frame hitches: don't pile up decisions
		Decide();
		if (bExiting) { return; }
	}
	for (int32 i = 0; i < Arenas.Num(); ++i)
	{
		if (!ArenaIdle[i]) { Arenas[i]->TickAction(DeltaSeconds); }
	}
	if (Now() >= NextStatsTime)
	{
		LogStats();
		NextStatsTime += UFPSAgentSettings::Get().Combat.Ppo.StatsLogIntervalSeconds;
	}
}

FPSCombatStats::FLevelSummary ACombatTaskActor::LevelSummary() const
{
	FPSCombatStats::FLevelSummary L;
	if (Difficulty.Num() == 0) { return L; }
	L.Min = 1.f;
	L.Max = 0.f;
	for (const FPSCurriculum::FDifficultyController& D : Difficulty)
	{
		L.Mean += D.Level;
		L.Min = FMath::Min(L.Min, D.Level);
		L.Max = FMath::Max(L.Max, D.Level);
	}
	L.Mean /= Difficulty.Num();
	return L;
}

void ACombatTaskActor::LogStats()
{
	const double Wall = FMath::Max(1e-6, FPlatformTime::Seconds() - WindowWallStart);
	const double DecisionsPerSecond = WindowDecisions / Wall;
	const FPSCombatStats::FLevelSummary L = LevelSummary();
	UE_LOG(LogFPSAgent, Display, TEXT("%s"), *FPSCombatStats::LogLine(Window, Now(), L, DecisionsPerSecond));
	const FString& Csv = FFPSRunArgs::Get().StatsCsv;
	if (!Csv.IsEmpty())
	{
		bool bOk = true;
		if (IFileManager::Get().FileSize(*Csv) <= 0)
		{
			bOk = FFileHelper::SaveStringToFile(FPSCombatStats::CsvHeader() + TEXT("\n"), *Csv);
		}
		bOk = bOk && FFileHelper::SaveStringToFile(FPSCombatStats::CsvRow(Window, Now(), L, DecisionsPerSecond) + TEXT("\n"), *Csv,
			FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
		if (!bOk && !bCsvWarned)
		{
			bCsvWarned = true;
			UE_LOG(LogFPSAgent, Warning, TEXT("Combat: could not write -StatsCsv '%s'"), *Csv);
		}
	}
	Window.Reset();
	WindowDecisions = 0;
	WindowWallStart = FPlatformTime::Seconds();
}

void ACombatTaskActor::FinishEval()
{
	const FFPSRunArgs& Args = FFPSRunArgs::Get();
	const float Gate = UFPSAgentSettings::Get().Curriculum.Stage(StageIndex).PromotionClearRate;
	const FString Json = FPSCombatStats::EvalJson(EvalTotals, FString::Printf(TEXT("S%d"), StageIndex), Args.StartDifficulty, Args.Seed,
		bScripted ? FString(TEXT("(scripted)")) : Args.ResumeFrom, Gate);
	UE_LOG(LogFPSAgent, Display, TEXT("CombatEval: %s"), *Json);
	if (!Args.EvalJson.IsEmpty())
	{
		FFileHelper::SaveStringToFile(Json, *Args.EvalJson);
	}
	LogStats();
	Exit(0, FString::Printf(TEXT("eval finished: %d episodes, clear rate %.1f%% (gate %.0f%%)"),
		EvalTotals.Episodes, 100.0 * EvalTotals.ClearRate(), 100.0 * Gate));
}
