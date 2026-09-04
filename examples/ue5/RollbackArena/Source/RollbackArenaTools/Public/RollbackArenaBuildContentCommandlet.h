#pragma once

#include "Commandlets/Commandlet.h"
#include "RollbackArenaBuildContentCommandlet.generated.h"

UCLASS()
class ROLLBACKARENATOOLS_API URollbackArenaBuildContentCommandlet final : public UCommandlet
{
    GENERATED_BODY()

public:
    URollbackArenaBuildContentCommandlet();
    virtual int32 Main(const FString& Params) override;
};
