#include "Character/BeanPlayerController.h"
#include "Arena/ArenaGameMode.h"
#include "Camera/CameraActor.h"
#include "Character/BeanCharacter.h"
#include "Common/FPSAgentSettings.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "FPSAgent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Learning/CombatTaskActor.h"
#include "Weapons/RifleComponent.h"

void ABeanPlayerController::BuildInput()
{
	if (Mapping)
	{
		return;
	}
	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = Type;
		return Action;
	};
	MoveAction   = MakeAction(TEXT("IA_Move"),   EInputActionValueType::Axis2D);
	LookAction   = MakeAction(TEXT("IA_Look"),   EInputActionValueType::Axis2D);
	FireAction   = MakeAction(TEXT("IA_Fire"),   EInputActionValueType::Boolean);
	JumpAction   = MakeAction(TEXT("IA_Jump"),   EInputActionValueType::Boolean);
	CrouchAction = MakeAction(TEXT("IA_Crouch"), EInputActionValueType::Boolean);
	WalkAction   = MakeAction(TEXT("IA_Walk"),   EInputActionValueType::Boolean);
	ReloadAction = MakeAction(TEXT("IA_Reload"), EInputActionValueType::Boolean);

	Mapping = NewObject<UInputMappingContext>(this, TEXT("IMC_Bean"));
	auto Swizzle = [this]() { return NewObject<UInputModifierSwizzleAxis>(Mapping); }; // X -> Y (forward)
	auto Negate  = [this]() { return NewObject<UInputModifierNegate>(Mapping); };
	Mapping->MapKey(MoveAction, EKeys::W).Modifiers.Add(Swizzle());
	{
		FEnhancedActionKeyMapping& Back = Mapping->MapKey(MoveAction, EKeys::S);
		Back.Modifiers.Add(Swizzle());
		Back.Modifiers.Add(Negate());
	}
	Mapping->MapKey(MoveAction, EKeys::A).Modifiers.Add(Negate());
	Mapping->MapKey(MoveAction, EKeys::D);
	Mapping->MapKey(LookAction, EKeys::Mouse2D);
	Mapping->MapKey(FireAction, EKeys::LeftMouseButton);
	Mapping->MapKey(JumpAction, EKeys::SpaceBar);
	Mapping->MapKey(CrouchAction, EKeys::LeftControl);
	Mapping->MapKey(WalkAction, EKeys::LeftShift);
	Mapping->MapKey(ReloadAction, EKeys::R);
}

void ABeanPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	BuildInput();
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		UE_LOG(LogFPSAgent, Error, TEXT("Enhanced Input is not active: check Config/DefaultInput.ini"));
		return;
	}
	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ABeanPlayerController::OnMove);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ABeanPlayerController::OnLook);
	Input->BindAction(FireAction, ETriggerEvent::Started, this, &ABeanPlayerController::OnFirePressed);
	Input->BindAction(FireAction, ETriggerEvent::Completed, this, &ABeanPlayerController::OnFireReleased);
	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ABeanPlayerController::OnJumpPressed);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ABeanPlayerController::OnJumpReleased);
	Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &ABeanPlayerController::OnCrouchPressed);
	Input->BindAction(CrouchAction, ETriggerEvent::Completed, this, &ABeanPlayerController::OnCrouchReleased);
	Input->BindAction(WalkAction, ETriggerEvent::Started, this, &ABeanPlayerController::OnWalkPressed);
	Input->BindAction(WalkAction, ETriggerEvent::Completed, this, &ABeanPlayerController::OnWalkReleased);
	Input->BindAction(ReloadAction, ETriggerEvent::Started, this, &ABeanPlayerController::OnReload);
}

void ABeanPlayerController::BeginPlay()
{
	Super::BeginPlay();
	BuildInput();
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
		{
			Subsystem->AddMappingContext(Mapping, 0);
		}
	}
	SetInputMode(FInputModeGameOnly());
	SetShowMouseCursor(false);
}

ABeanCharacter* ABeanPlayerController::GetBean() const
{
	return Cast<ABeanCharacter>(GetPawn());
}

void ABeanPlayerController::OnMove(const FInputActionValue& Value)
{
	ABeanCharacter* Bean = GetBean();
	if (!Bean)
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	const FRotationMatrix Yaw(FRotator(0.f, GetControlRotation().Yaw, 0.f));
	Bean->AddMovementInput(Yaw.GetUnitAxis(EAxis::X), Axis.Y);
	Bean->AddMovementInput(Yaw.GetUnitAxis(EAxis::Y), Axis.X);
}

void ABeanPlayerController::OnLook(const FInputActionValue& Value)
{
	const FVector2D Delta = Value.Get<FVector2D>() * UFPSAgentSettings::Get().MouseDegreesPerCount;
	FRotator Rotation = GetControlRotation();
	Rotation.Yaw += Delta.X;
	Rotation.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Rotation.Pitch + Delta.Y), -89.0, 89.0);
	SetControlRotation(Rotation);
}

