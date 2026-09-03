# Rollback Netcode 0.1 实现计划

> **针对 Agent 工作者：** 必需的子 Skill：使用 `executing-tasks` 在本会话逐任务执行。步骤使用复选框 (`- [ ]`) 追踪。目标明确要求持续自主执行，因此无需再次选择执行模式。

**目标：** 交付一个从确定性模拟一直贯通到真实 UDP 多进程、回放、反同步诊断、报告和浏览器时间线的 C++23 rollback netcode 实验室。

**架构：** 一个 `rollback_lab` 静态库承载所有生产逻辑，一个薄 CLI 组合相同 API。Core、Simulation、Netcode、Protocol、Transport、Replay/Report、Platform/CLI 单向依赖，两个 Peer 只能交换 packet。

**技术栈：** C++23、CMake 4.x、CTest、自包含轻量测试 runner、Winsock/BSD sockets、Win32/POSIX process、内嵌 HTML/CSS/JS、GitHub Actions。

## 全局约束

- Canonical simulation 为 60 tick/s，只接收 frame 和 inputs，不读取 wall clock、OS、线程调度或全局随机。
- Canonical state 不包含普通 `float`/`double`；1 world unit = 1,024 integer subunits；checked 64-bit intermediate 后写回有界 32-bit。
- 两个 Peer 不共享 World、Remote Input、Snapshot、Rollback Decision 或 Hash；测试只能比较公开报告。
- Snapshot/history capacity 256；maximum rollback window 120；projectile capacity 64；packet <= 1,200 bytes；input redundancy <= 32。
- Protocol 和 replay 逐字段 little-endian，未知版本/type、截断、损坏、超长均 fail closed，并返回 typed error + context。
- CRC-32/ISO-HDLC 与 64-bit FNV-1a 只保证误损坏检测/确定性身份，不宣称安全认证、密码学安全或反作弊。
- PCG32 算法版本固定并写入报告；相同 seeds 的进程内 packet schedule、report 和 final hash 必须逐字节稳定。
- 所有队列、数组、文件、运行时间、线程和子进程有界；无线程 detach；RAII cleanup。
- CMake 是跨平台权威；MSVC `/W4 /permissive- /EHsc`，GCC/Clang `-Wall -Wextra -Wpedantic`。
- 不修改 RollbackLab 外的仓库；不合并、不 tag、不 release、不删除远端分支、不保存凭据。

---

### Task 1: PACT-00 Bootstrap、main 基线与真实分支谱系

**文件：**

- 创建: `README.md`, `LICENSE`, `.gitignore`, `.gitattributes`, `AGENTS.md`
- 创建: `CMakeLists.txt`, `CMakePresets.json`, `cmake/RollbackLabWarnings.cmake`
- 创建: `tests/test_main.cpp`, `tests/test_smoke.cpp`, `include/rollback_lab/version.hpp`
- 创建: `.github/workflows/ci.yml`
- 修改: `tasks/20260903-215400-rollback-netcode-0.1/progress.md`

**接口：**

- 产出: `rollback_lab::kSimulationVersion`, `rollback_lab::kProtocolVersion`；可发现 CTest smoke target；`main` baseline SHA；`feat/rollback-netcode-0.1` 基于 `origin/main`。

- [x] **Step 1: 锁定基线验收并建立 smoke RED**

```cpp
RL_TEST(version_contract_is_nonzero) {
    RL_CHECK(rollback_lab::kSimulationVersion == 1U);
    RL_CHECK(rollback_lab::kProtocolVersion == 1U);
}
```

- [x] **Step 2: 配置 RED，确认缺少版本头/target 导致预期失败**

运行: `cmake --preset msvc-debug && cmake --build --preset msvc-debug`

预期: FAIL，因为 `rollback_lab/version.hpp` 或测试 target 尚不存在；把输出保存到 `evidence/PACT-00/red.txt`。

- [x] **Step 3: 最小 GREEN**

建立静态库、CLI 与轻量 runner；preset 覆盖 MSVC Debug/Release、Ninja Clang Debug/Release、ASan/UBSan、tests、benchmark。README 首屏写 rollback 数据流、最短构建/演示命令、限制和 AI 声明占位为“artifacts produced on feature branch”，不伪造结果。

