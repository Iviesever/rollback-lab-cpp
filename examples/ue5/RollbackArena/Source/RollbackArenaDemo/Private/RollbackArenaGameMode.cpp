#include "RollbackArenaGameMode.h"
#include "RollbackArenaView.h"
#include "RollbackArenaHUD.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

ARollbackArenaGameMode::ARollbackArenaGameMode()
{
    DefaultPawnClass=nullptr;
    HUDClass=ARollbackArenaHUD::StaticClass();
}
void ARollbackArenaGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
    // Input controls the SDK through the arena; no Pawn/physics owns gameplay state.
    NewPlayer->bAutoManageActiveCameraTarget=false;
}
void ARollbackArenaGameMode::StartPlay()
{
    Super::StartPlay();
    GetWorld()->SpawnActor<ARollbackArenaView>();
}
