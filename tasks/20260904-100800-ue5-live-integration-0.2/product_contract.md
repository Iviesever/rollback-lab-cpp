# RollbackLab 0.2.0：UE 5.8 Live Integration `/goal`

在现有 `<repository-root>` 仓库中，将已合并的 RollbackLab 0.1.0 推进为 RollbackLab 0.2.0 Release Candidate：把现有引擎无关的确定性 C++23 回滚核心封装为可嵌入 SDK，通过稳定的 C ABI 接入 Unreal Engine 5.8，并交付一个可交互、可自动验证、可打包的双视角实时回滚竞技场。

不要创建新 GitHub 仓库，不要复制第二套模拟逻辑，不要把 Unreal Actor 状态当作权威模拟状态。持续自主工作，直到所有 P0 验收条件通过、完整验证证据已生成、功能分支已推送并建立 Draft PR；不要在只完成 C API、只通过 Editor、只做出视觉原型或只通过编译时提前结束。

## 一、恢复真实状态与工作边界

1. 当前仓库固定为：
```text
<repository-root>
```

1. 首先完整读取并遵守：
```text
AGENTS.md
README.md
docs/ARCHITECTURE.md
docs/ROLLBACK_ALGORITHM.md
docs/DETERMINISM_CONTRACT.md
docs/PROTOCOL.md
docs/UDP_DEMO.md
docs/REPLAY_FORMAT.md
docs/DESYNC_DIAGNOSIS.md
docs/TESTING.md
docs/KNOWN_LIMITATIONS.md
docs/AI_ASSISTANCE.md
tasks/20260903-215400-rollback-netcode-0.1/**
```

1. 执行 fresh Git/GitHub 检查：
```text
git status
当前分支
origin/main 当前真实 SHA
最近 commits
PR #1 的真实合并状态
GitHub checks
Releases
Tags
Open issues
```

1. 不盲信历史提示词中的 SHA、测试数量或产物。所有结论以当前仓库和新鲜命令为准。
2. 工作树不干净时先识别变更来源，禁止覆盖未知用户修改。
3. 从真实最新 `origin/main` 创建：
```text
feat/ue5-live-integration-0.2
```

1. 不创建第二份 clone、worktree 或平行集成 checkout。
2. 不修改：
```text
<excluded-repository-SeedForge>
<excluded-repository-MQB>
任何其他仓库
```

1. Unreal Engine 路径为：
```text
<read-only-engine-root>
```

该目录是只读基础设施，绝不修改 Engine 文件。

10. 所有源码、SDK、临时构建、UE 项目、日志、截图、报告和打包产物必须保持在 `<repository-root>` 内。
11. 使用一个主 Agent，最多两个任务互不重叠的子 Agent。子 Agent可以分别负责：

\- C ABI、CMake、SDK 与 ABI 审查；
\- UE 生命周期、视觉表现、自动化与打包审查。

12. 同一时间只允许一个进程运行 UBT、UAT、UnrealEditor、BuildPlugin、Cook、Stage 或 Package。
13. 本 Goal 允许：

\- 修改当前功能分支；
\- 构建、测试和打包；
\- 创建局部、版本锁定的工具依赖；
\- commit；
\- push；
\- 创建或更新 Draft PR。

14. 本 Goal 不允许：

\- 合并 PR；
\- 删除远端分支；
\- 创建或移动 Tag；
\- 发布 GitHub Release；
\- 修改系统级环境；
\- 将凭据、用户信息或机器专有绝对路径提交到仓库。

15. 在生产修改前创建：
```text
tasks/<timestamp>-ue5-live-integration-0.2/
├─ product_contract.md
├─ integration_decision.md
├─ implementation_plan.md
├─ verification_matrix.md
├─ progress.md
└─ evidence/
```

1. 每个 PACT 后更新 `progress.md`，记录：
```text
Current PACT
Exact HEAD
Changed contracts
Commands
Passed/failed results
Artifact paths
Blockers
Quota/time state
Next action
```

上下文压缩或会话恢复时必须从这些文件恢复真实状态。

## 二、0.1.0 不可破坏基线

