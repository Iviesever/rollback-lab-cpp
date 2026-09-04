#include "RollbackLabRuntime.h"
#include "RollbackLabSdk.h"
#include "HAL/PlatformTLS.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include <cmath>
#include <limits>

namespace RollbackLabBridge
{
using namespace Private;

namespace
{
TSet<FRuntime*>& Runtimes()
{
    static TSet<FRuntime*> Instances;
    return Instances;
}

struct FSessionOwner final
{
    TSharedPtr<FSdk> Library;
    rl_session* Handle = nullptr;
    ~FSessionOwner()
    {
        if (Handle != nullptr)
        {
            const rl_status Status = Library->Api.rl_session_destroy(Handle);
            checkf(Status == RL_OK, TEXT("RollbackLab session destruction failed: %u"), Status);
            --SessionCount;
        }
    }
};

struct FDriverOwner final
{
    TSharedPtr<FSdk> Library;
    rl_live* Handle = nullptr;
    ~FDriverOwner()
    {
        if (Handle != nullptr)
        {
            const rl_status Status = Library->Api.rl_live_destroy(Handle);
            checkf(Status == RL_OK, TEXT("RollbackLab live driver destruction failed: %u"), Status);
            --DriverCount;
        }
    }
};

struct FUdpOwner final
{
    TSharedPtr<FSdk> Library;
    rl_udp_peer* Handle = nullptr;
    ~FUdpOwner()
    {
        if (Handle != nullptr)
        {
            const rl_status Status = Library->Api.rl_udp_peer_destroy(Handle);
            checkf(Status == RL_OK, TEXT("RollbackLab UDP driver destruction failed: %u"), Status);
            --DriverCount;
        }
    }
};

struct FRunResources final
{
    TSharedPtr<FSdk> Library;
    FSessionOwner Peers[2];
    FDriverOwner Driver; // Drivers are destroyed before Peers, then Library.
    FUdpOwner Udp;
};

constexpr uint32 MaximumArtifactBytes = 64U * 1024U * 1024U;

template <typename DriverType, typename CopyFunction>
FResult CopyString(DriverType* Driver, CopyFunction Copy, FString& Output, const TCHAR* Operation)
{
    uint32 Required = 0;
    rl_status Status = Copy(Driver, nullptr, 0, &Required);
    if (Status != RL_BUFFER_TOO_SMALL) return SdkResult(Status == RL_OK ? RL_INTERNAL_FAILURE : Status, Operation);
    if (Required == 0 || Required > MaximumArtifactBytes)
        return Failure(EError::SdkFailure, TEXT("SDK text artifact exceeds its bounded buffer contract."), RL_CAPACITY);
    TArray<char> Bytes;
    Bytes.SetNumUninitialized(static_cast<int32>(Required));
    const uint32 Capacity = Required;
    Status = Copy(Driver, Bytes.GetData(), Capacity, &Required);
    if (Status != RL_OK) return SdkResult(Status, Operation);
    if (Required == 0 || Required > Capacity || Bytes[Required - 1] != '\0')
        return Failure(EError::SdkFailure, TEXT("SDK text artifact is not a bounded NUL-terminated string."), RL_INTERNAL_FAILURE);
    const FUTF8ToTCHAR Converted(Bytes.GetData(), static_cast<int32>(Required - 1));
    Output = FString(Converted.Length(), Converted.Get());
    return {};
}
}

FStartOptions::FStartOptions()
{
    Scenario.api_version = RL_API_VERSION;
    Scenario.struct_size = sizeof(Scenario);
    Scenario.scenario_seed = 1;
    Scenario.transport_seed = 2;
    Scenario.frame_count = 600;
    Scenario.base_latency_ticks = 5;
    Scenario.jitter_ticks = 3;
    Scenario.loss_percent = 5;
    Scenario.reorder_percent = 10;
    Scenario.duplicate_percent = 5;
    Scenario.max_queue_packets = 4096;
    Scenario.max_queue_bytes = 4U << 20U;
    Scenario.bandwidth_bytes_per_tick = 1U << 20U;
    Scenario.max_packet_age_ticks = 600;
    Scenario.tail_redundancy_ticks = 64;
    Udp.api_version = RL_API_VERSION;
    Udp.struct_size = sizeof(Udp);
    Udp.scenario_seed = UINT64_C(12648430);
    Udp.transport_seed = UINT64_C(0x55445030);
    Udp.frame_count = 240;
    Udp.handshake_timeout_ms = 15000;
    Udp.run_timeout_ms = 15000;
}

struct FRuntime::FImpl
{
    uint32 OwnerThread = FPlatformTLS::GetCurrentThreadId();
    TUniquePtr<FRunResources> Resources;
    FPeerView Peers[2];
    rl_live_step_result LastStep = Initialized<rl_live_step_result>();
    rl_udp_step_result LastUdpStep = Initialized<rl_udp_step_result>();
    double UdpCreatedAt = 0.0;
    rl_version_info Version = Initialized<rl_version_info>();
    FClockState Clock;
    FResult LastResult;
    FStartOptions LastOptions;
    bool bHasOptions = false;
    bool bFailed = false;

