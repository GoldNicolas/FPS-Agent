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

/**
 * Spike A toy task (-Stage=SpikeA): walk the bean to a random point on a walled floor. It proves the training stack
 * (packaged Linux build -> external trainer -> snapshots -> resume), not gameplay. All values are toy choices with no
 * external source; tune freely.
 */
USTRUCT(BlueprintType)
struct FReachTaskSettings
{
	GENERATED_BODY()
	/** Side of each square arena floor (walled). */
	UPROPERTY(EditAnywhere, Category="SpikeA") float ArenaSizeCm = 2000.f;
	/** Distance between arena centers. Arenas never touch, so agents never meet (hard rule 3). */
	UPROPERTY(EditAnywhere, Category="SpikeA") float ArenaSpacingCm = 4000.f;
	/** Starts and targets keep this far from the walls. */
	UPROPERTY(EditAnywhere, Category="SpikeA") float WallMarginCm = 150.f;
	UPROPERTY(EditAnywhere, Category="SpikeA") float TargetMinDistanceCm = 300.f;
	UPROPERTY(EditAnywhere, Category="SpikeA") float TargetMaxDistanceCm = 1200.f;
	/** Success when the bean's center is within this 2D distance of the target. */
	UPROPERTY(EditAnywhere, Category="SpikeA") float ReachRadiusCm = 100.f;
	/** Reward per meter closed in a step (negative when moving away). */
	UPROPERTY(EditAnywhere, Category="SpikeA") float ProgressRewardPerMeter = 1.f;
	/** One-off reward on reaching the target. */
	UPROPERTY(EditAnywhere, Category="SpikeA") float ReachBonus = 5.f;
	/** Cost per second of episode time, so the shortest path wins. */
	UPROPERTY(EditAnywhere, Category="SpikeA") float TimePenaltyPerSecond = 0.5f;
	/** Episode cap in steps (one step per 60 Hz tick: 360 = 6 s). Hitting it is a truncation, not a failure. */
	UPROPERTY(EditAnywhere, Category="SpikeA") int32 MaxEpisodeSteps = 360;
	/** Trainer writes snapshots every N iterations. */
	UPROPERTY(EditAnywhere, Category="SpikeA") int32 IterationsPerSnapshot = 10;
	/** Trainer stops after N iterations and tells the games to exit. -Iterations= overrides. */
	UPROPERTY(EditAnywhere, Category="SpikeA") int32 MaxIterations = 1000000;
	/** Ask the trainer for GPU (it falls back to CPU without CUDA). */
	UPROPERTY(EditAnywhere, Category="SpikeA") bool bTrainOnGpu = true;
	/** Ask the trainer for TensorBoard logs (skipped with a warning if the package is missing). */
	UPROPERTY(EditAnywhere, Category="SpikeA") bool bUseTensorboard = true;
	/** Game-side stats line in the log every N seconds of game time. */
	UPROPERTY(EditAnywhere, Category="SpikeA") float StatsLogIntervalSeconds = 10.f;
};