开始新功能前运行现有最低成本完整基线，至少包括：
```text
MSVC Debug configure/build/CTest
MSVC Release configure/build/CTest
当前 structured fuzz smoke
当前 property smoke
一份标准 simulate
Replay verification
Desync demo
一次真实 localhost UDP demo
Viewer sample generation
```

记录准确命令、退出码、测试结果和产物。

以下 0.1.0 合同不可静默改变：

- 60 Tick/s-compatible canonical simulation；
- Canonical State 无 float/double；
- 1/1024 world-unit integer scale；
- 两个 Peer 不共享 World、Input History、Snapshot 或 Rollback Decision；
- 输入预测、Earliest Dirty Frame、Snapshot Restore、Resimulation 语义；
- 120 帧 fail-closed rollback window；
- 256 帧历史；
- 协议 v1 字段和严格 Decoder；
- Replay v1；
- Confirmed-only Desync；
- 现有 Golden Hash、Sample Report、Sample Replay 与既有验证身份；
- 已声明的 0.1.0 Known Limitations。

如果新 SDK 需要调整内部文件结构，必须证明现有公开 C++ API、CLI 命令、报告和协议语义保持兼容。协议或 Replay 版本只有在语义确实改变时才能升级，不得为版本号而升级。

## 三、唯一产品目标

交付下面的数据流：
```text
UE Input / Wall Clock / Rendering
              │
              ▼
       UE Rollback Bridge
              │
        Stable C ABI v1
              │
              ▼
   Existing RollbackLab Core
              │
  ┌───────────┴───────────┐
  ▼                       ▼
Peer A Session        Peer B Session
  │                       │
  └──── Seeded Packets ───┘
              │
              ▼
Read-only Presentation Snapshots
              │
              ▼
Side-by-side UE Arenas + Rollback HUD
```

必须证明：
```text
同一 Scenario/Input/Transport
CLI final hash
=
C API final hash
=
UE packaged final hash
```

UE 中不得重写或近似实现：

- Simulation；
- Rollback；
- Prediction；
- Snapshot；
- Protocol；
- Transport decisions；
- Canonical Hash。

## 四、PACT-80：集成合同与可行性决策

在写完整实现前，完成一轮有界的集成 Spike，并在 `integration_decision.md` 中比较：

### 方案 A：共享库 + C ABI
```text
rollback_lab_c.dll
rollback_lab_c.lib
rollback_lab_c.h
```

优点是隔离 C++/STL ABI，缺点是需要正确 Stage DLL。

### 方案 B：静态库 + C ABI
```text
rollback_lab_c.lib
rollback_lab_c.h
```

优点是部署更简单，缺点是需要严格匹配 MSVC Toolset 和 CRT。

### 禁止方案

- 在 UE Module 中复制 RollbackLab 源码形成第二套所有权；
- 直接跨 Unreal Module 暴露 STL 容器、异常、`std::unique_ptr`、`std::optional` 或 C++ 类布局；
- 为迁就 UE 而把原项目整体降级为一套不同的模拟；
- 将 CLI 子进程当作 P0 的唯一 UE 集成方式。

优先选择共享库+C ABI。只有实际 BuildPlugin/Package Spike 证明 DLL Staging 是阻塞项时，才允许使用静态库+C ABI。无论选择哪种，公开边界都必须保持 C ABI。

锁定：

- API Ownership；
- Error Model；
- Versioning；
- Struct Layout；
- Runtime/CRT；
- DLL/Library Staging；
- UE fixed-step adapter；
- Automation；
- Artifact Manifest；
- Non-goals。

完成后单独提交。

## 五、PACT-81：可嵌入 SDK 与稳定 C ABI

新增或等价实现：
```text
include/rollback_lab/c_api/rollback_lab_c.h
src/c_api/**
tests/c_api/**
cmake/install/export/package logic
scripts/BuildSdk.ps1
scripts/VerifySdk.ps1
```

### C ABI 要求

1. C Header 必须能由真正的 C11 `.c` Consumer 编译，不得要求 C++。
2. 所有公开符号使用稳定前缀，例如：
```text
rl_*
```

1. 使用：

