#include "RollbackLabSdk.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Windows/WindowsHWrapper.h"

THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace RollbackLabBridge::Private
{
std::atomic<uint32> LibraryCount{0};
std::atomic<uint32> SessionCount{0};
std::atomic<uint32> DriverCount{0};

FSdk::FSdk(void* InHandle) : Handle(InHandle) { ++LibraryCount; }
FSdk::~FSdk()
{
    Api = {};
    FPlatformProcess::FreeDllHandle(Handle);
    --LibraryCount;
}

namespace
{
bool IsHex(const FString& Value, int32 Length)
{
    if (Value.Len() != Length) return false;
    for (TCHAR Character : Value)
        if (!((Character >= TEXT('0') && Character <= TEXT('9')) ||
              (Character >= TEXT('a') && Character <= TEXT('f')) ||
              (Character >= TEXT('A') && Character <= TEXT('F')))) return false;
    return true;
}

bool IsRelativeManifestPath(const FString& Value)
{
    if (Value.IsEmpty() || Value.StartsWith(TEXT("/")) || Value.Contains(TEXT(".."))) return false;
    for (TCHAR Character : Value)
        if (!((Character >= TEXT('0') && Character <= TEXT('9')) ||
              (Character >= TEXT('a') && Character <= TEXT('z')) ||
              (Character >= TEXT('A') && Character <= TEXT('Z')) ||
              Character == TEXT('_') || Character == TEXT('-') || Character == TEXT('.') || Character == TEXT('/'))) return false;
    return true;
}

bool HashFile(const FString& Path, int64 Limit, FString& Output)
{
    const int64 Size = IFileManager::Get().FileSize(*Path);
    if (Size < 0 || Size > Limit) return false;
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path)) return false;
    unsigned char Digest[SHA256_DIGEST_LENGTH]{};
    if (SHA256(Bytes.GetData(), static_cast<size_t>(Bytes.Num()), Digest) == nullptr) return false;
    constexpr TCHAR Hex[] = TEXT("0123456789abcdef");
    Output.Reset(SHA256_DIGEST_LENGTH * 2);
    for (unsigned char Byte : Digest)
    {
        Output.AppendChar(Hex[Byte >> 4]);
        Output.AppendChar(Hex[Byte & 15]);
    }
    return true;
}

bool NumberIs(const FJsonObject& Object, const TCHAR* Key, double Expected)
{
    double Value = 0;
    return Object.TryGetNumberField(Key, Value) && Value == Expected;
}

bool StringIs(const FJsonObject& Object, const TCHAR* Key, const TCHAR* Expected)
{
    FString Value;
    return Object.TryGetStringField(Key, Value) && Value == Expected;
}

