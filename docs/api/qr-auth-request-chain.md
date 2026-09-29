# 有上下文的请求链路与扫码登录基础

> 状态：原生 QR 适配与离线契约测试已实现，后续已补[身份验证与凭据交接](authentication-transaction.md)。**不等于产品已能登录，也不证明真实官方接口可用。** 没有调用真实账号接口、采集凭据或改动 QML 窗口。

## 1. 本轮的分层

| 层／target | 内容 | 边界 |
| --- | --- | --- |
| domain | `QrLoginChallenge`、`QrLoginStatus` | Qt Core 值；challenge 是短期登录材料，不记录／持久化 |
| application | `IQrLoginGateway`、`AuthFailure`、QR／身份／交接类型化结果 | 只依赖 domain/Core；没有 HTTP、JSON、Cookie 或具体服务实现 |
| protocol | `prepareEapiRequest` | 固定 eapi 请求 profile、密文 form 与显式头；不管理会话 |
| `cirrus_netease_api` | 封闭 `ApiOperation`、`SessionApiClient` | operation 契约、请求 token/origin、编码／解码、凭据暂存和取消；不依赖 application |
| `cirrus_netease_auth` | `QrLoginGateway` | 实现应用 port，解析 QR 业务结果并限制同一登录尝试的并发／key 使用 |

`SessionApiClient` 拥有一个 `HttpTransport` 和一个当前 epoch 的内存 Cookie store；其传输／QNetworkAccessManager 采用 QObject parent 单一所有权。扫码适配器通过 `QPointer` 借用共享 client：自身销毁取消所发请求；client 先销毁时，存活适配器返回脱敏失败，不访问悬空对象。

这些组件必须在同一个所属事件循环线程创建、使用和销毁。值成员中的 CookieJar 不随外层 QObject 自动迁移，**不能构造后把 client 整体 moveToThread**。目前测试使用单线程 QCoreApplication；同线程身份确认／凭据提升及发布屏障已实现，但网络 QThread 装配、GUI 侧异步 facade 和跨线程完成屏障尚未实现。port 的同步 requestId 返回值只在所属线程取得，不能跨线程直接调用或用阻塞等待获得它。

## 2. 已接入的 operation 契约

来源为锁定 commit `a8c781fd64faab17fedfd46e0615a2609307f163`，不是官方公开规范。

| 原生 operation | 官方逻辑路径 | 原生参数 | 成功／状态判定 |
| --- | --- | --- | --- |
| QrKey | `/api/login/qrcode/unikey` | 固定 `type: 3` | `code=200`，根对象 `unikey` 为非空、≤512 字符的可打印 ASCII key |
| QrCheck | `/api/login/qrcode/client/login` | `key`，固定 `type: 3` | 800 过期、801 待扫码、802 待手机确认、803 扫码授权成功 |

- 两者固定使用 eapi，实际主机是 `interfacepc.music.163.com`，路径 `/api/` 改为 `/eapi/`；响应 JSON 上限 **64 KiB**。
- `e_r=false` 对应锁定配置的明文 JSON 响应。损坏／非 JSON 响应直接失败，不回退到其他 crypto、主机或格式。
- 本表列出两个 QR operation；registry 后续新增了 weapi `AccountIdentity`，共三个远程 operation。拒绝未知 operation、额外参数、数字／超长 key 和任意 domain/crypto 参数。静态 `inventory.json` 不参与运行。
- `login_qr_key.js` 给官方正文加的外层 `data` **不属于官方响应**；其包装对象不能被原生 mapper 当成功 fixture。
- QR URL 在本地构造为 `https://music.163.com/login?codekey=...`，key 使用 URL percent encoding；不接受服务响应中的 URL，也不调用上游 `/login/qr/create` 包装路由。
- 当前只生成 pc 形式的 URL，没有 QR 矩阵／图片生成，也没有 web `chainId` 分支。

### 请求 profile 的诚实范围

固定采用参考实现的 pc profile：os/appver/osver/channel 及 versioncode、buildver、resolution、requestId。其 eapi 分支选择 api/iphone User-Agent，本轮保留这一组合而非擅自“修正”；它是待实测的协议兼容字段，不是用户系统检测结果。

设备 ID 由装配方显式提供，≤128 可打印 ASCII 字符；目前尚无设备 ID 持久化或匿名 `MUSIC_A` bootstrap。没有复制上游全局 NMTID 随机 fallback、第三方 checktoken SDK、IP 伪造、Node 服务或任意 header override。

`__csrf`、`MUSIC_U`、`MUSIC_A`、`NMTID` 只从当前 token、目标 origin 下的 Cookie 取值。同名且适用值冲突时失败，不猜选哪份凭据。请求 header object 与显式 Cookie 一致；Cookie 使用 `encodeURIComponent` 规则（空格 `%20` 而非 `+`）。requestId 的四位后缀只是相关标识，不是加密 nonce。

