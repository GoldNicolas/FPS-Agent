#include "Learning/CombatArena.h"
#include "Arena/ArenaBuilder.h"
#include "Arena/FloorGenerator.h"
#include "Arena/MapBuilder.h"
#include "Bots/ScriptedBot.h"
#include "Character/BeanAIController.h"
#include "Character/BeanCharacter.h"
#include "Character/HealthComponent.h"
#include "Common/FPSAgentSettings.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "FPSAgent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Learning/ReachTaskLogic.h"
#include "Weapons/RifleComponent.h"

namespace
{
	using FPSCombatReward::ETerm;

	const UFPSAgentSettings& Settings() { return UFPSAgentSettings::Get(); }
	/** Keeps bots off the walls (spawn geometry, not a mechanic). */
	constexpr float WallMarginCm = 200.f;

	FVector SpawnOffset() { return FVector(0.0, 0.0, Settings().Body.StandingHalfHeight + 2.f); }
}

const FCombatStageSpec& UCombatArena::StageSpec() const
{
	return Settings().Curriculum.Stage(StageIndex);
}

FVector2D UCombatArena::ToLocal(const FVector& World) const
{
	const FVector L = World - MapOrigin;
	return FVector2D(L.X, L.Y);
}

float UCombatArena::FloorHalfSizeCm(int32 InStageIndex)
{
	return 0.5f * (Settings().Curriculum.Stage(InStageIndex).FloorTiles + 2) * FMapSpec().TileCm;
}

float UCombatArena::GetBotRadiusCm() const
{
	return 0.5f * StageSpec().FloorTiles * FMapSpec().TileCm - WallMarginCm;
}

void UCombatArena::Build(UWorld* World, int32 InArenaIndex, const FVector& InCenter, int32 InBaseSeed, int32 InStageIndex)
{
	StageIndex = InStageIndex;
	WorldPtr = World;
	ArenaIndex = InArenaIndex;
	Center = InCenter;
	BaseSeed = InBaseSeed;

	BaseSpec = FPSArena::MakeOpenFloorSpec(StageSpec().FloorTiles, FString::Printf(TEXT("floor_%d"), ArenaIndex));
	MapOrigin = Center - BaseSpec.Center();
	FPSArena::BuildMap(World, BaseSpec, MapOrigin);
	CurrentMap = BaseSpec;
	Graph = FPSNav::FTileGraph::Build(CurrentMap);
	// Cover changes per episode (S1+): a pool of movable cubes sized for the densest layout, parked hidden below the floor (D-031).
	const int32 Interior = StageSpec().FloorTiles * StageSpec().FloorTiles;
	const int32 PoolSize = FMath::CeilToInt(FMath::Max(StageSpec().CoverDensityEasy, StageSpec().CoverDensityHard) * (1.f + Settings().Curriculum.JitterFraction) * Interior);
	for (int32 i = 0; i < PoolSize; ++i)
	{
		AStaticMeshActor* Block = FPSArena::SpawnBlock(World, MapOrigin + FVector(0.0, 0.0, -1000.0 - 10.0 * i), FVector(BaseSpec.TileCm, BaseSpec.TileCm, FPSMapFormat::LowCoverCm));
		Block->SetActorHiddenInGame(true);
		Block->SetActorEnableCollision(false);
		CoverPool.Add(Block);
	}

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

	const int32 MaxBots = FMath::Max(StageSpec().BotsEasy, StageSpec().BotsHard);
	for (int32 i = 0; i < MaxBots; ++i)
	{
		AScriptedBot* Bot = World->SpawnActor<AScriptedBot>(Spawn + FVector(200.0 * (i + 1), 0.0, 0.0), FRotator::ZeroRotator, Params);
		if (!Bot)
		{
			UE_LOG(LogFPSAgent, Error, TEXT("Combat: could not spawn bot %d for arena %d"), i, ArenaIndex);
			continue;
		}
		if (!Bot->GetController())
		{
			Bot->SpawnDefaultController(); // CharacterMovement only simulates controlled pawns
		}
		Bot->GetHealth()->OnDamaged.AddUObject(this, &UCombatArena::OnBotDamaged);
		Bot->GetHealth()->OnDied.AddUObject(this, &UCombatArena::OnBotDied);
		Bot->Deactivate();
		Bots.Add(Bot);
	}
	Brains.SetNum(Bots.Num());
	Senses.SetNum(Bots.Num());
	BotRngs.SetNum(Bots.Num());
	NextFootstep.Init(0.0, Bots.Num() + 1);
}

