#pragma once

#include "RollbackLabRuntime.h"
#include <atomic>

DECLARE_LOG_CATEGORY_EXTERN(LogRollbackLabBridge, Log, All);

namespace RollbackLabBridge::Private
{
template <typename T> T Initialized()
{
    T Value{};
    Value.api_version = RL_API_VERSION;
    Value.struct_size = sizeof(T);
    return Value;
}

inline FResult Failure(EError Error, const FString& Message, rl_status Status = RL_OK)
{
    return {Error, Status, Message};
}

inline FResult SdkResult(rl_status Status, const TCHAR* Operation)
{
    if (Status == RL_OK) return {};
    return Failure(Status == RL_WRONG_THREAD ? EError::WrongThread : EError::SdkFailure,
        FString::Printf(TEXT("%s returned SDK status %u."), Operation, Status), Status);
}

struct FApi
{
#define RL_DECLARE(Name) decltype(&::Name) Name = nullptr;
    RL_DECLARE(rl_get_version)
    RL_DECLARE(rl_session_create)
    RL_DECLARE(rl_session_destroy)
    RL_DECLARE(rl_session_advance)
    RL_DECLARE(rl_session_ingest_remote)
    RL_DECLARE(rl_session_flush_corrections)
    RL_DECLARE(rl_session_get_snapshot)
    RL_DECLARE(rl_session_get_confirmed_frame)
    RL_DECLARE(rl_session_get_metrics)
    RL_DECLARE(rl_session_get_hash)
    RL_DECLARE(rl_session_hash_at)
    RL_DECLARE(rl_session_serialize_state)
    RL_DECLARE(rl_live_create)
    RL_DECLARE(rl_live_destroy)
    RL_DECLARE(rl_live_step)
    RL_DECLARE(rl_live_get_correction)
    RL_DECLARE(rl_live_copy_report)
    RL_DECLARE(rl_live_copy_trace)
    RL_DECLARE(rl_live_copy_replay)
#undef RL_DECLARE
};

extern std::atomic<uint32> LibraryCount;
extern std::atomic<uint32> SessionCount;
extern std::atomic<uint32> DriverCount;

// Shared by the driver, both sessions, and runtime. The final lease unloads DLL.
struct FSdk final
{
    explicit FSdk(void* InHandle);
    ~FSdk();
    FSdk(const FSdk&) = delete;
    FSdk& operator=(const FSdk&) = delete;
    void* Handle;
    FApi Api;
    rl_version_info Version{};
};

FResult LoadSdk(const FStartOptions& Options, TSharedPtr<FSdk>& Output);
}
