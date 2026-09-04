#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RollbackArenaHUD.generated.h"
class ARollbackArenaView;

UCLASS()
class ROLLBACKARENADEMO_API ARollbackArenaHUD final : public AHUD
{
    GENERATED_BODY()
public:
    void SetArena(ARollbackArenaView* InArena) { Arena=InArena; }
    virtual void DrawHUD() override;
private:
    UPROPERTY() TObjectPtr<ARollbackArenaView> Arena;
    void Label(const FString& Text, float X, float Y, float Size, FLinearColor Color);
    void WorldLine(const FVector& A, const FVector& B, FLinearColor Color, float Width=1.0f);
    void PeerPanel(uint32 Peer, float Scale);
};
