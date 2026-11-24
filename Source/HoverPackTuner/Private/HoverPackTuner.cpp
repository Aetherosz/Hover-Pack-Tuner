#include "HoverPackTuner.h"

#include "HoverPackTunerConfig.h"
#include "HoverPackTunerSubsystem.h"

#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/FGHoverPack.h"
#include "FGCharacterPlayer.h"
#include "FGPowerInfoComponent.h"
#include "Interfaces/IPluginManager.h"
#include "Patching/NativeHookManager.h"
#include "Subsystem/SubsystemActorManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogHoverPackTuner, Log, All);

namespace HPT = HoverPackTunerStats;

TWeakObjectPtr<UConfigManager> FHoverPackTuner::ConfigManager;
TWeakObjectPtr<UWorld> FHoverPackTuner::ActiveWorld;
TMap<TWeakObjectPtr<AFGHoverPack>, FHoverPackTuner::FPackState> FHoverPackTuner::Packs;
FTSTicker::FDelegateHandle FHoverPackTuner::SaveTicker;
bool FHoverPackTuner::bSavePending = false;

namespace
{
	/**
	 * HoverPack Configurator rewrites speed, acceleration and range whenever the movement mode changes,
	 * from its own copy of the first values it saw. Two mods writing those would give results that
	 * depend on timing, so they are left to the Configurator when it is installed.
	 */
	bool IsConfiguratorInstalled()
	{
		static const bool bInstalled = IPluginManager::Get().FindEnabledPlugin(TEXT("NoHoverPackDrift")).IsValid();
		return bInstalled;
	}

	bool IsLeftToConfigurator(int32 Stat)
	{
		return (Stat == HPT::Speed || Stat == HPT::Acceleration || Stat == HPT::Range) && IsConfiguratorInstalled();
	}

	TArray<int32> GetDefaultPercents()
	{
		TArray<int32> Result;
		Result.Reserve(HPT::NumValues);
		for (const HPT::FTier& Tier : HPT::Tiers)
		{
			Result.Append(Tier.Defaults, HPT::NumStats);
		}
		return Result;
	}
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

void FHoverPackTuner::RegisterHooks()
{
	FWorldDelegates::OnStartGameInstance.AddStatic(&FHoverPackTuner::RegisterConfig);
	FWorldDelegates::OnWorldCleanup.AddStatic(&FHoverPackTuner::OnWorldCleanup);
	// The first step of both leaving a world and quitting, while SML's config manager can still save.
	FWorldDelegates::OnWorldBeginTearDown.AddStatic(&FHoverPackTuner::OnWorldBeginTearDown);

#if !WITH_EDITOR
	SUBSCRIBE_METHOD_AFTER(UConfigManager::MarkConfigurationDirty,
		[](UConfigManager* Self, const FConfigId& Id)
		{
			if (Id == UHoverPackTunerConfig::GetId())
			{
				ScheduleSave();
				OnConfigChanged();
			}
		});

	SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGCharacterPlayer::BeginPlay, GetMutableDefault<AFGCharacterPlayer>(),
		[](AFGCharacterPlayer* Self)
		{
			OnPlayerBeginPlay(Self->GetWorld(), Self->GetGameInstance());
		});

	// After: by then the Blueprint BeginPlay has set the Mk's stats and the game has set the power draw.
	SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGHoverPack::BeginPlay, GetMutableDefault<AFGHoverPack>(),
		[](AFGHoverPack* Self)
		{
			OnPackBeginPlay(Self);
		});
#endif
}