- `uint8_t/uint16_t/uint32_t/uint64_t`；
- 明确枚举底层传输值；
- opaque handle；
- caller-owned output struct；
- caller-supplied buffer 或两阶段 sizing；
- API version；
- `struct_size`；
- 显式 status code。

1. 禁止跨边界暴露：

- STL；
- Unreal 类型；
- C++ 引用；
- 异常；
- 虚函数；
- C++ allocator 所有权；
- 内部可变指针；
- 编译器专有类布局。

1. Handle 必须在 SDK 内创建并在 SDK 内销毁。
2. Null、错误版本、错误 `struct_size`、错误 Peer、错误 Frame、过期输入、超 Rollback Window、缓冲区不足等情况返回 typed status。
3. 不允许异常穿过 ABI；意外异常必须在最外层转为明确 internal failure。
4. 每个 Handle 的线程安全和线程亲和语义必须记录。不要虚假宣称 thread-safe。
5. API 至少支持：
   - 创建/销毁 Session；
   - 推进一帧；
   - 注入 Remote Input；
   - 执行 Correction；
   - 获取当前 World Snapshot；
   - 获取 Confirmed Frame；
   - 获取 Rollback Metrics；
   - 获取 State Hash；
   - 查询版本与能力。
6. 可以增加一个供引擎演示使用的 Dual-Peer Facade，但它必须复用现有生产 Session/Transport，不得拥有第二套算法。
7. 提供：

- C Consumer Smoke；
- C++ Consumer Smoke；
- ABI Version/Size tests；
- invalid-argument tests；
- two-handle isolation；
- direct C++ API versus C ABI parity；
- 100+ 有界场景 Parity Sweep。

1. CMake 至少产出：

- Core Target；
- C ABI Target；
- CLI；
- Tests；
- Install Tree；
- Package Config；
- 独立 `find_package` Consumer。

1. SDK ZIP 必须包含：

- Header；
- Library/DLL；
- Import Library；
- License；
- README；
- Version Manifest；
- exact source Git SHA；
- SHA-256。

1. 不提交生成的 SDK 二进制到 Git 历史；通过忽略的 Artifacts 目录交付。

完成 SDK、Consumer、Parity 和现有完整回归后单独提交。

## 六、PACT-82：UE 5.8 Runtime Plugin

在仓库内创建：
```text
examples/ue5/RollbackArena/
examples/ue5/RollbackArena/Plugins/RollbackLabBridge/
```

建议模块：
```text
RollbackLabBridge
RollbackLabBridgeTests
RollbackArenaDemo
```

职责可调整，但依赖方向必须清楚。

### Bridge 要求

1. UE 只能通过已验证的 C ABI 调用 Core。
2. SDK 的 Header、Library、DLL 和 Manifest 必须由仓库脚本从当前 exact HEAD 构建或 Stage。
3. UBT 配置必须明确：
   - include path；
   - `.lib`；
   - delay-load 或 static link policy；
   - RuntimeDependencies；
   - Win64 configuration；
   - Debug/Development/Shipping 兼容边界。
4. 不依赖用户手工复制 DLL。
5. 错误 SDK 版本、错误 ABI、缺失 DLL、错误 Manifest 或 SHA 不匹配必须 fail closed，并给出明确日志。
6. 创建 UE 侧 RAII/Subsystem Wrapper：
   - UObject 不直接拥有裸 ABI 资源；
   - Handle 在 EndPlay/Deinitialize/Module Shutdown 正确释放；
   - Restart 不泄漏；
   - PIE 多次进入退出不残留；
   - Packaged Exit 不使用已卸载 DLL。
7. UE fixed-step adapter：
   - Canonical Core 固定 60 Tick/s；
   - UE Delta Seconds 仅进入外层 accumulator；
   - 有明确最大 catch-up steps；
   - 长帧不能触发无界 while-loop；
   - 不把被截断的 Wall Clock 写入 Canonical State；
   - Pause、Step 和 Reset 有明确语义。
