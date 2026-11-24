#include "HoverPackTunerConfig.h"

#include "HoverPackTunerLabel.h"
#include "HoverPackTunerStats.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/WidgetExtension/CP_Integer.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"
#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "HoverPackTuner"

const FString UHoverPackTunerConfig::StatusKey = TEXT("Status");

FConfigId UHoverPackTunerConfig::GetId()
{
	FConfigId Id;
	Id.ModReference = TEXT("HoverPackTuner");
	Id.ConfigCategory = TEXT("");
	return Id;
}

bool UHoverPackTunerConfig::HasTiers()
{
	return IPluginManager::Get().FindEnabledPlugin(TEXT("ProgressiveHoverPack")).IsValid();
}

UHoverPackTunerConfig::UHoverPackTunerConfig()
{
	namespace HPT = HoverPackTunerStats;

	ConfigId = GetId();
	DisplayName = LOCTEXT("ConfigName", "Hover Pack Tuner");
	Description = LOCTEXT("ConfigDescription",
		"Change the Hover Pack's stats, for each Mk if Progressive Hover Pack is installed. Values are percent of the normal Hover Pack. In multiplayer the host's settings count.");

	const FText StatNames[HPT::NumStats] = {
		LOCTEXT("SpeedName", "Speed (%)"),
		LOCTEXT("AccelerationName", "Acceleration (%)"),
		LOCTEXT("RangeName", "Range (%)"),
		LOCTEXT("PowerName", "Power use (%)"),
		LOCTEXT("UseDistanceName", "Use distance (%)"),
	};
	const FText StatTips[HPT::NumStats] = {
		LOCTEXT("SpeedTip", "How fast you hover. Sprinting scales with it."),
		LOCTEXT("AccelerationTip", "How quickly you get up to speed. Sprinting scales with it."),
		LOCTEXT("RangeTip", "How far you can get from a power connection."),
		LOCTEXT("PowerTip", "How much power the pack draws from the grid."),
		LOCTEXT("UseDistanceTip", "How far you can reach while hovering. It can't go below your normal reach, so very low values all feel the same."),
	};

	RootSection = CreateDefaultSubobject<UConfigPropertySection>(TEXT("RootSection"));

	UHoverPackTunerLabel* Status = CreateDefaultSubobject<UHoverPackTunerLabel>(TEXT("Status"));
	Status->DisplayName = LOCTEXT("StatusMenu", "Values are percent of the normal Hover Pack.");
	Status->bAllowUserReset = false;
	RootSection->SectionProperties.Add(StatusKey, Status);

	for (const HPT::FTier& Tier : HPT::Tiers)
	{
		UConfigPropertySection* Section = CreateDefaultSubobject<UConfigPropertySection>(Tier.Key);
		Section->DisplayName = FText::FromString(Tier.Label);
		RootSection->SectionProperties.Add(Tier.Key, Section);

		for (int32 Stat = 0; Stat < HPT::NumStats; ++Stat)
		{
			UConfigPropertyInteger* Value = CreateDefaultSubobject<UConfigPropertyInteger>(
				FName(*FString::Printf(TEXT("%s_%s"), Tier.Key, HPT::StatKeys[Stat])));
			Value->DisplayName = StatNames[Stat];
			Value->Tooltip = FText::Format(
				LOCTEXT("StatTooltip", "{0}\n100 = the normal Hover Pack. Default: {1}. Limits: {2} to {3}."),
				StatTips[Stat], FText::AsNumber(Tier.Defaults[Stat]),
				FText::AsNumber(HPT::MinPercent), FText::AsNumber(HPT::MaxPercent));
			Value->DefaultValue = Tier.Defaults[Stat];
			Value->Value = Tier.Defaults[Stat];
			Value->bRequiresWorldReload = false;
			Section->SectionProperties.Add(HPT::StatKeys[Stat], Value);
		}
	}
}

