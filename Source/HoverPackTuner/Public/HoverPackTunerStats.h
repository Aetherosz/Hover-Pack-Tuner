#pragma once

#include "CoreMinimal.h"

/** The packs and stats this mod can change. All values are percentages of the vanilla Hover Pack. */
namespace HoverPackTunerStats
{
	enum EStat : int32
	{
		Speed,
		Acceleration,
		Range,
		Power,
		UseDistance,
		NumStats
	};

	inline constexpr int32 NumTiers = 4;
	inline constexpr int32 VanillaTier = 2;
	inline constexpr int32 NumValues = NumTiers * NumStats;
	inline constexpr int32 MinPercent = 1;
	inline constexpr int32 MaxPercent = 500;

	struct FTier
	{
		const TCHAR* Key;
		const TCHAR* Label;
		const TCHAR* ClassPath;
		/** What Progressive Hover Pack 1.1.0 gives this pack, in the order of EStat. */
		int32 Defaults[NumStats];
	};

	inline constexpr FTier Tiers[NumTiers] = {
		{ TEXT("Mk1"), TEXT("Mk.1"), TEXT("/ProgressiveHoverPack/Equip/Equip_HoverpackMk1.Equip_HoverpackMk1_C"), { 33, 25, 35, 25, 25 } },
		{ TEXT("Mk2"), TEXT("Mk.2"), TEXT("/ProgressiveHoverPack/Equip/Equip_HoverpackMk2.Equip_HoverpackMk2_C"), { 70, 50, 50, 50, 50 } },
		{ TEXT("Mk3"), TEXT("Mk.3"), TEXT("/Game/FactoryGame/Equipment/HoverPack/Equip_HoverPack.Equip_HoverPack_C"), { 100, 100, 100, 100, 100 } },
		{ TEXT("Mk4"), TEXT("Mk.4"), TEXT("/ProgressiveHoverPack/Equip/Equip_HoverpackMk4.Equip_HoverpackMk4_C"), { 160, 150, 150, 200, 150 } },
	};

	inline constexpr const TCHAR* StatKeys[NumStats] = {
		TEXT("Speed"), TEXT("Acceleration"), TEXT("Range"), TEXT("Power"), TEXT("UseDistance")
	};
}
