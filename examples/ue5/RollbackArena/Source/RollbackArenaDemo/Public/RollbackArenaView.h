#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RollbackArenaModel.h"
#include "RollbackArenaView.generated.h"

class UCameraComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class URollbackLabSubsystem;

UCLASS()
class ROLLBACKARENADEMO_API ARollbackArenaView final : public AActor
{
    GENERATED_BODY()
public:
    ARollbackArenaView();
    virtual ~ARollbackArenaView() override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    static FVector ProjectPosition(uint32 Peer, int32 X, int32 Y, double Z=8.0);
    const RollbackArena::FModel* GetModel() const { return Model.Get(); }
    const FString& GetFailure() const { return Failure; }
    float CorrectionAlpha(uint32 Peer) const;
    bool IsSmoke() const { return bSmoke; }
    bool IsWaitingForCapture() const { return bCapturePending; }

private:
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Players;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Projectiles;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Floors;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    UPROPERTY() TObjectPtr<URollbackLabSubsystem> Bridge;
    TWeakObjectPtr<APlayerController> Controller;
    TUniquePtr<RollbackArena::FModel> Model;
    FDelegateHandle ScreenshotHandle;
    FString Failure, CaptureDirectory, TracePath, RequestedGitSha, PendingCaptureName;
    uint64 SeenCorrection[2]{};
    float CorrectionAge[2]{10.0f,10.0f};
    double StartedAt=0.0, ExitAfterSeconds=0.0;
    bool bSmoke=false, bCapturePending=false, bStartCaptured=false;
    bool bCorrectionCaptured=false, bFinalCaptured=false, bWritten=false, bExiting=false;
    uint32 WarmupTicks=3;
    uint32 PendingButtons=0;

    bool InitializeVisuals();
    bool ReadSettings(RollbackArena::FSettings& Settings);
    void UpdateProjection();
    void ReadControls(bool& bSkipClock);
    uint32 LocalButtons() const;
    void RequestCapture(const FString& Name);
    void ScreenshotProcessed();
    bool SaveEvidence(bool bSuccess);
    void Fail(const FString& Reason);
    void Quit(uint8 Status);
};
