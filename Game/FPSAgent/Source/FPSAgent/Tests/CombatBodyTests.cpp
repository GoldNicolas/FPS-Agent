#include "Misc/AutomationTest.h"
#include "Arena/FloorGenerator.h"
#include "Arena/MapBuilder.h"
#include "Bots/ScriptedBot.h"
#include "Character/BeanAIController.h"
#include "Tests/FPSTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSOpenFloorTest, "FPSAgent.Arena.OpenFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSOpenFloorTest::RunTest(const FString& Parameters)
{
	const FMapSpec S = FPSArena::MakeOpenFloorSpec(3, TEXT("floor3"));
	TestEqual(TEXT("name"), S.Name, FString(TEXT("floor3")));
	TestEqual(TEXT("rows = tiles + wall ring"), S.Rows(), 5);
	TestEqual(TEXT("cols"), S.Cols(), 5);
	TestEqual(TEXT("top wall row"), S.Tiles[0], FString(TEXT("#####")));
	TestEqual(TEXT("inner row"), S.Tiles[2], FString(TEXT("#...#")));
	TestTrue(TEXT("inside walkable"), S.IsWalkable(2, 2));
	TestFalse(TEXT("ring not walkable"), S.IsWalkable(0, 2));
	TestEqual(TEXT("level 0"), S.LevelAt(2, 2), 0);
	TestTrue(TEXT("centre"), S.Center().Equals(FVector(1000.0, 1000.0, 0.0), 1e-3));
	TestTrue(TEXT("builds blocks"), FPSArena::ComputeMapBlocks(S).Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSAIControllerAimTest, "FPSAgent.Character.AIControllerKeepsAim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSAIControllerAimTest::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	ABeanCharacter* Bean = W.SpawnBean(FVector(0.0, 0.0, 100.0), FRotator::ZeroRotator);
	ABeanAIController* C = W.World->SpawnActor<ABeanAIController>();
	C->Possess(Bean);
	C->SetControlRotation(FRotator(-20.0, 30.0, 0.0));
	C->UpdateControlRotation(1.f / 60.f, true); // AAIController would reset the pitch to 0 here
	TestEqual(TEXT("pitch kept"), static_cast<float>(FRotator::NormalizeAxis(C->GetControlRotation().Pitch)), -20.f, 1e-3f);
	TestEqual(TEXT("yaw kept"), static_cast<float>(C->GetControlRotation().Yaw), 30.f, 1e-3f);
	TestEqual(TEXT("pawn turned to the yaw"), static_cast<float>(Bean->GetActorRotation().Yaw), 30.f, 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSScriptedBotTest, "FPSAgent.Bots.ScriptedBot.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSScriptedBotTest::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AScriptedBot* Bot = W.World->SpawnActor<AScriptedBot>(FVector(0.0, 0.0, 100.0), FRotator::ZeroRotator, P);
	Bot->Deactivate();
	TestFalse(TEXT("inactive"), Bot->IsActive());
	TestTrue(TEXT("inactive bots are hidden"), Bot->IsHidden());

	Bot->Activate(FVector(500.0, 0.0, 90.0), 180.f, true, 3);
	TestTrue(TEXT("active"), Bot->IsActive());
	TestTrue(TEXT("alive"), Bot->IsAlive());
	TestFalse(TEXT("visible"), Bot->IsHidden());
	TestTrue(TEXT("placed"), Bot->GetActorLocation().Equals(FVector(500.0, 0.0, 90.0), 1.0));
	TestEqual(TEXT("faces yaw 180"), FMath::Abs(static_cast<float>(FRotator::NormalizeAxis(Bot->GetActorRotation().Yaw))), 180.f, 1e-3f);
	TestTrue(TEXT("wants to crouch"), Bot->GetCharacterMovement()->bWantsToCrouch);

	Bot->GetHealth()->ApplyHit(500.f, nullptr);
	TestFalse(TEXT("dead"), Bot->IsAlive());
	TestTrue(TEXT("dead bots hide"), Bot->IsHidden());
	TestFalse(TEXT("dead bots have no collision"), Bot->GetActorEnableCollision());

	Bot->Activate(FVector(0.0, 500.0, 90.0), 0.f, false, 4);
	TestTrue(TEXT("alive again next episode"), Bot->IsAlive());
	TestEqual(TEXT("full health"), Bot->GetHealth()->GetHealth(), 100.f);
	TestFalse(TEXT("standing"), Bot->GetCharacterMovement()->bWantsToCrouch);
	return true;
}

#endif
