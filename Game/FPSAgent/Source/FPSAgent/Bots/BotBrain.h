#pragma once

#include "CoreMinimal.h"
#include "Arena/TileGraph.h"
#include "Common/FPSAgentTypes.h"

/** Scripted-bot behaviours (spec §4). Pure: positions are map-local XY in cm; maps are never rotated, so directions match world XY. */
namespace FPSBots
{
	enum class EBotBehaviour : uint8 { Static, Strafe, Peek, Crouch, Patrol, Explore };
	FPSAGENT_API const TCHAR* BehaviourName(EBotBehaviour Behaviour);

	/** What a bot wants this tick. */
	struct FBotIntent
	{
		/** Unit or zero XY direction to move in. */
		FVector2D MoveDir = FVector2D::ZeroVector;
		bool bCrouch = false;
		/** Where its body should face (degrees). */
		float LookYawDeg = 0.f;
	};

	struct FBotSetup
	{
		EBotBehaviour Behaviour = EBotBehaviour::Static;
		bool bCrouched = false;
		float FacingYawDeg = 0.f;
		/** Peek: the tile behind cover and the adjacent tile in the open. (-1, -1) = none: the bot strafes (D-031). */
		FIntPoint PeekHidden = FIntPoint(-1, -1);
		FIntPoint PeekExposed = FIntPoint(-1, -1);
	};

	class FPSAGENT_API FBotBrain
	{
	public:
		/** InMap and InGraph must stay alive while the brain is used (the arena owns them for the episode). */
		void Start(const FBotSetup& InSetup, const FMapSpec& InMap, const FPSNav::FTileGraph& InGraph, int32 Seed, const FVector2D& Pos, double Now, const FBotBehaviourSettings& B);
		/** Patrol/Explore bots walk to Target and look around, then resume. The others turn to face it and hold their spot (D-031). */
		void Investigate(const FVector2D& Target, const FVector2D& Pos, double Now, const FBotBehaviourSettings& B);
		FBotIntent Update(const FVector2D& Pos, double Now, const FBotBehaviourSettings& B);

		EBotBehaviour GetBehaviour() const { return Setup.Behaviour; }
		bool IsMover() const { return Setup.Behaviour == EBotBehaviour::Patrol || Setup.Behaviour == EBotBehaviour::Explore; }
		bool IsInvestigating() const { return Mode != EMode::Base; }
		const TArray<FIntPoint>& GetWaypoints() const { return Waypoints; }

	private:
		enum class EMode : uint8 { Base, Investigate, LookAround };

		bool PlanTo(const FIntPoint& Goal, const FVector2D& Pos, double Now, const FBotBehaviourSettings& B);
		FVector2D FollowPath(const FVector2D& Pos, double Now, const FBotBehaviourSettings& B, bool& bArrived);
		FIntPoint PickExploreTarget(const FVector2D& Pos, const FBotBehaviourSettings& B);
		void PickPatrolWaypoints(const FVector2D& Pos, const FBotBehaviourSettings& B);

		FBotSetup Setup;
		const FMapSpec* Map = nullptr;
		const FPSNav::FTileGraph* Graph = nullptr;
		FRandomStream Rng;
		EMode Mode = EMode::Base;
		FVector2D Anchor = FVector2D::ZeroVector;
		float FacingYaw = 0.f;
		double LastUpdate = 0.0;

		TArray<FVector2D> PathPoints;
		int32 PathIndex = 0;
		double BestDist = TNumericLimits<double>::Max();
		double ProgressTime = 0.0;

		double NextToggle = 0.0;
		float StrafeSign = 1.f;
		bool bCrouchNow = false;
		bool bPeekOut = false;
		double PauseUntil = -1.0;

		TArray<FIntPoint> Waypoints;
		int32 WaypointIndex = 0;
		TArray<double> VisitTime;

		double LookUntil = 0.0;
		float LookYaw = 0.f;
	};
}
