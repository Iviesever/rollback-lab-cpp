#pragma once

#include "RollbackLabRuntime.h"
#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

namespace RollbackLabBridgeTests
{
using namespace RollbackLabBridge;

inline FStartOptions Scenario()
{
    FStartOptions Options;
    Options.Scenario.frame_count = 120;
    return Options;
}

inline bool Start(FAutomationTestBase& Test, FRuntime& Runtime, const FStartOptions& Options = Scenario())
{
    const FResult Result = Runtime.Start(Options);
    return Test.TestTrue(FString::Printf(TEXT("Real SDK Start succeeds: %s"), *Result.Message), Result.IsOk());
}

// Each negative case edits its own copy. The staged SDK stays immutable.
struct FSdkFixture
{
    FString Root;
    bool bReady = false;

    FSdkFixture()
    {
        Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/RollbackLab"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
        const FString Source = FRuntime::DefaultSdkRoot();
        IFileManager::Get().MakeDirectory(*FPaths::Combine(Root, TEXT("bin")), true);
        bReady = IFileManager::Get().Copy(*FPaths::Combine(Root, TEXT("manifest.json")), *FPaths::Combine(Source, TEXT("manifest.json"))) == COPY_OK;
        bReady = bReady && IFileManager::Get().Copy(*FPaths::Combine(Root, TEXT("bin/rollback_lab_c.dll")), *FPaths::Combine(Source, TEXT("bin/rollback_lab_c.dll"))) == COPY_OK;
    }

    ~FSdkFixture()
    {
        // Root is always a fresh GUID child of the project Saved directory.
        IFileManager::Get().DeleteDirectory(*Root, false, true);
    }

    FStartOptions Options() const
    {
        FStartOptions Result = Scenario();
        Result.SdkRootOverride = Root;
        return Result;
    }
};
}