8. Actor Transform、Velocity、Projectile Visual 只是只读投影，绝不反向写回 Core。
9. 两个 Peer 使用两个不同 Handle，测试证明没有共享内部 World/History。
10. UE Presentation 使用 Engine Basic Shapes 和代码原生材质；不需要 Marketplace 资产。
11. 不要求用户手工创建 Blueprint、拖节点或摆 Actor。必要 `.umap` 必须由仓库提供或脚本可重建。
12. UE Plugin 必须可以独立 BuildPlugin。
13. Plugin 和 Demo 版本更新到 0.2.0 Candidate，但不得发布正式 Release。

先通过 focused Automation，再运行完整 Core 回归，并单独提交。

## 七、PACT-83：双视角实时回滚竞技场

实现一个代码原生的 Live Demo：
```text
┌────────────────────┬────────────────────┐
│ Peer A View        │ Peer B View        │
│                    │                    │
│ Player A/B         │ Player A/B         │
│ Projectiles        │ Projectiles        │
│ Predicted State    │ Predicted State    │
└────────────────────┴────────────────────┘
```

### 必须可见的信息

- Logical Frame；
- Predicted Frame；
- Confirmed Frame；
- State Hash；
- Rollback Count；
- Resimulated Frames；
- Last Rollback From；
- Last Rollback Depth；
- Latency；
- Jitter；
- Loss；
- Reorder；
- Duplicate；
- Scenario Seed；
- Transport Seed；
- SDK/ABI Version。

### 回滚表现

发生 Correction 时至少显示：

- Correction 前的位置 Ghost；
- Correction 后的位置；
- 两者之间的线或方向；
- 短暂 Rollback Flash；
- Rollback 起始帧和重演深度。

不得通过伪造随机闪烁显示“回滚”。视觉事件必须来自真实 Core Correction Result 或 Trace Hook。

### 运行模式

1. Deterministic Auto Demo：
   - 固定输入脚本；
   - 固定 Scenario/Transport Seed；
   - 必须产生至少一次真实 Rollback；
   - 最终 Confirmed State 收敛。
2. Interactive Demo：
   - 玩家可控制 Peer A 的本地输入；
   - Peer B 可以使用确定性脚本；
   - 可以 Pause、Single Step、Reset；
   - 可以切换若干有界网络预设；
   - 不对交互式输入运行作跨机器确定性声明。
3. Desync Demonstration：
   - 通过受控 Simulation Variant；
   - 只在 Confirmed Boundary 报告；
   - 显示 Earliest Divergent Frame；
   - 不把正常预测差异误报为 Desync。

### 性能与所有权

- Presentation 可以 Tick；
- Canonical Simulation 只能按 fixed step 推进；
- Projectile Actor/Component 应复用或有界；
- 不为每个 Tick 创建无界 UObject；
- HUD 查找和 Actor 枚举不得成为每帧无界全世界扫描；
- 任何 Timer、Delegate、Handle 和动态库资源都必须在 Restart/EndPlay 清理。

完成交互、Automation、截图检查后单独提交。

## 八、PACT-84：三路 Parity 与 Packaged Evidence

增加：
```text
scripts/BuildUnrealPlugin.ps1
scripts/BuildUnrealDemo.ps1
scripts/TestUnrealIntegration.ps1
scripts/PackageUnrealDemo.ps1
scripts/VerifyUnrealIntegration.ps1
```

命名可以适配现有约定，但不得重复实现同一 Build 逻辑。

### 自动 Smoke

提供类似：
```text
-RollbackLabSmoke
-RollbackLabScenarioSeed=<uint64>
-RollbackLabTransportSeed=<uint64>
-RollbackLabTrace=<path>
-RollbackLabCaptureDir=<path>
-RollbackLabGitSha=<sha>
```

自动模式必须：

1. 加载真实 SDK；
2. 创建两个独立 Peer；
3. 运行固定场景；
4. 经过真实预测、Packet Delivery、Correction 和 Resimulation；
5. 至少产生一次 Rollback；
6. 达到目标 Confirmed Frame；
7. 双方 Confirmed Hash 相同；
8. 与同输入 CLI 和 C API 报告一致；
9. Replay Verification 通过；
10. 输出固定字段顺序 JSON；
11. 生成至少三张截图：
    - Start；
    - Rollback Correction；
    - Confirmed Convergence 或 Desync；