int32 UCombatArena::NextEpisodeSeed() const
{
	return FPSReach::EpisodeSeed(BaseSeed, ArenaIndex, EpisodeCount);
}

void UCombatArena::ApplyCover(const FMapSpec& Map)
{
	int32 Used = 0;
	// Hides and parks every pool block from index Used on, and records how many are placed.
	auto ParkRest = [this, &Used]()
	{
		for (int32 i = Used; i < CoverPool.Num(); ++i)
		{
			CoverPool[i]->SetActorHiddenInGame(true);
			CoverPool[i]->SetActorEnableCollision(false);
			CoverPool[i]->SetActorLocation(MapOrigin + FVector(0.0, 0.0, -1000.0 - 10.0 * i));
		}
		VisibleCover = Used;
	};
	for (int32 R = 0; R < Map.Rows(); ++R)
	{
		for (int32 C = 0; C < Map.Cols(); ++C)
		{
			const TCHAR Ch = Map.TileAt(R, C);
			if (Ch != TEXT('l') && Ch != TEXT('h'))
			{
				continue;
			}
			if (!CoverPool.IsValidIndex(Used))
			{
				UE_LOG(LogFPSAgent, Warning, TEXT("Combat: arena %d's cover pool (%d) is too small for this layout"), ArenaIndex, CoverPool.Num());
				ParkRest();
				return; // warn once, not per row
			}
			const float H = Ch == TEXT('h') ? FPSMapFormat::HighCoverCm : FPSMapFormat::LowCoverCm;
			AStaticMeshActor* Block = CoverPool[Used++];
			Block->SetActorLocation(MapOrigin + Map.TileCenter(R, C) + FVector(0.0, 0.0, 0.5 * H)); // from the floor top up
			Block->SetActorScale3D(FVector(Map.TileCm, Map.TileCm, H) / 100.0);
			Block->SetActorHiddenInGame(false);
			Block->SetActorEnableCollision(true);
		}
	}
	ParkRest();
}

void UCombatArena::AddSound(FPSBots::FSoundEvent E)
{
	// The agent hears a rough position: seeded noise on a disc, drawn once per sound with the bots' formula (spec section 2).
	const float Angle = SoundRng.FRandRange(0.f, 2.f * PI);
	const float Radius = Settings().Bots.HearingNoiseCm * FMath::Sqrt(SoundRng.FRand());
	E.HeardOffset = FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0.0);
	Sounds.Add(E);
}

void UCombatArena::StartEpisode(const FPSCurriculum::FS0Episode& Config, int32 Seed, double Now)
{
	StartEpisode(FPSCurriculum::FromS0(Config, BaseSpec), Seed, Now);
}

