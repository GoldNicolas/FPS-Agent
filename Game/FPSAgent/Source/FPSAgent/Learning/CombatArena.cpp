#include "Learning/CombatArena.h"
#include "Arena/FloorGenerator.h"
#include "Arena/MapBuilder.h"
#include "Bots/ScriptedBot.h"
#include "Character/BeanAIController.h"
#include "Character/BeanCharacter.h"
#include "Character/HealthComponent.h"
#include "Common/FPSAgentSettings.h"
#include "Engine/World.h"
#include "FPSAgent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Learning/ReachTaskLogic.h"
#include "Weapons/RifleComponent.h"

namespace
{
	using FPSCombatReward::ETerm;

	const UFPSAgentSettings& Settings() { return UFPSAgentSettings::Get(); }
	const FCombatStageSpec& Stage() { return Settings().Curriculum.S0; }
	/** Keeps bots off the walls (spawn geometry, not a mechanic). */
	constexpr float WallMarginCm = 200.f;

	FVector SpawnOffset() { return FVector(0.0, 0.0, Settings().Body.StandingHalfHeight + 2.f); }
}

float UCombatArena::FloorHalfSizeCm()
{
	return 0.5f * (Stage().FloorTiles + 2) * FMapSpec().TileCm;
}

float UCombatArena::GetBotRadiusCm() const
{
	return 0.5f * Stage().FloorTiles * FMapSpec().TileCm - WallMarginCm;
}

void UCombatArena::Build(UWorld* World, int32 InArenaIndex, const FVector& InCenter, int32 InBaseSeed)
{
	WorldPtr = World;
	ArenaIndex = InArenaIndex;
	Center = InCenter;
	BaseSeed = InBaseSeed;

	const FMapSpec Spec = FPSArena::MakeOpenFloorSpec(Stage().FloorTiles, FString::Printf(TEXT("s0floor_%d"), ArenaIndex));
	FPSArena::BuildMap(World, Spec, Center - Spec.Center());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Spawn = Center + SpawnOffset();
	Agent = World->SpawnActor<ABeanCharacter>(Spawn, FRotator::ZeroRotator, Params);
	if (!Agent)
	{
		UE_LOG(LogFPSAgent, Error, TEXT("Combat: could not spawn the agent for arena %d"), ArenaIndex);
		return;
	}
	Agent->AIControllerClass = ABeanAIController::StaticClass();
	Agent->SpawnDefaultController(); // CharacterMovement only simulates controlled pawns
	Agent->GetRifle()->OnShotFired.AddUObject(this, &UCombatArena::OnAgentShot);
	Agent->GetHealth()->OnDamaged.AddUObject(this, &UCombatArena::OnAgentDamaged);
	Agent->GetHealth()->OnDied.AddUObject(this, &UCombatArena::OnAgentDied);

	const int32 MaxBots = FMath::Max(Stage().BotsEasy, Stage().BotsHard);
	for (int32 i = 0; i < MaxBots; ++i)
	{
		AScriptedBot* Bot = World->SpawnActor<AScriptedBot>(Spawn + FVector(200.0 * (i + 1), 0.0, 0.0), FRotator::ZeroRotator, Params);
		if (!Bot)
		{
			UE_LOG(LogFPSAgent, Error, TEXT("Combat: could not spawn bot %d for arena %d"), i, ArenaIndex);
			continue;
		}
		Bot->GetHealth()->OnDamaged.AddUObject(this, &UCombatArena::OnBotDamaged);
		Bot->GetHealth()->OnDied.AddUObject(this, &UCombatArena::OnBotDied);
		Bot->Deactivate();
		Bots.Add(Bot);
	}
}

int32 UCombatArena::NextEpisodeSeed() const
{
	return FPSReach::EpisodeSeed(BaseSeed, ArenaIndex, EpisodeCount);
}

