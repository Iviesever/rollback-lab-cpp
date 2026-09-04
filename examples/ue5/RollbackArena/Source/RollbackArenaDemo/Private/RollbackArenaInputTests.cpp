#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RollbackArenaView.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
// Uses the generated map, its real GameMode, PlayerController, View and subsystem.
// Each batch is injected before the next actual PIE world tick. In particular,
// Tap sends down AND up before that tick, reproducing a physical short key press.
// No test calls LocalButtons, FModel controls, FRuntime steps or UWorld::Tick.
class FArenaPlayerInputCommands final : public IAutomationLatentCommand
{
public:
    explicit FArenaPlayerInputCommands(FAutomationTestBase& InTest)
        : Test(InTest), Baseline(RollbackLabBridge::FRuntime::GetLifetimeStats()),
          PlaySettings(NewObject<ULevelEditorPlaySettings>(GetTransientPackage()))
    {
        PlaySettings->SetPlayNetMode(PIE_Standalone);
        PlaySettings->SetRunUnderOneProcess(true);
        PlaySettings->SetPlayNumberOfClients(1);
        PlaySettings->bLaunchSeparateServer = false;
        PlaySettings->bAutoCompileBlueprintsOnLaunch = false;
        PlaySettings->NewWindowWidth = 640;
        PlaySettings->NewWindowHeight = 360;
        StartedHandle = FEditorDelegates::PostPIEStarted.AddRaw(this, &FArenaPlayerInputCommands::OnStarted);
        EndedHandle = FEditorDelegates::EndPIE.AddRaw(this, &FArenaPlayerInputCommands::OnEnded);
    }

    virtual ~FArenaPlayerInputCommands() override
    {
        FEditorDelegates::PostPIEStarted.Remove(StartedHandle);
        FEditorDelegates::EndPIE.Remove(EndedHandle);
        ForceTeardown();
    }

