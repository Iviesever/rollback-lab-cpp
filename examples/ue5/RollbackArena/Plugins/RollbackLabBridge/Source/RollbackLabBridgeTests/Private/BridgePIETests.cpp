#include "BridgeTestSupport.h"
#include "RollbackLabSubsystem.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
using namespace RollbackLabBridgeTests;

namespace
{
// FStartPIEForAutomationCommand in UE 5.8 uses RequestPlaySession followed by
// StartQueuedPlaySessionRequest. This latent state machine adds bounded teardown
// and per-cycle SDK ownership assertions to that same real Editor lifecycle.
class FBridgePIECycles final : public IAutomationLatentCommand
{
public:
    explicit FBridgePIECycles(FAutomationTestBase& InTest)
        : Test(InTest), Baseline(FRuntime::GetLifetimeStats()),
          Settings(NewObject<ULevelEditorPlaySettings>(GetTransientPackage()))
    {
        Settings->SetPlayNetMode(PIE_Standalone);
        Settings->SetRunUnderOneProcess(true);
        Settings->SetPlayNumberOfClients(1);
        Settings->bLaunchSeparateServer = false;
        Settings->bAutoCompileBlueprintsOnLaunch = false;
        Settings->NewWindowWidth = 640;
        Settings->NewWindowHeight = 360;
        StartedHandle = FEditorDelegates::PostPIEStarted.AddRaw(this, &FBridgePIECycles::OnStarted);
        EndedHandle = FEditorDelegates::EndPIE.AddRaw(this, &FBridgePIECycles::OnEnded);
    }

    virtual ~FBridgePIECycles() override
    {
        FEditorDelegates::PostPIEStarted.Remove(StartedHandle);
        FEditorDelegates::EndPIE.Remove(EndedHandle);
        // Covers an aborted Automation queue as well as any failed phase.
        ForceEditorTeardown();
    }

    virtual bool Update() override
    {
        if (GEditor == nullptr)
        {
            Test.AddError(TEXT("PIE restart test lost its Editor instance."));
            return true;
        }
        switch (Stage)
        {
        case EStage::Request:
        {
            bStarted = false;
            bEnded = false;
            FRequestPlaySessionParams Params;
            Params.SessionDestination = EPlaySessionDestinationType::InProcess;
            Params.WorldType = EPlaySessionWorldType::PlayInEditor;
            Params.EditorPlaySettings = Settings.Get();
            Params.GlobalMapOverride = TEXT("/Engine/Maps/Entry");
            Params.StartLocation = FVector::ZeroVector;
            Params.bAllowOnlineSubsystem = false;
            bOwnsSession = true;
            Stage = EStage::WaitReady;
            PhaseStart = FPlatformTime::Seconds();
            GEditor->RequestPlaySession(Params);
            GEditor->StartQueuedPlaySessionRequest();
            return false;
        }
        case EStage::WaitReady:
        {
            UWorld* World = FindPIEWorld();
            if (bStarted && World != nullptr && World->AreActorsInitialized() && World->HasBegunPlay())
            {
                ExerciseActualSubsystem(*World);
                BeginEnd();
                return false;
            }
            if (bEnded || FPlatformTime::Seconds() - PhaseStart > StartTimeoutSeconds)
            {
                Test.AddError(FString::Printf(TEXT("PIE cycle %u failed to reach a live initialized world within %.0f seconds."), Cycle + 1, StartTimeoutSeconds));
                bAbort = true;
                BeginEnd();
            }
            return false;
        }
        case EStage::WaitEnd:
        {
            // A timed-out asynchronous start may publish PlayWorld after BeginEnd.
            if (GEditor->PlayWorld != nullptr && !GEditor->ShouldEndPlayMap()) GEditor->RequestEndPlayMap();
            if (!GEditor->IsPlaySessionInProgress() && FindPIEWorld() == nullptr)
            {
                bOwnsSession = false;
                const FLifetimeStats After = FRuntime::GetLifetimeStats();
                bool bPassed = Test.TestTrue(CycleLabel(TEXT("PostPIEStarted fired")), bStarted);
                bPassed &= Test.TestTrue(CycleLabel(TEXT("EndPIE fired")), bEnded);
                bPassed &= Test.TestEqual(CycleLabel(TEXT("sessions return to baseline after automatic Deinitialize")), After.Sessions, Baseline.Sessions);
                bPassed &= Test.TestEqual(CycleLabel(TEXT("drivers return to baseline after automatic Deinitialize")), After.Drivers, Baseline.Drivers);
                bPassed &= Test.TestEqual(CycleLabel(TEXT("DLL leases return to baseline after automatic Deinitialize")), After.Libraries, Baseline.Libraries);
                if (!bPassed || bAbort) return true;
                ++Cycle;
                if (Cycle == CycleCount)
                {
                    Test.AddInfo(TEXT("Completed three actual PIE start/SDK step/EndPIE cycles with no retained SDK resources."));
                    return true;
                }
                Stage = EStage::Request;
                return false;
            }
            if (FPlatformTime::Seconds() - PhaseStart > EndTimeoutSeconds)
            {
                Test.AddError(FString::Printf(TEXT("PIE cycle %u did not finish Editor teardown within %.0f seconds."), Cycle + 1, EndTimeoutSeconds));
                ForceEditorTeardown();
                return true;
            }
            return false;
        }
        }
        return true;
    }

private:
    enum class EStage : uint8 { Request, WaitReady, WaitEnd };
    static constexpr uint32 CycleCount = 3;
    static constexpr double StartTimeoutSeconds = 20.0;
    static constexpr double EndTimeoutSeconds = 10.0;
    FAutomationTestBase& Test;
    FLifetimeStats Baseline;
    TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
    FDelegateHandle StartedHandle;
    FDelegateHandle EndedHandle;
    EStage Stage = EStage::Request;
    uint32 Cycle = 0;
    double PhaseStart = 0.0;
    bool bStarted = false;
    bool bEnded = false;
    bool bAbort = false;
    bool bOwnsSession = false;