- [x] **Step 4: 基线验证**

运行: `cmake --preset msvc-debug; cmake --build --preset msvc-debug; ctest --preset msvc-debug --output-on-failure`

预期: configure/build 成功，1+ smoke tests PASS，零项目警告。

- [x] **Step 5: Git/GitHub 基线和分支**

```powershell
git init -b main
git add .
git commit -m "chore: bootstrap rollback lab"
gh repo create Iviesever/rollback-lab-cpp --public --source . --remote origin --push --description "Deterministic C++23 rollback netcode lab with fixed-tick simulation, input prediction, latency/loss emulation, UDP verification, desync diagnosis, replays, and an interactive timeline."
git fetch origin main
git switch -c feat/rollback-netcode-0.1 origin/main
```

记录 `git rev-parse origin/main` 为 base SHA，确认 `git merge-base HEAD origin/main` 相同；提交后更新 progress/evidence。

---

### Task 2: PACT-10 确定性模拟与 canonical hash

**文件：**

- 创建: `include/rollback_lab/core/{error,frame,checked_math,pcg32,hash}.hpp`
- 创建: `include/rollback_lab/simulation/{input,state,simulation,scripted_input}.hpp`
- 创建: `src/core/{checked_math,hash}.cpp`
- 创建: `src/simulation/{state,simulation,scripted_input}.cpp`
- 创建: `tests/unit/{frame_test,checked_math_test,simulation_test,golden_trace_test}.cpp`

**接口：**

- 产出: `Result<T>`, `FrameNumber`, `InputFrame`, `InputPair`, `WorldState`, `SimulationVariant`, `simulate_frame`, `serialize_canonical`, `hash_canonical`, `scripted_input`。

```cpp
Result<WorldState> simulate_frame(const WorldState& before,
                                  FrameNumber frame,
                                  const InputPair& inputs,
                                  SimulationVariant variant = SimulationVariant::canonical);
std::vector<std::byte> serialize_canonical(const WorldState& state);
std::uint64_t hash_canonical(const WorldState& state);
InputFrame scripted_input(std::uint64_t scenario_seed, FrameNumber frame, PlayerId player);
```

- [x] **Step 1: 写 simulation RED tests**

覆盖：同 seed/input 的逐帧 hash 一致；frame 必须等于 state boundary；四边 clamp；对向输入抵消；projectile 64 上限和稳定 ID；collision/damage/score/respawn；cooldown；checked overflow typed failure；canonical bytes 无 padding/float。

```cpp
RL_TEST(simulation_repeats_identical_hash_history) {
    const auto first = run_scripted_world(0xC0FFEEU, 600U);
    const auto second = run_scripted_world(0xC0FFEEU, 600U);
    RL_CHECK(first.hashes == second.hashes);
    RL_CHECK(first.final_state == second.final_state);
}
```

- [x] **Step 2: 验证 RED**

运行: `cmake --build --preset msvc-debug; ctest --preset msvc-debug -R "frame|checked_math|simulation|golden" --output-on-failure`

预期: FAIL 于缺失生产 API，而不是测试 runner/config 错误。

- [x] **Step 3: 最小 GREEN**

实现 1,024-scale integer movement；固定 arena/player/projectile capacities；stable-ID ascending loops；widened checked arithmetic；明确 input conflict policy；逐字段 canonical serializer；FNV-1a；PCG32 scripted inputs。`SimulationVariant::damage_bias` 只供受控 desync scenario 使用，默认路径不读取测试状态。

- [x] **Step 4: focused 和 full regression**

运行上述 focused CTest，再运行 `ctest --preset msvc-debug --output-on-failure`。预期全部 PASS；将黄金 hash/bytes 写入测试常量并记录工具链。

- [x] **Step 5: 证据与提交**

保存 RED/GREEN/full logs、更新 matrix/progress，提交 `feat: add deterministic arena simulation`。

---

### Task 3: PACT-20 Snapshot ring 与 rollback session

**文件：**

- 创建: `include/rollback_lab/netcode/{frame_ring,metrics,session}.hpp`
- 创建: `src/netcode/session.cpp`
- 创建: `tests/unit/{frame_ring_test,rollback_session_test}.cpp`
- 创建: `tests/integration/convergence_test.cpp`