void ABeanPlayerController::OnFirePressed()   { if (ABeanCharacter* B = GetBean()) { B->GetRifle()->SetTriggerHeld(true); } }
void ABeanPlayerController::OnFireReleased()  { if (ABeanCharacter* B = GetBean()) { B->GetRifle()->SetTriggerHeld(false); } }
void ABeanPlayerController::OnJumpPressed()   { if (ABeanCharacter* B = GetBean()) { B->Jump(); } }
void ABeanPlayerController::OnJumpReleased()  { if (ABeanCharacter* B = GetBean()) { B->StopJumping(); } }
void ABeanPlayerController::OnCrouchPressed() { if (ABeanCharacter* B = GetBean()) { B->Crouch(); } }
void ABeanPlayerController::OnCrouchReleased(){ if (ABeanCharacter* B = GetBean()) { B->UnCrouch(); } }
void ABeanPlayerController::OnWalkPressed()   { if (ABeanCharacter* B = GetBean()) { B->SetWalking(true); } }
void ABeanPlayerController::OnWalkReleased()  { if (ABeanCharacter* B = GetBean()) { B->SetWalking(false); } }
void ABeanPlayerController::OnReload()        { if (ABeanCharacter* B = GetBean()) { B->GetRifle()->RequestReload(); } }

// Console variables (global: they also work from the editor console and Unreal-MCP's console tool,
// which run against the editor world and never reach a PIE player controller's Exec functions).
static TAutoConsoleVariable<int32> CVarTopDown(TEXT("fps.TopDown"), 0,
	TEXT("1 = view the arena from above (north up); 0 = back to the bean."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarShowHitboxes(TEXT("fps.ShowHitboxes"), 0,
	TEXT("1 = draw every bean's head/body/legs hitboxes."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarWatchAgent(TEXT("fps.WatchAgent"), -1,
	TEXT("Combat stages: N >= 0 = view through arena N's agent (first person, its own aim); -1 = back to the spectator. Viewing only."), ECVF_Default);

void ABeanPlayerController::ShowHitboxes(bool bShow)
{
	CVarShowHitboxes->Set(bShow ? 1 : 0);
}

void ABeanPlayerController::TopDownView(bool bOn)
{
	CVarTopDown->Set(bOn ? 1 : 0);
}

void ABeanPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	const bool bWantTopDown = CVarTopDown.GetValueOnGameThread() != 0;
	if (bWantTopDown != bTopDownActive)
	{
		ApplyTopDown(bWantTopDown);
	}
	const bool bWantHitboxes = CVarShowHitboxes.GetValueOnGameThread() != 0;
	if (bWantHitboxes != bHitboxesShown)
	{
		ApplyHitboxes(bWantHitboxes);
	}
	const int32 WantWatch = FMath::Max(-1, CVarWatchAgent.GetValueOnGameThread());
	if (WantWatch != WatchedArena || (WantWatch >= 0 && !WatchedAgent.IsValid()))
	{
		ApplyWatchAgent(WantWatch);
	}
}

void ABeanPlayerController::ApplyWatchAgent(int32 ArenaIndex)
{
	if (ArenaIndex < 0)
	{
		const bool bWasWatching = WatchedArena >= 0;
		WatchedArena = -1;
		WatchedAgent = nullptr;
		if (bWasWatching)
		{
			if (APawn* MyPawn = GetPawn())
			{
				SetViewTargetWithBlend(MyPawn, 0.f);
			}
		}
		return;
	}
	for (TActorIterator<ACombatTaskActor> It(GetWorld()); It; ++It)
	{
		if (ABeanCharacter* Agent = It->GetArenaAgent(ArenaIndex))
		{
			WatchedArena = ArenaIndex;
			WatchedAgent = Agent;
			SetViewTargetWithBlend(Agent, 0.f); // the agent's camera follows its controller's aim
			UE_LOG(LogFPSAgent, Display, TEXT("fps.WatchAgent: viewing through arena %d's agent"), ArenaIndex);
			return;
		}
	}
	// No combat task (yet), or no such arena: PlayerTick tries again next frame.
}

void ABeanPlayerController::ApplyHitboxes(bool bShow)
{
	bHitboxesShown = bShow;
	for (TActorIterator<ABeanCharacter> It(GetWorld()); It; ++It)
	{
		It->SetHitboxesVisible(bShow);
	}
}

void ABeanPlayerController::ApplyTopDown(bool bOn)
{
	bTopDownActive = bOn;
	if (!bOn)
	{
		if (APawn* MyPawn = GetPawn())
		{
			SetViewTargetWithBlend(MyPawn, 0.f);
		}
		return;
	}
	if (!TopDownCamera)
	{
		FBox Bounds(FVector(-2000.f), FVector(2000.f));
		if (const AArenaGameMode* Arena = GetWorld()->GetAuthGameMode<AArenaGameMode>())
		{
			if (Arena->GetArenaBounds().IsValid)
			{
				Bounds = Arena->GetArenaBounds();
			}
		}
		// Fit the whole arena in a 90-degree (horizontal) camera on a 16:9 view, north (+X) up.
		const FVector Extent = Bounds.GetExtent();
		const double HalfH = FMath::DegreesToRadians(45.0);
		const double HalfV = FMath::Atan(FMath::Tan(HalfH) * 9.0 / 16.0);
		const double Height = FMath::Max(Extent.X / FMath::Tan(HalfV), Extent.Y / FMath::Tan(HalfH)) * 1.08;
		const FVector Location(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Max.Z + Height);
		TopDownCamera = GetWorld()->SpawnActor<ACameraActor>(Location, FRotator::ZeroRotator);
		TopDownCamera->SetActorRotation(FRotator(-90.0, 0.0, 0.0)); // after spawn: spawning composes with class defaults
	}
	SetViewTargetWithBlend(TopDownCamera, 0.f);
}