## 3. 网络侧隔离与凭据暂存

1. 装配方先推进应用 token，再在网络所属线程调用 `advanceTo(token)`。只有当前 token 能开始请求；不会根据新传入的请求 token 自动接受更高 epoch。
2. 推进时先清空 Cookie、更新当前 token，并使旧任务失效，再通知和取消。同步重入如果已推进到更新 token，旧调用不能报告自己仍是当前上下文。
3. 每项请求捕获 **原 token 和 registry 生成的可信 origin**。完成时不使用当前 token 重贴标签，也不使用响应提供的 origin。
4. 完成时再次校验 token；即使 HTTP 已成功、取消已来不及，旧结果仍被丢弃，不能更新新 jar。
5. 只有成功 HTTP、合法 JSON 和已识别的结果才能暂存 Set-Cookie：QrKey 须有合法根 `unikey`；QrCheck 为 801～803。HTTP／业务失败、过期、坏 JSON 和错误包装的 Cookie 不写入。
6. Cookie 批次原子校验，保留已有域／路径／过期规则和限额；jar 仍只有当前 epoch。**普通 epoch 推进一定清空凭据，不自动继承扫码阶段 MUSIC_U。**

传输仍关闭自动 Cookie、重定向、缓存和认证复用。新增的显式 Cookie ≤16 KiB、User-Agent ≤1 KiB；控制字符／换行被拒绝。Set-Cookie 每条 ≤4 KiB、最多64条、合计 ≤64 KiB，失败响应不向上交付凭据。

Qt 的 legacy `rawHeaderPairs()` 会合并重复头；本轮使用 Qt 6.8+ 的 `headers().values(SetCookie)` 保留原边界，不用逗号拆分（Expires 日期自身含逗号）。参见 [QHttpHeaders 的合并说明](https://doc.qt.io/qt-6/qhttpheaders.html#combining-values)。这些是交付给上层的限额，不声称替代 Qt 底层 HTTP 解析器的全部内存限制。

当前 API client 最多32个在途 HTTP 请求；扫码 gateway 最多1个远程 QR 操作。尚未实现通用有界队列／优先级／重试 scheduler。没有后台自动轮询或隐式重试；轮询节奏／总时限将由后续认证用例管理。

## 4. 应用 port 的语义

- `requestKey(context)`、`checkStatus(context, key)` 返回独立 requestId；完成信号按值携带原 token 与领域结果／脱敏错误。
- 包括输入错误在内，初始完成都发生在调用返回之后。取消是最佳努力；接收方始终必须检查 `SessionCoordinator::accepts(context)`。
- 只能轮询本 gateway 在同一 token 下取得的 key。同一尝试只发出一个有效 challenge；过期、授权后不能继续轮询。刷新二维码要开始新尝试／新 epoch。
- 换号会立即释放旧任务占用的单请求槽；旧任务稍后仍只完成一次，并标为 StaleContext，不能让新请求错误地一直 Busy。
- HTTP 429 和业务 429 映射为 RateLimited；业务 code 可供脱敏诊断，但官方 message／原始正文不进入应用层。仅解析有界数字形式 Retry-After 秒数；日期／未知形式保持未知，不当作零延迟重试。
- 502 等拒绝不重试 noCookie、其他端点或协议；不复制上游 QR catch 分支的越作用域变量错误。

**`QrLoginStatus::Authorized` 不等于 `SessionPhase::Authenticated`。** QR 状态本身不会发布已登录。后续 `QrLoginService` 通过 port 验证账号身份，再经 Committing、一次性快照收据和精确 token 交接发布 Authenticated；如果已退出／再次切号则丢弃。详见[认证事务](authentication-transaction.md)。

## 5. 离线验证与剩余门禁

覆盖显式头／注入拒绝、重复 Set-Cookie／Expires／限额、profile 与密文 payload、作用域 Cookie 链、坏正文／错误 Cookie 不入库、取消、并发限额、HTTP 成功后切号的晚到响应、同步重入、QR 状态与包装差异、queued 值结果和对象销毁。

QR 基础增量最初有8个 Qt Test 套件；后续认证增量扩展为10个。当前统计与命令见[原生基础记录](native-foundation.md)。`http_transport`／`session_api_client` 的 Cookie 回归各重复3次通过。fixture 全部为合成数据，不含真实 Cookie。

账号身份验证、精确 token 凭据交接及同线程发布屏障已在[后续增量](authentication-transaction.md)实现。下一步为设备／匿名 bootstrap、安全存储与恢复、线程装配及授权环境下真实 QR／退出验证；XEAPI 可独立推进。可用的产品登录、真实 socket/TLS、音乐查询／播放、UI 均不在已完成范围。
