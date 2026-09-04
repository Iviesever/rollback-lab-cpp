#pragma once

#include "RollbackLabRuntime.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RollbackLabSubsystem.generated.h"

UCLASS()
class ROLLBACKLABBRIDGE_API URollbackLabSubsystem final : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    URollbackLabSubsystem();
    virtual ~URollbackLabSubsystem() override;
    virtual void Deinitialize() override;
    RollbackLabBridge::FRuntime& GetRuntime();
    const RollbackLabBridge::FRuntime& GetRuntime() const;

private:
    TUniquePtr<RollbackLabBridge::FRuntime> Runtime;
};
