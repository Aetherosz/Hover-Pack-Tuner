#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HoverPackTunerStats.h"

class AFGHoverPack;
class UConfigManager;
class UConfigPropertySection;
class UGameInstance;
class UWorld;

/**
 * Changes the stats of hover pack instances, never class defaults.
 *
 * Progressive Hover Pack works out the Mk.1, 2 and 4 stats in each pack's Blueprint BeginPlay from the
 * vanilla class defaults. Changing those defaults would feed into its maths, and the Mk classes' own
 * defaults aren't what is used in game. So each pack's values are captured after BeginPlay, and every
 * change is worked out from that capture.
 *
 * A friend of AFGHoverPack (see Config/AccessTransformers.ini).
 */
class FHoverPackTuner
{
public:
	static void RegisterHooks();
	/** Client: the host's values arrived or changed. */
	static void OnHostValues(UWorld* World);

private:
	struct FPackState
	{
		int32 Tier = INDEX_NONE;
		float Baseline[HoverPackTunerStats::NumStats] = {};
		/** Whether this mod has changed the stat. One it never changed is never written. */
		bool bWritten[HoverPackTunerStats::NumStats] = {};
	};

	static void RegisterConfig(UGameInstance* GameInstance);
	static void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	static void OnPlayerBeginPlay(UWorld* World, UGameInstance* GameInstance);
	static void OnPackBeginPlay(AFGHoverPack* Pack);
	static void OnConfigChanged();
	static void OnWorldBeginTearDown(UWorld* World);

	/** Seconds without a change before the settings are written to disk. */
	static constexpr float SaveDelay = 0.5f;
	static void ScheduleSave();
	static void SaveNow();

	static float& StatField(AFGHoverPack* Pack, int32 Stat);
	static void CheckBaseline(const AFGHoverPack* Pack, const FPackState& State);
	static void Apply(AFGHoverPack* Pack, FPackState& State, const TArray<int32>& Percents);
	static void ApplyAll(UWorld* World);
	static void ShareWithClients(UWorld* World);

	static TArray<int32> GetConfigPercents();
	/** The values in force: this machine's config on the host, the replicated ones on a client. */
	static TArray<int32> GetPercents(UWorld* World);
	static UConfigPropertySection* GetConfigRoot();
	static void UpdateStatus(const UWorld* World);

	static TWeakObjectPtr<UConfigManager> ConfigManager;
	static TWeakObjectPtr<UWorld> ActiveWorld;
	static TMap<TWeakObjectPtr<AFGHoverPack>, FPackState> Packs;
	static FTSTicker::FDelegateHandle SaveTicker;
	static bool bSavePending;
};