    virtual bool Update() override
    {
        if (GEditor == nullptr)
        {
            Test.AddError(TEXT("Arena input test lost its Editor instance."));
            return true;
        }
        if (Stage == EStage::Request)
        {
            FRequestPlaySessionParams Params;
            Params.SessionDestination = EPlaySessionDestinationType::InProcess;
            Params.WorldType = EPlaySessionWorldType::PlayInEditor;
            Params.EditorPlaySettings = PlaySettings.Get();
            Params.GlobalMapOverride = TEXT("/Game/Maps/RollbackArena");
            Params.StartLocation = FVector::ZeroVector;
            Params.bAllowOnlineSubsystem = false;
            bOwnsSession = true;
            Stage = EStage::WaitReady;
            PhaseStart = FPlatformTime::Seconds();
            GEditor->RequestPlaySession(Params);
            GEditor->StartQueuedPlaySessionRequest();
            return false;
        }
        if (Stage == EStage::WaitReady)
        {
            UWorld* World = FindPIEWorld();
            if (bStarted && World != nullptr && World->AreActorsInitialized() && World->HasBegunPlay())
            {
                APlayerController* Player = World->GetFirstPlayerController();
                ARollbackArenaView* Arena = nullptr;
                for (TActorIterator<ARollbackArenaView> It(World); It; ++It) { Arena = *It; break; }
                if (Player != nullptr && Player->PlayerInput != nullptr && Arena != nullptr && Arena->GetModel() != nullptr)
                {
                    if (!Test.TestTrue(TEXT("Generated Arena begins without a production failure"), Arena->GetFailure().IsEmpty()) ||
                        !Test.TestFalse(TEXT("Input fixture is ordinary interactive-capable PIE, not smoke"), Arena->IsSmoke()))
                    {
                        BeginEnd();
                        return false;
                    }
                    PIEWorld = World;
                    Controller = Player;
                    View = Arena;
                    Tap(EKeys::Two);
                    WaitForTick(EStage::Interactive);
                    return false;
                }
            }
            if (bEnded || FPlatformTime::Seconds() - PhaseStart > 20.0)
            {
                Test.AddError(TEXT("Arena input test did not acquire its generated PIE world/controller/model within 20 seconds."));
                BeginEnd();
            }
            return false;
        }
        if (Stage == EStage::WaitEnd) return WaitForEnd();

        if (!PIEWorld.IsValid() || !Controller.IsValid() || !View.IsValid() || View->GetModel() == nullptr || bEnded)
        {
            Test.AddError(TEXT("Arena input fixture lost its live world/controller/view during input assertions."));
            BeginEnd();
            return false;
        }
        if (PIEWorld->GetTimeSeconds() <= InputWorldTime)
        {
            if (FPlatformTime::Seconds() - PhaseStart > 5.0)
            {
                Test.AddError(TEXT("Arena input events were not followed by a real PIE world tick within 5 seconds."));
                BeginEnd();
            }
            return false;
        }
        const RollbackArena::FModel& Model = *View->GetModel();
        const RollbackLabBridge::FRuntime& Runtime = Model.Runtime();
        const rl_world_snapshot& Snapshot = Runtime.GetPeer(RL_PEER_A).Snapshot;
        if (!Test.TestTrue(TEXT("Input processing retains a healthy Arena"), View->GetFailure().IsEmpty()))
        {
            BeginEnd();
            return false;
        }

        switch (Stage)
        {
        case EStage::Interactive:
            if (!Test.TestTrue(TEXT("Actual Two key selects Interactive mode"), Model.GetSettings().Mode == RollbackArena::EMode::Interactive))
            {
                BeginEnd();
                return false;
            }
            ExpectedFrame = Snapshot.frame;
            Tap(EKeys::P);
            WaitForTick(EStage::Paused);
            break;
        case EStage::Paused:
            Test.TestTrue(TEXT("Actual P key pauses the bridge clock"), Runtime.GetClockState().bPaused);
            Test.TestEqual(TEXT("Pause key does not advance canonical frame"), Snapshot.frame, ExpectedFrame);
            PreviousX = Snapshot.players[0].x;
            Tap(EKeys::D);
            Tap(EKeys::Period);
            ExpectOneStep(EStage::QuickMove, Snapshot);
            break;
        case EStage::QuickMove:
            CheckOneStep(Runtime, Snapshot);
            Test.TestTrue(TEXT("Short D down/up before one world tick moves canonical A exactly once"), Snapshot.players[0].x > PreviousX);
            PreviousX = Snapshot.players[0].x;
            Tap(EKeys::Period);
            ExpectOneStep(EStage::AfterQuickMove, Snapshot);
            break;
        case EStage::AfterQuickMove:
            CheckOneStep(Runtime, Snapshot);
            Test.TestEqual(TEXT("Released short D is not replayed on the following period step"), Snapshot.players[0].x, PreviousX);
            Key(EKeys::D, IE_Pressed);
            Tap(EKeys::Period);
            ExpectOneStep(EStage::HeldFirst, Snapshot);
            break;
        case EStage::HeldFirst:
            CheckOneStep(Runtime, Snapshot);
            Test.TestTrue(TEXT("Held D moves A on the first period step"), Snapshot.players[0].x > PreviousX);
            PreviousX = Snapshot.players[0].x;
            Tap(EKeys::Period);
            ExpectOneStep(EStage::HeldSecond, Snapshot);
            break;
        case EStage::HeldSecond:
            CheckOneStep(Runtime, Snapshot);
            Test.TestTrue(TEXT("Held D remains active across period steps"), Snapshot.players[0].x > PreviousX);
            PreviousX = Snapshot.players[0].x;
            Key(EKeys::D, IE_Released);
            Tap(EKeys::Period);
            ExpectOneStep(EStage::Released, Snapshot);
            break;
        case EStage::Released:
            CheckOneStep(Runtime, Snapshot);
            Test.TestEqual(TEXT("Releasing held D stops canonical movement"), Snapshot.players[0].x, PreviousX);
            PreviousProjectiles = CountAProjectiles(Snapshot);
            Tap(EKeys::SpaceBar);
            Tap(EKeys::Period);
            ExpectOneStep(EStage::QuickAttack, Snapshot);
            break;
        case EStage::QuickAttack:
            CheckOneStep(Runtime, Snapshot);
            Test.TestTrue(TEXT("Short Space down/up before one world tick spawns A's canonical projectile"), CountAProjectiles(Snapshot) > PreviousProjectiles);
            PreviousProjectiles = CountAProjectiles(Snapshot);
            NeutralStepsRemaining = 16;
            Tap(EKeys::Period);
            ExpectOneStep(EStage::AfterQuickAttack, Snapshot);
            break;
        case EStage::AfterQuickAttack:
            CheckOneStep(Runtime, Snapshot);
            if (--NeutralStepsRemaining != 0)
            {
                Tap(EKeys::Period);
                ExpectOneStep(EStage::AfterQuickAttack, Snapshot);
            }
            else
            {
                // Past the core's normal attack cooldown, a mistakenly retained
                // Space press would spawn a second projectile. No Space is injected.
                Test.TestEqual(TEXT("Released short Space is consumed once, including after cooldown"), CountAProjectiles(Snapshot), PreviousProjectiles);
                Tap(EKeys::R);
                WaitForTick(EStage::Reset);
            }
            break;
        case EStage::Reset:
            Test.TestEqual(TEXT("Actual R key resets the canonical boundary"), Snapshot.frame, 0U);
            Test.TestEqual(TEXT("Actual R key resets logical ticks"), Runtime.GetLastStep().logical_tick, 0U);
            Test.TestFalse(TEXT("Actual R key resumes the fresh clock"), Runtime.GetClockState().bPaused);
            Test.TestTrue(TEXT("Reset retains Interactive mode"), Model.GetSettings().Mode == RollbackArena::EMode::Interactive);
            BeginEnd();
            break;
        default:
            Test.AddError(TEXT("Unexpected Arena input test phase."));
            BeginEnd();
            break;
        }
        return false;
    }

private:
    enum class EStage : uint8
    {
        Request, WaitReady, Interactive, Paused, QuickMove, AfterQuickMove,
        HeldFirst, HeldSecond, Released, QuickAttack, AfterQuickAttack, Reset, WaitEnd
    };
    FAutomationTestBase& Test;
    RollbackLabBridge::FLifetimeStats Baseline;
    TStrongObjectPtr<ULevelEditorPlaySettings> PlaySettings;
    TWeakObjectPtr<UWorld> PIEWorld;
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<ARollbackArenaView> View;
    FDelegateHandle StartedHandle, EndedHandle;
    EStage Stage = EStage::Request;
    double PhaseStart = 0.0;
    double InputWorldTime = 0.0;
    uint32 ExpectedFrame = 0;
    uint32 ExpectedLogicalTick = 0;
    int32 PreviousX = 0;
    uint32 PreviousProjectiles = 0;
    uint32 NeutralStepsRemaining = 0;
    bool bStarted = false, bEnded = false, bOwnsSession = false;