FResult VerifyManifest(const FStartOptions& Options, FString& DllPath)
{
    FString Root = Options.SdkRootOverride.IsEmpty() ? FRuntime::DefaultSdkRoot() : Options.SdkRootOverride;
    if (Root.IsEmpty()) return Failure(EError::MissingManifest, TEXT("RollbackLab plugin SDK directory is unavailable."));
    Root = FPaths::ConvertRelativePathToFull(Root);
    const FString ManifestPath = FPaths::Combine(Root, TEXT("manifest.json"));
    const int64 ManifestSize = IFileManager::Get().FileSize(*ManifestPath);
    if (ManifestSize < 0) return Failure(EError::MissingManifest, TEXT("RollbackLab SDK manifest.json is missing."));
    if (ManifestSize > 1024 * 1024) return Failure(EError::InvalidManifest, TEXT("RollbackLab SDK manifest exceeds the bounded size."));
    FString Json;
    TSharedPtr<FJsonObject> Object;
    if (!FFileHelper::LoadFileToString(Json, *ManifestPath) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object.IsValid())
        return Failure(EError::InvalidManifest, TEXT("RollbackLab SDK manifest is not a JSON object."));

    if (!NumberIs(*Object, TEXT("schema_version"), 1) ||
        !NumberIs(*Object, TEXT("abi_version"), RL_API_VERSION) ||
        !NumberIs(*Object, TEXT("simulation_version"), 1) ||
        !NumberIs(*Object, TEXT("protocol_version"), 1) ||
        !NumberIs(*Object, TEXT("replay_version"), 1) ||
        !StringIs(*Object, TEXT("sdk_version"), TEXT("0.2.0-candidate")))
        return Failure(EError::VersionMismatch, TEXT("RollbackLab SDK/ABI/simulation/protocol/replay version mismatch."));

    bool bClean = false;
    if (!Object->TryGetBoolField(TEXT("source_clean"), bClean) || !bClean ||
        !StringIs(*Object, TEXT("configuration"), TEXT("Release")) ||
        !StringIs(*Object, TEXT("architecture"), TEXT("x64")) ||
        !StringIs(*Object, TEXT("runtime"), TEXT("MD")) ||
        !StringIs(*Object, TEXT("linkage"), TEXT("shared")))
        return Failure(EError::InvalidManifest, TEXT("RollbackLab requires a clean Release x64 /MD shared SDK."));

    FString SourceSha;
    if (!Object->TryGetStringField(TEXT("source_git_sha"), SourceSha) || !IsHex(SourceSha, 40) ||
        SourceSha != TEXT(ROLLBACKLAB_EXPECTED_SOURCE_SHA) ||
        (!Options.ExpectedSourceGitSha.IsEmpty() && Options.ExpectedSourceGitSha != SourceSha))
        return Failure(EError::SourceMismatch, TEXT("RollbackLab SDK source SHA differs from the compiled or requested identity."));

    const TArray<TSharedPtr<FJsonValue>>* Files = nullptr;
    if (!Object->TryGetArrayField(TEXT("files"), Files) || Files->Num() == 0 || Files->Num() > 256)
        return Failure(EError::InvalidManifest, TEXT("RollbackLab SDK manifest has no bounded file list."));
    TSet<FString> Seen;
    FString DllHash;
    for (const TSharedPtr<FJsonValue>& Value : *Files)
    {
        if (!Value.IsValid() || Value->Type != EJson::Object)
            return Failure(EError::InvalidManifest, TEXT("RollbackLab SDK manifest file entry is not an object."));
        const TSharedPtr<FJsonObject> Entry = Value->AsObject();
        FString Path, Hash;
        if (!Entry->TryGetStringField(TEXT("path"), Path) || !Entry->TryGetStringField(TEXT("sha256"), Hash) ||
            !IsRelativeManifestPath(Path) || !IsHex(Hash, 64) || Seen.Contains(Path.ToLower()))
            return Failure(EError::InvalidManifest, TEXT("RollbackLab SDK manifest contains an invalid/duplicate path or digest."));
        Seen.Add(Path.ToLower());
        if (Path == TEXT("bin/rollback_lab_c.dll")) DllHash = Hash;
    }
    if (DllHash.IsEmpty() || !Seen.Contains(TEXT("include/rollback_lab/c_api/rollback_lab_c.h")) || !Seen.Contains(TEXT("lib/rollback_lab_c.lib")))
        return Failure(EError::InvalidManifest, TEXT("RollbackLab SDK manifest omits the DLL/header/import library contract."));

    FString Actual;
    if (!HashFile(ManifestPath, 1024 * 1024, Actual) || Actual != TEXT(ROLLBACKLAB_EXPECTED_MANIFEST_SHA256))
        return Failure(EError::HashMismatch, TEXT("RollbackLab SDK manifest SHA-256 differs from the compiled manifest."));
    DllPath = FPaths::Combine(Root, TEXT("bin/rollback_lab_c.dll"));
    if (!IFileManager::Get().FileExists(*DllPath))
        return Failure(EError::MissingLibrary, TEXT("RollbackLab SDK DLL is missing."));
    if (!HashFile(DllPath, 64LL * 1024 * 1024, Actual) || !Actual.Equals(DllHash, ESearchCase::IgnoreCase))
        return Failure(EError::HashMismatch, TEXT("RollbackLab SDK DLL SHA-256 mismatch."));
    return {};
}
}

