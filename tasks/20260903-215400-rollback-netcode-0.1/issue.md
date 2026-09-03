# Rollback Netcode 0.1 — 需求单

## 原始意图

用户要求从零交付一个公开的现代 C++23 作品集项目，用一个小型二维双人 Arena 证明真实 rollback netcode 数据流。项目必须同时覆盖确定性模拟、预测与回滚、恶劣网络仿真、严格二进制协议、输入回放、反同步诊断、真实 localhost UDP 多进程验证、机器可读报告和浏览器时间线。

权威需求是 `C:\Users\Iviesever\.codex\attachments\c51167df-78d5-4b69-88e0-e255a798aeb0\goal-objective.md`。本记录只压缩意图，不降低其中任何 P0 条件。

## 用户价值

- 在 30 秒内让读者理解 fixed tick、prediction、late input、restore、resimulation 和 confirmed convergence 的关系。
- 用可重复的测试和真实 UDP 路径证明工程实现，而不是只展示概念或 README。
- 为面试提供可讲解、可现场修改、诚实披露 AI 参与的完整样本。

## 成功标准

- 两个生产 Peer 仅通过版本化 packet 交换数据，绝不共享 world、input history、snapshot 或 rollback decision。
- 相同确认输入在各编译器和 replay 中生成相同逐帧 canonical hash。
- 进程内 seeded transport 与真实 localhost UDP 都被验证；网络预测差异不会被误报成 canonical desync。
- 全部 30 条最终完成条件都有当前 HEAD 的新鲜证据。
- `main` 只包含最小基线，功能在 `feat/rollback-netcode-0.1`，分支推送后创建 Draft PR，但不合并、打 tag 或发布。

## 非目标

不实现商业游戏、引擎集成、3D、完整物理/ECS、公网服务、匹配/NAT、账号/数据库/云、加密/反作弊、语音、Lobby 或大型第三方框架。