void UCombatArena::StartEpisode(const FPSCurriculum::FCombatEpisode& Config, int32 Seed, double Now)
{
	++EpisodeCount;
	bReview = Config.bReview;
	Level = Config.Level;
	EpisodeStart = Now;
	LastHitTime = -1.0e9;
	LastScriptedSoundTime = -1.0e9;
	Held = FPSCombatObs::FAgentAction();
	Kills = 0;
	Shots = 0;
	Hits = 0;
	Headshots = 0;
	Tracks.Reset();
	Sounds.Reset();
	SoundRng.Initialize(static_cast<int32>(HashCombineFast(static_cast<uint32>(Seed), 0x50D5Eu) & 0x7fffffff));
	BotSpeedSum = 0.0;
	BotSpeedSamples = 0;
	for (double& T : NextFootstep) { T = Now; }
	CurrentMap = Config.Map;
	Graph = FPSNav::FTileGraph::Build(CurrentMap);
	if (!Agent)
	{
		if (StageIndex > 0)
		{
			ApplyCover(CurrentMap);
		}
		return;
	}
	const UFPSAgentSettings& S = Settings();
	const FVector Spawn = Center + FVector(Config.AgentOffset.X, Config.AgentOffset.Y, 0.0) + SpawnOffset();
	const FRotator Facing(0.0, Config.AgentYawDeg, 0.0);
	Agent->SetDeadState(false);
	Agent->ApplyCrouchNow(false); // stand up BEFORE the teleport: standing up after it would lift the bean ~30 cm (Task 7 finding)
	Agent->SetActorLocationAndRotation(Spawn, Facing, false, nullptr, ETeleportType::TeleportPhysics);
	if (AController* C = Agent->GetController())
	{
		C->SetControlRotation(Facing);
	}
	Agent->GetHealth()->ResetToFull();
	Agent->GetRifle()->ResetWeapon();
	Agent->GetRifle()->SeedRandom(static_cast<int32>(HashCombineFast(static_cast<uint32>(Seed), 0x9E3779B9u) & 0x7fffffff)); // own stream, not the sampler's
	Agent->SetDeadState(false);
	Agent->SetWalking(false);
	Agent->StopJumping();
	Agent->GetCharacterMovement()->StopMovementImmediately();

	ActiveBots = 0;
	for (int32 i = 0; i < Bots.Num(); ++i)
	{
		AScriptedBot* Bot = Bots[i];
		if (!Bot)
		{
			continue;
		}
		if (!Config.Bots.IsValidIndex(i))
		{
			Bot->Deactivate();
			continue;
		}
		const FPSCurriculum::FBotSpawn& B = Config.Bots[i];
		const FVector Pos = Center + FVector(B.Offset.X, B.Offset.Y, 0.0) + SpawnOffset();
		const int32 BotSeed = static_cast<int32>(HashCombineFast(static_cast<uint32>(Seed), static_cast<uint32>(i + 1)) & 0x7fffffff);
		Bot->Activate(Pos, B.YawDeg, B.bCrouched, BotSeed, B.SpeedScale);
		FPSBots::FBotSetup Setup;
		Setup.Behaviour = B.Behaviour;
		Setup.bCrouched = B.bCrouched;
		Setup.FacingYawDeg = B.YawDeg;
		Setup.PeekHidden = B.PeekHidden;
		Setup.PeekExposed = B.PeekExposed;
		Brains[i].Start(Setup, CurrentMap, Graph, static_cast<int32>(HashCombineFast(static_cast<uint32>(BotSeed), 0xB4A1Fu) & 0x7fffffff), ToLocal(Pos), Now, S.Bots);
		Senses[i].Reset();
		BotRngs[i].Initialize(static_cast<int32>(HashCombineFast(static_cast<uint32>(BotSeed), 0x5E45Eu) & 0x7fffffff));
		++ActiveBots;
	}

	// Cover goes in after the beans stood up and teleported, so a crouched bean can't be trapped under a block that moved onto its old spot.
	if (StageIndex > 0)
	{
		ApplyCover(CurrentMap);
	}

	FPSBots::FBotSetup AgentSetup;
	AgentSetup.Behaviour = FPSBots::EBotBehaviour::Explore;
	AgentSetup.FacingYawDeg = Config.AgentYawDeg;
	AgentBrain.Start(AgentSetup, CurrentMap, Graph, static_cast<int32>(HashCombineFast(static_cast<uint32>(Seed), 0x51EDu) & 0x7fffffff), ToLocal(Spawn), Now, S.Bots);

	// Initial tracks (no reward) so the first observation already sees what is in view; the aim helper starts from here.
	TArray<FPSPerception::FSighting> Sightings;
	Sense(Sightings);
	FPSPerception::UpdateTracks(Tracks, Sightings, Now, S.Combat.Perception.MemorySeconds);
	Ledger.BeginEpisode(FPSCombatReward::AimPotential(NearestVisibleAimErrorDeg(Sightings), StageSpec().AimHelperWeight, S.Combat.Reward));
}

void UCombatArena::TickAction(float DeltaSeconds)
{
	const UFPSAgentSettings& S = Settings();
	Ledger.Add(ETerm::Time, S.Combat.Reward.TimePerSecond * DeltaSeconds);
	const double TickNow = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (StageIndex > 0) // S1+ only: S0 keeps build A's static, non-reacting bots (snapshots and references stay comparable)
	{
		TickBots(TickNow, DeltaSeconds);
		EmitFootsteps(TickNow);
	}
	{
		const FBotBehaviourSettings& Bs = S.Bots;
		const double Keep = Bs.ReactionSeconds + Bs.SoundMemorySeconds + 1.0;
		Sounds.RemoveAll([TickNow, Keep](const FPSBots::FSoundEvent& E) { return TickNow - E.Time > Keep; }); // every stage: the agent's own shots pile up in S0 too
	}
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
	if (StageSpec().bAgentCanMove)
	{
		Agent->AddMovementInput(FPSCombatObs::MoveWorld(Held.Move, R.Yaw), 1.f);
	}
}

void UCombatArena::TickBots(double Now, float DeltaSeconds)
{
	const FBotBehaviourSettings& B = Settings().Bots;
	for (int32 i = 0; i < Bots.Num(); ++i)
	{
		AScriptedBot* Bot = Bots[i];
		if (Bot && Bot->IsAlive())
		{
			Bot->Drive(Brains[i].Update(ToLocal(Bot->GetActorLocation()), Now, B), DeltaSeconds, B.TurnRateDegPerSec);
			BotSpeedSum += Bot->GetHorizontalSpeed();
			++BotSpeedSamples;
		}
	}
}

