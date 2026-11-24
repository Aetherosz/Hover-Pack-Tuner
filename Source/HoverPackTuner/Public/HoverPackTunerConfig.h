#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "HoverPackTunerConfig.generated.h"

/**
 * Shows up in the game's Mods -> Mod Configuration menu: one section per Mk, five values each.
 * SML saves it to FactoryGame/Configs/HoverPackTuner.cfg.
 */
UCLASS()
class UHoverPackTunerConfig : public UModConfiguration
{
	GENERATED_BODY()

public:
	UHoverPackTunerConfig();

	static const FString StatusKey;
	static FConfigId GetId();

	/** Whether Progressive Hover Pack is installed. Without it only the vanilla pack (Mk.3) is shown and changed. */
	static bool HasTiers();

	/**
	 * SML's C++ property classes have no menu widget (only its Blueprint subclasses do), so this
	 * swaps the default object's properties for those. Call before registering.
	 */
	static void UseMenuWidgets();
};