    bool OnOwnerThread() const { return IsInGameThread() && OwnerThread == FPlatformTLS::GetCurrentThreadId(); }
    bool Running() const { return Resources.IsValid() && !bFailed && LastStep.finished == 0; }

    FResult CheckAccess(bool bRequireRunning) const
    {
        if (!OnOwnerThread()) return Failure(EError::WrongThread, TEXT("RollbackLab runtime calls must remain on their creating game thread."), RL_WRONG_THREAD);
        if (!Resources.IsValid() || (bRequireRunning && !Running())) return Failure(EError::NotRunning, TEXT("RollbackLab runtime is not running."));
        return {};
    }

    FResult RefreshViews(bool bReadCorrections)
    {
        FPeerView Updated[2] = {Peers[0], Peers[1]};
        for (uint32 Peer = 0; Peer < 2; ++Peer)
        {
            if (Resources->Peers[Peer].Handle == nullptr) { Updated[Peer] = {}; continue; }
            Updated[Peer].Snapshot = Initialized<rl_world_snapshot>();
            Updated[Peer].Metrics = Initialized<rl_metrics>();
            rl_status Status = Resources->Library->Api.rl_session_get_snapshot(Resources->Peers[Peer].Handle, &Updated[Peer].Snapshot);
            if (Status != RL_OK) return SdkResult(Status, TEXT("rl_session_get_snapshot"));
            Status = Resources->Library->Api.rl_session_get_metrics(Resources->Peers[Peer].Handle, &Updated[Peer].Metrics);
            if (Status != RL_OK) return SdkResult(Status, TEXT("rl_session_get_metrics"));
            if (Updated[Peer].Snapshot.api_version != RL_API_VERSION || Updated[Peer].Snapshot.struct_size != sizeof(rl_world_snapshot) ||
                Updated[Peer].Snapshot.player_count != 2 || Updated[Peer].Snapshot.projectile_capacity != 64 ||
                Updated[Peer].Metrics.api_version != RL_API_VERSION || Updated[Peer].Metrics.struct_size != sizeof(rl_metrics))
                return Failure(EError::VersionMismatch, TEXT("SDK presentation snapshot/metrics layout differs from ABI v1."));
            Updated[Peer].HandleIdentity = reinterpret_cast<UPTRINT>(Resources->Peers[Peer].Handle);
            if (bReadCorrections)
            {
                rl_live_correction Correction = Initialized<rl_live_correction>();
                Status = Resources->Udp.Handle != nullptr
                    ? Resources->Library->Api.rl_udp_peer_get_correction(Resources->Udp.Handle, &Correction)
                    : Resources->Library->Api.rl_live_get_correction(Resources->Driver.Handle, Peer, &Correction);
                if (Status != RL_OK) return SdkResult(Status, TEXT("rl_live_get_correction"));
                if (Correction.performed != 0)
                {
                    Updated[Peer].LastCorrection = Correction;
                    ++Updated[Peer].CorrectionRevision;
                }
            }
        }
        Peers[0] = Updated[0];
        Peers[1] = Updated[1];
        return {};
    }

