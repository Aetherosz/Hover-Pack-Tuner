#pragma once

#include "CoreMinimal.h"
#include "Subsystem/ModSubsystem.h"
#include "HoverPackTunerSubsystem.generated.h"

/** Carries the host's values to the clients. Not saved: the host's config is the only source. */
UCLASS()
class AHoverPackTunerSubsystem : public AModSubsystem
{
	GENERATED_BODY()

public:
	AHoverPackTunerSubsystem();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server only. */
	void SetPercents(const TArray<int32>& NewPercents);
	const TArray<int32>& GetPercents() const { return Percents; }

private:
	UFUNCTION()
	void OnRep_Percents();

	/** Tier by tier, each in the order of HoverPackTunerStats::EStat. Empty until the host sends it. */
	UPROPERTY(ReplicatedUsing = OnRep_Percents)
	TArray<int32> Percents;
};
