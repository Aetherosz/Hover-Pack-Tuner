#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FHoverPackTunerModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
};
