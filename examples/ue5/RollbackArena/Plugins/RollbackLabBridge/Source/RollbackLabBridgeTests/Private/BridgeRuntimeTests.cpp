#include "BridgeTestSupport.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
using namespace RollbackLabBridgeTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeRealPeers, "RollbackLab.Bridge.Runtime.IndependentPeers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeRealPeers::RunTest(const FString&)
{
    FRuntime Runtime;
    if (!Start(*this, Runtime)) return false;
    const FPeerView& A = Runtime.GetPeer(RL_PEER_A);
    const FPeerView& B = Runtime.GetPeer(RL_PEER_B);
    TestTrue(TEXT("A has a real opaque resource"), A.HandleIdentity != 0);
    TestTrue(TEXT("B has a different opaque resource"), B.HandleIdentity != 0 && B.HandleIdentity != A.HandleIdentity);
    TestEqual(TEXT("SDK API version"), Runtime.GetVersionInfo().api_version, RL_API_VERSION);
    TestEqual(TEXT("SDK live/session/bytes capabilities"), Runtime.GetVersionInfo().capabilities & UINT64_C(7), UINT64_C(7));
    bool bSawPredictionDifference = false;
    for (uint32 Tick = 0; Tick < 24; ++Tick)
    {
        if (!TestTrue(TEXT("SDK advances one transport tick"), Runtime.SingleStep().IsOk())) return false;
        bSawPredictionDifference |= Runtime.GetPeer(0).Snapshot.state_hash != Runtime.GetPeer(1).Snapshot.state_hash;
    }
    TestTrue(TEXT("Delayed peers can hold different predicted worlds"), bSawPredictionDifference);
    TestEqual(TEXT("Exactly 24 transport steps"), Runtime.GetLastStep().logical_tick, 24U);
    Runtime.Stop();
    TestFalse(TEXT("Stop releases running state"), Runtime.IsRunning());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeConvergence, "RollbackLab.Bridge.Runtime.CorrectionConvergenceArtifacts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeConvergence::RunTest(const FString&)
{
    FRuntime Runtime;
    FStartOptions Options = Scenario();
    if (!Start(*this, Runtime, Options)) return false;
    uint64 PreviousRevision[2] = {};
    bool bSawCorrection = false;
    for (uint32 Tick = 0; Tick < Options.Scenario.frame_count + 96 && !Runtime.IsFinished(); ++Tick)
    {
        if (!TestTrue(TEXT("Real scenario step"), Runtime.SingleStep().IsOk())) return false;
        for (uint32 Peer = 0; Peer < 2; ++Peer)
        {
            const FPeerView& View = Runtime.GetPeer(Peer);
            if (View.CorrectionRevision != PreviousRevision[Peer])
            {
                bSawCorrection = true;
                TestTrue(TEXT("Revision advances monotonically"), View.CorrectionRevision > PreviousRevision[Peer]);
                TestEqual(TEXT("Revision comes from actual correction"), View.LastCorrection.performed, 1U);
                TestTrue(TEXT("Actual correction has resimulation"), View.LastCorrection.resimulated_frames > 0);
                TestEqual(TEXT("Ghost endpoints share correction boundary"), View.LastCorrection.before.frame, View.LastCorrection.after.frame);
                PreviousRevision[Peer] = View.CorrectionRevision;
            }
        }
    }
    TestTrue(TEXT("Fixed demo produces real rollback"), bSawCorrection);
    TestTrue(TEXT("Bounded run finishes"), Runtime.IsFinished());
    TestEqual(TEXT("A confirmed target"), Runtime.GetPeer(0).Metrics.confirmed_frame, Options.Scenario.frame_count);
    TestEqual(TEXT("B confirmed target"), Runtime.GetPeer(1).Metrics.confirmed_frame, Options.Scenario.frame_count);
    TestEqual(TEXT("Confirmed peers converge"), Runtime.GetPeer(0).Snapshot.state_hash, Runtime.GetPeer(1).Snapshot.state_hash);
    FString Report, Trace;
    TArray<uint8> Replay;
    TestTrue(TEXT("Copy actual SDK report"), Runtime.CopyReport(Report).IsOk());
    TestTrue(TEXT("Copy actual SDK trace"), Runtime.CopyTrace(Trace).IsOk());
    TestTrue(TEXT("Copy actual SDK replay"), Runtime.CopyReplay(Replay).IsOk());
    TSharedPtr<FJsonObject> Parsed;
    TestTrue(TEXT("Report is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Report), Parsed));
    Parsed.Reset();
    TestTrue(TEXT("Trace is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Trace), Parsed));
    TestTrue(TEXT("Replay bytes materialized"), Replay.Num() > 32);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeReset, "RollbackLab.Bridge.Runtime.Reset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeReset::RunTest(const FString&)
{
    FRuntime Runtime;
    if (!Start(*this, Runtime)) return false;
    const uint64 InitialHash = Runtime.GetPeer(0).Snapshot.state_hash;
    for (uint32 Tick = 0; Tick < 12; ++Tick) if (!TestTrue(TEXT("Advance before reset"), Runtime.SingleStep().IsOk())) return false;
    Runtime.SetPaused(true);
    if (!TestTrue(TEXT("Reset reconstructs driver and peers"), Runtime.Reset().IsOk())) return false;
    TestEqual(TEXT("Reset frame A"), Runtime.GetPeer(0).Snapshot.frame, 0U);
    TestEqual(TEXT("Reset frame B"), Runtime.GetPeer(1).Snapshot.frame, 0U);
    TestEqual(TEXT("Reset canonical state"), Runtime.GetPeer(0).Snapshot.state_hash, InitialHash);
    TestEqual(TEXT("Reset metrics"), Runtime.GetPeer(0).Metrics.rollback_count, 0U);
    TestEqual(TEXT("Reset logical tick"), Runtime.GetLastStep().logical_tick, 0U);
    TestEqual(TEXT("Reset correction revision"), Runtime.GetPeer(0).CorrectionRevision, UINT64_C(0));
    TestEqual(TEXT("Reset accumulator"), Runtime.GetClockState().AccumulatorSeconds, 0.0);
    TestEqual(TEXT("Reset discarded wall time"), Runtime.GetClockState().DiscardedSeconds, 0.0);
    TestFalse(TEXT("Reset starts unpaused"), Runtime.GetClockState().bPaused);
    return true;
}
#endif