12. 返回正确进程退出码；
13. Watchdog 超时必须返回非零；
14. 即使失败也尽可能写出 Failure Trace；
15. 不允许直接复制 Peer A State 到 Peer B 制造成功。

### 必须验证

- Core MSVC Debug；
- Core MSVC Release；
- Clang Debug/Release；
- ASan+UBSan；
- 当前 structured fuzz；
- 当前 10,000-seed full property sweep；
- C ABI C Consumer；
- C ABI C++ Consumer；
- C++/C API parity；
- UE Automation；
- Editor Development Build；
- Editor launch/load；
- BuildPlugin：
  - UnrealEditor Development；
  - UnrealGame Development；
  - UnrealGame Shipping；
- Win64 BuildCookRun；
- 普通 packaged EXE；
- packaged Smoke；
- JSON Trace reparse；
- Replay；
- Screenshot materialization；
- DLL/Library staging；
- SDK/Plugin/Demo manifest；
- SHA-256；
- 日志 Error/Warning 审计；
- clean worktree。

不得使用旧 Artifact 充当本轮证据。Manifest 必须绑定同一个 clean exact HEAD。

完成后单独提交。

## 九、PACT-85：有条件冲刺——两个 UE 客户端真实 UDP

只有同时满足以下条件时才能开始：
```text
P0 全部绿色
当前时间早于 2026-09-05 04:00 UTC+8
Codex 界面可见额度仍高于约 22%
```

额度不可见时以时间和 P0 完整度判断，不要暂停询问用户。

目标：
```text
rollback_lab relay
RollbackArena.exe -Peer=A
RollbackArena.exe -Peer=B
```

要求：

1. 三个独立进程。
2. 两个 UE 客户端各自拥有独立 Rollback Session 和表现世界。
3. Relay 只转发 Versioned Packet。
4. 动态 localhost 端口。
5. 握手验证：
   - Protocol Version；
   - Simulation Version；
   - ABI Version；
   - Scenario Identity；
   - Peer Identity。
6. 每个客户端使用本地 Scripted Input，远端输入只能通过 UDP 到达。
7. 必须真实发生 Prediction 和 Rollback。
8. 达到目标 Confirmed Frame 后：
   - 双方 Hash 相等；
   - 双方 Replay 重建一致；
   - 所有子进程退出；
   - 无残留进程。
9. Negative cases 至少覆盖：
   - 缺失 Peer；
   - Version Mismatch；
   - Watchdog；
   - Controlled Desync。
10. 自动 Orchestrator 必须 fail closed。
11. 不允许使用 UE Replication、Iris 或 Network Prediction Plugin 替代 RollbackLab 协议；本 PACT验证的是现有 Core 的真实引擎客户端接入。

如果在功能冻结前仍无法闭环：

- 停止继续扩张；
- 回滚未完成的 PACT-85 生产代码；
- 保留调查 Evidence；
- 不破坏已绿色的 P0；
- 在 Known Limitations 中准确记录延期；
- 不将 UDP-UE 描述为已完成。

完成时单独提交。

## 十、PACT-86：作品集、教学材料与独立审计

更新或新增：
```text
README.md
docs/C_ABI.md
docs/SDK_PACKAGING.md
docs/UE5_INTEGRATION.md
docs/FIXED_STEP_ADAPTER.md
docs/ENGINE_PRESENTATION_BOUNDARY.md
docs/UE5_TESTING.md
docs/CODE_WALKTHROUGH.md
docs/INTERVIEW_GUIDE.md
docs/LIVE_CHANGE_DRILLS.md
docs/KNOWN_LIMITATIONS.md
docs/AI_ASSISTANCE.md
docs/RELEASE_NOTES_0.2_CANDIDATE.md
```

README 首屏必须能在 30 秒内解释：
```text
C++23 Rollback Core
        ↓
Stable C ABI SDK
        ↓
UE 5.8 Runtime Plugin
        ↓
Two Independent Peer Worlds
        ↓
Visible Prediction / Correction / Convergence
        ↓
Packaged Hash-Parity Evidence
```

至少包含：

