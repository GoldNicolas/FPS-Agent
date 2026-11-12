#include "Misc/AutomationTest.h"
#include "Arena/ArenaBuilder.h"
#include "Common/Perception.h"
#include "Tests/FPSTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace FPSPerception;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSPerceptionMathTest, "FPSAgent.Perception.Math",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSPerceptionMathTest::RunTest(const FString& Parameters)
{
	const FVector Eye(0.0, 0.0, 0.0);
	const FVector2D Right10 = AimErrorDeg(Eye, FRotator::ZeroRotator, FVector(1000.0, 1000.0 * FMath::Tan(FMath::DegreesToRadians(10.0)), 0.0));
	TestEqual(TEXT("10 deg right = +10 yaw"), static_cast<float>(Right10.X), 10.f, 1e-3f);
	TestEqual(TEXT("level = 0 pitch"), static_cast<float>(Right10.Y), 0.f, 1e-3f);
	const FVector2D FromLeft = AimErrorDeg(Eye, FRotator(0.0, 350.0, 0.0), FVector(1000.0, 0.0, 0.0));
	TestEqual(TEXT("view yaw 350 -> turn +10"), static_cast<float>(FromLeft.X), 10.f, 1e-3f);
	const FVector2D Up5 = AimErrorDeg(Eye, FRotator(355.0, 0.0, 0.0), FVector(1000.0, 0.0, 0.0));
	TestEqual(TEXT("looking 5 deg down -> pitch up +5"), static_cast<float>(Up5.Y), 5.f, 1e-3f);

	auto AtYaw = [](double Deg) { return FVector(FMath::Cos(FMath::DegreesToRadians(Deg)) * 1000.0, FMath::Sin(FMath::DegreesToRadians(Deg)) * 1000.0, 0.0); };
	TestTrue(TEXT("51 deg is inside a 103 deg FOV"), InFieldOfView(Eye, FRotator::ZeroRotator, AtYaw(51.0), 103.f));
	TestFalse(TEXT("52 deg is outside"), InFieldOfView(Eye, FRotator::ZeroRotator, AtYaw(52.0), 103.f));
	TestFalse(TEXT("behind is outside"), InFieldOfView(Eye, FRotator::ZeroRotator, AtYaw(180.0), 103.f));
	const FVector Up30(1000.0, 0.0, 1000.0 * FMath::Tan(FMath::DegreesToRadians(30.0)));
	const FVector Up40(1000.0, 0.0, 1000.0 * FMath::Tan(FMath::DegreesToRadians(40.0)));
	TestTrue(TEXT("30 deg up is inside a 70.53 deg vertical FOV"), InFieldOfView(Eye, FRotator::ZeroRotator, Up30, 103.f, 70.53f));
	TestFalse(TEXT("40 deg up is outside"), InFieldOfView(Eye, FRotator::ZeroRotator, Up40, 103.f, 70.53f));
	TestTrue(TEXT("looking up 40 deg sees it"), InFieldOfView(Eye, FRotator(40.0, 0.0, 0.0), Up40, 103.f, 70.53f));

	const FVector V = ToViewFrame(FVector(0.0, 100.0, 5.0), 90.f);
	TestTrue(TEXT("world +Y is forward when facing yaw 90"), V.Equals(FVector(100.0, 0.0, 5.0), 1e-3));
	const FVector R = ToViewFrame(FVector(0.0, 100.0, 0.0), 0.f);
	TestTrue(TEXT("world +Y is right when facing yaw 0"), R.Equals(FVector(0.0, 100.0, 0.0), 1e-3));

	const TArray<float> Pitches = { -20.f, 0.f, 15.f };
	const TArray<FVector> Rays = RayDirections(0.f, 24, Pitches);
	TestEqual(TEXT("24 x 3 rays"), Rays.Num(), 72);
	TestTrue(TEXT("first ray: ahead, pitched down 20"), Rays[0].Equals(FRotator(-20.0, 0.0, 0.0).Vector(), 1e-4));
	TestTrue(TEXT("level ring starts straight ahead"), Rays[24].Equals(FVector(1.0, 0.0, 0.0), 1e-4));
	TestTrue(TEXT("15 deg steps"), Rays[25].Equals(FRotator(0.0, 15.0, 0.0).Vector(), 1e-4));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSPerceptionTracksTest, "FPSAgent.Perception.Tracks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSPerceptionTracksTest::RunTest(const FString& Parameters)
{
	auto Sight = [](int32 Bot, bool bVisible, bool bAlive)
	{
		FSighting S;
		S.BotIndex = Bot;
		S.bVisible = bVisible;
		S.bAlive = bAlive;
		S.Head = FVector(100.0 * Bot, 0.0, 0.0);
		return S;
	};
	TArray<FTrack> T;
	UpdateTracks(T, { Sight(1, true, true) }, 10.0, 2.f);
	TestEqual(TEXT("visible bot gets a track"), T.Num(), 1);
	TestTrue(TEXT("track visible"), T[0].bVisible);
	UpdateTracks(T, { Sight(1, false, true) }, 11.0, 2.f);
	TestEqual(TEXT("remembered within 2 s"), T.Num(), 1);
	TestFalse(TEXT("now remembered, not visible"), T[0].bVisible);
	TestEqual(TEXT("last seen time kept"), T[0].LastSeenTime, 10.0);
	UpdateTracks(T, { Sight(1, false, true) }, 12.5, 2.f);
	TestEqual(TEXT("forgotten after 2 s"), T.Num(), 0);
	UpdateTracks(T, { Sight(2, true, true) }, 20.0, 2.f);
	UpdateTracks(T, { Sight(2, false, false) }, 20.1, 2.f);
	TestEqual(TEXT("dead bots drop out"), T.Num(), 0);
	UpdateTracks(T, { Sight(3, true, true) }, 30.0, 2.f);
	UpdateTracks(T, { Sight(4, false, true) }, 30.1, 2.f);
	TestEqual(TEXT("never-seen bots get no track"), T.Num(), 1);

	FTrack A; A.BotIndex = 0; A.bVisible = true;  A.LastSeenTime = 5.0;
	FTrack B; B.BotIndex = 1; B.bVisible = false; B.LastSeenTime = 9.0;
	FTrack C; C.BotIndex = 2; C.bVisible = false; C.LastSeenTime = 8.0;
	const TArray<FTrack> Top = SelectTracks({ C, B, A }, 2);
	TestEqual(TEXT("limited"), Top.Num(), 2);
	TestEqual(TEXT("visible first"), Top[0].BotIndex, 0);
	TestEqual(TEXT("then most recent"), Top[1].BotIndex, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSPerceptionWorldTest, "FPSAgent.Perception.World",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSPerceptionWorldTest::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	W.SpawnBean(FVector(500.0, 0.0, 100.0), FRotator::ZeroRotator);                                // a pawn on the +X line
	FPSArena::SpawnBlock(W.World, FVector(500.0, 1000.0, 150.0), FVector(50.0, 400.0, 400.0));    // a wall on the y=1000 line
	FPSArena::SpawnBlock(W.World, FVector(1000.0, -1000.0, 150.0), FVector(50.0, 400.0, 400.0));  // a wall on the y=-1000 line
	W.Step();
	TestTrue(TEXT("pawns never block line of sight"), HasLineOfSight(W.World, FVector(0.0, 0.0, 100.0), FVector(1000.0, 0.0, 100.0)));
	TestFalse(TEXT("walls block line of sight"), HasLineOfSight(W.World, FVector(0.0, 1000.0, 100.0), FVector(1000.0, 1000.0, 100.0)));
	TestEqual(TEXT("rays pass through pawns"), RayFraction(W.World, FVector(0.0, 0.0, 100.0), FVector(1.0, 0.0, 0.0), 3000.f), 1.f);
	TestEqual(TEXT("rays stop at walls"), RayFraction(W.World, FVector(0.0, -1000.0, 100.0), FVector(1.0, 0.0, 0.0), 3000.f), 975.f / 3000.f, 0.01f);
	return true;
}

#endif
