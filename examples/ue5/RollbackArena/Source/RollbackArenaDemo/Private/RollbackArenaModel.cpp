#include "RollbackArenaModel.h"

namespace RollbackArena
{
namespace
{
RollbackLabBridge::FResult InvalidSettings(const TCHAR* Message)
{
    return {RollbackLabBridge::EError::InvalidArgument, RL_INVALID_ARGUMENT, Message};
}

RollbackLabBridge::FResult NotStarted()
{
    return {RollbackLabBridge::EError::NotRunning, RL_INVALID_FRAME,
        TEXT("Start a validated arena scenario before controlling it.")};
}

RollbackLabBridge::FStartOptions MakeOptions(const FSettings& Settings)
{
    RollbackLabBridge::FStartOptions Options;
    rl_live_config& Scenario = Options.Scenario;
    Scenario.scenario_seed = Settings.ScenarioSeed;
    Scenario.transport_seed = Settings.TransportSeed;
    Scenario.frame_count = Settings.FrameCount;
    Scenario.max_queue_packets = 4096;
    Scenario.max_queue_bytes = 4U << 20U;
    Scenario.bandwidth_bytes_per_tick = 1U << 20U;
    Scenario.max_packet_age_ticks = 600;
    Scenario.tail_redundancy_ticks = 64;
    switch (Settings.NetworkPreset)
    {
    case 0:
        Scenario.base_latency_ticks = 0;
        Scenario.jitter_ticks = 0;
        Scenario.loss_percent = 0;
        Scenario.reorder_percent = 0;
        Scenario.duplicate_percent = 0;
        Scenario.burst_loss_percent = 0;
        break;
    case 1:
        Scenario.base_latency_ticks = 5;
        Scenario.jitter_ticks = 3;
        Scenario.loss_percent = 5;
        Scenario.reorder_percent = 10;
        Scenario.duplicate_percent = 5;
        Scenario.burst_loss_percent = 1;
        break;
    case 2:
        Scenario.base_latency_ticks = 12;
        Scenario.jitter_ticks = 6;
        Scenario.loss_percent = 20;
        Scenario.reorder_percent = 20;
        Scenario.duplicate_percent = 10;
        Scenario.burst_loss_percent = 3;
        break;
    }
    Options.PeerBVariant = Settings.Mode == EMode::Desync
        ? RL_VARIANT_DAMAGE_BIAS : RL_VARIANT_CANONICAL;
    return Options;
}
}

FModel::FModel(RollbackLabBridge::FRuntime& InRuntime)
    : BorrowedRuntime(InRuntime), Options(MakeOptions(Settings)) {}

FSettings FModel::DefaultSettings(EMode Mode)
{
    FSettings Result;
    Result.Mode = Mode;
    if (Mode == EMode::Interactive) Result.FrameCount = 36000;
    if (Mode == EMode::Desync) Result.ScenarioSeed = 1;
    return Result;
}

const TCHAR* FModel::ModeName(EMode Mode)
{
    switch (Mode)
    {
    case EMode::AutoDemo: return TEXT("Auto demo");
    case EMode::Interactive: return TEXT("Interactive");
    case EMode::Desync: return TEXT("Desync");
    }
    return TEXT("Unknown");
}

const TCHAR* FModel::PresetName(uint32 Preset)
{
    switch (Preset)
    {
    case 0: return TEXT("Local");
    case 1: return TEXT("Default");
    case 2: return TEXT("Hostile");
    }
    return TEXT("Unknown");
}

RollbackLabBridge::FResult FModel::Start(const FSettings& RequestedSettings)
{
    if (RequestedSettings.Mode != EMode::AutoDemo && RequestedSettings.Mode != EMode::Interactive &&
        RequestedSettings.Mode != EMode::Desync)
        return InvalidSettings(TEXT("Arena mode is not supported."));
    if (RequestedSettings.FrameCount == 0 || RequestedSettings.FrameCount > 36000)
        return InvalidSettings(TEXT("Arena frame count must be between 1 and 36000."));
    if (RequestedSettings.NetworkPreset > 2)
        return InvalidSettings(TEXT("Arena network preset must be Local, Default or Hostile."));

    RollbackLabBridge::FStartOptions Candidate = MakeOptions(RequestedSettings);
    const RollbackLabBridge::FResult Result = BorrowedRuntime.Start(Candidate);
    if (Result.IsOk())
    {
        Settings = RequestedSettings;
        Options = MoveTemp(Candidate);
        bHasSettings = true;
    }
    return Result;
}

RollbackLabBridge::FResult FModel::Reset()
{
    if (!bHasSettings) return NotStarted();
    return Start(Settings);
}

RollbackLabBridge::FResult FModel::ChangePreset(uint32 Preset)
{
    if (Preset > 2) return InvalidSettings(TEXT("Arena network preset must be Local, Default or Hostile."));
    if (!bHasSettings) return NotStarted();
    FSettings Next = Settings;
    Next.NetworkPreset = Preset;
    return Start(Next);
}

RollbackLabBridge::FResult FModel::Tick(double DeltaSeconds, uint32 Buttons)
{
    if (!bHasSettings) return NotStarted();
    const bool bInteractive = Settings.Mode == EMode::Interactive;
    return BorrowedRuntime.TickWallClock(DeltaSeconds, bInteractive, bInteractive ? Buttons : 0U);
}

RollbackLabBridge::FResult FModel::Step(uint32 Buttons)
{
    if (!bHasSettings) return NotStarted();
    const bool bInteractive = Settings.Mode == EMode::Interactive;
    return BorrowedRuntime.SingleStep(bInteractive, bInteractive ? Buttons : 0U);
}

void FModel::SetPaused(bool bPaused)
{
    BorrowedRuntime.SetPaused(bPaused);
}
}