    FResult AdvanceOne(bool bOverrideLocalInput, uint32 Buttons)
    {
        if (Resources->Udp.Handle != nullptr)
        {
            rl_udp_step_result Step = Initialized<rl_udp_step_result>();
            // Real monotonic elapsed time is a transport deadline, never a
            // canonical simulation input or fabricated accumulated frame time.
            const uint64 ElapsedMs = static_cast<uint64>(FMath::Max(0.0, FPlatformTime::Seconds() - UdpCreatedAt) * 1000.0);
            const rl_status Status = Resources->Library->Api.rl_udp_peer_step(Resources->Udp.Handle, ElapsedMs, &Step);
            LastUdpStep = Step;
            LastStep.logical_tick = Step.logical_tick;
            LastStep.finished = Step.finished;
            LastStep.desync_detected = Step.desync_detected;
            LastStep.earliest_divergent_frame = Step.earliest_divergent_frame;
            const FResult Refreshed = RefreshViews(true);
            if (Status != RL_OK || !Refreshed.IsOk()) bFailed = true;
            // Keep failed resources alive until evidence copies precede Stop.
            return Status != RL_OK ? SdkResult(Status, TEXT("rl_udp_peer_step")) : Refreshed;
        }
        rl_live_step_result Step = Initialized<rl_live_step_result>();
        const rl_status Status = Resources->Library->Api.rl_live_step(Resources->Driver.Handle, bOverrideLocalInput ? 1U : 0U, Buttons, &Step);
        if (Status != RL_OK)
        {
            // Retain resources so a failure trace can still be copied before Stop.
            bFailed = true;
            return SdkResult(Status, TEXT("rl_live_step"));
        }
        LastStep = Step;
        FResult Result = RefreshViews(true);
        if (!Result.IsOk()) bFailed = true;
        return Result;
    }
};

FRuntime::FRuntime() : Impl(MakeUnique<FImpl>())
{
    check(IsInGameThread());
    Runtimes().Add(this);
}

FRuntime::~FRuntime()
{
    check(Impl->OnOwnerThread());
    Stop();
    Runtimes().Remove(this);
}

FResult FRuntime::Start(const FStartOptions& Options)
{
    if (!Impl->OnOwnerThread()) return Failure(EError::WrongThread, TEXT("RollbackLab Start requires its creating game thread."), RL_WRONG_THREAD);
    Stop();
    Impl->Peers[0] = {};
    Impl->Peers[1] = {};
    Impl->LastStep = Initialized<rl_live_step_result>();
    Impl->LastUdpStep = Initialized<rl_udp_step_result>();
    Impl->Version = Initialized<rl_version_info>();
    Impl->Clock = {};
    Impl->bFailed = false;

    if (Options.bUdp && (Options.LocalPeer > RL_PEER_B || Options.UdpVariant > RL_VARIANT_DAMAGE_BIAS))
        return Impl->LastResult = Failure(EError::InvalidArgument, TEXT("UDP local peer or simulation variant is invalid."), RL_INVALID_ARGUMENT);
    TUniquePtr<FRunResources> Created = MakeUnique<FRunResources>();
    FResult Result = LoadSdk(Options, Created->Library);
    if (!Result.IsOk())
    {
        UE_LOG(LogRollbackLabBridge, Display, TEXT("SDK startup rejected (%u): %s"), static_cast<uint32>(Result.Error), *Result.Message);
        return Impl->LastResult = Result;
    }
    for (uint32 Peer = 0; Peer < 2; ++Peer)
    {
        if (Options.bUdp && Peer != Options.LocalPeer) continue;
        Created->Peers[Peer].Library = Created->Library;
        rl_session_config Config = Initialized<rl_session_config>();
        Config.local_peer = Peer;
        Config.max_rollback_frames = 120;
        Config.simulation_variant = Options.bUdp ? Options.UdpVariant
            : (Peer == RL_PEER_B ? Options.PeerBVariant : RL_VARIANT_CANONICAL);
        const rl_status Status = Created->Library->Api.rl_session_create(&Config, &Created->Peers[Peer].Handle);
        if (Created->Peers[Peer].Handle != nullptr) ++SessionCount;
        if (Status != RL_OK) return Impl->LastResult = SdkResult(Status, TEXT("rl_session_create"));
        if (Created->Peers[Peer].Handle == nullptr)
            return Impl->LastResult = Failure(EError::SdkFailure, TEXT("SDK returned success without a session handle."), RL_INTERNAL_FAILURE);
    }
    if (Options.bUdp)
    {
        Created->Udp.Library = Created->Library;
        Impl->UdpCreatedAt = FPlatformTime::Seconds();
        const rl_status Status = Created->Library->Api.rl_udp_peer_create(&Options.Udp, Created->Peers[Options.LocalPeer].Handle, &Created->Udp.Handle);
        if (Created->Udp.Handle != nullptr) ++DriverCount;
        if (Status != RL_OK) return Impl->LastResult = SdkResult(Status, TEXT("rl_udp_peer_create"));
        if (Created->Udp.Handle == nullptr)
            return Impl->LastResult = Failure(EError::SdkFailure, TEXT("SDK returned success without a UDP driver."), RL_INTERNAL_FAILURE);
    }
    else
    {
        if (Created->Peers[0].Handle == Created->Peers[1].Handle)
            return Impl->LastResult = Failure(EError::SdkFailure, TEXT("SDK returned aliased peer handles."), RL_INTERNAL_FAILURE);
        Created->Driver.Library = Created->Library;
        const rl_status Status = Created->Library->Api.rl_live_create(&Options.Scenario, Created->Peers[0].Handle, Created->Peers[1].Handle, &Created->Driver.Handle);
        if (Created->Driver.Handle != nullptr) ++DriverCount;
        if (Status != RL_OK) return Impl->LastResult = SdkResult(Status, TEXT("rl_live_create"));
        if (Created->Driver.Handle == nullptr)
            return Impl->LastResult = Failure(EError::SdkFailure, TEXT("SDK returned success without a live driver."), RL_INTERNAL_FAILURE);
    }
    Impl->Version = Created->Library->Version;
    Impl->Resources = MoveTemp(Created);
    Result = Impl->RefreshViews(false);
    if (!Result.IsOk())
    {
        Stop();
        return Impl->LastResult = Result;
    }
    Impl->LastOptions = Options;
    Impl->bHasOptions = true;
    return Impl->LastResult = FResult{};
}

FResult FRuntime::Reset()
{
    if (!Impl->OnOwnerThread()) return Failure(EError::WrongThread, TEXT("RollbackLab Reset requires its creating game thread."), RL_WRONG_THREAD);
    if (!Impl->bHasOptions) return Impl->LastResult = Failure(EError::NotRunning, TEXT("RollbackLab has no validated scenario to reset."));
    const FStartOptions Options = Impl->LastOptions;
    return Start(Options);
}

void FRuntime::Stop()
{
    checkf(Impl->OnOwnerThread(), TEXT("RollbackLab Stop/destruction requires its creating game thread."));
    Impl->Resources.Reset();
    Impl->Peers[0].HandleIdentity = 0;
    Impl->Peers[1].HandleIdentity = 0;
    Impl->Clock.LastTickSteps = 0;
}

FResult FRuntime::TickWallClock(double DeltaSeconds, bool bOverrideLocalInput, uint32 Buttons)
{
    const FResult Access = Impl->CheckAccess(true);
    if (!Access.IsOk()) return Access.Error == EError::WrongThread ? Access : (Impl->LastResult = Access);
    if (!std::isfinite(DeltaSeconds) || DeltaSeconds < 0.0 || Buttons > 31U)
        return Impl->LastResult = Failure(EError::InvalidArgument, TEXT("Wall delta must be finite and nonnegative; input buttons must use the five ABI bits."), RL_INVALID_ARGUMENT);
    Impl->Clock.LastTickSteps = 0;
    if (Impl->Clock.bPaused) return Impl->LastResult = FResult{};
    const double Total = Impl->Clock.AccumulatorSeconds + DeltaSeconds;
    if (!std::isfinite(Total))
        return Impl->LastResult = Failure(EError::InvalidArgument, TEXT("Wall-time accumulator overflow."), RL_INVALID_ARGUMENT);
    const uint32 Steps = Total >= FixedStepSeconds * MaximumCatchUpSteps
        ? MaximumCatchUpSteps : static_cast<uint32>(std::floor(Total / FixedStepSeconds));
    const double Debt = Total - static_cast<double>(Steps) * FixedStepSeconds;
    const double Remainder = Debt >= FixedStepSeconds ? std::fmod(Debt, FixedStepSeconds) : Debt;
    const double Discarded = FMath::Max(0.0, Debt - Remainder);
    const double NewDiscarded = Impl->Clock.DiscardedSeconds + Discarded;
    Impl->Clock.DiscardedSeconds = std::isfinite(NewDiscarded) ? NewDiscarded : std::numeric_limits<double>::max();
    Impl->Clock.AccumulatorSeconds = FMath::Max(0.0, Remainder);
    for (uint32 Index = 0; Index < Steps && Impl->Running(); ++Index)
    {
        FResult Result = Impl->AdvanceOne(bOverrideLocalInput, Buttons);
        if (!Result.IsOk()) return Impl->LastResult = Result;
        ++Impl->Clock.LastTickSteps;
    }
    return Impl->LastResult = FResult{};
}

FResult FRuntime::SingleStep(bool bOverrideLocalInput, uint32 Buttons)
{
    const FResult Access = Impl->CheckAccess(true);
    if (!Access.IsOk()) return Access.Error == EError::WrongThread ? Access : (Impl->LastResult = Access);
    if (Buttons > 31U) return Impl->LastResult = Failure(EError::InvalidArgument, TEXT("Input buttons must use the five ABI bits."), RL_INVALID_ARGUMENT);
    Impl->Clock.LastTickSteps = 0;
    FResult Result = Impl->AdvanceOne(bOverrideLocalInput, Buttons);
    if (Result.IsOk()) Impl->Clock.LastTickSteps = 1;
    return Impl->LastResult = Result;
}

void FRuntime::SetPaused(bool bPaused)
{
    check(Impl->OnOwnerThread());
    Impl->Clock.bPaused = bPaused;
}
bool FRuntime::IsRunning() const { check(Impl->OnOwnerThread()); return Impl->Running(); }
bool FRuntime::IsFinished() const { check(Impl->OnOwnerThread()); return Impl->LastStep.finished != 0; }
bool FRuntime::IsUdp() const { check(Impl->OnOwnerThread()); return Impl->Resources.IsValid() && Impl->Resources->Udp.Handle != nullptr; }
bool FRuntime::IsPeerActive(uint32 Peer) const { check(Impl->OnOwnerThread() && Peer < 2); return Impl->Peers[Peer].HandleIdentity != 0; }
uint32 FRuntime::GetLocalPeer() const { check(Impl->OnOwnerThread()); return Impl->LastOptions.LocalPeer; }
const rl_udp_step_result& FRuntime::GetLastUdpStep() const { check(Impl->OnOwnerThread()); return Impl->LastUdpStep; }
const FPeerView& FRuntime::GetPeer(uint32 Peer) const { check(Impl->OnOwnerThread() && Peer < 2); return Impl->Peers[Peer]; }
const rl_live_step_result& FRuntime::GetLastStep() const { check(Impl->OnOwnerThread()); return Impl->LastStep; }
const rl_version_info& FRuntime::GetVersionInfo() const { check(Impl->OnOwnerThread()); return Impl->Version; }
const FClockState& FRuntime::GetClockState() const { check(Impl->OnOwnerThread()); return Impl->Clock; }
const FResult& FRuntime::GetLastResult() const { check(Impl->OnOwnerThread()); return Impl->LastResult; }

FResult FRuntime::CopyReport(FString& Output) const
{
    const FResult Access = Impl->CheckAccess(false);
    if (!Access.IsOk()) return Access;
    if (Impl->Resources->Udp.Handle != nullptr)
        return CopyString(Impl->Resources->Udp.Handle, Impl->Resources->Library->Api.rl_udp_peer_copy_report, Output, TEXT("rl_udp_peer_copy_report"));
    return CopyString(Impl->Resources->Driver.Handle, Impl->Resources->Library->Api.rl_live_copy_report, Output, TEXT("rl_live_copy_report"));
}

FResult FRuntime::CopyTrace(FString& Output) const
{
    const FResult Access = Impl->CheckAccess(false);
    if (!Access.IsOk()) return Access;
    if (Impl->Resources->Udp.Handle != nullptr) return CopyFailure(Output);
    return CopyString(Impl->Resources->Driver.Handle, Impl->Resources->Library->Api.rl_live_copy_trace, Output, TEXT("rl_live_copy_trace"));
}

FResult FRuntime::CopyFailure(FString& Output) const
{
    const FResult Access = Impl->CheckAccess(false);
    if (!Access.IsOk()) return Access;
    if (Impl->Resources->Udp.Handle == nullptr)
        return Failure(EError::InvalidArgument, TEXT("UDP status requires a UDP run."), RL_INVALID_ARGUMENT);
    return CopyString(Impl->Resources->Udp.Handle, Impl->Resources->Library->Api.rl_udp_peer_copy_failure, Output, TEXT("rl_udp_peer_copy_failure"));
}

FResult FRuntime::CopyReplay(TArray<uint8>& Output) const
{
    const FResult Access = Impl->CheckAccess(false);
    if (!Access.IsOk()) return Access;
    uint32 Required = 0;
    const TCHAR* Operation = Impl->Resources->Udp.Handle != nullptr ? TEXT("rl_udp_peer_copy_replay") : TEXT("rl_live_copy_replay");
    const auto Copy = [this](uint8* Buffer, uint32 Capacity, uint32* Size)
    {
        const auto& Resources = *Impl->Resources;
        return Resources.Udp.Handle != nullptr
            ? Resources.Library->Api.rl_udp_peer_copy_replay(Resources.Udp.Handle, Buffer, Capacity, Size)
            : Resources.Library->Api.rl_live_copy_replay(Resources.Driver.Handle, Buffer, Capacity, Size);
    };
    rl_status Status = Copy(nullptr, 0, &Required);
    if (Status != RL_BUFFER_TOO_SMALL) return SdkResult(Status == RL_OK ? RL_INTERNAL_FAILURE : Status, Operation);
    if (Required == 0 || Required > MaximumArtifactBytes)
        return Failure(EError::SdkFailure, TEXT("SDK replay exceeds its bounded buffer contract."), RL_CAPACITY);
    TArray<uint8> Bytes;
    Bytes.SetNumUninitialized(static_cast<int32>(Required));
    const uint32 Capacity = Required;
    Status = Copy(Bytes.GetData(), Capacity, &Required);
    if (Status != RL_OK) return SdkResult(Status, Operation);
    if (Required == 0 || Required > Capacity)
        return Failure(EError::SdkFailure, TEXT("SDK replay returned an invalid byte count."), RL_INTERNAL_FAILURE);
    Bytes.SetNum(static_cast<int32>(Required), EAllowShrinking::No);
    Output = MoveTemp(Bytes);
    return {};
}

FLifetimeStats FRuntime::GetLifetimeStats()
{
    return {LibraryCount.load(), SessionCount.load(), DriverCount.load()};
}

void FRuntime::StopAll()
{
    check(IsInGameThread());
    for (FRuntime* Runtime : Runtimes()) Runtime->Stop();
}

FString FRuntime::DefaultSdkRoot()
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("RollbackLabBridge"));
    return Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Binaries/ThirdParty/RollbackLab")) : FString();
}
}
