#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Learning/CombatStats.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace FPSCombatStats;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatStatsTest, "FPSAgent.Combat.Stats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatStatsTest::RunTest(const FString& Parameters)
{
	FEpisodeRecord A;
	A.bCleared = true; A.Return = 3.f; A.Shots = 10; A.Hits = 4; A.Headshots = 1; A.Kills = 2; A.Bots = 2; A.Seconds = 8.f;
	A.Terms[static_cast<int32>(FPSCombatReward::ETerm::Kill)] = 2.f;
	FEpisodeRecord B;
	B.bDied = true; B.Return = -1.f; B.Shots = 5; B.Hits = 1; B.Seconds = 4.f;
	FWindow W;
	W.Add(A);
	W.Add(B);
	TestEqual(TEXT("episodes"), W.Episodes, 2);
	TestEqual(TEXT("clear rate"), static_cast<float>(W.ClearRate()), 0.5f, 1e-6f);
	TestEqual(TEXT("death rate"), static_cast<float>(W.DeathRate()), 0.5f, 1e-6f);
	TestEqual(TEXT("avg return"), static_cast<float>(W.AvgReturn()), 1.f, 1e-6f);
	TestEqual(TEXT("accuracy = hits / shots"), static_cast<float>(W.Accuracy()), 5.f / 15.f, 1e-6f);
	TestEqual(TEXT("headshot rate = headshots / hits"), static_cast<float>(W.HeadshotRate()), 1.f / 5.f, 1e-6f);
	TestEqual(TEXT("clear time counts cleared episodes only"), static_cast<float>(W.AvgClearSeconds()), 8.f, 1e-6f);
	TestEqual(TEXT("avg kill term"), static_cast<float>(W.AvgTerm(static_cast<int32>(FPSCombatReward::ETerm::Kill))), 1.f, 1e-6f);

	const FLevelSummary L{ 0.3f, 0.1f, 0.5f };
	TArray<FString> Header, Row;
	CsvHeader().ParseIntoArray(Header, TEXT(","), false);
	CsvRow(W, 12.0, L, 900.0).ParseIntoArray(Row, TEXT(","), false);
	TestEqual(TEXT("csv columns match"), Row.Num(), Header.Num());
	TestTrue(TEXT("csv has every term"), CsvHeader().Contains(TEXT("term_AimHelper")));
	TestTrue(TEXT("log line prefix"), LogLine(W, 12.0, L, 900.0).StartsWith(TEXT("CombatStats: ")));

	TSharedPtr<FJsonObject> Obj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(EvalJson(W, TEXT("S0"), 1.f, 7, TEXT("/snap"), 0.7f));
	TestTrue(TEXT("eval json parses"), FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid());
	if (Obj.IsValid())
	{
		TestEqual(TEXT("eval episodes"), static_cast<int32>(Obj->GetNumberField(TEXT("episodes"))), 2);
		TestEqual(TEXT("eval clear rate"), static_cast<float>(Obj->GetNumberField(TEXT("clearRate"))), 0.5f, 1e-6f);
		TestFalse(TEXT("0.5 < 0.7 gate: not passed"), Obj->GetBoolField(TEXT("passed")));
		TestEqual(TEXT("stage"), Obj->GetStringField(TEXT("stage")), FString(TEXT("S0")));
		TestTrue(TEXT("terms object"), Obj->HasField(TEXT("terms")));
	}
	W.Reset();
	TestEqual(TEXT("reset"), W.Episodes, 0);
	return true;
}

#endif
