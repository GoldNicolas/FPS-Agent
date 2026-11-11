#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSSanityTest, "FPSAgent.Sanity.Pipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFPSSanityTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("arithmetic still works"), 2 + 2, 4);
	return true;
}

#endif