void FHoverPackTuner::OnPlayerBeginPlay(UWorld* World, UGameInstance* GameInstance)
{
	if (!World)
	{
		return;
	}
	RegisterConfig(GameInstance); // fallback if OnStartGameInstance fired before we loaded
	if (ActiveWorld.Get() == World)
	{
		return;
	}
	ActiveWorld = World;

	const bool bHasTiers = UHoverPackTunerConfig::HasTiers();
	if (!bHasTiers)
	{
		UE_LOG(LogHoverPackTuner, Display, TEXT("Progressive Hover Pack isn't installed: only the normal Hover Pack is changed."));
	}
	for (int32 TierIndex = 0; TierIndex < HPT::NumTiers; ++TierIndex)
	{
		const HPT::FTier& Tier = HPT::Tiers[TierIndex];
		if (!bHasTiers && TierIndex != HPT::VanillaTier)
		{
			continue;
		}
		if (FSoftClassPath(Tier.ClassPath).TryLoadClass<AFGHoverPack>())
		{
			UE_LOG(LogHoverPackTuner, Display, TEXT("Found %s: %s"), Tier.Label, Tier.ClassPath);
		}
		else
		{
			UE_LOG(LogHoverPackTuner, Error, TEXT("Missing %s: %s. Its settings won't do anything."), Tier.Label, Tier.ClassPath);
		}
	}
	if (World->GetNetMode() != NM_Client)
	{
		ShareWithClients(World);
	}
	ApplyAll(World);
	UpdateStatus(World);
}

void FHoverPackTuner::OnPackBeginPlay(AFGHoverPack* Pack)
{
	for (auto It = Packs.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	// Exact classes only, so other mods' hover packs are left alone.
	const FString ClassPath = Pack->GetClass()->GetPathName();
	for (int32 TierIndex = 0; TierIndex < HPT::NumTiers; ++TierIndex)
	{
		if (!ClassPath.Equals(HPT::Tiers[TierIndex].ClassPath, ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (Packs.Contains(Pack))
		{
			return; // a second capture would compound
		}
		FPackState State;
		State.Tier = TierIndex;
		for (int32 Stat = 0; Stat < HPT::NumStats; ++Stat)
		{
			State.Baseline[Stat] = StatField(Pack, Stat);
		}
		CheckBaseline(Pack, State);
		Apply(Pack, Packs.Add(Pack, State), GetPercents(Pack->GetWorld()));
		return;
	}
}

void FHoverPackTuner::OnConfigChanged()
{
	UWorld* World = ActiveWorld.Get();
	if (World && World->GetNetMode() != NM_Client)
	{
		ShareWithClients(World);
		ApplyAll(World);
	}
}

void FHoverPackTuner::OnHostValues(UWorld* World)
{
	ApplyAll(World);
}

void FHoverPackTuner::OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	for (auto It = Packs.CreateIterator(); It; ++It)
	{
		const AFGHoverPack* Pack = It.Key().Get();
		if (!Pack || Pack->GetWorld() == World)
		{
			It.RemoveCurrent();
		}
	}
	if (ActiveWorld.Get() == World)
	{
		ActiveWorld.Reset();
		UpdateStatus(nullptr);
	}
}

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------

void FHoverPackTuner::ScheduleSave()
{
	// SML's own save timer stops while the game is paused, and its last-moment save at exit runs after
	// the game instance has shut down, which crashes. The core ticker keeps running while paused.
	// Every change restarts the wait, so a drag or opening the menu ends in a single write.
	bSavePending = true;
	if (SaveTicker.IsValid())
	{
		FTSTicker::RemoveTicker(SaveTicker);
	}
	SaveTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
		{
			SaveTicker.Reset();
			SaveNow();
			return false; // once
		}), SaveDelay);
}

void FHoverPackTuner::SaveNow()
{
	if (SaveTicker.IsValid())
	{
		FTSTicker::RemoveTicker(SaveTicker);
		SaveTicker.Reset();
	}
	if (!bSavePending)
	{
		return;
	}
	bSavePending = false;
	if (UConfigManager* Manager = ConfigManager.Get())
	{
		Manager->FlushPendingSaves();
	}
}

