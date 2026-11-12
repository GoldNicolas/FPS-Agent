#include "Misc/AutomationTest.h"
#include "Common/FPSAgentSettings.h"
#include "Common/FPSRunArgs.h"
#include "LearningAgentsPPOTrainer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSSettingsDefaultsTest, "FPSAgent.Settings.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSSettingsDefaultsTest::RunTest(const FString& Parameters)
{
	const UFPSAgentSettings& S = UFPSAgentSettings::Get();
	TestTrue(TEXT("one headshot kills from full HP + armor"), S.Rifle.DamageHead >= S.Health.MaxHealth + S.Health.MaxArmor);
	TestTrue(TEXT("fire rate positive"), S.Rifle.FireRate > 0.f);
	TestTrue(TEXT("walk slower than run"), S.Movement.WalkSpeed < S.Movement.RunSpeed);
	TestTrue(TEXT("crouch capsule smaller"), S.Body.CrouchedHalfHeight < S.Body.StandingHalfHeight);
	TestTrue(TEXT("range has lanes"), S.Range.Lanes.Num() > 0);

	const FCombatSettings& C = S.Combat;
	TestEqual(TEXT("15 decisions per second (D-023)"), 60 / C.Ppo.TicksPerDecision, 15);
	TestTrue(TEXT("discount in the agreed band"), C.Ppo.Discount >= 0.99f && C.Ppo.Discount <= 0.997f);
	TestEqual(TEXT("72 rays"), C.Perception.RayYawCount * C.Perception.RayPitchesDeg.Num(), 72);
	TestEqual(TEXT("clear bonus max 2.0"), C.Reward.ClearBase + C.Reward.ClearSpeed, 2.f);
	const FLearningAgentsPPOTrainingSettings LaTraining;
	const FLearningAgentsPPOTrainerSettings LaTrainer;
	TestEqual(TEXT("iterations per gather = Learning Agents default"), C.Ppo.IterationsPerGather, LaTraining.IterationsPerGather);
	TestEqual(TEXT("policy batch = Learning Agents default"), C.Ppo.PolicyBatchSize, LaTraining.PolicyBatchSize);
	TestEqual(TEXT("critic batch = Learning Agents default"), C.Ppo.CriticBatchSize, LaTraining.CriticBatchSize);
	TestEqual(TEXT("replay steps = Learning Agents default"), C.Ppo.ReplayMaxSteps, LaTrainer.MaximumRecordedStepsPerIteration);
	TestEqual(TEXT("replay episodes = Learning Agents default"), C.Ppo.ReplayMaxEpisodes, LaTrainer.MaximumRecordedEpisodesPerIteration);
	TestEqual(TEXT("S0 time limit 20 s"), S.Curriculum.S0.TimeLimitSeconds, 20.f);
	TestFalse(TEXT("S0 agent can't move"), S.Curriculum.S0.bAgentCanMove);
	TestTrue(TEXT("arenas at least floor + 50 m apart"),
		C.ArenaSpacingCm >= (S.Curriculum.S0.FloorTiles + 2) * 400.f + 5000.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRunArgsTest, "FPSAgent.Settings.RunArgs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSRunArgsTest::RunTest(const FString& Parameters)
{
	const FFPSRunArgs A = FFPSRunArgs::Parse(TEXT("-nullrhi -Stage=S2 -Seed=42 -Arenas=16"));
	TestEqual(TEXT("stage"), A.Stage, FString(TEXT("S2")));
	TestEqual(TEXT("seed"), A.Seed, 42);
	TestEqual(TEXT("arenas"), A.Arenas, 16);
	const FFPSRunArgs D = FFPSRunArgs::Parse(TEXT("-nullrhi"));
	TestTrue(TEXT("no stage by default"), D.Stage.IsEmpty());
	TestEqual(TEXT("default seed"), D.Seed, 0);
	TestEqual(TEXT("default arenas"), D.Arenas, 1);
	TestEqual(TEXT("arenas clamped"), FFPSRunArgs::Parse(TEXT("-Arenas=0")).Arenas, 1);
	TestEqual(TEXT("map"), FFPSRunArgs::Parse(TEXT("-nullrhi -Map=map01 -Seed=3")).Map, FString(TEXT("map01")));
	TestTrue(TEXT("map unset by default"), D.Map.IsEmpty());

	const FFPSRunArgs L = FFPSRunArgs::Parse(TEXT("-nullrhi -Stage=SpikeA -Infer -ResumeFrom=\"/w/runs/1/trainer/FPSAgent0/Snapshots\" -Iterations=200"));
	TestTrue(TEXT("spike A stage"), L.IsSpikeA());
	TestTrue(TEXT("infer flag"), L.bInfer);
	TestEqual(TEXT("resume dir (quoted)"), L.ResumeFrom, FString(TEXT("/w/runs/1/trainer/FPSAgent0/Snapshots")));
	TestEqual(TEXT("iterations"), L.Iterations, 200);
	TestTrue(TEXT("stage match is case-insensitive"), FFPSRunArgs::Parse(TEXT("-Stage=spikea")).IsSpikeA());
	TestFalse(TEXT("S0 is not spike A"), D.IsSpikeA());
	TestFalse(TEXT("infer off by default"), D.bInfer);
	TestFalse(TEXT("scripted off by default"), D.bScripted);
	const FFPSRunArgs Sc = FFPSRunArgs::Parse(TEXT("-Stage=SpikeA -Scripted"));
	TestTrue(TEXT("scripted"), Sc.bScripted);
	TestTrue(TEXT("scripted implies infer"), Sc.bInfer);
	TestTrue(TEXT("resume unset by default"), D.ResumeFrom.IsEmpty());
	TestEqual(TEXT("iterations 0 = settings"), D.Iterations, 0);

	// C-005: the trainer counts gradient iterations (IterationsPerGather = 32 per experience batch).
	const FFPSRunArgs Bt = FFPSRunArgs::Parse(TEXT("-Stage=SpikeA -Batches=30"));
	TestEqual(TEXT("batches"), Bt.Batches, 30);
	TestEqual(TEXT("batches -> gradient iterations"), Bt.GradientIterations(32, 1000000), 960);
	TestEqual(TEXT("-Iterations passes through"), L.GradientIterations(32, 1000000), 200);
	TestEqual(TEXT("settings when neither is given"), D.GradientIterations(32, 1000000), 1000000);
	TestEqual(TEXT("batches win over iterations"), FFPSRunArgs::Parse(TEXT("-Batches=2 -Iterations=7")).GradientIterations(32, 5), 64);
	TestEqual(TEXT("negative batches clamped"), FFPSRunArgs::Parse(TEXT("-Batches=-3")).Batches, 0);
	TestEqual(TEXT("huge batches saturate"), FFPSRunArgs::Parse(TEXT("-Batches=2000000000")).GradientIterations(32, 5), MAX_int32);
	TestEqual(TEXT("batches unset by default"), D.Batches, 0);
	const FFPSRunArgs Cb = FFPSRunArgs::Parse(TEXT("-Stage=S0 -StartDifficulty=0.6 -Eval -EvalEpisodes=50 -EvalJson=/r/eval.json -StatsCsv=/r/stats.csv -Set=\"Combat.Reward.Kill=1.5;Combat.Ppo.Discount=0.997\""));
	TestEqual(TEXT("combat stage index"), Cb.CombatStageIndex(), 0);
	TestTrue(TEXT("combat stage"), Cb.IsCombatStage());
	TestEqual(TEXT("start difficulty"), Cb.StartDifficulty, 0.6f);
	TestTrue(TEXT("eval"), Cb.bEval);
	TestTrue(TEXT("eval implies infer"), Cb.bInfer);
	TestEqual(TEXT("eval episodes"), Cb.EvalEpisodes, 50);
	TestEqual(TEXT("eval json"), Cb.EvalJson, FString(TEXT("/r/eval.json")));
	TestEqual(TEXT("stats csv"), Cb.StatsCsv, FString(TEXT("/r/stats.csv")));
	TestEqual(TEXT("-Set keeps ; and ="), Cb.SetOverrides, FString(TEXT("Combat.Reward.Kill=1.5;Combat.Ppo.Discount=0.997")));
	TestEqual(TEXT("s3 is stage 3"), FFPSRunArgs::CombatStageIndexOf(TEXT("s3")), 3);
	TestEqual(TEXT("SpikeA is not combat"), FFPSRunArgs::CombatStageIndexOf(TEXT("SpikeA")), -1);
	TestEqual(TEXT("S4 is not a stage"), FFPSRunArgs::CombatStageIndexOf(TEXT("S4")), -1);
	TestFalse(TEXT("no stage: not combat"), D.IsCombatStage());
	TestEqual(TEXT("difficulty clamped"), FFPSRunArgs::Parse(TEXT("-StartDifficulty=3")).StartDifficulty, 1.f);
	TestEqual(TEXT("eval episodes default"), D.EvalEpisodes, 200);
	TestFalse(TEXT("eval off by default"), D.bEval);
	return true;
}

#endif
