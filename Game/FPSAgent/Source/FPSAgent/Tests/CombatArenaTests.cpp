#include "Misc/AutomationTest.h"
#include "Arena/ArenaBuilder.h"
#include "Bots/ScriptedBot.h"
#include "Common/FPSAgentSettings.h"
#include "Learning/CombatArena.h"
#include "Tests/FPSTestWorld.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

using FPSCombatReward::ETerm;

namespace
{
	/** Agent at the origin facing +X; bot 0 1000 cm ahead, bot 1 1500 cm to the right (outside the 103 deg view). */
	FPSCurriculum::FS0Episode TwoBots()
	{
		FPSCurriculum::FS0Episode E;
		E.AgentYawDeg = 0.f;
		E.BotOffsets = { FVector2D(1000.0, 0.0), FVector2D(0.0, 1500.0) };
		E.BotCrouched = { false, false };
		return E;
	}

	void AimAtHead(UCombatArena* A, int32 Bot)
	{
		FVector Eye; FRotator Unused;
		A->GetAgent()->GetActorEyesViewPoint(Eye, Unused);
		const FVector Head = A->GetBot(Bot)->GetHitbox(EHitZone::Head)->GetComponentLocation();
		A->GetAgent()->GetController()->SetControlRotation((Head - Eye).Rotation());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatArenaEpisodeTest, "FPSAgent.Combat.Arena.Episode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatArenaEpisodeTest::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	TStrongObjectPtr<UCombatArena> A(NewObject<UCombatArena>(GetTransientPackage()));
	A->Build(W.World, 0, FVector::ZeroVector, 7);
	A->StartEpisode(TwoBots(), 11, 0.0);
	W.Step();
	const float SpawnZ = UFPSAgentSettings::Get().Body.StandingHalfHeight + 2.f;
	TestTrue(TEXT("agent at the centre"), A->GetAgent()->GetActorLocation().Equals(FVector(0.0, 0.0, SpawnZ), 1.0));
	TestEqual(TEXT("bot pool = max bots"), A->GetBotCount(), 5);
	TestTrue(TEXT("bot 0 placed"), A->GetBot(0)->GetActorLocation().Equals(FVector(1000.0, 0.0, SpawnZ), 1.0));
	TestTrue(TEXT("bot 0 alive"), A->GetBot(0)->IsAlive());
	TestTrue(TEXT("bot 1 active"), A->GetBot(1)->IsActive());
	TestFalse(TEXT("bot 2 unused"), A->GetBot(2)->IsActive());
	TestTrue(TEXT("running"), A->GetEpisodeEnd(5.0) == ECombatEpisodeEnd::Running);
	TestTrue(TEXT("times out at the 20 s limit"), A->GetEpisodeEnd(20.0) == ECombatEpisodeEnd::TimedOut);
	TestTrue(TEXT("end check has no side effects (D-022)"), A->GetEpisodeEnd(5.0) == ECombatEpisodeEnd::Running);

	A->PrepareDecision(0.1);
	TestEqual(TEXT("first sighting of the bot in view"), A->GetLedger().PendingTerm(ETerm::FirstSighting), 0.2f, 1e-6f);
	A->TakeDecisionReward();
	A->PrepareDecision(0.2);
	TestEqual(TEXT("no second sighting reward"), A->GetLedger().PendingTerm(ETerm::FirstSighting), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatArenaClearTest, "FPSAgent.Combat.Arena.KillAndClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatArenaClearTest::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	TStrongObjectPtr<UCombatArena> A(NewObject<UCombatArena>(GetTransientPackage()));
	A->Build(W.World, 0, FVector::ZeroVector, 7);
	A->StartEpisode(TwoBots(), 11, 0.0);
	W.Step();
	AimAtHead(A.Get(), 0);
	const FShotResult First = A->GetAgent()->GetRifle()->FireShot(0);
	TestTrue(TEXT("headshot kill"), First.bKill);
	TestEqual(TEXT("kill reward"), A->GetLedger().PendingTerm(ETerm::Kill), 1.f, 1e-6f);
	TestEqual(TEXT("damage = health + armor removed (150/150)"), A->GetLedger().PendingTerm(ETerm::DamageDealt), 1.f, 1e-5f);
	AimAtHead(A.Get(), 1);
	A->GetAgent()->GetRifle()->FireShot(0);
	TestTrue(TEXT("bot 1 dead"), !A->GetBot(1)->IsAlive());
	TestTrue(TEXT("cleared"), A->GetEpisodeEnd(5.0) == ECombatEpisodeEnd::Cleared);
	A->PrepareDecision(5.0);
	TestEqual(TEXT("clear bonus 0.5 + 1.5 * 15/20"), A->GetLedger().PendingTerm(ETerm::Clear), 0.5f + 1.5f * 0.75f, 1e-5f);
	A->TakeDecisionReward();
	A->PrepareDecision(6.0);
	TestEqual(TEXT("clear bonus paid once (D-022)"), A->GetLedger().PendingTerm(ETerm::Clear), 0.f);
	const FPSCombatStats::FEpisodeRecord R = A->FinishEpisode(ECombatEpisodeEnd::Cleared, 6.0);
	TestTrue(TEXT("record cleared"), R.bCleared);
	TestEqual(TEXT("kills"), R.Kills, 2);
	TestEqual(TEXT("shots"), R.Shots, 2);
	TestEqual(TEXT("hits"), R.Hits, 2);
	TestEqual(TEXT("headshots"), R.Headshots, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatArenaDeathTest, "FPSAgent.Combat.Arena.DeathBeatsClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatArenaDeathTest::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	TStrongObjectPtr<UCombatArena> A(NewObject<UCombatArena>(GetTransientPackage()));
	A->Build(W.World, 0, FVector::ZeroVector, 7);
	A->StartEpisode(TwoBots(), 11, 0.0);
	W.Step();
	A->GetBot(0)->GetHealth()->ApplyHit(500.f, nullptr);
	A->GetBot(1)->GetHealth()->ApplyHit(500.f, nullptr);
	A->GetAgent()->GetHealth()->ApplyHit(500.f, A->GetBot(0));
	TestTrue(TEXT("a dead agent beats a clear (spec §6)"), A->GetEpisodeEnd(3.0) == ECombatEpisodeEnd::Died);
	A->PrepareDecision(3.0);
	TestEqual(TEXT("no clear bonus"), A->GetLedger().PendingTerm(ETerm::Clear), 0.f);
	TestEqual(TEXT("death penalty"), A->GetLedger().PendingTerm(ETerm::Death), -1.f, 1e-6f);
	TestEqual(TEXT("damage taken (150 of 150)"), A->GetLedger().PendingTerm(ETerm::DamageTaken), -1.f, 1e-5f);
	TestEqual(TEXT("bot kills by others don't pay"), A->GetLedger().PendingTerm(ETerm::Kill), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatArenaObservationTest, "FPSAgent.Combat.Arena.Observation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatArenaObservationTest::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	TStrongObjectPtr<UCombatArena> A(NewObject<UCombatArena>(GetTransientPackage()));
	A->Build(W.World, 0, FVector::ZeroVector, 7);
	A->StartEpisode(TwoBots(), 11, 0.0);
	W.Step();
	A->GetAgent()->GetController()->SetControlRotation(FRotator(60.0, 0.0, 0.0)); // look at the sky
	A->GetAgent()->GetRifle()->FireShot(0);
	TestEqual(TEXT("miss penalty"), A->GetLedger().PendingTerm(ETerm::Miss), -0.01f, 1e-6f);
	A->GetAgent()->GetController()->SetControlRotation(FRotator::ZeroRotator);
	A->PrepareDecision(0.1);

	FPSCombatObs::FObservationData O;
	A->BuildObservation(O, 0.1);
	const FCombatPerceptionSettings& P = UFPSAgentSettings::Get().Combat.Perception;
	TestEqual(TEXT("self size"), O.Self.Num(), FPSCombatObs::SelfSize);
	TestEqual(TEXT("ray count"), O.Rays.Num(), P.RayYawCount * P.RayPitchesDeg.Num());
	bool bInRange = true;
	bool bSomeShort = false;
	for (const float F : O.Rays) { bInRange &= (F >= 0.f && F <= 1.f); bSomeShort |= F < 1.f; }
	TestTrue(TEXT("ray fractions in [0, 1]"), bInRange);
	TestTrue(TEXT("down rays hit the floor"), bSomeShort);
	TestEqual(TEXT("only the bot in view is observed"), O.Enemies.Num(), 1);
	if (O.Enemies.Num() == 1)
	{
		TestEqual(TEXT("enemy size"), O.Enemies[0].Num(), FPSCombatObs::EnemySize);
		TestEqual(TEXT("visible flag"), O.Enemies[0][11], 1.f);
	}
	TestFalse(TEXT("not hit"), O.bHit);
	TestEqual(TEXT("no sounds in S0"), O.Sounds.Num(), 0);

	const FPSCombatObs::FAgentAction Act = A->ScriptedAction(4.f / 60.f);
	TestTrue(TEXT("scripted agent targets the bot ahead (no-target scan would be Aim.X = 1)"), FMath::Abs(Act.Aim.X) < 0.05);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatArenaFairnessTest, "FPSAgent.Combat.Arena.FairnessAndScripted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatArenaFairnessTest::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	TStrongObjectPtr<UCombatArena> A(NewObject<UCombatArena>(GetTransientPackage()));
	A->Build(W.World, 0, FVector::ZeroVector, 7);
	// Bot 0: 20 deg right at 10 m, in view. Bot 1: straight ahead at 15 m, behind a wall at x = 1000.
	FPSCurriculum::FS0Episode E;
	E.AgentYawDeg = 0.f;
	const double Rad = FMath::DegreesToRadians(20.0);
	E.BotOffsets = { FVector2D(1000.0 * FMath::Cos(Rad), 1000.0 * FMath::Sin(Rad)), FVector2D(1500.0, 0.0) };
	E.BotCrouched = { false, false };
	FPSArena::SpawnBlock(W.World, FVector(1000.0, 0.0, 200.0), FVector(50.0, 300.0, 400.0));
	A->StartEpisode(E, 11, 0.0);
	W.Step();

	A->PrepareDecision(0.1);
	TestEqual(TEXT("only the visible bot pays a first sighting"), A->GetLedger().PendingTerm(ETerm::FirstSighting), 0.2f, 1e-6f);
	FPSCombatObs::FObservationData O;
	A->BuildObservation(O, 0.1);
	TestEqual(TEXT("the bot behind the wall is not observed (rule 4)"), O.Enemies.Num(), 1);

	const FPSCombatObs::FAgentAction Act = A->ScriptedAction(4.f / 60.f); // 720 deg/s -> 48 deg per decision
	TestEqual(TEXT("scripted agent turns right toward the visible bot: 20 of 48 deg"), static_cast<float>(Act.Aim.X), 20.f / 48.f, 0.005f);
	TestFalse(TEXT("no fire 20 deg off target"), Act.bFire);

	// Hit indicator (D-024): the direction of damage taken shows even from behind a wall.
	A->GetAgent()->GetHealth()->ApplyHit(10.f, A->GetBot(1));
	const double T = W.World->GetTimeSeconds();
	A->BuildObservation(O, T + 0.1);
	TestTrue(TEXT("hit indicator on"), O.bHit);
	TestEqual(TEXT("hit size"), O.Hit.Num(), FPSCombatObs::HitSize);
	if (O.Hit.Num() == FPSCombatObs::HitSize)
	{
		TestTrue(TEXT("hit comes from ahead (bot 1)"), O.Hit[0] > 0.9f);
		TestEqual(TEXT("hit age 0.1 s / 1 s"), O.Hit[3], 0.1f, 1e-3f);
	}
	A->BuildObservation(O, T + 1.5);
	TestFalse(TEXT("hit indicator expires after HitIndicatorSeconds"), O.bHit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatArenaS1Test, "FPSAgent.Combat.Arena.S1",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatArenaS1Test::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	TStrongObjectPtr<UCombatArena> A(NewObject<UCombatArena>(GetTransientPackage()));
	A->Build(W.World, 0, FVector::ZeroVector, 7, /*StageIndex=*/1);
	TestEqual(TEXT("S1 arena"), A->GetStageIndex(), 1);

	FPSCurriculum::FCombatEpisode E;
	E.Map = A->GetBaseSpec();
	const int32 Mid = E.Map.Rows() / 2;            // 11 on the 22 x 22 S1 floor
	E.Map.Tiles[Mid - 3][Mid] = TEXT('h');          // world centre (1000, 200): a high block north of the agent
	E.Map.Tiles[Mid][Mid + 3] = TEXT('l');          // world centre (-200, 1400): a low block east of the agent
	E.AgentYawDeg = 0.f;
	FPSCurriculum::FBotSpawn Patrol;
	Patrol.Offset = FVector2D(800.0, 800.0);
	Patrol.YawDeg = 45.f;                            // facing away from the agent: it can't see it
	Patrol.Behaviour = FPSBots::EBotBehaviour::Patrol;
	FPSCurriculum::FBotSpawn Static;
	Static.Offset = FVector2D(-800.0, 0.0);
	Static.Behaviour = FPSBots::EBotBehaviour::Static;
	E.Bots = { Patrol, Static };
	A->StartEpisode(E, 11, 0.0);

	TestEqual(TEXT("two cover blocks placed"), A->GetVisibleCoverCount(), 2);
	TestFalse(TEXT("high cover blocks sight (moved pool block has collision)"), FPSPerception::HasLineOfSight(W.World, FVector(500.0, 200.0, 100.0), FVector(1500.0, 200.0, 100.0)));
	TestFalse(TEXT("low cover blocks at 1 m"), FPSPerception::HasLineOfSight(W.World, FVector(-200.0, 1000.0, 100.0), FVector(-200.0, 1800.0, 100.0)));
	TestTrue(TEXT("low cover is clear above 1.2 m"), FPSPerception::HasLineOfSight(W.World, FVector(-200.0, 1000.0, 150.0), FVector(-200.0, 1800.0, 150.0)));

	A->TickAction(1.f / 60.f);
	TestFalse(TEXT("the patrol bot walks"), A->GetBot(0)->GetPendingMovementInputVector().IsNearlyZero());
	TestTrue(TEXT("the static bot holds"), A->GetBot(1)->GetPendingMovementInputVector().IsNearlyZero());

	A->GetAgent()->GetRifle()->FireShot(0);
	bool bGunshot = false;
	for (const FPSBots::FSoundEvent& S : A->GetSounds()) { bGunshot |= S.Kind == FPSBots::ESoundKind::Gunshot && S.Source == INDEX_NONE; }
	TestTrue(TEXT("the agent's shot is a sound event"), bGunshot);
	A->PrepareDecision(0.1);
	TestFalse(TEXT("not before the reaction delay"), A->GetBotBrain(0).IsInvestigating());
	A->PrepareDecision(0.6);
	TestTrue(TEXT("the patrol bot investigates the shot it heard"), A->GetBotBrain(0).IsInvestigating());

	FPSBots::FSoundEvent Step;
	Step.Position = FVector(0.0, 1000.0, 100.0);
	Step.Time = 0.55;
	Step.Kind = FPSBots::ESoundKind::Footstep;
	Step.Source = 0;
	A->EmitSound(Step);
	FPSCombatObs::FObservationData O;
	A->BuildObservation(O, 0.6);
	TestEqual(TEXT("the agent hears one enemy sound"), O.Sounds.Num(), 1);
	if (O.Sounds.Num() == 1)
	{
		TestTrue(TEXT("...to its right (+Y at yaw 0)"), O.Sounds[0][1] > 0.9f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatArenaS1FairnessTest, "FPSAgent.Combat.Arena.S1Fairness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatArenaS1FairnessTest::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	TStrongObjectPtr<UCombatArena> A(NewObject<UCombatArena>(GetTransientPackage()));
	A->Build(W.World, 0, FVector::ZeroVector, 7, /*StageIndex=*/1);
	FPSCurriculum::FCombatEpisode E;
	E.Map = A->GetBaseSpec();
	const int32 Mid = E.Map.Rows() / 2;
	E.Map.Tiles[Mid - 3][Mid] = TEXT('h');      // a high block at world (1000, 200), between the agent and the bot
	E.AgentYawDeg = 0.f;
	FPSCurriculum::FBotSpawn Hidden;
	Hidden.Offset = FVector2D(1600.0, 200.0);   // behind the block
	Hidden.YawDeg = 180.f;                       // facing the agent: only the cover hides it
	Hidden.Behaviour = FPSBots::EBotBehaviour::Patrol;
	E.Bots = { Hidden };
	A->StartEpisode(E, 21, 0.0);
	A->PrepareDecision(0.1);
	A->PrepareDecision(0.6);
	TestFalse(TEXT("a bot behind high cover doesn't see the agent (rule 4)"), A->GetBotBrain(0).IsInvestigating());

	auto Sound = [](double X, double Y, double Time, FPSBots::ESoundKind Kind, int32 Source)
	{
		FPSBots::FSoundEvent S;
		S.Position = FVector(X, Y, 90.0);
		S.Time = Time;
		S.Kind = Kind;
		S.Source = Source;
		return S;
	};
	A->EmitSound(Sound(1500.0, 600.0, 0.6, FPSBots::ESoundKind::Footstep, 1)); // another bot's step, right next to bot 0
	A->PrepareDecision(1.1);
	TestFalse(TEXT("bots ignore other bots' sounds"), A->GetBotBrain(0).IsInvestigating());

	A->EmitSound(Sound(0.0, 500.0, 1.0, FPSBots::ESoundKind::Gunshot, INDEX_NONE)); // the agent's own shot
	A->EmitSound(Sound(0.0, 2000.0, 1.0, FPSBots::ESoundKind::Footstep, 0));       // 20 m: beyond the 15 m footstep radius
	A->EmitSound(Sound(0.0, 500.0, -0.5, FPSBots::ESoundKind::Footstep, 0));       // 1.5 s old at Now = 1.0 (memory 1 s)
	FPSCombatObs::FObservationData O;
	A->BuildObservation(O, 1.0);
	TestEqual(TEXT("the agent hears neither its own, far nor stale sounds"), O.Sounds.Num(), 0);

	for (int32 i = 0; i < 6; ++i)
	{
		A->EmitSound(Sound(0.0, 300.0 + 50.0 * i, 1.0 + 0.01 * i, FPSBots::ESoundKind::Footstep, 0));
	}
	A->BuildObservation(O, 1.1);
	TestEqual(TEXT("at most MaxSounds (4) sounds"), O.Sounds.Num(), 4);
	if (O.Sounds.Num() == 4)
	{
		TestTrue(TEXT("newest first"), O.Sounds[0][4] <= O.Sounds[3][4]);
	}
	A->StartEpisode(E, 22, 2.0);
	TestEqual(TEXT("sounds reset at each episode start"), A->GetSounds().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatArenaScriptedS1Test, "FPSAgent.Combat.Arena.ScriptedS1",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatArenaScriptedS1Test::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	TStrongObjectPtr<UCombatArena> A(NewObject<UCombatArena>(GetTransientPackage()));
	A->Build(W.World, 0, FVector::ZeroVector, 7, /*StageIndex=*/1);
	FPSCurriculum::FCombatEpisode E;
	E.Map = A->GetBaseSpec();
	E.AgentYawDeg = 0.f;
	FPSCurriculum::FBotSpawn Behind;
	Behind.Offset = FVector2D(-1500.0, 0.0); // behind the agent: out of view
	Behind.YawDeg = 180.f;
	E.Bots = { Behind };
	A->StartEpisode(E, 3, 0.0);
	A->PrepareDecision(0.1);
	const FPSCombatObs::FAgentAction Explore = A->ScriptedAction(4.f / 60.f);
	TestTrue(TEXT("nothing in view: the reference explores (moves or turns)"), !Explore.Move.IsNearlyZero() || FMath::Abs(Explore.Aim.X) > 0.01);
	TestFalse(TEXT("nothing in view: no fire"), Explore.bFire);

	FPSCurriculum::FCombatEpisode F = E;
	F.Bots[0].Offset = FVector2D(1000.0, 0.0); // straight ahead
	A->StartEpisode(F, 4, 0.0);
	A->PrepareDecision(0.1);
	const FPSCombatObs::FAgentAction Shoot = A->ScriptedAction(4.f / 60.f);
	TestTrue(TEXT("enemy in view: stands still to shoot"), Shoot.Move.IsNearlyZero());
	TestTrue(TEXT("enemy in view: aims at it"), FMath::Abs(Shoot.Aim.X) < 0.05);
	return true;
}

#endif