**接口：**

- 消费: Task 2 的 `WorldState`, `InputFrame`, `simulate_frame`, `hash_canonical`。
- 产出: 独立 `RollbackSession`, `RollbackMetrics`, `AdvanceResult`, `CorrectionResult`, confirmed input/hash exports。

```cpp
Result<AdvanceResult> RollbackSession::advance(InputFrame local);
Result<CorrectionResult> RollbackSession::ingest_remote(InputFrame remote);
Result<void> RollbackSession::observe_remote_confirmation(FrameNumber frame,
                                                          std::uint64_t remote_hash);
SessionReport RollbackSession::report() const;
```

- [x] **Step 1: 写 ring/session RED tests**

覆盖 ring wrap、missing/stale key、120 boundary；zero latency zero rollback；last-known match no rollback；mismatch exact earliest frame；multiple late inputs coalesce；restore pre-frame snapshot；local input/event not double-applied；metrics exact；confirmed hash convergence；too-old input fail closed。

```cpp
RL_TEST(late_mismatch_restores_earliest_boundary_once) {
    RollbackSession session(make_config(PeerId::a));
    advance_with_missing_remote(session, 12U);
    RL_REQUIRE(session.ingest_remote(input(5U, PeerId::b, Button::left)).ok());
    const auto correction = session.flush_corrections();
    RL_CHECK(correction.rollback_from == FrameNumber{5U});
    RL_CHECK(correction.resimulated_frames == 7U);
}
```

- [x] **Step 2: 运行 RED**

运行: `cmake --build --preset msvc-debug; ctest --preset msvc-debug -R "frame_ring|rollback|convergence" --output-on-failure`

预期: FAIL 于未定义 ring/session 行为。

- [x] **Step 3: 最小 GREEN**

用固定 `std::array<Slot,256>` + frame tags 实现 histories/snapshots/hashes；session 内部保存 local/remote/predicted input；ingest 只标脏，flush 一次恢复最早 boundary 并重演；event range replace；confirmed boundary 只在双方实际 input 连续时推进。

- [x] **Step 4: 独立性回归**

集成测试只通过 public input/packet façade 驱动两个 session，编译期禁止复制 session，运行时比较 report/replay exports。运行 focused 与 full CTest，预期全部 PASS。

- [x] **Step 5: 证据与提交**

记录 metric vectors 和 convergence hash，提交 `feat: implement bounded rollback sessions`。

---

### Task 4: PACT-30 二进制协议与 seeded transport

**文件：**

- 创建: `include/rollback_lab/protocol/{bytes,crc32,packet,codec,sequence_window}.hpp`
- 创建: `src/protocol/{crc32,codec,sequence_window}.cpp`
- 创建: `include/rollback_lab/transport/{config,event,seeded_transport}.hpp`
- 创建: `src/transport/seeded_transport.cpp`
- 创建: `tests/protocol/{codec_test,truncation_test,sequence_test,random_bytes_test}.cpp`
- 创建: `tests/unit/seeded_transport_test.cpp`
- 创建: `tests/fuzz/protocol_fuzz_smoke.cpp`

**接口：**

- 消费: input/frame/hash types。
- 产出: packet model/codec, sequence classifications, logical-time deliveries and trace events。

```cpp
Result<std::vector<std::byte>> encode_packet(const Packet& packet);
DecodeResult decode_packet(std::span<const std::byte> bytes);
SequenceDisposition SequenceWindow::observe(std::uint32_t sequence);
Result<void> SeededTransport::send(Endpoint from, Endpoint to,
                                   std::span<const std::byte> bytes, LogicalTick now);
std::vector<Delivery> SeededTransport::deliver(LogicalTick now);
```

- [x] **Step 1: 写 protocol/transport RED tests**

Codec 覆盖 hello/input/hash/goodbye round trip、golden bytes、每 byte boundary 截断、magic/version/type/count/length/CRC、random bytes。Sequence 覆盖 duplicate/new out-of-order/stale/wrap。Transport 覆盖 0/1/5/20% loss、jitter、reorder、duplicate、burst、queue overflow、age timeout、同 seed schedule/report byte identity。

- [x] **Step 2: 运行 RED**

