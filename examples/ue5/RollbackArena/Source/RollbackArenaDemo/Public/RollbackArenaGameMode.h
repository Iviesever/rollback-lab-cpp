#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RollbackArenaGameMode.generated.h"

UCLASS()
class ROLLBACKARENADEMO_API ARollbackArenaGameMode final : public AGameModeBase
{
    GENERATED_BODY()
public:
    ARollbackArenaGameMode();
    virtual void StartPlay() override;
    virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
};
