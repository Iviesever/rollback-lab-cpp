#include "BridgeTestSupport.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
using namespace RollbackLabBridgeTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeMissingManifest, "RollbackLab.Bridge.Loader.MissingManifest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeMissingManifest::RunTest(const FString&)
{
    FSdkFixture Fixture;
    if (!TestTrue(TEXT("SDK fixture copied"), Fixture.bReady)) return false;
    IFileManager::Get().Delete(*FPaths::Combine(Fixture.Root, TEXT("manifest.json")));
    FRuntime Runtime;
    const FResult Result = Runtime.Start(Fixture.Options());
    TestEqual(TEXT("Missing manifest has typed failure"), static_cast<uint8>(Result.Error), static_cast<uint8>(EError::MissingManifest));
    TestFalse(TEXT("Invalid SDK never runs"), Runtime.IsRunning());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeMalformedManifest, "RollbackLab.Bridge.Loader.MalformedManifest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeMalformedManifest::RunTest(const FString&)
{
    FSdkFixture Fixture;
    if (!TestTrue(TEXT("SDK fixture copied"), Fixture.bReady)) return false;
    FFileHelper::SaveStringToFile(TEXT("{invalid-json"), *FPaths::Combine(Fixture.Root, TEXT("manifest.json")));
    FRuntime Runtime;
    const FResult Result = Runtime.Start(Fixture.Options());
    TestEqual(TEXT("Malformed manifest has typed failure"), static_cast<uint8>(Result.Error), static_cast<uint8>(EError::InvalidManifest));
    TestFalse(TEXT("Invalid SDK never runs"), Runtime.IsRunning());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeWrongManifestVersion, "RollbackLab.Bridge.Loader.UnsupportedVersion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeWrongManifestVersion::RunTest(const FString&)
{
    FSdkFixture Fixture;
    if (!TestTrue(TEXT("SDK fixture copied"), Fixture.bReady)) return false;
    const FString Path = FPaths::Combine(Fixture.Root, TEXT("manifest.json"));
    FString Json;
    TSharedPtr<FJsonObject> Object;
    if (!TestTrue(TEXT("Read staged manifest"), FFileHelper::LoadFileToString(Json, *Path)) ||
        !TestTrue(TEXT("Parse staged manifest"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object))) return false;
    Object->SetNumberField(TEXT("abi_version"), 99);
    Json.Reset();
    FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
    FFileHelper::SaveStringToFile(Json, *Path);
    FRuntime Runtime;
    const FResult Result = Runtime.Start(Fixture.Options());
    TestEqual(TEXT("Unsupported ABI is diagnosed before loading"), static_cast<uint8>(Result.Error), static_cast<uint8>(EError::VersionMismatch));
    TestFalse(TEXT("Invalid SDK never runs"), Runtime.IsRunning());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeMissingDll, "RollbackLab.Bridge.Loader.MissingDll", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeMissingDll::RunTest(const FString&)
{
    FSdkFixture Fixture;
    if (!TestTrue(TEXT("SDK fixture copied"), Fixture.bReady)) return false;
    IFileManager::Get().Delete(*FPaths::Combine(Fixture.Root, TEXT("bin/rollback_lab_c.dll")));
    FRuntime Runtime;
    const FResult Result = Runtime.Start(Fixture.Options());
    TestEqual(TEXT("Missing DLL has typed failure"), static_cast<uint8>(Result.Error), static_cast<uint8>(EError::MissingLibrary));
    TestFalse(TEXT("Missing SDK never runs"), Runtime.IsRunning());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeDllHashMismatch, "RollbackLab.Bridge.Loader.DllHashMismatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeDllHashMismatch::RunTest(const FString&)
{
    FSdkFixture Fixture;
    if (!TestTrue(TEXT("SDK fixture copied"), Fixture.bReady)) return false;
    const FString Path = FPaths::Combine(Fixture.Root, TEXT("bin/rollback_lab_c.dll"));
    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("Read real DLL"), FFileHelper::LoadFileToArray(Bytes, *Path)) || !TestTrue(TEXT("DLL has bytes"), Bytes.Num() > 0)) return false;
    Bytes.Last() ^= 1;
    FFileHelper::SaveArrayToFile(Bytes, *Path);
    FRuntime Runtime;
    const FResult Result = Runtime.Start(Fixture.Options());
    TestEqual(TEXT("Corrupt DLL rejected before LoadLibrary"), static_cast<uint8>(Result.Error), static_cast<uint8>(EError::HashMismatch));
    TestFalse(TEXT("Corrupt SDK never runs"), Runtime.IsRunning());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeSourceMismatch, "RollbackLab.Bridge.Loader.SourceMismatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeSourceMismatch::RunTest(const FString&)
{
    FStartOptions Options = Scenario();
    Options.ExpectedSourceGitSha = FString::ChrN(40, TEXT('0'));
    FRuntime Runtime;
    const FResult Result = Runtime.Start(Options);
    TestEqual(TEXT("Wrong caller source identity rejected"), static_cast<uint8>(Result.Error), static_cast<uint8>(EError::SourceMismatch));
    TestFalse(TEXT("Mismatched SDK never runs"), Runtime.IsRunning());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeManifestHashMismatch, "RollbackLab.Bridge.Loader.ManifestHashMismatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBridgeManifestHashMismatch::RunTest(const FString&)
{
    FSdkFixture Fixture;
    if (!TestTrue(TEXT("SDK fixture copied"), Fixture.bReady)) return false;
    const FString Path = FPaths::Combine(Fixture.Root, TEXT("manifest.json"));
    FString Json;
    if (!TestTrue(TEXT("Read real manifest"), FFileHelper::LoadFileToString(Json, *Path))) return false;
    Json += TEXT("\n "); // Still valid JSON, but no longer the manifest compiled into the bridge.
    FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    FRuntime Runtime;
    const FResult Result = Runtime.Start(Fixture.Options());
    TestEqual(TEXT("Altered manifest fails compiled checksum"), static_cast<uint8>(Result.Error), static_cast<uint8>(EError::HashMismatch));
    TestFalse(TEXT("Unbound manifest never runs"), Runtime.IsRunning());
    return true;
}
#endif