void FHoverPackTuner::OnWorldBeginTearDown(UWorld* World)
{
	SaveNow();
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------

float& FHoverPackTuner::StatField(AFGHoverPack* Pack, int32 Stat)
{
	switch (Stat)
	{
	case HPT::Speed: return Pack->mHoverSpeed;
	case HPT::Acceleration: return Pack->mHoverAccelerationSpeed;
	case HPT::Range: return Pack->mPowerConnectionSearchRadius;
	case HPT::Power: return Pack->mPowerConsumption;
	default:
		check(Stat == HPT::UseDistance);
		return Pack->mCharacterUseDistanceWhenActive;
	}
}

void FHoverPackTuner::CheckBaseline(const AFGHoverPack* Pack, const FPackState& State)
{
	const HPT::FTier& Tier = HPT::Tiers[State.Tier];
	UE_LOG(LogHoverPackTuner, Display,
		TEXT("%s defaults on %s: speed %.2f, acceleration %.2f, range %.2f, power %.2f, use distance %.2f"),
		Tier.Label, *Pack->GetName(), State.Baseline[HPT::Speed], State.Baseline[HPT::Acceleration],
		State.Baseline[HPT::Range], State.Baseline[HPT::Power], State.Baseline[HPT::UseDistance]);

	UClass* VanillaClass = FSoftClassPath(HPT::Tiers[HPT::VanillaTier].ClassPath).TryLoadClass<AFGHoverPack>();
	if (!VanillaClass)
	{
		return;
	}
	AFGHoverPack* Vanilla = VanillaClass->GetDefaultObject<AFGHoverPack>();
	for (int32 Stat = 0; Stat < HPT::NumStats; ++Stat)
	{
		const float Expected = StatField(Vanilla, Stat) * Tier.Defaults[Stat] / 100.f;
		if (!FMath::IsNearlyEqual(State.Baseline[Stat], Expected, FMath::Max(0.01f, FMath::Abs(Expected) * 0.001f)))
		{
			UE_LOG(LogHoverPackTuner, Warning,
				TEXT("%s %s is %.2f, expected %.2f (%d%%). Another mod or a new Progressive Hover Pack version changed it."),
				Tier.Label, HPT::StatKeys[Stat], State.Baseline[Stat], Expected, Tier.Defaults[Stat]);
		}
	}
}

void FHoverPackTuner::Apply(AFGHoverPack* Pack, FPackState& State, const TArray<int32>& Percents)
{
	const HPT::FTier& Tier = HPT::Tiers[State.Tier];
	bool bPowerChanged = false;

	for (int32 Stat = 0; Stat < HPT::NumStats; ++Stat)
	{
		// Values from the host arrive over the network, so they are clamped here as well.
		const int32 Percent = FMath::Clamp(Percents[State.Tier * HPT::NumStats + Stat], HPT::MinPercent, HPT::MaxPercent);
		const bool bDefault = Percent == Tier.Defaults[Stat];
		if (IsLeftToConfigurator(Stat) || (bDefault && !State.bWritten[Stat]))
		{
			continue;
		}
		// Scaled from the capture, so repeated changes don't compound and the default gives back the exact original.
		const float Value = bDefault ? State.Baseline[Stat] : State.Baseline[Stat] * Percent / Tier.Defaults[Stat];
		float& Field = StatField(Pack, Stat);
		State.bWritten[Stat] = !bDefault;
		if (Field != Value)
		{
			Field = Value;
			bPowerChanged |= Stat == HPT::Power;
		}
	}

	// Speed, acceleration, range and use distance are read whenever they are needed. The power draw is
	// only handed to the grid in BeginPlay, so it has to be handed over again.
	if (bPowerChanged && Pack->HasAuthority() && Pack->mPowerInfo)
	{
		const float Draw = Pack->GetPowerConsumption();
		Pack->mPowerInfo->SetTargetConsumption(Draw);
		Pack->mPowerInfo->SetMaximumTargetConsumption(Draw);
	}
}

void FHoverPackTuner::ApplyAll(UWorld* World)
{
	if (!World)
	{
		return;
	}
	const TArray<int32> Percents = GetPercents(World);
	for (TPair<TWeakObjectPtr<AFGHoverPack>, FPackState>& Entry : Packs)
	{
		AFGHoverPack* Pack = Entry.Key.Get();
		if (Pack && Pack->GetWorld() == World)
		{
			Apply(Pack, Entry.Value, Percents);
		}
	}
}

void FHoverPackTuner::ShareWithClients(UWorld* World)
{
	if (USubsystemActorManager* Manager = World->GetSubsystem<USubsystemActorManager>())
	{
		Manager->RegisterSubsystemActor(AHoverPackTunerSubsystem::StaticClass());
		if (AHoverPackTunerSubsystem* Subsystem = Manager->GetSubsystemActor<AHoverPackTunerSubsystem>())
		{
			Subsystem->SetPercents(GetConfigPercents());
		}
	}
}

// ---------------------------------------------------------------------------
// Mod config
// ---------------------------------------------------------------------------

void FHoverPackTuner::RegisterConfig(UGameInstance* GameInstance)
{
	if (!GameInstance || ConfigManager.IsValid())
	{
		return;
	}
	UConfigManager* Manager = GameInstance->GetSubsystem<UConfigManager>();
	if (!Manager)
	{
		UE_LOG(LogHoverPackTuner, Error, TEXT("SML config manager not found; using default settings"));
		return;
	}
	UHoverPackTunerConfig::UseMenuWidgets();
	Manager->RegisterModConfiguration(UHoverPackTunerConfig::StaticClass());
	ConfigManager = Manager;
	UpdateStatus(nullptr);
}

UConfigPropertySection* FHoverPackTuner::GetConfigRoot()
{
	UConfigManager* Manager = ConfigManager.Get();
	return Manager ? Manager->GetConfigurationRootSection(UHoverPackTunerConfig::GetId()) : nullptr;
}

TArray<int32> FHoverPackTuner::GetConfigPercents()
{
	TArray<int32> Result = GetDefaultPercents();
	const UConfigPropertySection* Root = GetConfigRoot();
	if (!Root)
	{
		return Result;
	}
	for (int32 TierIndex = 0; TierIndex < HPT::NumTiers; ++TierIndex)
	{
		const TObjectPtr<UConfigProperty>* Found = Root->SectionProperties.Find(HPT::Tiers[TierIndex].Key);
		const UConfigPropertySection* Section = Found ? Cast<UConfigPropertySection>(Found->Get()) : nullptr;
		if (!Section)
		{
			continue;
		}
		for (int32 Stat = 0; Stat < HPT::NumStats; ++Stat)
		{
			const TObjectPtr<UConfigProperty>* Entry = Section->SectionProperties.Find(HPT::StatKeys[Stat]);
			if (const UConfigPropertyInteger* Value = Entry ? Cast<UConfigPropertyInteger>(Entry->Get()) : nullptr)
			{
				Result[TierIndex * HPT::NumStats + Stat] = FMath::Clamp(Value->Value, HPT::MinPercent, HPT::MaxPercent);
			}
		}
	}
	return Result;
}

TArray<int32> FHoverPackTuner::GetPercents(UWorld* World)
{
	if (!World || World->GetNetMode() != NM_Client)
	{
		return GetConfigPercents();
	}
	for (TActorIterator<AHoverPackTunerSubsystem> It(World); It; ++It)
	{
		if (It->GetPercents().Num() == HPT::NumValues)
		{
			return It->GetPercents();
		}
	}
	return GetDefaultPercents(); // a client never uses its own config, not even while waiting
}

void FHoverPackTuner::UpdateStatus(const UWorld* World)
{
	const UConfigPropertySection* Root = GetConfigRoot();
	const TObjectPtr<UConfigProperty>* Status = Root ? Root->SectionProperties.Find(UHoverPackTunerConfig::StatusKey) : nullptr;
	if (!Status || !*Status)
	{
		return;
	}
	FString Text = World && World->GetNetMode() == NM_Client
		? TEXT("Multiplayer: the host's settings are used.")
		: TEXT("Values are percent of the normal Hover Pack.");
	if (!UHoverPackTunerConfig::HasTiers())
	{
		Text += TEXT("\nProgressive Hover Pack isn't installed, so only the normal Hover Pack is listed.");
	}
	if (IsConfiguratorInstalled())
	{
		Text += TEXT("\nHoverPack Configurator is installed and controls speed, acceleration and range. Only power use and use distance work here.");
	}
	(*Status)->DisplayName = FText::FromString(Text);
}