void UCombatArena::StartEpisode(const FPSCurriculum::FS0Episode& Config, int32 Seed, double Now)
{
	++EpisodeCount;
	bReview = Config.bReview;
	Level = Config.Level;
	EpisodeStart = Now;
	LastHitTime = -1.0e9;
	Held = FPSCombatObs::FAgentAction();
	Kills = 0;
	Shots = 0;
	Hits = 0;
	Headshots = 0;
	Tracks.Reset();
	if (!Agent)
	{
		return;
	}

	const FVector Spawn = Center + SpawnOffset();
	const FRotator Facing(0.0, Config.AgentYawDeg, 0.0);
	Agent->SetActorLocationAndRotation(Spawn, Facing, false, nullptr, ETeleportType::TeleportPhysics);
	if (AController* C = Agent->GetController())
	{
		C->SetControlRotation(Facing);
	}
	Agent->GetHealth()->ResetToFull();
	Agent->GetRifle()->ResetWeapon();
	Agent->GetRifle()->SeedRandom(static_cast<int32>(HashCombineFast(static_cast<uint32>(Seed), 0x9E3779B9u) & 0x7fffffff)); // own stream, not the sampler's
	Agent->SetDeadState(false);
	Agent->UnCrouch();
	Agent->SetWalking(false);
	Agent->StopJumping();
	Agent->GetCharacterMovement()->StopMovementImmediately();

	ActiveBots = 0;
	for (int32 i = 0; i < Bots.Num(); ++i)
	{
		AScriptedBot* Bot = Bots[i];
		if (!Bot) { continue; }
		if (Config.BotOffsets.IsValidIndex(i))
		{
			const FVector Pos = Spawn + FVector(Config.BotOffsets[i].X, Config.BotOffsets[i].Y, 0.0);
			const float FaceAgentYaw = (Spawn - Pos).GetSafeNormal2D().Rotation().Yaw;
			const bool bCrouched = Config.BotCrouched.IsValidIndex(i) && Config.BotCrouched[i];
			Bot->Activate(Pos, FaceAgentYaw, bCrouched, static_cast<int32>(HashCombineFast(static_cast<uint32>(Seed), static_cast<uint32>(i + 1)) & 0x7fffffff));
			++ActiveBots;
		}
		else
		{
			Bot->Deactivate();
		}
	}

	// Initial tracks (no reward) so the first observation already sees what is in view; the aim helper starts from here.
	TArray<FPSPerception::FSighting> Sightings;
	Sense(Sightings);
	FPSPerception::UpdateTracks(Tracks, Sightings, Now, Settings().Combat.Perception.MemorySeconds);
	Ledger.BeginEpisode(FPSCombatReward::AimPotential(NearestVisibleAimErrorDeg(Sightings), Stage().AimHelperWeight, Settings().Combat.Reward));
}

void UCombatArena::TickAction(float DeltaSeconds)
{
	const UFPSAgentSettings& S = Settings();
	Ledger.Add(ETerm::Time, S.Combat.Reward.TimePerSecond * DeltaSeconds);
	if (!Agent || IsAgentDead())
	{
		return;
	}
	AController* C = Agent->GetController();
	if (!C)
	{
		return;
	}
	const FCombatPerceptionSettings& P = S.Combat.Perception;
	const FVector2D Step = FPSCombatObs::AimPerTickDeg(Held.Aim, P, DeltaSeconds);
	FRotator R = C->GetControlRotation();
	R.Yaw = FRotator::NormalizeAxis(R.Yaw + Step.X);
	R.Pitch = FMath::Clamp(FRotator::NormalizeAxis(R.Pitch) + Step.Y, -P.MaxPitchDeg, P.MaxPitchDeg);
	R.Roll = 0.0;
	C->SetControlRotation(R);
	if (Stage().bAgentCanMove)
	{
		Agent->AddMovementInput(FPSCombatObs::MoveWorld(Held.Move, R.Yaw), 1.f);
	}
}

void UCombatArena::SetAction(const FPSCombatObs::FAgentAction& Action)
{
	Held = FPSCombatObs::Sanitize(Action);
	if (!Agent || IsAgentDead())
	{
		return;
	}
	Agent->GetRifle()->SetTriggerHeld(Held.bFire);
	if (Held.bReload)
	{
		Agent->GetRifle()->RequestReload();
	}
	if (Stage().bAgentCanMove)
	{
		if (Held.bCrouch) { Agent->Crouch(); } else { Agent->UnCrouch(); }
		Agent->SetWalking(Held.bWalk);
		if (Held.bJump) { Agent->Jump(); } else { Agent->StopJumping(); }
	}
}

