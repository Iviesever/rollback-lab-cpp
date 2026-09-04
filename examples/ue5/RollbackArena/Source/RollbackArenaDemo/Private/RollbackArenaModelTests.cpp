#include "RollbackArenaModel.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
using namespace RollbackArena;
using namespace RollbackLabBridge;

bool StartModel(FAutomationTestBase& Test, FModel& Model, const FSettings& Settings)
{
    const FResult Result = Model.Start(Settings);
    return Test.TestTrue(FString::Printf(TEXT("Model starts real SDK: %s"), *Result.Message), Result.IsOk());
}

bool FinishModel(FAutomationTestBase& Test, FModel& Model, uint32 Buttons)
{
    const uint32 Limit = Model.GetSettings().FrameCount + 512U;
    for (uint32 Tick = 0; Tick < Limit && !Model.Runtime().IsFinished(); ++Tick)
    {
        const FResult Result = Model.Step(Buttons);
        if (!Test.TestTrue(FString::Printf(TEXT("Bounded model step: %s"), *Result.Message), Result.IsOk())) return false;
    }
    return Test.TestTrue(TEXT("Actual SDK run finishes within bounded test steps"), Model.Runtime().IsFinished());
}

void CheckCommonBounds(FAutomationTestBase& Test, const rl_live_config& Config)
{
    Test.TestEqual(TEXT("ABI version"), Config.api_version, RL_API_VERSION);
    Test.TestEqual(TEXT("Exact config size"), Config.struct_size, static_cast<uint32>(sizeof(rl_live_config)));
    Test.TestEqual(TEXT("Queue packet bound"), Config.max_queue_packets, 4096U);
    Test.TestEqual(TEXT("Queue byte bound"), Config.max_queue_bytes, 4U << 20U);
    Test.TestEqual(TEXT("Bandwidth bound"), Config.bandwidth_bytes_per_tick, 1U << 20U);
    Test.TestEqual(TEXT("Packet age bound"), Config.max_packet_age_ticks, 600U);
    Test.TestEqual(TEXT("Tail redundancy"), Config.tail_redundancy_ticks, 64U);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaModelDefaults, "RollbackLab.Arena.Model.DefaultsAndModeOptions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaModelDefaults::RunTest(const FString&)
{
    const FSettings Auto = FModel::DefaultSettings(EMode::AutoDemo);
    TestEqual(TEXT("CLI default scenario seed"), Auto.ScenarioSeed, UINT64_C(12648430));
    TestEqual(TEXT("CLI default transport seed"), Auto.TransportSeed, UINT64_C(5351397));
    TestEqual(TEXT("Auto mode"), static_cast<uint8>(Auto.Mode), static_cast<uint8>(EMode::AutoDemo));
    TestEqual(TEXT("Auto frames"), Auto.FrameCount, 240U);
    TestEqual(TEXT("Default network preset"), Auto.NetworkPreset, 1U);
    const FSettings Interactive = FModel::DefaultSettings(EMode::Interactive);
    TestEqual(TEXT("Interactive mode"), static_cast<uint8>(Interactive.Mode), static_cast<uint8>(EMode::Interactive));
    TestEqual(TEXT("Bounded interactive frames"), Interactive.FrameCount, 36000U);
    const FSettings Desync = FModel::DefaultSettings(EMode::Desync);
    TestEqual(TEXT("Desync mode"), static_cast<uint8>(Desync.Mode), static_cast<uint8>(EMode::Desync));
    TestEqual(TEXT("Controlled desync seed"), Desync.ScenarioSeed, UINT64_C(1));
    TestEqual(TEXT("Controlled desync frames"), Desync.FrameCount, 240U);

    FRuntime Runtime;
    FModel Model(Runtime);
    for (const FSettings& Current : {Auto, Interactive, Desync})
    {
        if (!StartModel(*this, Model, Current)) return false;
        const FStartOptions& Options = Model.GetOptions();
        CheckCommonBounds(*this, Options.Scenario);
        TestEqual(TEXT("Configured scenario seed"), Options.Scenario.scenario_seed, Current.ScenarioSeed);
        TestEqual(TEXT("Configured transport seed"), Options.Scenario.transport_seed, Current.TransportSeed);
        TestEqual(TEXT("Configured target"), Options.Scenario.frame_count, Current.FrameCount);
        TestEqual(TEXT("Damage variant is limited to Desync mode"), Options.PeerBVariant,
            Current.Mode == EMode::Desync ? RL_VARIANT_DAMAGE_BIAS : RL_VARIANT_CANONICAL);
        TestTrue(TEXT("Runtime remains the borrowed instance"), &Model.Runtime() == &Runtime);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaModelAutoParity, "RollbackLab.Arena.Model.AutoCliParityAndCorrection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaModelAutoParity::RunTest(const FString&)
{
    FRuntime Runtime;
    FModel Model(Runtime);
    if (!StartModel(*this, Model, FModel::DefaultSettings(EMode::AutoDemo))) return false;
    TestTrue(TEXT("Peer resources are independent"), Runtime.GetPeer(0).HandleIdentity != 0 &&
        Runtime.GetPeer(0).HandleIdentity != Runtime.GetPeer(1).HandleIdentity);
    // User buttons must never replace the deterministic script in Auto mode.
    if (!FinishModel(*this, Model, RL_BUTTON_LEFT | RL_BUTTON_ATTACK)) return false;
    TestEqual(TEXT("Auto retains the CLI 240-frame golden hash"), Runtime.GetPeer(0).Snapshot.state_hash, UINT64_C(0x4B35DC3FD8F6009C));
    TestEqual(TEXT("Peer B converges independently"), Runtime.GetPeer(1).Snapshot.state_hash, Runtime.GetPeer(0).Snapshot.state_hash);
    TestEqual(TEXT("A confirmed target"), Runtime.GetPeer(0).Metrics.confirmed_frame, 240U);
    TestEqual(TEXT("B confirmed target"), Runtime.GetPeer(1).Metrics.confirmed_frame, 240U);
    TestTrue(TEXT("Auto actually rolls back"), Runtime.GetPeer(0).Metrics.rollback_count + Runtime.GetPeer(1).Metrics.rollback_count > 0);
    TestTrue(TEXT("Correction projection comes from the SDK"), Runtime.GetPeer(0).CorrectionRevision + Runtime.GetPeer(1).CorrectionRevision > 0);
    TestEqual(TEXT("Prediction is not desync"), Runtime.GetLastStep().desync_detected, 0U);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaModelInteractive, "RollbackLab.Arena.Model.InteractivePauseStepReset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaModelInteractive::RunTest(const FString&)
{
    FRuntime Runtime;
    FModel Model(Runtime);
    if (!StartModel(*this, Model, FModel::DefaultSettings(EMode::Interactive))) return false;
    const uint64 InitialHash = Runtime.GetPeer(0).Snapshot.state_hash;
    const int32 InitialX = Runtime.GetPeer(0).Snapshot.players[0].x;
    if (!TestTrue(TEXT("Interactive Tick forwards sampled right input"), Model.Tick(FRuntime::FixedStepSeconds, RL_BUTTON_RIGHT).IsOk())) return false;
    const int32 RightX = Runtime.GetPeer(0).Snapshot.players[0].x;
    TestTrue(TEXT("Sampled input moves A right"), RightX > InitialX);
    TestEqual(TEXT("One logical Tick"), Runtime.GetLastStep().logical_tick, 1U);
    Model.SetPaused(true);
    TestTrue(TEXT("Pause reaches bridge clock"), Runtime.GetClockState().bPaused);
    const uint64 PausedHash = Runtime.GetPeer(0).Snapshot.state_hash;
    if (!TestTrue(TEXT("Paused Tick succeeds"), Model.Tick(1.0, RL_BUTTON_LEFT).IsOk())) return false;
    TestEqual(TEXT("Pause prevents simulation"), Runtime.GetPeer(0).Snapshot.state_hash, PausedHash);
    TestEqual(TEXT("Pause accumulates no debt"), Runtime.GetClockState().AccumulatorSeconds, 0.0);
    if (!TestTrue(TEXT("Single step works while paused"), Model.Step(RL_BUTTON_LEFT).IsOk())) return false;
    TestEqual(TEXT("Single step increments one logical tick"), Runtime.GetLastStep().logical_tick, 2U);
    TestTrue(TEXT("Single step consumes sampled left input"), Runtime.GetPeer(0).Snapshot.players[0].x < RightX);
    TestTrue(TEXT("Single step leaves pause enabled"), Runtime.GetClockState().bPaused);
    const uint64 BeforeInvalid = Runtime.GetPeer(0).Snapshot.state_hash;
    TestFalse(TEXT("Unknown input bits are rejected by bridge"), Model.Step(0x80U).IsOk());
    TestEqual(TEXT("Bad input preserves canonical state"), Runtime.GetPeer(0).Snapshot.state_hash, BeforeInvalid);
    if (!TestTrue(TEXT("Model Reset delegates scenario recreation"), Model.Reset().IsOk())) return false;
    TestEqual(TEXT("Reset starts at frame zero"), Runtime.GetPeer(0).Snapshot.frame, 0U);
    TestEqual(TEXT("Reset restores original canonical hash"), Runtime.GetPeer(0).Snapshot.state_hash, InitialHash);
    TestEqual(TEXT("Reset clears rollback count"), Runtime.GetPeer(0).Metrics.rollback_count, 0U);
    TestEqual(TEXT("Reset clears logical ticks"), Runtime.GetLastStep().logical_tick, 0U);
    TestFalse(TEXT("Reset is unpaused"), Runtime.GetClockState().bPaused);
    TestEqual(TEXT("Reset retains interactive mode"), static_cast<uint8>(Model.GetSettings().Mode), static_cast<uint8>(EMode::Interactive));
    TestEqual(TEXT("Reset retains validated frame bound"), Model.GetSettings().FrameCount, 36000U);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaModelPresets, "RollbackLab.Arena.Model.PresetResetAndBounds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaModelPresets::RunTest(const FString&)
{
    FRuntime Runtime;
    FModel Model(Runtime);
    FSettings Settings = FModel::DefaultSettings(EMode::Interactive);
    Settings.FrameCount = 64;
    Settings.ScenarioSeed = 99;
    Settings.TransportSeed = 321;
    if (!StartModel(*this, Model, Settings)) return false;
    const uint32 Expected[3][6] = {{0, 0, 0, 0, 0, 0}, {5, 3, 5, 10, 5, 1}, {12, 6, 20, 20, 10, 3}};
    for (uint32 Preset = 0; Preset < 3; ++Preset)
    {
        if (!TestTrue(TEXT("Advance before network change"), Model.Step(RL_BUTTON_RIGHT).IsOk())) return false;
        Model.SetPaused(true);
        if (!TestTrue(TEXT("Preset change restarts bounded scenario"), Model.ChangePreset(Preset).IsOk())) return false;
        TestEqual(TEXT("Preset change resets frame"), Runtime.GetPeer(0).Snapshot.frame, 0U);
        TestEqual(TEXT("Selected preset is published"), Model.GetSettings().NetworkPreset, Preset);
        TestEqual(TEXT("Scenario seed survives preset change"), Model.GetSettings().ScenarioSeed, Settings.ScenarioSeed);
        TestEqual(TEXT("Transport seed survives preset change"), Model.GetSettings().TransportSeed, Settings.TransportSeed);
        TestEqual(TEXT("Target survives preset change"), Model.GetSettings().FrameCount, Settings.FrameCount);
        TestFalse(TEXT("Preset change restarts unpaused"), Runtime.GetClockState().bPaused);
        const rl_live_config& Config = Model.GetOptions().Scenario;
        CheckCommonBounds(*this, Config);
        TestEqual(TEXT("Preset latency"), Config.base_latency_ticks, Expected[Preset][0]);
        TestEqual(TEXT("Preset jitter"), Config.jitter_ticks, Expected[Preset][1]);
        TestEqual(TEXT("Preset loss"), Config.loss_percent, Expected[Preset][2]);
        TestEqual(TEXT("Preset reorder"), Config.reorder_percent, Expected[Preset][3]);
        TestEqual(TEXT("Preset duplicate"), Config.duplicate_percent, Expected[Preset][4]);
        TestEqual(TEXT("Preset burst loss"), Config.burst_loss_percent, Expected[Preset][5]);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaModelDesync, "RollbackLab.Arena.Model.ConfirmedDesync", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaModelDesync::RunTest(const FString&)
{
    FRuntime Runtime;
    FModel Model(Runtime);
    if (!StartModel(*this, Model, FModel::DefaultSettings(EMode::Desync))) return false;
    uint32 FirstDivergence = 0;
    for (uint32 Tick = 0; Tick < 752 && !Runtime.IsFinished(); ++Tick)
    {
        if (!TestTrue(TEXT("Controlled desync step succeeds"), Model.Step(RL_BUTTON_RIGHT).IsOk())) return false;
        const rl_live_step_result& Step = Runtime.GetLastStep();
        if (Step.desync_detected != 0)
        {
            TestTrue(TEXT("Only a confirmed boundary is divergent"), Step.earliest_divergent_frame > 0 &&
                Step.earliest_divergent_frame <= Runtime.GetPeer(0).Metrics.confirmed_frame &&
                Step.earliest_divergent_frame <= Runtime.GetPeer(1).Metrics.confirmed_frame);
            if (FirstDivergence == 0) FirstDivergence = Step.earliest_divergent_frame;
            else TestTrue(TEXT("Earliest divergence never moves later"), Step.earliest_divergent_frame <= FirstDivergence);
            FirstDivergence = Step.earliest_divergent_frame;
        }
    }
    TestTrue(TEXT("Desync demo finishes"), Runtime.IsFinished());
    TestTrue(TEXT("Damage-bias run demonstrates an actual confirmed desync"), FirstDivergence > 0);
    TestEqual(TEXT("Desync mode confirms A target"), Runtime.GetPeer(0).Metrics.confirmed_frame, 240U);
    TestEqual(TEXT("Desync mode confirms B target"), Runtime.GetPeer(1).Metrics.confirmed_frame, 240U);
    TestTrue(TEXT("Controlled variants retain unequal independent hashes"), Runtime.GetPeer(0).Snapshot.state_hash != Runtime.GetPeer(1).Snapshot.state_hash);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaModelValidation, "RollbackLab.Arena.Model.InvalidSettingsPreserveRun", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaModelValidation::RunTest(const FString&)
{
    FRuntime Runtime;
    FModel Model(Runtime);
    TestFalse(TEXT("Reset before Start fails"), Model.Reset().IsOk());
    TestFalse(TEXT("Preset change before Start fails"), Model.ChangePreset(0).IsOk());
    TestFalse(TEXT("Tick before Start fails"), Model.Tick(FRuntime::FixedStepSeconds).IsOk());
    TestFalse(TEXT("Step before Start fails"), Model.Step().IsOk());
    const FSettings Valid = FModel::DefaultSettings(EMode::AutoDemo);
    FSettings Invalid = Valid;
    Invalid.FrameCount = 0;
    FResult Result = Model.Start(Invalid);
    TestTrue(TEXT("Zero target is typed invalid argument"), Result.Error == EError::InvalidArgument && Result.SdkStatus == RL_INVALID_ARGUMENT);
    TestFalse(TEXT("Invalid startup creates no fake world"), Runtime.IsRunning());
    TestEqual(TEXT("Invalid startup creates no peer handle"), Runtime.GetPeer(0).HandleIdentity, static_cast<UPTRINT>(0));
    if (!StartModel(*this, Model, Valid)) return false;
    if (!TestTrue(TEXT("Advance valid run before rejected settings"), Model.Step().IsOk())) return false;
    const uint64 Before = Runtime.GetPeer(0).Snapshot.state_hash;
    const UPTRINT HandleBefore = Runtime.GetPeer(0).HandleIdentity;
    Invalid = Valid; Invalid.Mode = static_cast<EMode>(255);
    TestFalse(TEXT("Unknown mode rejected"), Model.Start(Invalid).IsOk());
    Invalid = Valid; Invalid.FrameCount = 36001;
    TestFalse(TEXT("Unbounded target rejected"), Model.Start(Invalid).IsOk());
    Invalid = Valid; Invalid.NetworkPreset = 3;
    TestFalse(TEXT("Unknown preset rejected"), Model.Start(Invalid).IsOk());
    TestFalse(TEXT("Invalid preset change rejected"), Model.ChangePreset(3).IsOk());
    TestEqual(TEXT("Rejected settings preserve existing state"), Runtime.GetPeer(0).Snapshot.state_hash, Before);
    TestEqual(TEXT("Rejected settings preserve existing resource"), Runtime.GetPeer(0).HandleIdentity, HandleBefore);
    TestEqual(TEXT("Rejected settings preserve published target"), Model.GetSettings().FrameCount, Valid.FrameCount);
    TestEqual(TEXT("Rejected settings preserve published preset"), Model.GetSettings().NetworkPreset, Valid.NetworkPreset);
    TestEqual(TEXT("Rejected settings preserve published options"), Model.GetOptions().Scenario.frame_count, Valid.FrameCount);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaModelBorrowing, "RollbackLab.Arena.Model.BorrowedRuntimeLifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArenaModelBorrowing::RunTest(const FString&)
{
    FRuntime Runtime;
    {
        FModel Model(Runtime);
        if (!StartModel(*this, Model, FModel::DefaultSettings(EMode::AutoDemo))) return false;
    }
    TestTrue(TEXT("Destroying model does not destroy subsystem-owned runtime"), Runtime.IsRunning());
    TestTrue(TEXT("Borrowed runtime remains operable"), Runtime.SingleStep().IsOk());
    Runtime.Stop();
    TestFalse(TEXT("Owner explicitly stops runtime"), Runtime.IsRunning());
    return true;
}
#endif
