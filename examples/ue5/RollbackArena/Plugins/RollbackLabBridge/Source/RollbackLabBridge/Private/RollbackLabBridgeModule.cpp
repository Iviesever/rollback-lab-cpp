#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"
#include "RollbackLabSdk.h"

DEFINE_LOG_CATEGORY(LogRollbackLabBridge);

class FRollbackLabBridgeModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        PreExitHandle = FCoreDelegates::OnEnginePreExit.AddStatic(&RollbackLabBridge::FRuntime::StopAll);
    }
    virtual void ShutdownModule() override
    {
        FCoreDelegates::OnEnginePreExit.Remove(PreExitHandle);
        RollbackLabBridge::FRuntime::StopAll();
    }
    virtual bool SupportsDynamicReloading() override { return false; }

private:
    FDelegateHandle PreExitHandle;
};

IMPLEMENT_MODULE(FRollbackLabBridgeModule, RollbackLabBridge)