void UCombatArena::PrepareDecision(double Now)
{
	const UFPSAgentSettings& S = Settings();
	TArray<FPSPerception::FSighting> Sightings;
	Sense(Sightings);
	FPSPerception::UpdateTracks(Tracks, Sightings, Now, S.Combat.Perception.MemorySeconds);
	for (const FPSPerception::FSighting& Si : Sightings)
	{
		if (Si.bVisible) { Ledger.TrySighting(Si.BotIndex, S.Combat.Reward); }
	}
	const ECombatEpisodeEnd End = GetEpisodeEnd(Now);
	if (End == ECombatEpisodeEnd::Cleared)
	{
		Ledger.TryClear(TimeLeftFraction(Now), S.Combat.Reward);
	}
	const float Potential = End == ECombatEpisodeEnd::Running
		? FPSCombatReward::AimPotential(NearestVisibleAimErrorDeg(Sightings), Stage().AimHelperWeight, S.Combat.Reward)
		: 0.f; // terminal state: potential 0
	Ledger.ApplyPotential(Potential, S.Combat.Ppo.Discount);
}

ECombatEpisodeEnd UCombatArena::GetEpisodeEnd(double Now) const
{
	if (IsAgentDead()) { return ECombatEpisodeEnd::Died; }
	if (ActiveBots > 0 && AllActiveBotsDead()) { return ECombatEpisodeEnd::Cleared; }
	if (Now - EpisodeStart >= Stage().TimeLimitSeconds - 1e-6) { return ECombatEpisodeEnd::TimedOut; }
	return ECombatEpisodeEnd::Running;
}

FPSCombatStats::FEpisodeRecord UCombatArena::FinishEpisode(ECombatEpisodeEnd End, double Now) const
{
	FPSCombatStats::FEpisodeRecord R;
	R.bCleared = End == ECombatEpisodeEnd::Cleared;
	R.bDied = End == ECombatEpisodeEnd::Died;
	R.bTimedOut = End == ECombatEpisodeEnd::TimedOut;
	R.bReview = bReview;
	R.Level = Level;
	R.Return = Ledger.EpisodeReturn();
	for (int32 t = 0; t < FPSCombatReward::TermCount; ++t) { R.Terms[t] = Ledger.Episode[t]; }
	R.Kills = Kills;
	R.Bots = ActiveBots;
	R.Shots = Shots;
	R.Hits = Hits;
	R.Headshots = Headshots;
	R.Seconds = static_cast<float>(Now - EpisodeStart);
	return R;
}

