#include "Misc/AutomationTest.h"
#include "Character/BeanHitboxLayout.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSHitboxLayoutTest, "FPSAgent.Character.HitboxLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSHitboxLayoutTest::RunTest(const FString& Parameters)
{
	const FBeanBody B;
	for (const float H : { B.StandingHalfHeight, B.CrouchedHalfHeight })
	{
		const FBeanHitboxLayout L = ComputeBeanHitboxLayout(B, H);
		const FString Tag = FString::Printf(TEXT("[H=%.0f] "), H);
		TestEqual(*(Tag + TEXT("legs start at the feet")), L.LegsCenterZ - static_cast<float>(L.LegsExtent.Z), -H, 1e-3f);
		TestEqual(*(Tag + TEXT("legs meet body")), L.LegsCenterZ + static_cast<float>(L.LegsExtent.Z), L.BodyCenterZ - static_cast<float>(L.BodyExtent.Z), 1e-3f);
		TestEqual(*(Tag + TEXT("body meets head")), L.BodyCenterZ + static_cast<float>(L.BodyExtent.Z), L.HeadCenterZ - L.HeadRadius, 1e-3f);
		TestTrue(*(Tag + TEXT("head inside capsule")), L.HeadCenterZ + L.HeadRadius <= H);
		TestTrue(*(Tag + TEXT("body has height")), L.BodyExtent.Z > 5.f);
		TestEqual(*(Tag + TEXT("eye at head center")), L.EyeZ, L.HeadCenterZ);
	}
	const float StandingHeadAboveFloor = B.StandingHalfHeight + ComputeBeanHitboxLayout(B, B.StandingHalfHeight).HeadCenterZ;
	const float CrouchedHeadAboveFloor = B.CrouchedHalfHeight + ComputeBeanHitboxLayout(B, B.CrouchedHalfHeight).HeadCenterZ;
	TestEqual(TEXT("standing head height"), StandingHeadAboveFloor, 158.f, 1e-3f);
	TestTrue(TEXT("crouching lowers the head"), CrouchedHeadAboveFloor < StandingHeadAboveFloor - 40.f);
	return true;
}

#endif
