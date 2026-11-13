#include "Misc/AutomationTest.h"
#include "Learning/Curriculum.h"
#include "Arena/FloorGenerator.h"
#include "Arena/TileGraph.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace FPSCurriculum;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCurriculumControllerTest, "FPSAgent.Combat.Curriculum.Controller",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCurriculumControllerTest::RunTest(const FString& Parameters)
{
	const FCurriculumSettings C; // window 20, raise > 0.7, lower < 0.4, step 0.05
	FDifficultyController D;
	D.Init(0.f);
	for (int32 i = 0; i < 19; ++i) { TestEqual(TEXT("no change before the window fills"), D.Record(true, C), 0.f); }
	TestEqual(TEXT("20 clears raise the level"), D.Record(true, C), 0.05f, 1e-6f);
	TestEqual(TEXT("level"), D.Level, 0.05f, 1e-6f);
	TestEqual(TEXT("window resets after a change"), D.Window.Num(), 0);

	FDifficultyController Low;
	Low.Init(0.f);
	for (int32 i = 0; i < 25; ++i) { Low.Record(false, C); }
	TestEqual(TEXT("can't go below 0"), Low.Level, 0.f);
	TestEqual(TEXT("at the bound the window keeps rolling"), Low.Window.Num(), 20);

	FDifficultyController Mid;
	Mid.Init(0.5f);
	for (int32 i = 0; i < 20; ++i) { Mid.Record(false, C); }
	TestEqual(TEXT("20 failures lower the level"), Mid.Level, 0.45f, 1e-6f);

	FDifficultyController Even;
	Even.Init(0.5f);
	for (int32 i = 0; i < 30; ++i) { Even.Record(i % 2 == 0, C); }
	TestEqual(TEXT("50% clears: no change"), Even.Level, 0.5f, 1e-6f);
	TestEqual(TEXT("rolling window keeps the last 20"), Even.Window.Num(), 20);

	FDifficultyController Top;
	Top.Init(1.f);
	for (int32 i = 0; i < 20; ++i) { Top.Record(true, C); }
	TestEqual(TEXT("can't go above 1"), Top.Level, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCurriculumSamplerTest, "FPSAgent.Combat.Curriculum.SampleS0",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCurriculumSamplerTest::RunTest(const FString& Parameters)
{
	const FCurriculumSettings C;
	const float MaxRadius = 3700.f;
	for (int32 Seed = 0; Seed < 200; ++Seed)
	{
		FRandomStream Rng(Seed);
		const FS0Episode E = SampleS0(Rng, 0.f, false, C, MaxRadius);
		TestEqual(TEXT("level 0: one bot"), E.BotOffsets.Num(), 1);
		TestEqual(TEXT("crouch flags match"), E.BotCrouched.Num(), E.BotOffsets.Num());
		for (const FVector2D& P : E.BotOffsets)
		{
			const double Dist = P.Size();
			TestTrue(TEXT("level 0: at least the minimum distance"), Dist >= C.S0.BotDistanceMinCm - 1.0);
			TestTrue(TEXT("level 0: at most easy max + jitter (1700)"), Dist <= 1700.0 + 1.0);
			const float Rel = FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(P.Y, P.X)) - E.AgentYawDeg);
			TestTrue(TEXT("level 0: within 45 + jitter (58.5) degrees of the facing"), FMath::Abs(Rel) <= 58.5f + 0.01f);
		}
	}
	for (int32 Seed = 0; Seed < 50; ++Seed)
	{
		FRandomStream Rng(Seed);
		const FS0Episode E = SampleS0(Rng, 1.f, false, C, MaxRadius);
		TestEqual(TEXT("level 1: five bots"), E.BotOffsets.Num(), 5);
		for (const FVector2D& P : E.BotOffsets) { TestTrue(TEXT("inside the radius"), P.Size() <= MaxRadius + 1.0); }
	}

	FRandomStream A(42), B(42);
	const FS0Episode EA = SampleS0(A, 0.7f, true, C, MaxRadius);
	const FS0Episode EB = SampleS0(B, 0.7f, true, C, MaxRadius);
	TestEqual(TEXT("deterministic count"), EA.BotOffsets.Num(), EB.BotOffsets.Num());
	TestEqual(TEXT("deterministic yaw"), EA.AgentYawDeg, EB.AgentYawDeg);
	for (int32 i = 0; i < EA.BotOffsets.Num(); ++i) { TestEqual(TEXT("deterministic offsets"), EA.BotOffsets[i], EB.BotOffsets[i]); }

	int32 Reviews = 0;
	FRandomStream R(7);
	for (int32 i = 0; i < 2000; ++i)
	{
		const FS0Episode E = SampleS0(R, 0.6f, true, C, MaxRadius);
		if (E.bReview)
		{
			++Reviews;
			TestTrue(TEXT("review level is easier"), E.Level <= 0.6f + 1e-6f);
		}
		else
		{
			TestEqual(TEXT("counted episodes use the controller level"), E.Level, 0.6f);
		}
	}
	TestTrue(FString::Printf(TEXT("about 20%% reviews (%d of 2000)"), Reviews), Reviews >= 330 && Reviews <= 470);
	FRandomStream NoReview(7);
	for (int32 i = 0; i < 200; ++i) { TestFalse(TEXT("no reviews when not allowed (-Eval)"), SampleS0(NoReview, 0.6f, false, C, MaxRadius).bReview); }
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCurriculumSampleS1Test, "FPSAgent.Combat.Curriculum.SampleS1",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCurriculumSampleS1Test::RunTest(const FString& Parameters)
{
	const FCurriculumSettings C; // S1 defaults from MakeS1StageSpec
	const FBotBehaviourSettings B; // 103 deg view cone
	auto CoverCount = [](const FMapSpec& S)
	{
		int32 N = 0;
		for (const FString& Row : S.Tiles) { for (const TCHAR Ch : Row) { N += (Ch == TEXT('l') || Ch == TEXT('h')) ? 1 : 0; } }
		return N;
	};
	auto Check = [&](const FCombatEpisode& E, const TCHAR* What)
	{
		const FPSNav::FTileGraph G = FPSNav::FTileGraph::Build(E.Map);
		const FVector2D MapCenter(E.Map.Center().X, E.Map.Center().Y);
		const FVector2D AgentLocal = MapCenter + E.AgentOffset;
		const FIntPoint AgentTile = FPSNav::TileAt(E.Map, AgentLocal);
		TestTrue(FString::Printf(TEXT("%s: agent on a walkable tile"), What), G.IsWalkable(AgentTile));
		TSet<FIntPoint> Used = { AgentTile };
		for (const FBotSpawn& Bot : E.Bots)
		{
			const FVector2D Local = MapCenter + Bot.Offset;
			const FIntPoint Tile = FPSNav::TileAt(E.Map, Local);
			TestTrue(FString::Printf(TEXT("%s: bot on its own walkable tile"), What), G.IsWalkable(Tile) && !Used.Contains(Tile));
			Used.Add(Tile);
			const float ToAgent = FMath::RadiansToDegrees(FMath::Atan2(AgentLocal.Y - Local.Y, AgentLocal.X - Local.X));
			TestTrue(FString::Printf(TEXT("%s: no bot sees the agent at spawn"), What), FMath::Abs(FRotator::NormalizeAxis(ToAgent - Bot.YawDeg)) > 0.5f * B.SightFovDeg);
			if (Bot.Behaviour == FPSBots::EBotBehaviour::Peek)
			{
				TestTrue(FString::Printf(TEXT("%s: peek starts hidden"), What), Bot.PeekHidden == Tile && !FPSNav::HasTileLos(E.Map, AgentTile, Bot.PeekHidden));
				TestTrue(FString::Printf(TEXT("%s: peek steps into the open"), What), FPSNav::HasTileLos(E.Map, AgentTile, Bot.PeekExposed));
			}
		}
	};
	for (int32 Seed = 0; Seed < 40; ++Seed)
	{
		FRandomStream Rng(Seed);
		const FCombatEpisode E = SampleS1(Rng, 0.f, false, C, B);
		Check(E, TEXT("level 0"));
		TestEqual(TEXT("level 0: one bot"), E.Bots.Num(), 1);
		TestTrue(TEXT("level 0: almost no cover (<= 5 tiles, jitter)"), CoverCount(E.Map) <= 5);
		const FPSBots::EBotBehaviour Be = E.Bots[0].Behaviour;
		TestTrue(TEXT("level 0: strafe, peek or crouch"), Be == FPSBots::EBotBehaviour::Strafe || Be == FPSBots::EBotBehaviour::Peek || Be == FPSBots::EBotBehaviour::Crouch);
		TestTrue(TEXT("level 0: 60% speed"), E.Bots[0].SpeedScale >= 0.6f && E.Bots[0].SpeedScale <= 0.64f + 1e-4f);
	}
	bool bPatrol = false, bExplore = false;
	for (int32 Seed = 0; Seed < 30; ++Seed)
	{
		FRandomStream Rng(100 + Seed);
		const FCombatEpisode E = SampleS1(Rng, 1.f, false, C, B);
		Check(E, TEXT("level 1"));
		TestEqual(TEXT("level 1: five bots"), E.Bots.Num(), 5);
		TestTrue(TEXT("level 1: medium cover (43..48 tiles)"), CoverCount(E.Map) >= 43 && CoverCount(E.Map) <= 48);
		for (const FBotSpawn& Bot : E.Bots)
		{
			bPatrol |= Bot.Behaviour == FPSBots::EBotBehaviour::Patrol;
			bExplore |= Bot.Behaviour == FPSBots::EBotBehaviour::Explore;
			TestTrue(TEXT("level 1: full speed"), Bot.SpeedScale >= 0.96f - 1e-4f && Bot.SpeedScale <= 1.f);
		}
	}
	TestTrue(TEXT("level 1 mixes in patrol and explore"), bPatrol && bExplore);
	FRandomStream A(42), Bs(42);
	const FCombatEpisode EA = SampleS1(A, 0.6f, true, C, B);
	const FCombatEpisode EB = SampleS1(Bs, 0.6f, true, C, B);
	TestTrue(TEXT("deterministic map"), EA.Map.Tiles == EB.Map.Tiles);
	TestTrue(TEXT("deterministic agent"), EA.AgentOffset.Equals(EB.AgentOffset, 1e-6) && EA.AgentYawDeg == EB.AgentYawDeg);
	TestEqual(TEXT("deterministic bots"), EA.Bots.Num(), EB.Bots.Num());

	FS0Episode S0;
	S0.AgentYawDeg = 30.f;
	S0.BotOffsets = { FVector2D(1000.0, 0.0) };
	S0.BotCrouched = { true };
	const FCombatEpisode F = FromS0(S0, FPSArena::MakeOpenFloorSpec(20, TEXT("s0")));
	TestTrue(TEXT("S0: agent at the centre"), F.AgentOffset.IsNearlyZero());
	TestEqual(TEXT("S0: bot faces the agent"), F.Bots[0].YawDeg, 180.f, 1e-3f);
	TestTrue(TEXT("S0: static and crouched"), F.Bots[0].Behaviour == FPSBots::EBotBehaviour::Static && F.Bots[0].bCrouched);
	return true;
}

#endif