void UCombatArena::BuildObservation(FPSCombatObs::FObservationData& Out, double Now) const
{
	const UFPSAgentSettings& S = Settings();
	const FCombatPerceptionSettings& P = S.Combat.Perception;
	if (!Agent)
	{
		Out = FPSCombatObs::FObservationData::Zero(P.RayYawCount * P.RayPitchesDeg.Num());
		return;
	}
	FVector Eye;
	FRotator View;
	Agent->GetActorEyesViewPoint(Eye, View);
	const float Yaw = View.Yaw;
	const FRifleFireState& Fire = Agent->GetRifle()->GetFireState();

	FPSCombatObs::FSelfState Self;
	Self.VelocityView = FPSPerception::ToViewFrame(Agent->GetVelocity(), Yaw);
	Self.bGrounded = !Agent->IsAirborne();
	Self.bCrouched = Agent->bIsCrouched;
	Self.bWalking = Agent->IsWalking();
	Self.Health = Agent->GetHealth()->GetHealth();
	Self.MaxHealth = S.Health.MaxHealth;
	Self.Armor = Agent->GetHealth()->GetArmor();
	Self.MaxArmor = S.Health.MaxArmor;
	Self.Ammo = Fire.Ammo;
	Self.MagazineSize = S.Rifle.MagazineSize;
	Self.bReloading = Fire.IsReloading();
	Self.SpreadDeg = Agent->GetRifle()->GetCurrentSpreadDegrees();
	Self.SprayShots = Fire.ShotIndex;
	Self.SecondsSinceShot = static_cast<float>(Now - Fire.LastShotTime);
	Self.AimPitchDeg = FRotator::NormalizeAxis(View.Pitch);
	Self.TimeLeftFraction = TimeLeftFraction(Now);
	Out.Self = FPSCombatObs::PackSelf(Self, P);

	Out.Rays.Reset();
	for (const FVector& Dir : FPSPerception::RayDirections(Yaw, P.RayYawCount, P.RayPitchesDeg))
	{
		Out.Rays.Add(FPSPerception::RayFraction(GetWorld(), Eye, Dir, P.RayRangeCm));
	}

	Out.Enemies.Reset();
	for (const FPSPerception::FTrack& T : FPSPerception::SelectTracks(Tracks, P.MaxEnemies))
	{
		FPSCombatObs::FEnemyState E;
		E.HeadView = FPSPerception::ToViewFrame(T.Head - Eye, Yaw);
		E.AimErrorDeg = FPSPerception::AimErrorDeg(Eye, View, T.Head);
		E.VelocityView = FPSPerception::ToViewFrame(T.Velocity, Yaw);
		E.bFacingAgent = T.bFacingAgent;
		E.bCrouched = T.bCrouched;
		E.bVisible = T.bVisible;
		E.SecondsSinceSeen = static_cast<float>(Now - T.LastSeenTime);
		Out.Enemies.Add(FPSCombatObs::PackEnemy(E, P));
	}

	const float HitAge = static_cast<float>(Now - LastHitTime);
	Out.bHit = HitAge >= 0.f && HitAge <= P.HitIndicatorSeconds;
	Out.Hit = Out.bHit ? FPSCombatObs::PackHit(FPSPerception::ToViewFrame(LastHitDirection, Yaw), HitAge, P) : TArray<float>();
	Out.Sounds.Reset(); // hearing arrives with moving and shooting bots (build B)
}

FPSCombatObs::FAgentAction UCombatArena::ScriptedAction(float DecisionSeconds) const
{
	const UFPSAgentSettings& S = Settings();
	FPSCombatObs::FScriptedTarget Target;
	if (!Agent)
	{
		return FPSCombatObs::FAgentAction();
	}
	FVector Eye;
	FRotator View;
	Agent->GetActorEyesViewPoint(Eye, View);
	double Best = TNumericLimits<double>::Max();
	for (const FPSPerception::FTrack& T : Tracks)
	{
		if (!T.bVisible) { continue; }
		const FVector2D Err = FPSPerception::AimErrorDeg(Eye, View, T.Head);
		if (Err.Size() < Best)
		{
			Best = Err.Size();
			Target.bHasTarget = true;
			Target.AimErrorDeg = Err;
			Target.DistanceCm = static_cast<float>(FVector::Dist(Eye, T.Head));
		}
	}
	const FRifleFireState& Fire = Agent->GetRifle()->GetFireState();
	return FPSCombatObs::ScriptedAction(Target, Fire.Ammo, S.Rifle.MagazineSize, Fire.IsReloading(), S.Body.HeadRadius, S.Combat.Perception, DecisionSeconds);
}

void UCombatArena::Sense(TArray<FPSPerception::FSighting>& Out) const
{
	Out.Reset();
	if (!Agent)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const FCombatPerceptionSettings& P = Settings().Combat.Perception;
	const bool bAgentAlive = !IsAgentDead();
	FVector Eye;
	FRotator View;
	Agent->GetActorEyesViewPoint(Eye, View);
	const double FacingCos = FMath::Cos(FMath::DegreesToRadians(0.5 * P.FieldOfViewDeg));
	for (int32 i = 0; i < Bots.Num(); ++i)
	{
		const AScriptedBot* Bot = Bots[i];
		if (!Bot || !Bot->IsActive()) { continue; }
		FPSPerception::FSighting S;
		S.BotIndex = i;
		S.bAlive = Bot->IsAlive();
		if (S.bAlive)
		{
			S.Head = Bot->GetHitbox(EHitZone::Head)->GetComponentLocation();
			const FVector Chest = Bot->GetHitbox(EHitZone::Body)->GetComponentLocation();
			S.bVisible = bAgentAlive && FPSPerception::InFieldOfView(Eye, View, S.Head, P.FieldOfViewDeg, P.VerticalFieldOfViewDeg)
				&& (FPSPerception::HasLineOfSight(World, Eye, S.Head) || FPSPerception::HasLineOfSight(World, Eye, Chest));
			S.Velocity = Bot->GetVelocity();
			const FVector ToAgent = (Agent->GetActorLocation() - Bot->GetActorLocation()).GetSafeNormal2D();
			S.bFacingAgent = FVector::DotProduct(Bot->GetActorForwardVector().GetSafeNormal2D(), ToAgent) > FacingCos;
			S.bCrouched = Bot->bIsCrouched;
		}
		Out.Add(S);
	}
}

