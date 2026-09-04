# Root cause: borrowed Windows DLL handle

## Observable symptom and reproduction

The first real UE Runtime passed the three clock Automation cases, then crashed
in `Lifecycle.PreExitStopsAll`, which starts two simultaneous Runtime instances
and calls StopAll. The access violation executed an SDK function address from
`FDriverOwner::~FDriverOwner` while releasing the remaining runtime.

Evidence: `artifacts/ue5-0.2/runs/20260904-113438-922-automation/editor.log`.
The parent process exited 3. A crash monitor retained redirected stdout after
the Editor exited, exposing a separate supervisor pipe-completion issue.

## Facts and ownership boundary

Each runtime held a separate FSdk wrapper with shared leases among its own driver
and two sessions. However UE5.8's stock
`WindowsPlatformProcess.cpp:2503-2506` returns `GetModuleHandle` when the DLL is
already loaded. That path does not acquire another OS reference. The first
runtime's final FreeDllHandle therefore unloaded the SDK beneath the second.

Session-before-DLL destruction order within one runtime was correct; ownership
between multiple runtime wrappers was not. This is not a canonical simulation,
CRT allocator, thread-affinity or fixed-step failure. No Engine file was edited.

## Materially different fix

Acquire one actual owned Win64 `LoadLibraryExW` reference per FSdk using the
verified absolute DLL path and DLL-load-directory/default safe search flags.
Release exactly that reference when its final native lease dies. This also
respects an independently preloaded DLL reference owned by another caller.

The original two-runtime tests remain unchanged; an external-preload lifetime
regression was added. No sleep or timeout was increased. GREEN evidence must
come from the next actual Editor/Automation run; this note alone is not a pass.

The process supervisor is being improved separately to preserve the real root
exit code, use direct log handles and contain only its own descendants in a
Windows job. Other projects' UE processes must never be terminated by name.
