#include "HoverPackTunerModule.h"

#include "HoverPackTuner.h"

void FHoverPackTunerModule::StartupModule()
{
	FHoverPackTuner::RegisterHooks();
}

IMPLEMENT_MODULE(FHoverPackTunerModule, HoverPackTuner)
