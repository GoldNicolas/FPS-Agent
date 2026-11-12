#include "Learning/TrainingSession.h"
#include "Async/Async.h"
#include "FPSAgent.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "LearningAgentsCommunicator.h"
#include "LearningAgentsInteractor.h"
#include "LearningAgentsManager.h"
#include "LearningAgentsNeuralNetwork.h"
#include "LearningAgentsTrainer.h"
#include "LearningAgentsTrainingEnvironment.h"
#include "Misc/CommandLine.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include <atomic>

void UTrainingSession::ExitProcess(const FString& LogPrefix, int32 Code, const FString& Reason)
{
	if (Code == 0)
	{
		UE_LOG(LogFPSAgent, Display, TEXT("%s: exiting (code 0): %s"), *LogPrefix, *Reason);
	}
	else
	{
		UE_LOG(LogFPSAgent, Error, TEXT("%s: exiting (code %d): %s"), *LogPrefix, Code, *Reason);
	}
	if (!GIsEditor)
	{
		FPlatformMisc::RequestExitWithStatus(false, static_cast<uint8>(Code));
	}
}

bool UTrainingSession::Setup(ULearningAgentsManager& Manager, const FTrainingSessionConfig& InConfig)
{
	Config = InConfig;
	// The Make* helpers take UPARAM(ref) pointers, so they need plain local variables of the base types.
	ULearningAgentsManager* Mgr = &Manager;
	ULearningAgentsInteractor* I = ULearningAgentsInteractor::MakeInteractor(Mgr, Config.InteractorClass, TEXT("Interactor"));
	ULearningAgentsPolicy* P = I ? ULearningAgentsPolicy::MakePolicy(Mgr, I, ULearningAgentsPolicy::StaticClass(), TEXT("Policy"),
		nullptr, nullptr, nullptr, true, true, true, Config.Policy, Config.Seed) : nullptr;
	Interactor = I;
	Policy = P;
	if (!I || !P)
	{
		ExitProcess(Config.LogPrefix, 1, TEXT("Learning Agents interactor/policy setup failed"));
		return false;
	}

	ULearningAgentsCritic* C = nullptr;
	if (!Config.bInfer)
	{
		C = ULearningAgentsCritic::MakeCritic(Mgr, I, P, ULearningAgentsCritic::StaticClass(), TEXT("Critic"), nullptr, true, Config.Critic, Config.Seed);
		Critic = C;
	}
	if (!Config.ResumeFrom.IsEmpty() && !LoadSnapshots(Config.ResumeFrom))
	{
		ExitProcess(Config.LogPrefix, 2, TEXT("-ResumeFrom is missing snapshot files"));
		return false;
	}
	if (Config.bInfer)
	{
		return true;
	}

	ULearningAgentsTrainingEnvironment* E = ULearningAgentsTrainingEnvironment::MakeTrainingEnvironment(Mgr, Config.EnvClass, TEXT("TrainingEnv"));
	Env = E;

	FLearningAgentsTrainerProcessSettings Process;
	Process.TaskName = TEXT("FPSAgent");
	FLearningAgentsSocketCommunicatorSettings Socket;
	Socket.bUseExternalTrainingProcess = true; // never spawn a local trainer; address/port come from -LearningAgents* flags

	// Watchdog. Learning Agents' connect loop counts only its 1 ms sleeps toward the timeout, not the time spent inside
	// each blocking connect(), so an unreachable trainer can block far longer than -LearningAgentsTimeout. A real
	// wall-clock deadline force-exits with code 1. Never probe the trainer port ourselves (D-020).
	float ConnectTimeout = Socket.Timeout;
	FParse::Value(FCommandLine::Get(), TEXT("LearningAgentsTimeout="), ConnectTimeout);
	const TSharedRef<std::atomic<bool>> bConnectDone = MakeShared<std::atomic<bool>>(false);
	if (!GIsEditor)
	{
		const double Deadline = FMath::Max(1.0, static_cast<double>(ConnectTimeout)) + 15.0;
		const FString Prefix = Config.LogPrefix;
		Async(EAsyncExecution::Thread, [bConnectDone, Deadline, Prefix]()
		{
			const double Start = FPlatformTime::Seconds();
			while (!bConnectDone->load())
			{
				if (FPlatformTime::Seconds() - Start > Deadline)
				{
					UE_LOG(LogFPSAgent, Error, TEXT("%s: exiting (code 1): no trainer connection after %.0f s (wall clock). Start the trainer first and wait for 'Server listening on'."), *Prefix, Deadline);
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

	Trainer = ULearningAgentsPPOTrainer::MakePPOTrainer(Mgr, I, E, P, C, Communicator,
		ULearningAgentsPPOTrainer::StaticClass(), TEXT("PPOTrainer"), Config.Trainer);
	if (!E || !C || !Trainer)
	{
		ExitProcess(Config.LogPrefix, 1, TEXT("Learning Agents trainer setup failed"));
		return false;
	}
	const FLearningAgentsPPOTrainingSettings& T = Config.Training;
	UE_LOG(LogFPSAgent, Display, TEXT("%s: trainer settings: iterations=%d (= %.1f batches of %d) snapshotEvery=%d device=%s tensorboard=%d seed=%d"),
		*Config.LogPrefix, T.NumberOfIterations, static_cast<double>(T.NumberOfIterations) / FMath::Max(1, T.IterationsPerGather),
		T.IterationsPerGather, T.IterationsPerSnapshot, T.Device == ELearningAgentsTrainingDevice::GPU ? TEXT("GPU") : TEXT("CPU"),
		T.bUseTensorboard ? 1 : 0, T.RandomSeed);
	return true;
}

UTrainingSession::EStatus UTrainingSession::RunTrainingStep()
{
	// RunTraining would start a NEW session if called after the trainer finished, so report Finished as soon as it ends.
	Trainer->RunTraining(Config.Training, FLearningAgentsTrainingGameSettings(), true, Config.bResetAgentsOnUpdate);
	if (Trainer->HasTrainingFailed())
	{
		return EStatus::Failed;
	}
	if (Trainer->IsTraining())
	{
		bTrainingStarted = true;
		return EStatus::Running;
	}
	return bTrainingStarted ? EStatus::Finished : EStatus::Running;
}

bool UTrainingSession::LoadSnapshots(const FString& Dir)
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
			UE_LOG(LogFPSAgent, Error, TEXT("%s: snapshot not found: %s"), *Config.LogPrefix, *Path);
			return false;
		}
	}
	for (const TPair<FString, ULearningAgentsNeuralNetwork*>& Item : Items)
	{
		FFilePath File;
		File.FilePath = FPaths::Combine(Dir, Item.Key);
		Item.Value->LoadNetworkFromSnapshot(File);
	}
	UE_LOG(LogFPSAgent, Display, TEXT("%s: loaded %d snapshots from %s"), *Config.LogPrefix, Items.Num(), *Dir);
	return true;
}
