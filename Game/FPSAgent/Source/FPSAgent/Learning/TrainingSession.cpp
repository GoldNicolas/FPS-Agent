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
#include "LearningNeuralNetwork.h"
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
		ExitProcess(Config.LogPrefix, 2, TEXT("-ResumeFrom snapshots are missing or don't match this build's network"));
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
	// Learning Agents' LoadFromSnapshot overwrites the network's sizes and compatibility hash without comparing them, so a snapshot
	// from another build or network setting would "load" and then score 0. Compare before and after; a mismatch is a bad argument (exit 2).
	struct FShape { int32 In = 0; int32 Out = 0; int32 Hash = 0; bool bKnown = false; };
	auto ShapeOf = [](const ULearningAgentsNeuralNetwork* Network)
	{
		FShape S;
		if (Network && Network->NeuralNetworkData)
		{
			S.In = Network->NeuralNetworkData->GetInputSize();
			S.Out = Network->NeuralNetworkData->GetOutputSize();
			S.Hash = Network->NeuralNetworkData->GetCompatibilityHash();
			S.bKnown = S.In > 0 || S.Out > 0;
		}
		return S;
	};
	TArray<FShape> Before;
	for (const TPair<FString, ULearningAgentsNeuralNetwork*>& Item : Items)
	{
		Before.Add(ShapeOf(Item.Value));
	}
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		FFilePath File;
		File.FilePath = FPaths::Combine(Dir, Items[i].Key);
		Items[i].Value->LoadNetworkFromSnapshot(File);
		const FShape After = ShapeOf(Items[i].Value);
		if (Before[i].bKnown && (After.In != Before[i].In || After.Out != Before[i].Out || After.Hash != Before[i].Hash))
		{
			UE_LOG(LogFPSAgent, Error, TEXT("%s: snapshot %s doesn't match this build's network (inputs %d vs %d, outputs %d vs %d, hash %d vs %d). Resume only snapshots from the same build and network settings."),
				*Config.LogPrefix, *File.FilePath, After.In, Before[i].In, After.Out, Before[i].Out, After.Hash, Before[i].Hash);
			return false;
		}
	}
	UE_LOG(LogFPSAgent, Display, TEXT("%s: loaded %d snapshots from %s"), *Config.LogPrefix, Items.Num(), *Dir);
	return true;
}
