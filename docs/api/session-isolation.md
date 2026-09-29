# 响应解码与会话隔离的架构边界

> 本页记录已实现的基础组件及尚未连接的边界，不代表登录功能已经完成。

## 1. 编译依赖

| Target | 已实现职责 | 依赖 |
| --- | --- | --- |
| cirrus_domain | 会话、AccountIdentity 及扫码 challenge／status 值类型 | Qt Core |
| cirrus_application | SessionCoordinator、QrLoginService、IQrLoginGateway 与脱敏结果 | cirrus_domain；不依赖 Network／协议／加密库 |
| cirrus_netease_protocol | 编码、加解密、解压、JSON envelope 检查 | Qt Core、OpenSSL、zlib |
| cirrus_netease_network | 有界异步 HTTP，不管理账号状态 | 协议路由工具、Qt Network |
| cirrus_netease_session | 当前 epoch 的内存 Cookie 存储 | cirrus_domain、Qt Network；不反向控制应用协调器 |
| cirrus_netease_api | operation 契约及捕获上下文的请求链 | protocol/network/session；不依赖 application |
| cirrus_netease_auth | QR port 实现、业务映射、key 归属与单请求限制 | application、netease_api |

应用层没有具体 NetEaseGateway、Cookie、QJsonObject、HTTP 状态或 QML 引用。基础设施可以依赖领域值类型，领域／应用不能反向依赖基础设施。暂不为没有实现的功能建立空 port 或通用 Manager。

`SessionCoordinator` 的单测只链接 application/domain 和 Qt Test；Cookie 与应用状态的组合测试另行链接基础设施。HTTP manager 的持有方式已统一为 QObject parent 所有权，注入的 unique_ptr 在构造时移交，不维持双重所有权。

## 2. 响应解码不是业务成功判定

`decodeResponse(bytes, encoding, limits)` 是纯函数，不接触网络对象、Cookie 或日志。

- 由 operation 的已验证契约选择 Json/GzipJson/EapiJson/EapiGzipJson/EapiAutoJson。
- Auto 只在**成功解密后**检查 gzip magic；加密失败不能回落为明文 JSON。
- 压缩输入、解密输入和展开后的 JSON 分别限额。默认输入约 1 MiB，JSON 4 MiB；可配置限额有 32 MiB 硬上限，eapi 密文仍受已有解密器约 1 MiB 的限制。
- zlib 流式展开并检查 CRC；当前只接受一个完整 gzip member，拒绝截断、尾随数据和串接 member。若真实契约需要多 member，须显式修改并补总量测试。
- 拒绝非法 UTF-8、坏 JSON、非 object 根；`code` 可缺失，存在时必须是有界整数或严格十进制整数字符串。
- `DecodedResponse::ok()` 只表示解码和 envelope 检查通过。`code=429`、`803` 或缺 code 都不能被统一当业务成功；由各 operation mapper 判断；当前 QR mapper 已明确区分这些情况。
- 错误不携带半个 JSON 对象或原始失败正文。HTTP 状态仍独立存在，不能被业务 code 改写。

大型解析以后在有界任务池执行；目前组件单测同步调用，不宣称已建网络 worker／任务池。

## 3. 会话生命周期

`SessionToken = coordinatorId + epoch`。它只是请求归属标记，**不是凭据或授权证明**；两个协调器即使 epoch 相同也不匹配。

- 初始为 Guest，身份为空。
- 开始／重新开始登录：递增 epoch，进入 Authenticating，旧账号／旧二维码请求失效。
- 接受已经由 gateway 验证的身份：只允许当前 Authenticating token；prepare 递增 epoch 进入 Committing，凭据绑定确认后 complete 才发布 Authenticated，且不再次旋转 token。
- 取消登录、会话到期、退出均递增 epoch。重复退出也会使旧游客请求失效。
- 旧登录结果、旧账号的到期通知和其他协调器的 token 不得改变当前状态。
- 通知按值发送；同步重入不改变已发出的快照。登录完成返回这次转换的确切 token，而不是事后读取可能已经改变的“当前 token”。

