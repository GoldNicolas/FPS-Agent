#include "Misc/AutomationTest.h"
#include "Bots/BotSenses.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace FPSBots;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSBotSensesSightTest, "FPSAgent.Bots.Senses.Sight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSBotSensesSightTest::RunTest(const FString& Parameters)
{
	const FBotBehaviourSettings B; // reaction 0.4 s, memory 3 s
	FRandomStream Rng(1);
	const FVector Agent(1000.0, 0.0, 0.0);
	const FVector Bot(0.0, 0.0, 0.0);
	const TArray<FSoundEvent> None;
	FVector T;
	FBotSenses S;
	TestFalse(TEXT("a new sighting waits for the reaction delay"), S.Update(true, Agent, None, Bot, 0.0, B, Rng, T));
	TestFalse(TEXT("still waiting at 0.3 s"), S.Update(true, Agent, None, Bot, 0.3, B, Rng, T));
	TestTrue(TEXT("acts at 0.4 s"), S.Update(true, Agent, None, Bot, 0.4, B, Rng, T));
	TestTrue(TEXT("target = the agent's position"), T.Equals(Agent, 1e-3));
	TestTrue(TEXT("remembers for 3 s"), S.Remembers(3.39, B));
	TestFalse(TEXT("forgets after 3 s"), S.Remembers(3.41, B));

	FBotSenses L;
	L.Update(true, Agent, None, Bot, 0.0, B, Rng, T);
	L.Update(false, Agent, None, Bot, 0.2, B, Rng, T);
	TestFalse(TEXT("losing sight restarts the delay"), L.Update(true, Agent, None, Bot, 0.5, B, Rng, T));
	TestTrue(TEXT("acts 0.4 s after the new sighting"), L.Update(true, Agent, None, Bot, 0.9, B, Rng, T));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSBotSensesHearingTest, "FPSAgent.Bots.Senses.Hearing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSBotSensesHearingTest::RunTest(const FString& Parameters)
{
	const FBotBehaviourSettings B; // gunshots 40 m, running footsteps 15 m, noise 2 m
	const FVector Bot(0.0, 0.0, 0.0);
	auto Event = [](double X, double Time, ESoundKind Kind, int32 Source) { FSoundEvent E; E.Position = FVector(X, 0.0, 0.0); E.Time = Time; E.Kind = Kind; E.Source = Source; return E; };
	TestTrue(TEXT("gunshot at 39.9 m"), CanHear(Event(3990.0, 0.0, ESoundKind::Gunshot, INDEX_NONE), Bot, 0.1, 1.f, B));
	TestFalse(TEXT("gunshot at 40.1 m"), CanHear(Event(4010.0, 0.0, ESoundKind::Gunshot, INDEX_NONE), Bot, 0.1, 1.f, B));
	TestTrue(TEXT("footstep at 14.9 m"), CanHear(Event(1490.0, 0.0, ESoundKind::Footstep, INDEX_NONE), Bot, 0.1, 1.f, B));
	TestFalse(TEXT("footstep at 15.1 m"), CanHear(Event(1510.0, 0.0, ESoundKind::Footstep, INDEX_NONE), Bot, 0.1, 1.f, B));
	TestFalse(TEXT("too old"), CanHear(Event(100.0, 0.0, ESoundKind::Gunshot, INDEX_NONE), Bot, 1.5, 1.f, B));

	TestTrue(TEXT("running is audible"), IsAudibleMovement(540.f, false, false, 300.f, B));
	TestTrue(TEXT("a 60% bot (324 cm/s) is audible"), IsAudibleMovement(324.f, false, false, 300.f, B));
	TestFalse(TEXT("walking is silent"), IsAudibleMovement(300.f, false, true, 300.f, B));
	TestFalse(TEXT("crouching is silent"), IsAudibleMovement(190.f, true, false, 300.f, B));
	TestFalse(TEXT("standing still is silent"), IsAudibleMovement(0.f, false, false, 300.f, B));

	FRandomStream Rng(5);
	FVector T;
	FBotSenses H;
	const TArray<FSoundEvent> Shot = { Event(3000.0, 1.0, ESoundKind::Gunshot, INDEX_NONE) };
	TestFalse(TEXT("not before the reaction delay"), H.Update(false, FVector::ZeroVector, Shot, Bot, 1.2, B, Rng, T));
	TestTrue(TEXT("investigates the gunshot"), H.Update(false, FVector::ZeroVector, Shot, Bot, 1.4, B, Rng, T));
	TestTrue(TEXT("noisy position within 2 m"), FVector::Dist(T, FVector(3000.0, 0.0, 0.0)) <= 200.0 + 1e-3);
	TestFalse(TEXT("each sound only once"), H.Update(false, FVector::ZeroVector, Shot, Bot, 1.5, B, Rng, T));
	FBotSenses O;
	const TArray<FSoundEvent> BotStep = { Event(100.0, 1.0, ESoundKind::Footstep, 2) };
	TestFalse(TEXT("bots ignore other bots' sounds"), O.Update(false, FVector::ZeroVector, BotStep, Bot, 2.0, B, Rng, T));

	FRandomStream R1(9), R2(9);
	FVector T1, T2;
	FBotSenses A1, A2;
	A1.Update(false, FVector::ZeroVector, Shot, Bot, 1.5, B, R1, T1);
	A2.Update(false, FVector::ZeroVector, Shot, Bot, 1.5, B, R2, T2);
	TestTrue(TEXT("deterministic noise"), T1.Equals(T2, 1e-6));
	return true;
}

#endif