void UCombatArena::EmitFootsteps(double Now)
{
	const UFPSAgentSettings& S = Settings();
	auto Emit = [&](const ABeanCharacter* Bean, int32 Source, int32 Slot)
	{
		if (!Bean || !NextFootstep.IsValidIndex(Slot) || Now < NextFootstep[Slot])
		{
			return;
		}
		if (!FPSBots::IsAudibleMovement(Bean->GetHorizontalSpeed(), Bean->bIsCrouched, Bean->IsWalking(), S.Movement.WalkSpeed, S.Bots))
		{
			return;
		}
		FPSBots::FSoundEvent E;
		E.Position = Bean->GetActorLocation();
		E.Time = Now;
		E.Kind = FPSBots::ESoundKind::Footstep;
		E.Source = Source;
		AddSound(E);
		NextFootstep[Slot] = Now + S.Bots.FootstepIntervalSeconds;
	};
	if (!IsAgentDead())
	{
		Emit(Agent, INDEX_NONE, 0);
	}
	for (int32 i = 0; i < Bots.Num(); ++i)
	{
		if (Bots[i] && Bots[i]->IsAlive())
		{
			Emit(Bots[i], i, i + 1);
		}
	}
}

void UCombatArena::SetAction(const FPSCombatObs::FAgentAction& Action)
{
	Held = FPSCombatObs::ApplyAbilities(FPSCombatObs::Sanitize(Action), Settings().Combat.Abilities); // D-033: a masked crouch/jump never reaches the bean
	if (!Agent || IsAgentDead())
	{
		return;
	}
	Agent->GetRifle()->SetTriggerHeld(Held.bFire);
	if (Held.bReload)
	{
		Agent->GetRifle()->RequestReload();
	}
	if (StageSpec().bAgentCanMove)
	{
		if (Held.bCrouch) { Agent->Crouch(); } else { Agent->UnCrouch(); }
		Agent->SetWalking(Held.bWalk);
		if (Held.bJump) { Agent->Jump(); } else { Agent->StopJumping(); }
	}
}

void UCombatArena::PrepareDecision(double Now)
{
	if (StageIndex > 0) // S1+ only: S0 keeps build A's static, non-reacting bots (snapshots and references stay comparable)
	{
		SenseBots(Now);
	}
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
		? FPSCombatReward::AimPotential(NearestVisibleAimErrorDeg(Sightings), StageSpec().AimHelperWeight, S.Combat.Reward)
		: 0.f; // terminal state: potential 0
	Ledger.ApplyPotential(Potential, S.Combat.Ppo.Discount);
}

ECombatEpisodeEnd UCombatArena::GetEpisodeEnd(double Now) const
{
	if (IsAgentDead()) { return ECombatEpisodeEnd::Died; }
	if (ActiveBots > 0 && AllActiveBotsDead()) { return ECombatEpisodeEnd::Cleared; }
	if (Now - EpisodeStart >= StageSpec().TimeLimitSeconds - 1e-6) { return ECombatEpisodeEnd::TimedOut; }
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
	R.BotSpeedCmS = BotSpeedSamples > 0 ? static_cast<float>(BotSpeedSum / static_cast<double>(BotSpeedSamples)) : 0.f;
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
	Out.Sounds.Reset();
	const FBotBehaviourSettings& Bs = S.Bots;
	TArray<const FPSBots::FSoundEvent*> Heard;
	for (const FPSBots::FSoundEvent& E : Sounds)
	{
		if (E.Source != INDEX_NONE && FPSBots::CanHear(E, Agent->GetActorLocation(), Now, Bs.SoundMemorySeconds, Bs))
		{
			Heard.Add(&E);
		}
	}
	Heard.StableSort([](const FPSBots::FSoundEvent& A, const FPSBots::FSoundEvent& B) { return A.Time > B.Time; }); // newest first
	for (int32 i = 0; i < Heard.Num() && i < P.MaxSounds; ++i)
	{
		const FVector Delta = Heard[i]->Position + Heard[i]->HeardOffset - Eye; // rough: the seeded noise drawn when the sound was made
		Out.Sounds.Add(FPSCombatObs::PackSound(FPSPerception::ToViewFrame(Delta, Yaw), static_cast<float>(Delta.Size()),
			static_cast<float>(Now - Heard[i]->Time), P, Bs.SoundMemorySeconds));
	}
}

