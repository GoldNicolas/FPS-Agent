#include "Misc/AutomationTest.h"
#include "Common/FPSAgentSettings.h"
#include "Common/SettingsOverride.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSSettingsOverrideTest, "FPSAgent.Settings.Override",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSSettingsOverrideTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UFPSAgentSettings> S(NewObject<UFPSAgentSettings>(GetTransientPackage()));
	TArray<FString> Applied;
	FString Error;
	const bool bOk = FPSSettingsOverride::Apply(*S, TEXT("Combat.Reward.Kill=1.5; Combat.Ppo.Discount=0.997;Combat.Ppo.bTrainOnGpu=false;Curriculum.S0.BotsHard=3"), Applied, Error);
	TestTrue(TEXT("valid overrides apply"), bOk);
	TestEqual(TEXT("error empty"), Error, FString());
	TestEqual(TEXT("float"), S->Combat.Reward.Kill, 1.5f);
	TestEqual(TEXT("float in another struct"), S->Combat.Ppo.Discount, 0.997f);
	TestFalse(TEXT("bool"), S->Combat.Ppo.bTrainOnGpu);
	TestEqual(TEXT("int"), S->Curriculum.S0.BotsHard, 3);
	TestEqual(TEXT("applied list"), Applied.Num(), 4);
	TestEqual(TEXT("applied text is trimmed"), Applied[1], FString(TEXT("Combat.Ppo.Discount=0.997")));

	auto Fails = [&](const TCHAR* Spec, const TCHAR* Expect)
	{
		TArray<FString> A;
		FString E;
		const bool bResult = FPSSettingsOverride::Apply(*S, Spec, A, E);
		TestFalse(FString::Printf(TEXT("rejects %s"), Spec), bResult);
		TestTrue(FString::Printf(TEXT("error for %s mentions '%s' (got '%s')"), Spec, Expect, *E), E.Contains(Expect));
	};
	Fails(TEXT("Combat.Reward.Nope=1"), TEXT("unknown setting"));
	Fails(TEXT("Combat.Reward.Kill.X=1"), TEXT("not a struct"));
	Fails(TEXT("Combat.Reward.Kill=abc"), TEXT("not a number"));
	Fails(TEXT("Curriculum.S0.BotsHard=2.5"), TEXT("not an integer"));
	Fails(TEXT("Combat.Ppo.bTrainOnGpu=maybe"), TEXT("not a bool"));
	Fails(TEXT("Combat.Reward.Kill"), TEXT("Path=Value"));

	TArray<FString> None;
	TestTrue(TEXT("empty spec is fine"), FPSSettingsOverride::Apply(*S, TEXT(""), None, Error));
	TestEqual(TEXT("nothing applied"), None.Num(), 0);
	return true;
}

#endif
