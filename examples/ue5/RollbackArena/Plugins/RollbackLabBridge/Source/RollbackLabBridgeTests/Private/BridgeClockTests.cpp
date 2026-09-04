#include "BridgeTestSupport.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
using namespace RollbackLabBridgeTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeClockBound, "RollbackLab.Bridge.Clock.BoundedCatchUp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeClockBound::RunTest(const FString&)
{
    FRuntime Runtime;
    if (!Start(*this, Runtime)) return false;
    if (!TestTrue(TEXT("Long frame accepted"), Runtime.TickWallClock(1.0).IsOk())) return false;
    TestEqual(TEXT("Catch-up capped at eight"), Runtime.GetClockState().LastTickSteps, 8U);
    TestEqual(TEXT("Only eight canonical steps"), Runtime.GetLastStep().logical_tick, 8U);
    TestTrue(TEXT("Whole excess ticks discarded observationally"), Runtime.GetClockState().DiscardedSeconds > 0.8);
    TestTrue(TEXT("Remainder bounded"), Runtime.GetClockState().AccumulatorSeconds >= 0.0 && Runtime.GetClockState().AccumulatorSeconds < FRuntime::FixedStepSeconds);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeClockControls, "RollbackLab.Bridge.Clock.PauseSingleStep", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeClockControls::RunTest(const FString&)
{
    FRuntime Runtime;
    if (!Start(*this, Runtime)) return false;
    if (!TestTrue(TEXT("Half step accumulated"), Runtime.TickWallClock(FRuntime::FixedStepSeconds / 2.0).IsOk())) return false;
    TestEqual(TEXT("Half step does not simulate"), Runtime.GetLastStep().logical_tick, 0U);
    Runtime.SetPaused(true);
    const double SavedAccumulator = Runtime.GetClockState().AccumulatorSeconds;
    TestTrue(TEXT("Paused wall time accepted"), Runtime.TickWallClock(2.0).IsOk());
    TestEqual(TEXT("Pause adds no debt"), Runtime.GetClockState().AccumulatorSeconds, SavedAccumulator);
    TestEqual(TEXT("Pause does not simulate"), Runtime.GetLastStep().logical_tick, 0U);
    TestTrue(TEXT("Paused single step"), Runtime.SingleStep().IsOk());
    TestEqual(TEXT("Single step does exactly one tick"), Runtime.GetLastStep().logical_tick, 1U);
    TestTrue(TEXT("Single step preserves pause"), Runtime.GetClockState().bPaused);
    TestEqual(TEXT("Single step preserves fractional debt"), Runtime.GetClockState().AccumulatorSeconds, SavedAccumulator);
    Runtime.SetPaused(false);
    TestTrue(TEXT("Remaining half step accepted"), Runtime.TickWallClock(FRuntime::FixedStepSeconds / 2.0).IsOk());
    TestEqual(TEXT("Fractional steps combine"), Runtime.GetLastStep().logical_tick, 2U);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeInvalidDelta, "RollbackLab.Bridge.Clock.InvalidDelta", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeInvalidDelta::RunTest(const FString&)
{
    FRuntime Runtime;
    if (!Start(*this, Runtime)) return false;
    for (const double Delta : {-0.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        const FResult Result = Runtime.TickWallClock(Delta);
        TestEqual(TEXT("Invalid delta is typed"), static_cast<uint8>(Result.Error), static_cast<uint8>(EError::InvalidArgument));
        TestEqual(TEXT("Invalid delta cannot mutate canonical tick"), Runtime.GetLastStep().logical_tick, 0U);
        TestEqual(TEXT("Invalid delta cannot poison accumulator"), Runtime.GetClockState().AccumulatorSeconds, 0.0);
    }
    return true;
}
#endif