    FString CycleLabel(const TCHAR* Detail) const
    {
        return FString::Printf(TEXT("PIE cycle %u: %s"), Cycle + 1, Detail);
    }

    UWorld* FindPIEWorld() const
    {
        if (GEngine == nullptr) return nullptr;
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
            if (Context.WorldType == EWorldType::PIE && Context.World() != nullptr) return Context.World();
        return nullptr;
    }

    void OnStarted(bool bSimulating)
    {
        bStarted = !bSimulating;
    }

    void OnEnded(bool)
    {
        bEnded = true;
    }

    void ExerciseActualSubsystem(UWorld& World)
    {
        UGameInstance* Instance = World.GetGameInstance();
        if (!Test.TestNotNull(CycleLabel(TEXT("real PIE GameInstance exists")), Instance)) { bAbort = true; return; }
        URollbackLabSubsystem* Subsystem = Instance->GetSubsystem<URollbackLabSubsystem>();
        if (!Test.TestNotNull(CycleLabel(TEXT("automatically created bridge subsystem exists")), Subsystem)) { bAbort = true; return; }
        FRuntime& Runtime = Subsystem->GetRuntime();
        // A future Demo GameMode may already have started this same subsystem.
        // Start replaces its scenario; only the actual Editor lifecycle ends it.
        const FResult Started = Runtime.Start(Scenario());
        if (!Test.TestTrue(CycleLabel(*FString::Printf(TEXT("real SDK starts: %s"), *Started.Message)), Started.IsOk())) { bAbort = true; return; }
        Runtime.SetPaused(true);
        for (uint32 Step = 0; Step < 4; ++Step)
            if (!Test.TestTrue(CycleLabel(TEXT("real SDK advances")), Runtime.SingleStep().IsOk())) { bAbort = true; return; }
        bool bPassed = Test.TestEqual(CycleLabel(TEXT("four transport ticks executed")), Runtime.GetLastStep().logical_tick, 4U);
        bPassed &= Test.TestTrue(CycleLabel(TEXT("two independent handles")), Runtime.GetPeer(0).HandleIdentity != 0 && Runtime.GetPeer(1).HandleIdentity != 0 && Runtime.GetPeer(0).HandleIdentity != Runtime.GetPeer(1).HandleIdentity);
        const FLifetimeStats During = FRuntime::GetLifetimeStats();
        bPassed &= Test.TestEqual(CycleLabel(TEXT("two sessions owned during PIE")), During.Sessions, Baseline.Sessions + 2U);
        bPassed &= Test.TestEqual(CycleLabel(TEXT("one live driver owned during PIE")), During.Drivers, Baseline.Drivers + 1U);
        bPassed &= Test.TestEqual(CycleLabel(TEXT("one SDK lease owned during PIE")), During.Libraries, Baseline.Libraries + 1U);
        bAbort |= !bPassed;
        // Deliberately no Runtime.Stop, StopAll or manual Subsystem.Deinitialize.
    }

    void BeginEnd()
    {
        Stage = EStage::WaitEnd;
        PhaseStart = FPlatformTime::Seconds();
        // CancelRequestPlaySession also clears active PlayInEditorSessionInfo.
        // EndPlayMap needs that state while Slate windows and PIE worlds close.
        if (GEditor->PlayWorld != nullptr) GEditor->RequestEndPlayMap();
        else if (GEditor->IsPlaySessionRequestQueued() && !GEditor->IsPlayingSessionInEditor()) GEditor->CancelRequestPlaySession();
    }

    void ForceEditorTeardown()
    {
        if (bOwnsSession && GEditor != nullptr)
        {
            if (GEditor->PlayWorld != nullptr) GEditor->EndPlayMap();
            else if (GEditor->IsPlaySessionRequestQueued() && !GEditor->IsPlayingSessionInEditor()) GEditor->CancelRequestPlaySession();
            bOwnsSession = false;
        }
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgePIERestart, "RollbackLab.Bridge.Lifecycle.PIERestartThreeCycles", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgePIERestart::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("PIE test requires the real Editor"), GEditor)) return false;
    if (!TestFalse(TEXT("PIE test does not replace an existing play session"), GEditor->IsPlaySessionInProgress())) return false;
    AddCommand(new FBridgePIECycles(*this));
    return true;
}
#endif
