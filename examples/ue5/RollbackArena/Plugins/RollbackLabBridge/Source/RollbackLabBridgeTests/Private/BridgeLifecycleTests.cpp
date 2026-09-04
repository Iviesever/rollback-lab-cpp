#include "BridgeTestSupport.h"
#include "RollbackLabSubsystem.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformProcess.h"
#include "Windows/WindowsHWrapper.h"

#if WITH_DEV_AUTOMATION_TESTS
using namespace RollbackLabBridgeTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeExternalPreload, "RollbackLab.Bridge.Lifecycle.ExternalPreloadedDll", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeExternalPreload::RunTest(const FString&)
{
    FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FRuntime::DefaultSdkRoot(), TEXT("bin/rollback_lab_c.dll")));
    FPaths::MakePlatformFilename(Path);
    struct FExternalReference
    {
        HMODULE Handle = nullptr;
        ~FExternalReference() { if (Handle != nullptr) ::FreeLibrary(Handle); }
    } External;
    External.Handle = ::LoadLibraryExW(*Path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!TestTrue(TEXT("External consumer owns a DLL reference"), External.Handle != nullptr)) return false;
    const FLifetimeStats Before = FRuntime::GetLifetimeStats();
    {
        FRuntime Runtime;
        if (!Start(*this, Runtime)) return false;
        TestTrue(TEXT("Bridge can use externally preloaded SDK"), Runtime.SingleStep().IsOk());
        Runtime.Stop();
    }
    TestEqual(TEXT("Bridge releases all its leases"), FRuntime::GetLifetimeStats().Libraries, Before.Libraries);
    if (!TestTrue(TEXT("External DLL reference survives bridge stop"), ::GetModuleHandleW(*Path) == External.Handle)) return false;
    const auto GetVersion = reinterpret_cast<decltype(&rl_get_version)>(FPlatformProcess::GetDllExport(External.Handle, TEXT("rl_get_version")));
    if (!TestTrue(TEXT("External consumer's export remains available"), GetVersion != nullptr)) return false;
    rl_version_info Version{};
    Version.api_version = RL_API_VERSION;
    Version.struct_size = sizeof(Version);
    TestEqual(TEXT("External consumer can still call SDK"), GetVersion(&Version), RL_OK);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeSharedLifetime, "RollbackLab.Bridge.Lifecycle.SharedLibraryLease", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeSharedLifetime::RunTest(const FString&)
{
    const FLifetimeStats Before = FRuntime::GetLifetimeStats();
    {
        FRuntime First;
        FRuntime Second;
        if (!Start(*this, First) || !Start(*this, Second)) return false;
        TestEqual(TEXT("Two runtimes own four real sessions"), FRuntime::GetLifetimeStats().Sessions, Before.Sessions + 4U);
        TestEqual(TEXT("Two live drivers"), FRuntime::GetLifetimeStats().Drivers, Before.Drivers + 2U);
        First.Stop();
        First.Stop();
        TestEqual(TEXT("Idempotent stop frees only first pair"), FRuntime::GetLifetimeStats().Sessions, Before.Sessions + 2U);
        TestTrue(TEXT("Second runtime retains loaded DLL"), Second.SingleStep().IsOk());
        TestEqual(TEXT("Second runtime remains authoritative"), Second.GetLastStep().logical_tick, 1U);
    }
    const FLifetimeStats After = FRuntime::GetLifetimeStats();
    TestEqual(TEXT("All sessions destroyed"), After.Sessions, Before.Sessions);
    TestEqual(TEXT("All borrowing drivers destroyed"), After.Drivers, Before.Drivers);
    TestEqual(TEXT("Final lease releases DLL"), After.Libraries, Before.Libraries);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeSubsystemLifetime, "RollbackLab.Bridge.Lifecycle.SubsystemDeinitialize", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeSubsystemLifetime::RunTest(const FString&)
{
    const FLifetimeStats Before = FRuntime::GetLifetimeStats();
    UGameInstance* GameInstance = NewObject<UGameInstance>();
    URollbackLabSubsystem* Subsystem = NewObject<URollbackLabSubsystem>(GameInstance);
    if (!Start(*this, Subsystem->GetRuntime())) return false;
    Subsystem->Deinitialize();
    Subsystem->Deinitialize();
    TestFalse(TEXT("Deinitialize stops native runtime"), Subsystem->GetRuntime().IsRunning());
    TestEqual(TEXT("Deinitialize releases sessions"), FRuntime::GetLifetimeStats().Sessions, Before.Sessions);
    TestEqual(TEXT("Deinitialize releases drivers before sessions"), FRuntime::GetLifetimeStats().Drivers, Before.Drivers);
    TestEqual(TEXT("Deinitialize releases final DLL lease"), FRuntime::GetLifetimeStats().Libraries, Before.Libraries);
    TestEqual(TEXT("Stopped runtime cannot advance"), static_cast<uint8>(Subsystem->GetRuntime().SingleStep().Error), static_cast<uint8>(EError::NotRunning));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgePreExitLifetime, "RollbackLab.Bridge.Lifecycle.PreExitStopsAll", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgePreExitLifetime::RunTest(const FString&)
{
    FRuntime First;
    FRuntime Second;
    if (!Start(*this, First) || !Start(*this, Second)) return false;
    FRuntime::StopAll();
    FRuntime::StopAll();
    TestFalse(TEXT("Pre-exit stops first runtime"), First.IsRunning());
    TestFalse(TEXT("Pre-exit stops second runtime"), Second.IsRunning());
    TestEqual(TEXT("Pre-exit releases all drivers"), FRuntime::GetLifetimeStats().Drivers, 0U);
    TestEqual(TEXT("Pre-exit releases all sessions"), FRuntime::GetLifetimeStats().Sessions, 0U);
    TestEqual(TEXT("Pre-exit releases final DLL leases"), FRuntime::GetLifetimeStats().Libraries, 0U);
    return true;
}
#endif
