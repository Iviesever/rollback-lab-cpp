#pragma once

#include "CoreMinimal.h"
#include "rollback_lab/c_api/rollback_lab_c.h"

namespace RollbackLabBridge
{
enum class EError : uint8
{
    None, InvalidArgument, NotRunning, WrongThread,
    MissingManifest, InvalidManifest, VersionMismatch, SourceMismatch,
    MissingLibrary, HashMismatch, MissingExport, LoadFailure, SdkFailure
};

struct FResult
{
    EError Error = EError::None;
    rl_status SdkStatus = RL_OK;
    FString Message;
    bool IsOk() const { return Error == EError::None; }
};

struct ROLLBACKLABBRIDGE_API FStartOptions
{
    FStartOptions();
    rl_live_config Scenario{};
    uint32 PeerBVariant = RL_VARIANT_CANONICAL;
    // Empty uses the plugin's staged SDK and compiled exact source identity.
    FString SdkRootOverride;
    FString ExpectedSourceGitSha;
};

struct FPeerView
{
    rl_world_snapshot Snapshot{};
    rl_metrics Metrics{};
    rl_live_correction LastCorrection{};
    uint64 CorrectionRevision = 0;
    // Observational diagnostic only. Never part of a hash, replay or scenario identity.
    UPTRINT HandleIdentity = 0;
};

struct FClockState
{
    double AccumulatorSeconds = 0.0;
    double DiscardedSeconds = 0.0;
    uint32 LastTickSteps = 0;
    bool bPaused = false;
};

struct FLifetimeStats
{
    uint32 Libraries = 0;
    uint32 Sessions = 0;
    uint32 Drivers = 0;
};

// Game-thread-affine RAII. All C resources stay private; views are cached copies.
// The live driver is destroyed before its borrowed peers and their DLL lease.
class ROLLBACKLABBRIDGE_API FRuntime final
{
public:
    static constexpr double FixedStepSeconds = 1.0 / 60.0;
    static constexpr uint32 MaximumCatchUpSteps = 8;

    FRuntime();
    ~FRuntime();
    FRuntime(const FRuntime&) = delete;
    FRuntime& operator=(const FRuntime&) = delete;

    FResult Start(const FStartOptions& Options);
    FResult Reset();
    void Stop();
    FResult TickWallClock(double DeltaSeconds, bool bOverrideLocalInput = false, uint32 Buttons = 0);
    FResult SingleStep(bool bOverrideLocalInput = false, uint32 Buttons = 0);
    void SetPaused(bool bPaused);

    bool IsRunning() const;
    bool IsFinished() const;
    const FPeerView& GetPeer(uint32 Peer) const;
    const rl_live_step_result& GetLastStep() const;
    const rl_version_info& GetVersionInfo() const;
    const FClockState& GetClockState() const;
    const FResult& GetLastResult() const;
    FResult CopyReport(FString& Output) const;
    FResult CopyTrace(FString& Output) const;
    FResult CopyReplay(TArray<uint8>& Output) const;

    static FString DefaultSdkRoot();
    static FLifetimeStats GetLifetimeStats();
    // Module pre-exit/shutdown hook; safe when called repeatedly.
    static void StopAll();

private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};
}