运行: `cmake --build --preset msvc-debug; ctest --preset msvc-debug -R "protocol|transport" --output-on-failure`

预期: FAIL 于缺少 codec/transport。

- [x] **Step 3: 最小 codec GREEN**

实现 cursor-based checked LE reader/writer；固定 header；<=32 input records；可选 confirmed hash；payload length；trailing CRC-32/ISO-HDLC。所有 decode failure 返回 `ErrorCode`、offset、context enum，不用字符串分支。

- [x] **Step 4: 最小 transport GREEN**

每 send 用 PCG32 决定 drop/duplicate/jitter/reorder/burst；scheduled delivery key 为 `(tick,reorder_rank,ordinal,copy)`；bounded packet/byte queue；overflow policy 配置为 typed fail 或 drop-oldest 并计数。

- [x] **Step 5: fuzz smoke 与回归**

运行 100,000 deterministic random/mutated byte inputs，预期 decode 永不 crash/越界，成功 decode 必须 encode/decode canonical round trip。运行 focused/full CTest。

- [x] **Step 6: 证据与提交**

保存 golden packet、fuzz counts、transport checksum，提交 `feat: add strict protocol and seeded transport`。

---

### Task 5: PACT-40 Replay、report、trace 与 desync

**文件：**

- 创建: `include/rollback_lab/replay/{format,verify}.hpp`, `src/replay/{format,verify}.cpp`
- 创建: `include/rollback_lab/report/{run_report,canonical_json,trace,desync}.hpp`
- 创建: `src/report/{canonical_json,trace,desync}.cpp`
- 创建: `src/cli/{arguments,commands}.cpp`, `include/rollback_lab/cli/commands.hpp`
- 创建: `tests/unit/{replay_test,report_test,desync_test}.cpp`
- 创建: `tests/integration/simulate_test.cpp`

**接口：**

- 产出: binary replay, stable JSON report/trace, desync diagnostic, and `simulate/replay/verify/benchmark/compare` command handlers。

```cpp
Result<std::vector<std::byte>> encode_replay(const Replay& replay);
Result<Replay> decode_replay(std::span<const std::byte> bytes);
Result<ReplayVerification> verify_replay(const Replay& replay);
Result<std::string> canonical_json(const RunReport& report);
Result<DesyncDiagnostic> compare_confirmed_hash(const HashObservation& remote);
```

- [x] **Step 1: 写 replay/report/desync RED tests**

覆盖 replay exact reconstruction、unsupported/corrupt/truncated fail；report fixed key order and timing exclusion from identity；trace event/frame bounds；controlled variant earliest divergence；speculative mismatch no desync；diagnostic input/state/version/seed fields。

- [x] **Step 2: 运行 RED**

运行: `cmake --build --preset msvc-debug; ctest --preset msvc-debug -R "replay|report|desync|simulate" --output-on-failure`

- [x] **Step 3: 最小 GREEN**

复用 checked byte/CRC primitives 实现 replay；显式 JSON writer 固定字段顺序和 escaping；hash exchange 只接受 <= local confirmed boundary；diagnostic 保留最近 32 input 和小型 state summary。CLI command 只组合 production APIs。

- [x] **Step 4: CLI 和回归**

运行 `rollback_lab simulate --scenario default --out artifacts/run`，再 `rollback_lab replay artifacts/run/input.rlr`、`verify`、`compare report report`、两个 benchmark。预期 hash 一致、schema 合法、命令返回 0。运行 full CTest。

- [x] **Step 5: 证据与提交**

提交 `feat: add replay reports and desync diagnosis`。

---

### Task 6: PACT-50 真实 localhost UDP 多进程

**文件：**

- 创建: `include/rollback_lab/transport/{udp_socket,process}.hpp`
- 创建: `src/transport/{udp_socket_win32,udp_socket_posix,process_win32,process_posix}.cpp`
- 创建: `include/rollback_lab/udp/{relay,peer,demo}.hpp`
- 创建: `src/udp/{relay,peer,demo}.cpp`
- 创建: `tests/udp/udp_demo_test.cpp`
- 创建: `tests/udp/udp_negative_test.cpp`

**接口：**