FResult LoadSdk(const FStartOptions& Options, TSharedPtr<FSdk>& Output)
{
    Output.Reset();
    FString DllPath;
    FResult Result = VerifyManifest(Options, DllPath);
    if (!Result.IsOk()) return Result;

    // UE 5.8 GetDllHandle can return GetModuleHandle's borrowed reference when
    // this path is already loaded. Every FSdk must instead own one OS reference,
    // including when another runtime or an external consumer loaded it first.
    FPaths::MakePlatformFilename(DllPath);
    void* Handle = ::LoadLibraryExW(*DllPath, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (Handle == nullptr)
        return Failure(EError::LoadFailure, FString::Printf(TEXT("Verified RollbackLab DLL load failed with Win32 error %lu."), ::GetLastError()));
    TSharedPtr<FSdk> Library = MakeShared<FSdk>(Handle);
#define RL_RESOLVE(Name) \
    Library->Api.Name = reinterpret_cast<decltype(Library->Api.Name)>(FPlatformProcess::GetDllExport(Handle, TEXT(#Name))); \
    if (Library->Api.Name == nullptr) return Failure(EError::MissingExport, TEXT("RollbackLab DLL is missing required export " #Name));
    RL_RESOLVE(rl_get_version)
    RL_RESOLVE(rl_session_create)
    RL_RESOLVE(rl_session_destroy)
    RL_RESOLVE(rl_session_advance)
    RL_RESOLVE(rl_session_ingest_remote)
    RL_RESOLVE(rl_session_flush_corrections)
    RL_RESOLVE(rl_session_get_snapshot)
    RL_RESOLVE(rl_session_get_confirmed_frame)
    RL_RESOLVE(rl_session_get_metrics)
    RL_RESOLVE(rl_session_get_hash)
    RL_RESOLVE(rl_session_hash_at)
    RL_RESOLVE(rl_session_serialize_state)
    RL_RESOLVE(rl_live_create)
    RL_RESOLVE(rl_live_destroy)
    RL_RESOLVE(rl_live_step)
    RL_RESOLVE(rl_live_get_correction)
    RL_RESOLVE(rl_live_copy_report)
    RL_RESOLVE(rl_live_copy_trace)
    RL_RESOLVE(rl_live_copy_replay)
    if (Options.bUdp)
    {
        RL_RESOLVE(rl_udp_peer_create)
        RL_RESOLVE(rl_udp_peer_destroy)
        RL_RESOLVE(rl_udp_peer_step)
        RL_RESOLVE(rl_udp_peer_get_correction)
        RL_RESOLVE(rl_udp_peer_copy_report)
        RL_RESOLVE(rl_udp_peer_copy_replay)
        RL_RESOLVE(rl_udp_peer_copy_failure)
    }
#undef RL_RESOLVE

    Library->Version = Initialized<rl_version_info>();
    const rl_status Status = Library->Api.rl_get_version(&Library->Version);
    if (Status != RL_OK) return SdkResult(Status, TEXT("rl_get_version"));
    const rl_version_info& Version = Library->Version;
    const uint64 RequiredCapabilities = RL_CAP_SESSION | RL_CAP_LIVE | RL_CAP_CANONICAL_BYTES | (Options.bUdp ? RL_CAP_UDP : UINT64_C(0));
    if (Version.api_version != RL_API_VERSION || Version.struct_size != sizeof(Version) ||
        Version.sdk_major != 0 || Version.sdk_minor != 2 || Version.sdk_patch != 0 ||
        Version.simulation_version != 1 || Version.protocol_version != 1 || Version.replay_version != 1 ||
        (Version.capabilities & RequiredCapabilities) != RequiredCapabilities)
        return Failure(EError::VersionMismatch, TEXT("Loaded RollbackLab ABI/version/capabilities do not match the bridge."));
    if (Version.source_git_sha[40] != '\0')
        return Failure(EError::SourceMismatch, TEXT("Loaded RollbackLab SDK has an invalid source identity."));
    const FString LoadedSha = UTF8_TO_TCHAR(Version.source_git_sha);
    if (!IsHex(LoadedSha, 40) || LoadedSha != TEXT(ROLLBACKLAB_EXPECTED_SOURCE_SHA))
        return Failure(EError::SourceMismatch, TEXT("Loaded RollbackLab DLL source SHA does not match its verified manifest."));
    Output = MoveTemp(Library);
    return {};
}
}
