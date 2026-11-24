#include "HoverPackTunerSubsystem.h"

#include "HoverPackTuner.h"
#include "Net/UnrealNetwork.h"

AHoverPackTunerSubsystem::AHoverPackTunerSubsystem()
{
	ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnServer_Replicate;
	bAlwaysRelevant = true;
}

void AHoverPackTunerSubsystem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHoverPackTunerSubsystem, Percents);
}

void AHoverPackTunerSubsystem::SetPercents(const TArray<int32>& NewPercents)
{
	if (HasAuthority() && Percents != NewPercents)
	{
		Percents = NewPercents;
		FlushNetDormancy();
		ForceNetUpdate();
	}
}

void AHoverPackTunerSubsystem::OnRep_Percents()
{
	FHoverPackTuner::OnHostValues(GetWorld());
}