- 消费: packet codec, rollback session, replay/report。
- 产出: move-only sockets/processes and `relay`, `peer`, `udp-demo` modes。

```cpp
Result<int> run_relay(const RelayConfig& config);
Result<int> run_peer(const PeerConfig& config);
Result<UdpDemoResult> run_udp_demo(const UdpDemoConfig& config);
```

- [x] **Step 1: 写 UDP RED integration tests**

正常场景断言两个 peer 是不同 PID、opaque relay、confirmed target、equal hash/input logs、replay success、zero exits、all children reaped。负向覆盖 reserved-port conflict、peer missing timeout、protocol mismatch、child abnormal exit。

- [x] **Step 2: 运行 RED**

运行: `cmake --build --preset msvc-debug; ctest --preset msvc-debug -R "udp" --output-on-failure`

预期: FAIL 于缺少 process/socket/commands，不使用 long sleep。

- [x] **Step 3: socket/process GREEN**

Windows 使用 scoped `WSAStartup`, nonblocking/select-or-poll socket, `CreateProcessW`, job object 或显式 terminate/wait/CloseHandle；POSIX 使用 BSD socket, poll, fork/exec 或 posix_spawn, waitpid/kill。随机端口由 bind port 0 获取并在启动协议中校验。

- [x] **Step 4: relay/peer/demo GREEN**

Hello/ready 握手包含 scenario/protocol/simulation/peer identity；relay 只看 routing envelope，不 decode state；peer 以 wall clock 调度 packet I/O，但 canonical tick 仅消费 inputs；supervisor 以 bounded poll 管理 watchdog 和 teardown。

- [x] **Step 5: 稳定性回归**

正常 demo 连续运行至少 20 次；负向 tests 每类至少一次；验证系统无残留 `rollback_lab` child。运行 full CTest。

- [x] **Step 6: 证据与提交**

保存 PIDs/ports/exits/reports/replay checks，提交 `feat: verify rollback over localhost udp processes`。

---

### Task 7: PACT-60 Viewer、samples 与作品集文档

**文件：**

- 创建: `viewer/template.html`, `viewer/sample-trace.json`, `viewer/sample-viewer.html`, `viewer/screenshot.png`
- 创建: `samples/{report.json,replay.rlr,desync-diagnostic.json}`
- 创建: `docs/{ARCHITECTURE,ROLLBACK_ALGORITHM,DETERMINISM_CONTRACT,PROTOCOL,UDP_DEMO,REPLAY_FORMAT,DESYNC_DIAGNOSIS,BENCHMARKING,TESTING,CODE_WALKTHROUGH,INTERVIEW_GUIDE,LIVE_CHANGE_DRILLS,KNOWN_LIMITATIONS,AI_ASSISTANCE}.md`
- 修改: `README.md`
- 创建: `tests/integration/viewer_generation_test.cpp`

**接口：**

- 消费: real `Trace` and artifacts from Tasks 5-6。
- 产出: `write_viewer(trace,path)` and checked-in reproducible portfolio artifacts。

- [ ] **Step 1: 写 viewer generation RED**

测试 sample viewer 自包含、无 CDN/http imports、内嵌 trace identity 匹配 source trace、必需 control/metric/marker IDs 存在、HTML escaping 正确、trace size under documented bound。

- [ ] **Step 2: 运行 RED**

运行: `ctest --preset msvc-debug -R viewer --output-on-failure`，预期缺少 generator/template 而 FAIL。

- [ ] **Step 3: 最小 GREEN viewer**

canvas/SVG 绘制双方和 projectile；range scrubber；play/pause；step ±1；frame/HP/score/hash；predicted/confirmed shading；rollback range；drop/delay/reorder/duplicate markers；desync marker；responsive and reduced-motion CSS。数据只来自嵌入的 production trace。

- [ ] **Step 4: 生成 samples 和完整文档**

用 final CLI commands 生成而非手写 sample data。Interview Guide 覆盖目标列出的 16 个主题；Live Change Drills 至少 12 个；AI Assistance 准确写明用户目标/边界与 Codex GPT-5.6 Sol 实现责任以及用户未手写代码。

- [ ] **Step 5: 浏览器 QA**