float UCombatArena::NearestVisibleAimErrorDeg(const TArray<FPSPerception::FSighting>& Sightings) const
{
	if (!Agent)
	{
		return -1.f;
	}
	FVector Eye;
	FRotator View;
	Agent->GetActorEyesViewPoint(Eye, View);
	float Best = -1.f;
	for (const FPSPerception::FSighting& S : Sightings)
	{
		if (!S.bVisible) { continue; }
		const float Err = static_cast<float>(FPSPerception::AimErrorDeg(Eye, View, S.Head).Size());
		if (Best < 0.f || Err < Best) { Best = Err; }
	}
	return Best;
}

bool UCombatArena::IsAgentDead() const
{
	return !Agent || Agent->GetHealth()->IsDead();
}

bool UCombatArena::AllActiveBotsDead() const
{
	for (const TObjectPtr<AScriptedBot>& Bot : Bots)
	{
		if (Bot && Bot->IsAlive()) { return false; }
	}
	return true;
}

float UCombatArena::TimeLeftFraction(double Now) const
{
	return FMath::Clamp(1.f - static_cast<float>((Now - EpisodeStart) / FMath::Max(1e-3f, Stage().TimeLimitSeconds)), 0.f, 1.f);
}

int32 UCombatArena::BotIndexOf(const AActor* Actor) const
{
	for (int32 i = 0; i < Bots.Num(); ++i)
	{
		if (Bots[i].Get() == Actor) { return i; }
	}
	return INDEX_NONE;
}

void UCombatArena::OnAgentShot(const FShotResult& Shot)
{
	++Shots;
	if (Shot.Zone != EHitZone::None && BotIndexOf(Shot.HitActor.Get()) != INDEX_NONE)
	{
		++Hits;
		Headshots += Shot.Zone == EHitZone::Head ? 1 : 0;
		return; // damage and kill rewards come from the bot's health events (damage actually applied)
	}
	Ledger.Add(ETerm::Miss, Settings().Combat.Reward.MissPerBullet);
}

void UCombatArena::OnAgentDamaged(UHealthComponent* Victim, const FPSCombat::FDamageResult& Result, AActor* DamageInstigator)
{
	Ledger.Add(ETerm::DamageTaken, FPSCombatReward::DamageTaken(Result.DamageToHealth + Result.DamageToArmor, Settings().Combat.Reward));
	if (const UWorld* World = GetWorld())
	{
		LastHitTime = World->GetTimeSeconds();
	}
	LastHitDirection = (DamageInstigator && Agent) ? (DamageInstigator->GetActorLocation() - Agent->GetActorLocation()).GetSafeNormal() : FVector::ZeroVector;
}

void UCombatArena::OnAgentDied(UHealthComponent* Victim, AActor* Killer)
{
	Ledger.Add(ETerm::Death, Settings().Combat.Reward.Death);
	if (Agent)
	{
		Agent->SetDeadState(true);
	}
}

void UCombatArena::OnBotDamaged(UHealthComponent* Victim, const FPSCombat::FDamageResult& Result, AActor* DamageInstigator)
{
	if (Agent && DamageInstigator == Agent)
	{
		Ledger.Add(ETerm::DamageDealt, FPSCombatReward::DamageDealt(Result.DamageToHealth + Result.DamageToArmor, Settings().Combat.Reward));
	}
}

void UCombatArena::OnBotDied(UHealthComponent* Victim, AActor* Killer)
{
	if (Agent && Killer == Agent)
	{
		++Kills;
		Ledger.Add(ETerm::Kill, Settings().Combat.Reward.Kill);
	}
}
