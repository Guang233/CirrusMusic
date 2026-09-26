# ADR-0002 · 模块化单体与单向数据流

- 日期：2026-09-26
- 状态：Proposed
- 负责人：架构／维护者

## 背景

功能范围大但项目从零开始，预计由单人或小团队维护。需要避免大对象堆业务，也不需要分布式系统的成本。

## 决策

使用单桌面进程，按 domain、application、infrastructure、platform、presentation、app 分层。应用层定义 ports，根装配器注入实现。播放、会话、队列各有一个权威协调器；QML 以类型化 ViewModel 和 ListModel 获取状态。

GUI、网络 worker、DB worker 与有界任务池职责固定。跨边界传值／令牌，不共享 QSqlQuery、QNetworkReply 或 UI 对象。业务结果显式携带错误和 sessionEpoch。

## 备选

- 单个 AppManager + QML HTTP：上手快但耦合与竞态不可控，不选。
- 本地服务器／微服务：IPC、部署、认证和资源成本没有当前收益，不选。
- 重型 DI、全局 event bus、运行时插件架构：早期抽象负担过高，不选。

## 后果

可用 fake ports 离线开发和测试；需要遵守 target 依赖、所有权及异步契约。不是为每个函数强行建立接口，接口集中在外部副作用和可替换策略。

## 验证与复议

M1 完成 fake 数据垂直切片和依赖边界测试；M2 用乱序响应验证唯一状态。只有出现真实的进程隔离安全需求、插件生态或跨端复用需求，才评审拆进程／新架构。

详见[架构](../03-system-architecture.md)。
