// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#include "VRM4UCapture.h"
#include "CoreMinimal.h"
#include "VRM4UCaptureLog.h"
#include "Modules/ModuleManager.h"
#include "Internationalization/Internationalization.h"
#include "OWLVrmVMCNodeRegistry.h"

#define LOCTEXT_NAMESPACE "VRM4UMisc"

DEFINE_LOG_CATEGORY(LogVRM4UCapture);

//////////////////////////////////////////////////////////////////////////

class FVRM4UCaptureModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FOWLVrmVMCNodeRegistry::Initialize();
	}

	virtual void ShutdownModule() override
	{
		FOWLVrmVMCNodeRegistry::Shutdown();
	}
};

//////////////////////////////////////////////////////////////////////////

IMPLEMENT_MODULE(FVRM4UCaptureModule, VRM4UCapture);

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
