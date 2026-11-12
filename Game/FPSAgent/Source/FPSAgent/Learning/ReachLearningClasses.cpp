#include "Learning/ReachLearningClasses.h"
#include "Learning/ReachTaskActor.h"
#include "LearningAgentsActions.h"
#include "LearningAgentsObservations.h"

namespace
{
	// Both listeners are created by the Make* helpers with the manager component as Outer, whose owner is the task.
	AReachTaskActor* TaskOf(const UObject* Listener)
	{
		return Listener ? Listener->GetTypedOuter<AReachTaskActor>() : nullptr;
	}
}

void UReachInteractor::SpecifyAgentObservation_Implementation(FLearningAgentsObservationSchemaElement& OutObservationSchemaElement, ULearningAgentsObservationSchema* InObservationSchema)
{
	TMap<FName, FLearningAgentsObservationSchemaElement> Elements;
	Elements.Add(TEXT("Target"), ULearningAgentsObservations::SpecifyLocationObservation(InObservationSchema, TargetScaleCm, TEXT("Target")));
	Elements.Add(TEXT("Velocity"), ULearningAgentsObservations::SpecifyVelocityObservation(InObservationSchema, VelocityScaleCmPerS, TEXT("Velocity")));
	OutObservationSchemaElement = ULearningAgentsObservations::SpecifyStructObservation(InObservationSchema, Elements);
}

void UReachInteractor::GatherAgentObservation_Implementation(FLearningAgentsObservationObjectElement& OutObservationObjectElement, ULearningAgentsObservationObject* InObservationObject, const int32 AgentId)
{
	AReachTaskActor* Task = TaskOf(this);
	const FVector AgentLocation = Task ? Task->GetAgentLocation(AgentId) : FVector::ZeroVector;
	const FVector Target = Task ? Task->GetTarget(AgentId) : FVector::ZeroVector;
	const FVector Velocity = Task ? Task->GetAgentVelocity(AgentId) : FVector::ZeroVector;

	TMap<FName, FLearningAgentsObservationObjectElement> Elements;
	Elements.Add(TEXT("Target"), ULearningAgentsObservations::MakeLocationObservation(InObservationObject, Target, FTransform(AgentLocation), TEXT("Target")));
	Elements.Add(TEXT("Velocity"), ULearningAgentsObservations::MakeVelocityObservation(InObservationObject, Velocity, FTransform::Identity, TEXT("Velocity")));
	OutObservationObjectElement = ULearningAgentsObservations::MakeStructObservation(InObservationObject, Elements);

	if (Task) { Task->NoteDecision(AgentId); }
}

void UReachInteractor::SpecifyAgentAction_Implementation(FLearningAgentsActionSchemaElement& OutActionSchemaElement, ULearningAgentsActionSchema* InActionSchema)
{
	OutActionSchemaElement = ULearningAgentsActions::SpecifyContinuousAction(InActionSchema, 2, 1.f, TEXT("Move"));
}

void UReachInteractor::PerformAgentAction_Implementation(const ULearningAgentsActionObject* InActionObject, const FLearningAgentsActionObjectElement& InActionObjectElement, const int32 AgentId)
{
	TArray<float> Values;
	if (ULearningAgentsActions::GetContinuousAction(Values, InActionObject, InActionObjectElement, TEXT("Move")) && Values.Num() == 2)
	{
		if (AReachTaskActor* Task = TaskOf(this)) { Task->ApplyMove(AgentId, Values[0], Values[1]); }
	}
}

void UReachTrainingEnv::GatherAgentReward_Implementation(float& OutReward, const int32 AgentId)
{
	AReachTaskActor* Task = TaskOf(this);
	OutReward = Task ? Task->TakeStepReward(AgentId) : 0.f;
}

void UReachTrainingEnv::GatherAgentCompletion_Implementation(ELearningAgentsCompletion& OutCompletion, const int32 AgentId)
{
	// Reaching the target ends the episode. The step cap (MaxEpisodeStepNum) is a truncation handled by the PPO trainer.
	AReachTaskActor* Task = TaskOf(this);
	OutCompletion = (Task && Task->IsAgentReached(AgentId)) ? ELearningAgentsCompletion::Termination : ELearningAgentsCompletion::Running;
}

void UReachTrainingEnv::ResetAgentEpisode_Implementation(const int32 AgentId)
{
	if (AReachTaskActor* Task = TaskOf(this)) { Task->ResetAgent(AgentId); }
}