FPSCombatObs::FAgentAction UCombatArena::ScriptedAction(float DecisionSeconds)
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
	FPSCombatObs::FAgentAction A = FPSCombatObs::ScriptedAction(Target, Fire.Ammo, S.Rifle.MagazineSize, Fire.IsReloading(), S.Body.HeadRadius, S.Combat.Perception, DecisionSeconds);
	if (!StageSpec().bAgentCanMove || Target.bHasTarget)
	{
		return A; // S0, or an enemy in sight: stand still, turn to the head, fire on target
	}
	// The agent may move and sees nobody: explore like a bot and investigate the newest enemy sound it hears (the same hearing as the policy).
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const FBotBehaviourSettings& B = S.Bots;
	const FVector2D Here = ToLocal(Agent->GetActorLocation());
	const FPSBots::FSoundEvent* Newest = nullptr;
	for (const FPSBots::FSoundEvent& E : Sounds)
	{
		if (E.Source != INDEX_NONE && E.Time > LastScriptedSoundTime && FPSBots::CanHear(E, Agent->GetActorLocation(), Now, B.SoundMemorySeconds, B)
			&& (!Newest || E.Time > Newest->Time))
		{
			Newest = &E;
		}
	}
	if (Newest)
	{
		LastScriptedSoundTime = Newest->Time;
		AgentBrain.Investigate(ToLocal(Newest->Position + Newest->HeardOffset), Here, Now, B);
	}
	const FPSBots::FBotIntent Intent = AgentBrain.Update(Here, Now, B);
	const float MaxPerDecision = FMath::Max(1e-3f, S.Combat.Perception.MaxTurnRateDegPerSec * DecisionSeconds);
	const float PitchFix = FMath::Clamp(static_cast<float>(-FRotator::NormalizeAxis(View.Pitch)) / MaxPerDecision, -1.f, 1.f); // level the view
	if (Intent.MoveDir.IsNearlyZero())
	{
		A.Aim = FVector2D(0.5, PitchFix); // standing (paused or looking around): sweep the view
		return A;
	}
	const float MoveYaw = FMath::RadiansToDegrees(FMath::Atan2(Intent.MoveDir.Y, Intent.MoveDir.X));
	const float YawError = FRotator::NormalizeAxis(MoveYaw - View.Yaw);
	A.Aim = FVector2D(FMath::Clamp(YawError / MaxPerDecision, -1.f, 1.f), PitchFix);
	if (FMath::Abs(YawError) < 45.f) // walk once roughly facing the path, so the eyes lead
	{
		const FVector Local = FPSPerception::ToViewFrame(FVector(Intent.MoveDir.X, Intent.MoveDir.Y, 0.0), View.Yaw);
		A.Move = FVector2D(Local.X, Local.Y).GetClampedToMaxSize(1.0);
	}
	return A;
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
	return FMath::Clamp(1.f - static_cast<float>((Now - EpisodeStart) / FMath::Max(1e-3f, StageSpec().TimeLimitSeconds)), 0.f, 1.f);
}

void UCombatArena::SenseBots(double Now)
{
	if (!Agent)
	{
		return;
	}
	const FBotBehaviourSettings& B = Settings().Bots;
	const UWorld* World = GetWorld();
	const bool bAgentAlive = !IsAgentDead();
	const FVector Head = Agent->GetHitbox(EHitZone::Head)->GetComponentLocation();
	const FVector Chest = Agent->GetHitbox(EHitZone::Body)->GetComponentLocation();
	for (int32 i = 0; i < Bots.Num(); ++i)
	{
		AScriptedBot* Bot = Bots[i];
		if (!Bot || !Bot->IsAlive())
		{
			continue;
		}
		FVector Eye;
		FRotator View;
		Bot->GetActorEyesViewPoint(Eye, View);
		// Fair: the same view cone and geometry-only line of sight as the agent's (spec section 4); only this arena's agent.
		const bool bSees = bAgentAlive && FPSPerception::InFieldOfView(Eye, View, Head, B.SightFovDeg)
			&& (FPSPerception::HasLineOfSight(World, Eye, Head) || FPSPerception::HasLineOfSight(World, Eye, Chest));
		FVector Target;
		if (Senses[i].Update(bSees, Agent->GetActorLocation(), Sounds, Bot->GetActorLocation(), Now, B, BotRngs[i], Target))
		{
			Brains[i].Investigate(ToLocal(Target), ToLocal(Bot->GetActorLocation()), Now, B);
		}
	}
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
	if (Agent)
	{
		FPSBots::FSoundEvent E;
		E.Position = Agent->GetActorLocation();
		E.Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		E.Kind = FPSBots::ESoundKind::Gunshot;
		E.Source = INDEX_NONE;
		AddSound(E);
	}
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