- Core 最短构建命令；
- SDK 最短构建命令；
- UE Plugin 构建命令；
- Packaged Demo 命令；
- Smoke 命令；
- Viewer/UE 截图；
- 三路 Parity 结果；
- 测试统计；
- 已知限制；
- AI assistance。

Interview Guide 至少回答：

- 为什么不直接把 C++ 类暴露给 UE Module；
- C ABI 解决和没有解决什么；
- DLL、Import Library、Static Library、CRT 分别是什么；
- `struct_size` 与 ABI Version 为什么必要；
- Handle 的资源所有权；
- 为什么 UE Delta Seconds 不进入 Canonical State；
- Fixed-step Accumulator 和 Maximum Catch-up 的权衡；
- Actor Transform 为什么只是 Presentation；
- 两个 Peer 如何证明没有共享状态；
- CLI/C API/UE Hash Parity 如何证明没有第二套算法；
- Prediction Difference 与 Confirmed Desync 的区别；
- 为什么 Packaged EXE 能发现 Editor 测试发现不了的问题；
- 为什么这不是 UE Replication、Iris 或生产网络方案；
- 为什么 localhost UDP 不能证明 WAN Production Readiness；
- AI 完成了什么，用户能诚实声明什么。

Live Change Drills 至少包含 12 个 UE/C ABI 相关练习。

### AI 署名

必须准确记录：

- 用户制定职业目标、产品方向、截止、边界和验收；
- Codex GPT-5.6 Sol 完成架构细化、C ABI、SDK、UE Plugin、Demo、测试、调试、打包、视觉检查和文档；
- 用户本轮不参与手写交付代码；
- 不得声称独立手写；
- 面试前用户必须亲自理解 C ABI、fixed-step、presentation ownership 和 rollback flow，并完成至少一个练习。

### 独立审计

由全新、只读审查上下文检查完整 `origin/main...HEAD`：

- ABI；
- CRT；
- Handle 生命周期；
- UE Module Shutdown；
- DLL Staging；
- PIE Restart；
- fixed-step；
- Peer 隔离；
- Hash Parity；
- Artifact Freshness；
- Smoke 真实性；
- 文档一致性；
- AI 署名。

只修复确认的 Blocker/High 和低风险直接相关 Medium，不在最后审计中加入新能力。

## 十一、严格非目标

禁止：

- 新 GitHub 仓库；
- 修改 SeedForge 或 MQB；
- 复制第二套 Rollback Core；
- UE Replication；
- Iris；
- UE Network Prediction Plugin；
- Dedicated Server；
- Matchmaking；
- NAT Traversal；
- STUN/TURN；
- 公网服务；
- 账号；
- 数据库；
- 加密；
- 反作弊；
- GAS；
- 完整商业游戏；
- 3D 物理引擎；
- 大型角色动画；
- Marketplace 内容；
- 全套菜单；
- SaveGame；
- Cloud Backend；
- 为视觉效果修改 Canonical Simulation；
- 为通过测试复制双方状态；
- 无界队列；
- detached thread；
- 无限重试；
- 通过增大 Sleep/Timeout 掩盖竞态；
- 在 P0 未稳定时做无关重构。

## 十二、修复与验证纪律

每个行为改动遵循：
```text
Acceptance Contract
→ RED
→ Minimal GREEN
→ Focused Regression
→ Relevant Full Regression
→ Evidence
→ Scoped Commit
```

同一故障两次修补后仍失败，必须创建 Root Cause Packet：
```text
Observable symptom
Minimal reproduction
Logs
Known facts
Rejected hypotheses
Ownership boundary
Why the next fix is materially different
```

不要进行第三次猜测式修补。

不得：

- 删除失败测试；
- 跳过测试；
- 降低严格性；
- 硬编码测试数量；
- 静音未知 Warning；
- 把旧 Artifact 当作新结果；
- 只验证 Editor 而省略 Packaged EXE；
- 只验证 Screenshot 文件存在而不检查内容；
- 将机器时间写入确定性身份。

## 十三、时间和额度控制

外部截止：
```text
2026-09-05 15:30 UTC+8
2026-09-05 16:30 JST
```

