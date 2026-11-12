#include "Misc/AutomationTest.h"
#include "Learning/CombatReward.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace FPSCombatReward;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatRewardTermsTest, "FPSAgent.Combat.Reward.Terms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatRewardTermsTest::RunTest(const FString& Parameters)
{
	const FCombatRewardSettings S;
	TestEqual(TEXT("kill from full = 1"), DamageDealt(150.f, S), 1.f, 1e-6f);
	TestEqual(TEXT("body shot"), DamageDealt(40.f, S), 40.f / 150.f, 1e-6f);
	TestEqual(TEXT("damage taken is negative"), DamageTaken(75.f, S), -0.5f, 1e-6f);
	TestEqual(TEXT("instant clear = 2.0"), ClearBonus(1.f, S), 2.f, 1e-6f);
	TestEqual(TEXT("clear at the limit = 0.5"), ClearBonus(0.f, S), 0.5f, 1e-6f);
	TestEqual(TEXT("clear halfway"), ClearBonus(0.5f, S), 1.25f, 1e-6f);
	TestEqual(TEXT("clear clamps"), ClearBonus(3.f, S), 2.f, 1e-6f);
	TestEqual(TEXT("no visible enemy: potential 0"), AimPotential(-1.f, 0.2f, S), 0.f);
	TestEqual(TEXT("on target: potential W"), AimPotential(0.f, 0.2f, S), 0.2f, 1e-6f);
	TestEqual(TEXT("half range: W/2"), AimPotential(15.f, 0.2f, S), 0.1f, 1e-6f);
	TestEqual(TEXT("beyond range: 0, never negative"), AimPotential(45.f, 0.2f, S), 0.f);
	TestEqual(TEXT("term names"), FString(TermName(ETerm::AimHelper)), FString(TEXT("AimHelper")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatRewardLedgerTest, "FPSAgent.Combat.Reward.Ledger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatRewardLedgerTest::RunTest(const FString& Parameters)
{
	const FCombatRewardSettings S;
	FLedger L;
	L.BeginEpisode(0.f);
	L.Add(ETerm::Kill, 1.f);
	L.Add(ETerm::Miss, -0.01f);
	TestEqual(TEXT("pending kill"), L.PendingTerm(ETerm::Kill), 1.f);
	TestEqual(TEXT("decision reward"), L.TakeDecision(), 0.99f, 1e-6f);
	TestEqual(TEXT("taken once: next decision is 0"), L.TakeDecision(), 0.f);
	TestEqual(TEXT("episode return"), L.EpisodeReturn(), 0.99f, 1e-6f);
	TestEqual(TEXT("episode kill term"), L.EpisodeTerm(ETerm::Kill), 1.f);

	TestTrue(TEXT("first sighting of bot 2"), L.TrySighting(2, S));
	TestFalse(TEXT("bot 2 again: no"), L.TrySighting(2, S));
	TestTrue(TEXT("bot 3 is new"), L.TrySighting(3, S));
	TestEqual(TEXT("two sightings pending"), L.PendingTerm(ETerm::FirstSighting), 0.4f, 1e-6f);

	TestTrue(TEXT("clear pays"), L.TryClear(0.5f, S));
	TestFalse(TEXT("clear pays once (D-022)"), L.TryClear(1.f, S));
	TestEqual(TEXT("clear amount"), L.PendingTerm(ETerm::Clear), 1.25f, 1e-6f);

	L.BeginEpisode(0.f);
	TestEqual(TEXT("new episode: return cleared"), L.EpisodeReturn(), 0.f);
	TestEqual(TEXT("new episode: pending cleared"), L.PendingTerm(ETerm::Clear), 0.f);
	TestTrue(TEXT("new episode: sightings reset"), L.TrySighting(2, S));
	TestTrue(TEXT("new episode: clear available"), L.TryClear(0.f, S));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatRewardPotentialTest, "FPSAgent.Combat.Reward.AimPotential",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatRewardPotentialTest::RunTest(const FString& Parameters)
{
	// Potential-based shaping telescopes: with discount 1 the episode sum is Phi(end) - Phi(start) = -Phi(start).
	FLedger L;
	L.BeginEpisode(0.1f);
	L.ApplyPotential(0.15f, 1.f);
	TestEqual(TEXT("closer aim pays the difference"), L.PendingTerm(ETerm::AimHelper), 0.05f, 1e-6f);
	L.ApplyPotential(0.f, 1.f);
	TestEqual(TEXT("sum telescopes to -Phi(start)"), L.PendingTerm(ETerm::AimHelper), -0.1f, 1e-6f);

	FLedger D;
	D.BeginEpisode(0.f);
	D.ApplyPotential(0.2f, 0.995f);
	TestEqual(TEXT("discounted gamma*Phi' - Phi"), D.PendingTerm(ETerm::AimHelper), 0.995f * 0.2f, 1e-6f);
	return true;
}

#endif