namespace
{
	/** Copy of Native as SML's Blueprint version of its class, or Native itself if there is none. */
	UConfigProperty* ToMenuProperty(UConfigProperty* Native, UObject* Outer, bool bRoot)
	{
		const UClass* NativeClass = Native->GetClass();
		const TCHAR* BlueprintName =
			NativeClass == UConfigPropertySection::StaticClass() ? TEXT("BP_ConfigPropertySection") :
			NativeClass == UConfigPropertyInteger::StaticClass() ? TEXT("BP_ConfigPropertyInteger") : nullptr;
		if (!BlueprintName)
		{
			// Has its own widget (our label): copy it as is.
			return DuplicateObject<UConfigProperty>(Native, Outer,
				MakeUniqueObjectName(Outer, NativeClass, FName(*(Native->GetName() + TEXT("_Menu")))));
		}
		UClass* BlueprintClass = LoadClass<UConfigProperty>(nullptr,
			*FString::Printf(TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/%s.%s_C"), BlueprintName, BlueprintName));
		if (!BlueprintClass)
		{
			UE_LOG(LogTemp, Error, TEXT("HoverPackTuner: %s not found, the config menu will be empty"), BlueprintName);
			return Native;
		}

		// Unique name: UE refuses to replace an object with one of a different class under the same name.
		// Sections look properties up by their map key, so the name isn't used.
		UConfigProperty* Menu = NewObject<UConfigProperty>(Outer, BlueprintClass,
			MakeUniqueObjectName(Outer, BlueprintClass, FName(*(Native->GetName() + TEXT("_Menu")))));
		Menu->DisplayName = Native->DisplayName;
		Menu->Tooltip = Native->Tooltip;
		Menu->bRequiresWorldReload = Native->bRequiresWorldReload;
		Menu->bHidden = Native->bHidden;
		Menu->bAllowUserReset = Native->bAllowUserReset;

		if (const UConfigPropertyInteger* Int = Cast<UConfigPropertyInteger>(Native))
		{
			UConfigPropertyInteger* MenuInt = CastChecked<UConfigPropertyInteger>(Menu);
			MenuInt->DefaultValue = Int->DefaultValue;
			MenuInt->Value = Int->Value;
			if (UCP_Integer* Widget = Cast<UCP_Integer>(MenuInt))
			{
				// SML's default is a text box with min = max = 0.
				Widget->WidgetType = ECP_IntegerWidgetType::CPI_Spinbox;
				Widget->MinValue = HoverPackTunerStats::MinPercent;
				Widget->MaxValue = HoverPackTunerStats::MaxPercent;
			}
		}
		else if (const UConfigPropertySection* Section = Cast<UConfigPropertySection>(Native))
		{
			UConfigPropertySection* MenuSection = CastChecked<UConfigPropertySection>(Menu);
			if (UCP_Section* Widget = Cast<UCP_Section>(MenuSection))
			{
				Widget->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
				if (!bRoot)
				{
					// SML shows "HeaderText (DisplayName)".
					Widget->HasHeader = true;
					Widget->HeaderText = LOCTEXT("SectionHeader", "Hover Pack");
				}
			}
			for (const TPair<FString, TObjectPtr<UConfigProperty>>& Child : Section->SectionProperties)
			{
				MenuSection->SectionProperties.Add(Child.Key, Child.Value ? ToMenuProperty(Child.Value, MenuSection, false) : nullptr);
			}
		}
		return Menu;
	}
}

void UHoverPackTunerConfig::UseMenuWidgets()
{
	namespace HPT = HoverPackTunerStats;

	UHoverPackTunerConfig* Default = GetMutableDefault<UHoverPackTunerConfig>();
	if (Default->RootSection && !HasTiers())
	{
		// Only the vanilla pack exists. The other sections stay in the file, hidden, so their values are
		// kept if Progressive Hover Pack is installed again.
		for (int32 TierIndex = 0; TierIndex < HPT::NumTiers; ++TierIndex)
		{
			const TObjectPtr<UConfigProperty>* Section = Default->RootSection->SectionProperties.Find(HPT::Tiers[TierIndex].Key);
			if (Section && *Section)
			{
				if (TierIndex == HPT::VanillaTier)
				{
					(*Section)->DisplayName = LOCTEXT("VanillaOnlyName", "normal");
				}
				else
				{
					(*Section)->bHidden = true;
				}
			}
		}
	}
	if (Default->RootSection)
	{
		Default->RootSection = CastChecked<UConfigPropertySection>(ToMenuProperty(Default->RootSection, Default, true));
	}
}

#undef LOCTEXT_NAMESPACE