内部硬停止：
```text
2026-09-05 15:00 UTC+8
```

功能冻结：
```text
2026-09-05 11:30 UTC+8
或额度剩余约 15%
以先发生者为准
```

额度不可程序化读取时，不要暂停或询问；使用时间边界和当前 PACT 完整度。

控制规则：
```text
>22% 且 P0 绿色且早于 04:00
允许开始 PACT-85

18%
停止扩展 PACT-85，只闭环或回滚

15%
停止所有新功能

8%
只修 Blocker、测试、打包、文档校准

3%
停止生产代码修改，只做最终 commit、push、PR、状态核对
```

功能冻结后只允许：

- 修 Blocker；
- 回归；
- Sanitizer/Fuzz；
- BuildPlugin；
- BuildCookRun；
- Packaged Smoke；
- Screenshot；
- Manifest；
- SHA；
- 文档事实校准；
- Draft PR。

不要为了耗尽额度创建第三个项目。

## 十四、最终完成条件

只有同时满足以下条件，才能报告 0.2.0 Goal 达成：

1. 工作基于真实最新 `origin/main`；
2. 使用独立功能分支；
3. 0.1.0 Core、CLI、Protocol、Replay 和 Golden Identity 保持兼容；
4. 稳定 C ABI v1 已完成；
5. 真正的 C Consumer 编译运行；
6. C++ API 与 C API Parity 通过；
7. SDK install/export 和独立 Consumer 通过；
8. SDK ZIP、Manifest 和 SHA 已生成；
9. UE 5.8 Runtime Plugin 使用 C ABI，而不是复制 Core；
10. 两个 UE Peer 拥有独立 Handle；
11. fixed-step adapter 有界；
12. Actor 状态只作 Presentation；
13. 双视角 Arena 可真实交互；
14. 固定 Demo 产生至少一次真实 Rollback；
15. CLI、C API、UE packaged final hash 一致；
16. UE Automation 全部通过；
17. Core 全部现有测试通过；
18. 10,000-seed full sweep 通过；
19. structured fuzz 通过；
20. ASan+UBSan 通过；
21. BuildPlugin 通过；
22. Win64 BuildCookRun 通过；
23. 普通 packaged EXE 可启动；
24. packaged Smoke 返回 0；
25. JSON Trace 可解析；
26. Replay Verification 通过；
27. 截图真实物化并经过视觉检查；
28. DLL/Library 在 Package 中正确 Stage；
29. Artifact 全部绑定同一 clean HEAD；
30. 文档、限制和实际实现一致；
31. AI Assistance 准确；
32. 工作树 clean；
33. 分支已 push；
34. Draft PR 已创建或更新；
35. PR Body 包含：
    - 产品变化；
    - ABI 设计；
    - UE 架构；
    - exact base/head；
    - test matrix；
    - Core/C/UE parity；
    - BuildPlugin；
    - BuildCookRun；
    - packaged smoke；
    - SDK/Plugin/Demo artifacts；
    - screenshots；
    - known limitations；
    - PACT-85 完成或延期状态；
    - AI assistance。
36. 不合并 PR；
37. 不创建 Tag；
38. 不发布正式 Release。

## 十五、最终报告格式

最终报告必须给出：
```text
Repository
Branch
Base SHA
Final HEAD
Ahead/behind
Commit list
C ABI version
SDK linkage decision and reason
C Consumer result
C++/C API parity result
Core test totals
Property sweep result
Fuzz result
Sanitizer result
UE Automation totals
BuildPlugin result
BuildCookRun result
Ordinary packaged launch result
Packaged smoke result
CLI/C/UE hash parity
PACT-85 UDP-UE status
SDK artifact path and SHA
Plugin artifact path and SHA
Demo artifact path and SHA
Trace and screenshot paths
Draft PR number
Independent audit findings
Known limitations
AI authorship statement
Whether merge is recommended
Whether v0.2.0 publication is recommended
User’s shortest next authorization
```

持续自主工作，不要在只完成计划、只完成 SDK、只在 Editor 中显示两个方块、只通过编译或只创建 Draft PR 时提前结束。
