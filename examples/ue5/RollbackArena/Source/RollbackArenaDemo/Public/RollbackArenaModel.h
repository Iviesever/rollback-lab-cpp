#pragma once

#include "RollbackLabRuntime.h"

namespace RollbackArena
{
enum class EMode : uint8
{
    AutoDemo,
    Interactive,
    Desync,
    UdpPeer
};

struct FSettings
{
    EMode Mode = EMode::AutoDemo;
    uint64 ScenarioSeed = UINT64_C(12648430);
    uint64 TransportSeed = UINT64_C(5351397);
    uint32 FrameCount = 240;
    uint32 NetworkPreset = 1;
    uint32 LocalPeer = RL_PEER_A;
    uint32 ListenPort = 0;
    uint32 RelayPort = 0;
    uint32 UdpVariant = RL_VARIANT_CANONICAL;
    uint32 HelloProtocol = 0;
    uint32 HelloSimulation = 0;
    uint32 HelloAbi = 0;
    uint32 HandshakeTimeoutMs = 15000;
    uint32 RunTimeoutMs = 15000;
};

// Game-thread settings/input adapter. The subsystem owns the borrowed runtime.
// This model owns no session, simulation state, transport or fixed-step clock.
class ROLLBACKARENADEMO_API FModel final
{
public:
    explicit FModel(RollbackLabBridge::FRuntime& InRuntime);
    FModel(const FModel&) = delete;
    FModel& operator=(const FModel&) = delete;

    static FSettings DefaultSettings(EMode Mode);
    static const TCHAR* ModeName(EMode Mode);
    static const TCHAR* PresetName(uint32 Preset);

    RollbackLabBridge::FResult Start(const FSettings& RequestedSettings);
    RollbackLabBridge::FResult Reset();
    RollbackLabBridge::FResult ChangePreset(uint32 Preset);
    RollbackLabBridge::FResult Tick(double DeltaSeconds, uint32 Buttons = 0);
    RollbackLabBridge::FResult Step(uint32 Buttons = 0);
    void SetPaused(bool bPaused);

    const FSettings& GetSettings() const { return Settings; }
    const RollbackLabBridge::FStartOptions& GetOptions() const { return Options; }
    RollbackLabBridge::FRuntime& Runtime() { return BorrowedRuntime; }
    const RollbackLabBridge::FRuntime& Runtime() const { return BorrowedRuntime; }

private:
    RollbackLabBridge::FRuntime& BorrowedRuntime;
    FSettings Settings;
    RollbackLabBridge::FStartOptions Options;
    bool bHasSettings = false;
};
}
