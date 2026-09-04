#include "RollbackLabSubsystem.h"

URollbackLabSubsystem::URollbackLabSubsystem()
    : Runtime(MakeUnique<RollbackLabBridge::FRuntime>()) {}
URollbackLabSubsystem::~URollbackLabSubsystem() = default;
void URollbackLabSubsystem::Deinitialize()
{
    Runtime->Stop();
    Super::Deinitialize();
}
RollbackLabBridge::FRuntime& URollbackLabSubsystem::GetRuntime() { return *Runtime; }
const RollbackLabBridge::FRuntime& URollbackLabSubsystem::GetRuntime() const { return *Runtime; }
