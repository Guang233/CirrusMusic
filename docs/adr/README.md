# 架构决策记录（ADR）

ADR 记录重要选择的背景、候选项、后果和复议条件。以下为 2026-09-26 基线提案；维护者已经明确的产品约束在记录中单独说明，不把实现建议伪装成已经验证。

| ADR | 决策 | 状态 | 验证节点 |
| --- | --- | --- | --- |
| [0001](0001-qt-cpp-stack.md) | C++20、Qt Quick/QML 与固定工具链 | QML 已确定；版本／依赖提案 | M0 |
| [0002](0002-modular-monolith.md) | 模块化单体与受控异步边界 | 提案 | M1 |
| [0003](0003-direct-official-api.md) | 官方 API 内置直连与独立协议适配器 | 直连已确定；实现分层提案 | M0/M2 |
| [0004](0004-playback-backend.md) | Qt Multimedia 首选、port 隔离 | 条件性提案，需 PoC | M0 |
| [0005](0005-custom-qml-controls.md) | 自有 Controls 模块，复用 Templates | 自绘方向已确定；工程路线提案 | M1 |
| [0006](0006-storage-and-secrets.md) | SQLite profile 隔离与系统密钥环 | 提案 | M1/M2 |
| [0007](0007-linux-distribution.md) | Linux 优先、Flatpak 主交付 | 提案 | M0/M4 |

状态流转：Proposed → Accepted → Superseded/Deprecated。采纳时补维护者和证据；被替代时新建 ADR 并互链，不删除历史。新增记录使用[模板](../templates/adr.md)。