通过浏览器打开 `sample-viewer.html`：检查 console 0 error；拖动 scrubber；step/play；比较 rollback markers 与 trace；检查 1440x900 和 390x844；保存可提交 screenshot。运行 full CTest。

- [ ] **Step 6: 证据与提交**

保存 browser observations/screenshots/checksums，提交 `docs: add interactive rollback timeline and portfolio guide`。

---

### Task 8: PACT-70 Property sweep、跨工具链 hardening 与 Draft PR

**文件：**

- 创建: `tests/property/scenario_sweep_test.cpp`
- 创建: `tools/{run-verification.ps1,download-llvm.ps1,check-children.ps1}`
- 修改: `.github/workflows/ci.yml`, `README.md`, verification/progress/evidence records
- 创建: `tasks/20260903-215400-rollback-netcode-0.1/evidence/final/*`

**接口：**

- 消费: 全部生产 API 和 CLI。
- 产出: final verification bundle, exact PR body, clean pushed branch, Draft PR。

- [ ] **Step 1: 写 property RED invariant**

```cpp
RL_TEST(property_sweep_10000_seeds_is_bounded_and_repeatable) {
    const auto result = sweep(0U, 10'000U);
    RL_CHECK(result.crashes == 0U);
    RL_CHECK(result.deadlocks == 0U);
    RL_CHECK(result.unbounded_failures == 0U);
    RL_CHECK(result.successes + result.declared_failures == 10'000U);
    RL_CHECK(result.identity_digest == sweep(0U, 10'000U).identity_digest);
}
```

- [ ] **Step 2: 运行 RED 后最小 GREEN harness**

先确认 test 因缺少 sweep API/target 正确失败；实现 bounded config generation、failure classification、identity digest 和 progress heartbeat，不放宽已有 production limits。

- [ ] **Step 3: Windows compiler matrix**

运行 MSVC Debug/Release configure/build/CTest、clean rebuild。若 PATH 无 Clang/GCC，则下载官方、version-locked portable LLVM 到项目忽略目录 `.tools/llvm-<version>`，校验 SHA256，不修改系统 PATH，运行 Clang Debug/Release。

- [ ] **Step 4: Sanitizer/fuzz/benchmark**

在 portable Clang 支持范围运行 ASan/UBSan；若 Windows runtime 不支持某组合，保留编译器证据和明确限制，不把未运行写成通过。运行 100,000+ fuzz smoke、两个 benchmark、UDP stress、replay/desync samples。

- [ ] **Step 5: 10,000 seeds 与完整审计**

运行 exact 10,000 sweep；重复 identity；`rg` 审计 canonical modules 中 `chrono|random_device|float|double`，审计 peer state copying；核对 30 条 completion conditions；刷新所有受影响 regression。

- [ ] **Step 6: 最终提交与远端**

确认 `git diff --check`、CTest 全绿、artifact checksums、docs 与实现一致。提交 `test: harden rollback lab release candidate`，推送 `feat/rollback-netcode-0.1`。

- [ ] **Step 7: 创建 Draft PR**

PR body 包含 product、architecture、exact base/head、test matrix/counts、10,000 seeds、UDP、sanitizer/fuzz、benchmark、viewer screenshot、limitations、AI assistance。调用 `gh pr create --draft --base main --head feat/rollback-netcode-0.1`，不 merge/tag/release。

- [ ] **Step 8: 完成交付证明**

重新读取 remote refs/PR、记录 ahead/behind/commit list、运行 final clean status。只有 verification matrix 全 Passed（不支持项按目标允许且证据明确）且全部 P0 条件满足后，才把目标标为 complete 并给出规定的最终报告字段。

## 计划自我审查

- 规范覆盖度：目标文件的 Simulation、Rollback、Transport、Protocol、Replay、Desync、UDP、Viewer、Quality、Docs、AI、Git/PR 均映射到 Task 1-8 和 V-rows。
- 占位符扫描：实现任务中没有 TBD、TODO、“稍后实现”或未绑定测试的泛化步骤。
- 类型一致性：frame boundary、inputs、hash、session、packet、replay、report、trace、UDP APIs 与技术蓝图相同；后续任务只消费先前明确产出的接口。
- 执行模式：用户已在权威目标中要求持续自主执行，故选择 `executing-tasks`，不派发子 Agent。