    void OnStarted(bool bSimulating) { bStarted = !bSimulating; }
    void OnEnded(bool) { bEnded = true; }

    UWorld* FindPIEWorld() const
    {
        if (GEngine == nullptr) return nullptr;
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
            if (Context.WorldType == EWorldType::PIE && Context.World() != nullptr) return Context.World();
        return nullptr;
    }

    void Key(const FKey& KeyName, EInputEvent Event)
    {
        Controller->InputKey(FInputKeyEventArgs::CreateSimulated(KeyName, Event, Event == IE_Released ? 0.0f : 1.0f));
    }
    void Tap(const FKey& KeyName) { Key(KeyName, IE_Pressed); Key(KeyName, IE_Released); }

    void WaitForTick(EStage Next)
    {
        Stage = Next;
        InputWorldTime = PIEWorld->GetTimeSeconds();
        PhaseStart = FPlatformTime::Seconds();
    }

    void ExpectOneStep(EStage Next, const rl_world_snapshot& Snapshot)
    {
        ExpectedFrame = Snapshot.frame + 1U;
        ExpectedLogicalTick = View->GetModel()->Runtime().GetLastStep().logical_tick + 1U;
        WaitForTick(Next);
    }

    void CheckOneStep(const RollbackLabBridge::FRuntime& Runtime, const rl_world_snapshot& Snapshot)
    {
        Test.TestTrue(TEXT("Single step leaves runtime paused"), Runtime.GetClockState().bPaused);
        Test.TestEqual(TEXT("Actual period tap advances exactly one canonical frame"), Snapshot.frame, ExpectedFrame);
        Test.TestEqual(TEXT("Actual period tap advances exactly one transport tick"), Runtime.GetLastStep().logical_tick, ExpectedLogicalTick);
    }

    static uint32 CountAProjectiles(const rl_world_snapshot& Snapshot)
    {
        uint32 Count = 0;
        for (const rl_projectile_snapshot& Projectile : Snapshot.projectiles)
            if (Projectile.active != 0 && Projectile.owner == RL_PEER_A) ++Count;
        return Count;
    }

    void BeginEnd()
    {
        Stage = EStage::WaitEnd;
        PhaseStart = FPlatformTime::Seconds();
        if (GEditor->PlayWorld != nullptr) GEditor->RequestEndPlayMap();
        else if (GEditor->IsPlaySessionRequestQueued() && !GEditor->IsPlayingSessionInEditor()) GEditor->CancelRequestPlaySession();
    }

    bool WaitForEnd()
    {
        if (GEditor->PlayWorld != nullptr && !GEditor->ShouldEndPlayMap()) GEditor->RequestEndPlayMap();
        if (!GEditor->IsPlaySessionInProgress() && FindPIEWorld() == nullptr)
        {
            bOwnsSession = false;
            Test.TestTrue(TEXT("Input fixture entered actual PIE"), bStarted);
            Test.TestTrue(TEXT("Input fixture completed EndPIE"), bEnded);
            const auto After = RollbackLabBridge::FRuntime::GetLifetimeStats();
            Test.TestEqual(TEXT("Input fixture releases SDK sessions through world teardown"), After.Sessions, Baseline.Sessions);
            Test.TestEqual(TEXT("Input fixture releases its live driver"), After.Drivers, Baseline.Drivers);
            Test.TestEqual(TEXT("Input fixture releases its DLL lease"), After.Libraries, Baseline.Libraries);
            return true;
        }
        if (FPlatformTime::Seconds() - PhaseStart > 10.0)
        {
            Test.AddError(TEXT("Arena input fixture did not finish PIE teardown within 10 seconds."));
            ForceTeardown();
            return true;
        }
        return false;
    }

    void ForceTeardown()
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaPlayerInputTest, "RollbackLab.Arena.Input.PlayerControllerShortTapHeldPauseStepReset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaPlayerInputTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("Arena input test requires the real Editor"), GEditor)) return false;
    if (!TestFalse(TEXT("Arena input test must not replace another PIE session"), GEditor->IsPlaySessionInProgress())) return false;
    AddCommand(new FArenaPlayerInputCommands(*this));
    return true;
}

#endif
