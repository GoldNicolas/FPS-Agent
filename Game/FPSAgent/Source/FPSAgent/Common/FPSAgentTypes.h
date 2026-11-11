#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "FPSAgentTypes.generated.h"

/** Hitscan trace channel: "Weapon" (ECC_GameTraceChannel1, default Block) in Config/DefaultEngine.ini. */
inline constexpr ECollisionChannel ECC_Weapon = ECC_GameTraceChannel1;

UENUM(BlueprintType)
enum class EHitZone : uint8 { None, Head, Body, Legs };

UENUM(BlueprintType)
enum class ERangeBotMode : uint8 { Static, Strafe };

/** Movement tuning (cm, cm/s). "verify" = community-sourced estimate, confirm by play-testing. */
USTRUCT(BlueprintType)
struct FMovementTuning
{
	GENERATED_BODY()
	/** ~5.4 m/s with a rifle (community-sourced, verify). */
	UPROPERTY(EditAnywhere, Category="Movement") float RunSpeed = 540.f;
	/** Shift-walk speed, ~55% of run (verify). */
	UPROPERTY(EditAnywhere, Category="Movement") float WalkSpeed = 300.f;
	/** Crouch speed, ~35% of run (verify). */
	UPROPERTY(EditAnywhere, Category="Movement") float CrouchSpeed = 190.f;
	/** High accel/braking = snappy counter-strafe (tune by feel). */
	UPROPERTY(EditAnywhere, Category="Movement") float MaxAcceleration = 4000.f;
	UPROPERTY(EditAnywhere, Category="Movement") float BrakingDeceleration = 6000.f;
	UPROPERTY(EditAnywhere, Category="Movement") float GroundFriction = 8.f;
	/** Jump launch speed (verify jump height). */
	UPROPERTY(EditAnywhere, Category="Movement") float JumpZVelocity = 420.f;
	UPROPERTY(EditAnywhere, Category="Movement") float AirControl = 0.3f;
	/** Fully accurate at or below this fraction of RunSpeed: 27.5% (secondary source, verify). */
	UPROPERTY(EditAnywhere, Category="Movement") float AccurateSpeedFraction = 0.275f;
};

/** Bean geometry (cm). Hitboxes derive from it via ComputeBeanHitboxLayout. */
USTRUCT(BlueprintType)
struct FBeanBody
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Body") float CapsuleRadius = 34.f;
	/** 1.76 m tall standing. */
	UPROPERTY(EditAnywhere, Category="Body") float StandingHalfHeight = 88.f;
	UPROPERTY(EditAnywhere, Category="Body") float CrouchedHalfHeight = 60.f;
	UPROPERTY(EditAnywhere, Category="Body") float HeadRadius = 12.f;
	UPROPERTY(EditAnywhere, Category="Body") float HeadTopClearance = 6.f;
	/** Fraction of total height covered by the legs hitbox. */
	UPROPERTY(EditAnywhere, Category="Body") float LegFraction = 0.386f;
	UPROPERTY(EditAnywhere, Category="Body") float BodyHalfDepth = 18.f;
	UPROPERTY(EditAnywhere, Category="Body") float BodyHalfWidth = 26.f;
	UPROPERTY(EditAnywhere, Category="Body") float LegsHalfDepth = 16.f;
	UPROPERTY(EditAnywhere, Category="Body") float LegsHalfWidth = 20.f;
};

