#include "BridgeTestSupport.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Windows/WindowsHWrapper.h"

#if WITH_DEV_AUTOMATION_TESTS
using namespace RollbackLabBridgeTests;
namespace
{
FStartOptions UdpOptions(uint32 Peer)
{
    FStartOptions Options;
    Options.bUdp = true;
    Options.LocalPeer = Peer;
    Options.Udp.relay_port = 9; // No matching RollbackLab relay/peer in this fixture.
    return Options;
}

class FWaitForUdpTimeout final : public IAutomationLatentCommand
{
public:
    explicit FWaitForUdpTimeout(FAutomationTestBase& InTest) : Test(InTest) {}
    virtual bool Update() override
    {
        if (!Runtime.IsValid())
        {
            Baseline = FRuntime::GetLifetimeStats();
            Runtime = MakeUnique<FRuntime>();
            FStartOptions Options = UdpOptions(RL_PEER_B);
            Options.Udp.handshake_timeout_ms = 40;
            if (!Start(Test, *Runtime, Options)) { Runtime.Reset(); return true; }
            Began = FPlatformTime::Seconds();
            return false;
        }
        const FResult Result = Runtime->SingleStep();
        Test.TestEqual(TEXT("Missing peer cannot advance canonical frame"), Runtime->GetPeer(RL_PEER_B).Snapshot.frame, 0U);
        if (Result.IsOk() && FPlatformTime::Seconds() - Began < 2.0) return false;
        Test.TestEqual(TEXT("Short handshake expires through Core"), Result.SdkStatus, RL_TIMEOUT);
        Test.TestFalse(TEXT("Failed driver is terminal"), Runtime->IsRunning());
        Test.TestEqual(TEXT("Typed phase records terminal failure"), Runtime->GetLastUdpStep().phase, RL_UDP_FAILED);
        FString Failure;
        Test.TestTrue(TEXT("Terminal status remains copyable before teardown"), Runtime->CopyFailure(Failure).IsOk());
        const uint64 Hash = Runtime->GetPeer(RL_PEER_B).Snapshot.state_hash;
        Test.TestFalse(TEXT("Terminal bridge cannot advance"), Runtime->SingleStep().IsOk());
        Test.TestEqual(TEXT("Terminal step preserves canonical state"), Runtime->GetPeer(RL_PEER_B).Snapshot.state_hash, Hash);
        Runtime->Stop(); Runtime->Stop(); Runtime.Reset();
        const auto After = FRuntime::GetLifetimeStats();
        Test.TestEqual(TEXT("Timed-out borrow released before session"), After.Sessions, Baseline.Sessions);
        Test.TestEqual(TEXT("Timed-out UDP driver released"), After.Drivers, Baseline.Drivers);
        Test.TestEqual(TEXT("Timed-out library lease released"), After.Libraries, Baseline.Libraries);
        return true;
    }
private:
    FAutomationTestBase& Test;
    TUniquePtr<FRuntime> Runtime;
    FLifetimeStats Baseline;
    double Began = 0.0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeUdpSingleSession, "RollbackLab.Bridge.Udp.SingleSessionIdentityAndBorrow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeUdpSingleSession::RunTest(const FString&)
{
    const auto Baseline = FRuntime::GetLifetimeStats();
    for (uint32 Peer : {RL_PEER_A, RL_PEER_B})
    {
        FRuntime Runtime;
        if (!Start(*this, Runtime, UdpOptions(Peer))) return false;
        TestTrue(TEXT("UDP mode is published"), Runtime.IsUdp());
        TestEqual(TEXT("Actual local peer is published"), Runtime.GetLocalPeer(), Peer);
        TestTrue(TEXT("Only the real local slot is active"), Runtime.IsPeerActive(Peer) && !Runtime.IsPeerActive(1U - Peer));
        TestTrue(TEXT("Local session is a real handle"), Runtime.GetPeer(Peer).HandleIdentity != 0);
        TestEqual(TEXT("Inactive slot has no handle"), Runtime.GetPeer(1U - Peer).HandleIdentity, static_cast<UPTRINT>(0));
        TestEqual(TEXT("Inactive slot has no fabricated snapshot"), Runtime.GetPeer(1U - Peer).Snapshot.player_count, 0U);
        TestEqual(TEXT("Exactly one SDK session per UDP client"), FRuntime::GetLifetimeStats().Sessions, Baseline.Sessions + 1U);
        TestEqual(TEXT("Exactly one borrowing driver"), FRuntime::GetLifetimeStats().Drivers, Baseline.Drivers + 1U);
        FString DllPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FRuntime::DefaultSdkRoot(), TEXT("bin/rollback_lab_c.dll")));
        FPaths::MakePlatformFilename(DllPath);
        struct FLease { HMODULE Handle = nullptr; ~FLease() { if (Handle) ::FreeLibrary(Handle); } } Lease;
        Lease.Handle = ::LoadLibraryExW(*DllPath, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!TestTrue(TEXT("Test owns an independent DLL lease"), Lease.Handle != nullptr)) return false;
        const auto Destroy = reinterpret_cast<decltype(&rl_session_destroy)>(FPlatformProcess::GetDllExport(Lease.Handle, TEXT("rl_session_destroy")));
        if (!TestTrue(TEXT("Session destroy export exists"), Destroy != nullptr)) return false;
        TestEqual(TEXT("UDP driver holds actual SDK borrow"), Destroy(reinterpret_cast<rl_session*>(Runtime.GetPeer(Peer).HandleIdentity)), RL_BORROWED);
        Runtime.Stop(); Runtime.Stop();
        TestEqual(TEXT("Stop releases session"), FRuntime::GetLifetimeStats().Sessions, Baseline.Sessions);
        TestEqual(TEXT("Stop releases driver"), FRuntime::GetLifetimeStats().Drivers, Baseline.Drivers);
        TestEqual(TEXT("Stop releases bridge DLL lease"), FRuntime::GetLifetimeStats().Libraries, Baseline.Libraries);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeUdpNoRelay, "RollbackLab.Bridge.Udp.NoRelayRemainsAtFrameZero", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeUdpNoRelay::RunTest(const FString&)
{
    FRuntime Runtime;
    if (!Start(*this, Runtime, UdpOptions(RL_PEER_B))) return false;
    const uint64 InitialHash = Runtime.GetPeer(RL_PEER_B).Snapshot.state_hash;
    FString Ready;
    TestTrue(TEXT("Actual bound endpoint is readable before the first step"), Runtime.CopyFailure(Ready).IsOk());
    TSharedPtr<FJsonObject> ReadyObject;
    if (!TestTrue(TEXT("Startup status is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Ready), ReadyObject) && ReadyObject.IsValid())) return false;
    double ReadyPort = 0, ReadyPhase = -1, ReadyStatus = -1;
    TestTrue(TEXT("Startup status exposes OS-assigned port"), ReadyObject->TryGetNumberField(TEXT("listen_port"), ReadyPort) && ReadyPort > 0 && ReadyPort <= 65535);
    TestTrue(TEXT("Startup status is healthy handshake phase zero"), ReadyObject->TryGetNumberField(TEXT("phase"), ReadyPhase) && ReadyPhase == RL_UDP_HANDSHAKE &&
        ReadyObject->TryGetNumberField(TEXT("sdk_status"), ReadyStatus) && ReadyStatus == RL_OK);
    TestEqual(TEXT("Endpoint discovery does not advance transport"), Runtime.GetLastUdpStep().logical_tick, 0U);
    TestEqual(TEXT("Endpoint discovery preserves canonical state"), Runtime.GetPeer(RL_PEER_B).Snapshot.state_hash, InitialHash);
    if (!TestTrue(TEXT("Bounded UDP pump returns without waiting"), Runtime.TickWallClock(60.0).IsOk())) return false;
    TestEqual(TEXT("UDP retains the bounded catch-up clock"), Runtime.GetClockState().LastTickSteps, FRuntime::MaximumCatchUpSteps);
    TestEqual(TEXT("No handshake means no speculative start"), Runtime.GetPeer(RL_PEER_B).Snapshot.frame, 0U);
    TestEqual(TEXT("Canonical state remains unchanged"), Runtime.GetPeer(RL_PEER_B).Snapshot.state_hash, InitialHash);
    TestEqual(TEXT("UDP remains in handshake"), Runtime.GetLastUdpStep().phase, RL_UDP_HANDSHAKE);
    TestEqual(TEXT("Handshake has not been fabricated"), Runtime.GetLastUdpStep().handshake_complete, 0U);
    FString Status;
    TestTrue(TEXT("Status copy is available during handshake"), Runtime.CopyFailure(Status).IsOk());
    TSharedPtr<FJsonObject> Object;
    if (!TestTrue(TEXT("Core status is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Status), Object) && Object.IsValid())) return false;
    double Port = 0;
    TestTrue(TEXT("Core returns a real OS-assigned port"), Object->TryGetNumberField(TEXT("listen_port"), Port) && Port > 0 && Port <= 65535);
    FString Report; TArray<uint8> Replay;
    TestFalse(TEXT("Incomplete driver cannot fabricate report"), Runtime.CopyReport(Report).IsOk());
    TestFalse(TEXT("Incomplete driver cannot fabricate replay"), Runtime.CopyReplay(Replay).IsOk());
    FRuntime::StopAll();
    TestFalse(TEXT("Pre-exit path also stops UDP"), Runtime.IsRunning());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeUdpTimeout, "RollbackLab.Bridge.Udp.ShortTimeoutAndTerminalTeardown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeUdpTimeout::RunTest(const FString&)
{
    AddCommand(new FWaitForUdpTimeout(*this));
    return true;
}
#endif