`prepareAuthentication(attempt, verifiedAccountId)` 与 `completeAuthentication(target)` **不联网、不验证密码**。仅由认证用例在确认身份、完成凭据交接后按序调用，未暴露为 QML invokable；Committing 不是私有账号用例的准入状态。账号 ID 保留字符串；具体服务 ID 的 schema 由 mapper 验证。

## 4. Cookie 信任边界

`SessionCookieStore` 是单一当前 epoch 的内存容器，不持久化、不导出全部凭据，也不挂到 QNetworkAccessManager 自动收发。

- 初次绑定合法 token；此后只能推进同一 coordinator 的 epoch，不能回退或换成另一个 coordinator。
- 推进时清空 Cookie；相同 token 的重复通知是无副作用的确认。
- apply/header/value 都要求请求创建时捕获的 token，旧回包无法修改新 jar；value 仅按目标 origin 查询，适用同名值冲突时拒绝猜选凭据。
- 只允许四个已登记官方 API 主机的 HTTPS 默认／443 端口；拒绝用户信息、fragment、CDN、上传节点、第三方域和 HTTP。
- 保留 host-only、Domain、Path、Secure、HttpOnly 和过期／删除语义，不移除 Domain 或自动扩为全部域。
- 当前仅接受官方 API 主机范围内的 Domain，拒绝 `.163.com` 等更宽的父域；这是保守策略，尚未用真实登录样本验证。
- 校验 __Secure-/__Host- 前缀规则，拒绝控制字符；非法批次不会部分覆盖旧凭据。
- 单 header 4 KiB、单批 64 个／64 KiB、jar 最多 128 个／64 KiB，生成 Cookie header 最多 16 KiB。超限拒绝，不静默截断授权材料。

Cookie 容器须在所属后端线程构造、使用和销毁。当前认证用例／协调器／适配器／client 在同一个事件循环线程；未来 GUI 通过值快照排队通知，不跨线程直接操作 QObject 或 jar。

## 5. 已连接与仍待完成的登录边界

HttpTransport 仍关闭自动 Cookie，现已支持受限的显式头与原边界 Set-Cookie。SessionApiClient 已连接编码／传输／解码／Cookie，并捕获 token 和 registry origin；QrLoginGateway 已实现取 key、状态查询和领域映射。详细契约、Qt 重复头处理和证据见[扫码请求链路](qr-auth-request-chain.md)。

身份确认、精确 token 凭据交接和同线程发布屏障现已实现，见[认证事务](authentication-transaction.md)。跨线程装配和安全存储仍未实现；登录生命周期必须遵循：

1. 每个请求捕获确切 token 和可信 origin；完成时使用原 token，不拿“当前 epoch”给旧响应重新贴标签。
2. 切号先使应用 token 失效，网络侧推进／清空 jar 后，才分发新账号的私有请求；取消只是优化，token 校验才是必需门禁。
3. 登录完成会产生新 epoch，认证阶段的 Cookie 不能任意继承。gateway 持有身份请求快照的私有凭据收据，并只向 prepare 返回的确切 Committing token 交接；如果已再次登出／换号则丢弃。
4. Cookie 和原始 JSON 不跨入应用层／QML。应用端只接收身份、能力、脱敏错误和原请求上下文。
5. 退出先在本地失效，不依赖远程 logout 成功。持久化以后通过独立凭证 port；当前不会用普通配置文件保存 Cookie。

现有组合测试已经过真实的原生编码／请求／解码代码和 fake QNetworkReply，覆盖 HTTP 成功之后切号、取消来不及、queued 值结果等边界；新增组合测试也覆盖两阶段认证事务；它们仍不能代替真实 socket/TLS 和真实账号验证。