/** Automatic rifle with Vandal-like stats (generic name). Degrees / seconds / cm. */
USTRUCT(BlueprintType)
struct FRifleStats
{
	GENERATED_BODY()
	/** 160 / 40 / 34, no falloff (2025-26 guides, verify). */
	UPROPERTY(EditAnywhere, Category="Damage") float DamageHead = 160.f;
	UPROPERTY(EditAnywhere, Category="Damage") float DamageBody = 40.f;
	UPROPERTY(EditAnywhere, Category="Damage") float DamageLegs = 34.f;
	UPROPERTY(EditAnywhere, Category="Damage") float MaxRangeCm = 10000.f;
	/** Rounds per second (guides, verify). */
	UPROPERTY(EditAnywhere, Category="Handling") float FireRate = 9.75f;
	UPROPERTY(EditAnywhere, Category="Handling") int32 MagazineSize = 25;
	/** Seconds (verify). */
	UPROPERTY(EditAnywhere, Category="Handling") float ReloadTime = 2.5f;
	/** Standing-still first-shot spread, cone half-angle (guide, verify). */
	UPROPERTY(EditAnywhere, Category="Spread") float FirstShotSpreadDeg = 0.25f;
	/** Spread just above the accurate-speed threshold (tune). */
	UPROPERTY(EditAnywhere, Category="Spread") float WalkSpreadDeg = 2.5f;
	/** Spread at full run speed (tune). */
	UPROPERTY(EditAnywhere, Category="Spread") float RunSpreadDeg = 5.f;
	UPROPERTY(EditAnywhere, Category="Spread") float AirSpreadDeg = 8.f;
	UPROPERTY(EditAnywhere, Category="Spread") float CrouchSpreadMultiplier = 0.85f;
	/** Shots 0..AccurateShots-1 get no spray bloom. */
	UPROPERTY(EditAnywhere, Category="Spread") int32 AccurateShots = 3;
	UPROPERTY(EditAnywhere, Category="Spread") float SprayBloomPerShotDeg = 0.35f;
	UPROPERTY(EditAnywhere, Category="Spread") float SprayBloomMaxDeg = 3.f;
	/** Our own recoil pattern: small kick, then vertical climb to a cap, then horizontal sway. */
	UPROPERTY(EditAnywhere, Category="Recoil") float RecoilKickFirstShotsDeg = 0.15f;
	UPROPERTY(EditAnywhere, Category="Recoil") float RecoilClimbPerShotDeg = 0.6f;
	UPROPERTY(EditAnywhere, Category="Recoil") float RecoilClimbMaxDeg = 6.f;
	UPROPERTY(EditAnywhere, Category="Recoil") float RecoilSwayAmplitudeDeg = 1.5f;
	UPROPERTY(EditAnywhere, Category="Recoil") int32 RecoilSwayPeriodShots = 6;
	/** Idle seconds that recover one step of the spray index (verify feel). */
	UPROPERTY(EditAnywhere, Category="Recoil") float RecoveryTimePerShot = 0.04f;
};

USTRUCT(BlueprintType)
struct FHealthTuning
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Health") float MaxHealth = 100.f;
	/** Heavy-shield equivalent (verify). */
	UPROPERTY(EditAnywhere, Category="Health") float MaxArmor = 50.f;
	/** Fraction of each hit taken by armor while armor remains (verify: 1.0 = armor absorbs everything first). */
	UPROPERTY(EditAnywhere, Category="Health") float ArmorAbsorbFraction = 1.f;
};

USTRUCT(BlueprintType)
struct FRangeLane
{
	GENERATED_BODY()
	/** Distance in front of the player (+X). */
	UPROPERTY(EditAnywhere, Category="Range") float DistanceCm = 1000.f;
	/** Sideways offset (+Y = right of the player). */
	UPROPERTY(EditAnywhere, Category="Range") float LateralCm = 0.f;
	UPROPERTY(EditAnywhere, Category="Range") ERangeBotMode Mode = ERangeBotMode::Static;
};

/** What the arena game mode builds. Precedence: -Map= command line > fps.PlayMap console variable > this setting. */
USTRUCT(BlueprintType)
struct FMapPlaySettings
{
	GENERATED_BODY()
	/** "" or "range" = shooting range; "map01" / "gen_007" = that spec in Content/MapSpecs; "pool" = seeded random pool entry. */
	UPROPERTY(EditAnywhere, Category="Map") FString PlayMap;
	/** Range bots spawned on bot spots when playing a map. */
	UPROPERTY(EditAnywhere, Category="Map") int32 BotCount = 6;
};

USTRUCT(BlueprintType)
struct FRangeSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Range") TArray<FRangeLane> Lanes;
	UPROPERTY(EditAnywhere, Category="Range") float StrafeHalfWidthCm = 300.f;
	UPROPERTY(EditAnywhere, Category="Range") float StrafeMinPeriod = 0.35f;
	UPROPERTY(EditAnywhere, Category="Range") float StrafeMaxPeriod = 1.1f;
	UPROPERTY(EditAnywhere, Category="Range") float BotRespawnDelay = 1.5f;
	UPROPERTY(EditAnywhere, Category="Range") float ArenaLengthCm = 6000.f;
	UPROPERTY(EditAnywhere, Category="Range") float ArenaWidthCm = 3000.f;
	UPROPERTY(EditAnywhere, Category="Range") float WallHeightCm = 400.f;
	UPROPERTY(EditAnywhere, Category="Range") float WallThicknessCm = 50.f;
};
