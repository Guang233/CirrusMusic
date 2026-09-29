# 身份验证与精确凭据交接

> 已实现：**单一后端事件循环线程内的离线可验证认证事务**。未调用真实账号接口，未连接产品入口／UI；不能据此宣称应用已能登录或官方接口兼容已验证。

## 1. 三个远程 operation，不是全部接口

在两个 eapi QR operation 之外，封闭 registry 新增 `AccountIdentity`：

- 参考锁定版本的 `module/login_status.js`，逻辑路径 `/api/w/nuser/account/get`，使用 **weapi**；实际 URL 为 `https://music.163.com/weapi/w/nuser/account/get`。
- 不接受调用者参数。使用现有 weapi 加密器、`e_r=false`、当前作用域 `csrf_token`、固定浏览器 User-Agent 和 `Referer: https://music.163.com`。Referer 不能由调用者指定，传输层仅允许该开关用于 weapi。
- 请求前要求该目标 URL 可见且无冲突的非空 `MUSIC_U`；保留 Cookie 域／路径，**不会把 QR 主机的 host-only Cookie 扩域到 music.163.com**。
- 明文 JSON 响应上限 64 KiB；HTTP 成功且 `code=200` 后，再映射官方根 `account`／`profile`。上游服务包装的 `data` 不属于官方正文。
- `account.id` 与 `profile.userId` 必须匹配；输出 `AccountIdentity { accountId, nickname }`。nickname 必须为合法 UTF-16 字符串，最多256个 UTF-16 单元且不含 NUL。
- ID 字符串必须是正、无前导零的十进制 signed-64-bit 值。数值 ID 只接受解析后的正安全整数（≤`2^53-1`）；更大 ID **必须使用字符串**，否则拒绝，不尝试修复／猜测。

最后一项来自真实回归发现：Qt 可将 `9007199254740993.0` 先舍入再归一化到整数存储，仅检查 QVariant 的 LongLong 不足以证明原数字无损。因此，本实现宁可拒绝宽数值 ID，也不绑定错误身份；不是对官方 ID 取值范围的断言。

## 2. 快照、收据和发布屏障

```text
Guest
  → beginAuthentication() → Authenticating(attempt)
  → requestKey / 调用方驱动 poll / 803
  → verifyIdentity(attempt)
      请求发出前捕获 Cookie 快照；响应 Cookie 只更新该快照
      映射 account/profile → VerifiedLogin(identity, opaqueReceipt)
  → prepareAuthentication(attempt, identity.accountId)
      → Committing(target = attempt 的下一 epoch)
  → promoteCredentials(attempt, target, opaqueReceipt)
      消费收据 → 绑定快照到 target → 网络侧确认
  → completeAuthentication(target)
      → Authenticated(target)，不再旋转 epoch
```

`803` 只证明扫码授权阶段完成；身份请求失败、凭据缺失、收据无效或交接失败，均不能发布 Authenticated。

- **快照在身份请求发出前捕获**，不是收到身份 A 之后再复制可能已被并发请求更新成 B 的当前 jar。身份响应中的合法 Cookie 更新只作用于该隔离快照。
- 快照中的所有未过期 `MUSIC_U` 必须与身份 URL 下的已验证值一致。其他主机／路径若保存不同值，拒绝收据，避免验证 music.163.com 的 A，却给 eapi 留下 B。保守拒绝多值，不猜测多个 token 是否属于同一账号。
- 客户端最多保留一个候选收据，允许在创建后60秒内提交；并非60秒后自动擦除内存。下一次候选、上下文推进、显式丢弃或对象销毁释放旧快照。
- UUID 收据只是一项基础设施内快照的句柄，不是 Cookie、账号授权或可持久化令牌。gateway 保存经映射身份与句柄的绑定；应用层不能导入／导出 jar。
- 匹配到的收据在提交尝试时即消费，包括错误目标导致的失败；只允许相同 coordinator、当前 attempt 到**恰好下一 epoch**，不能跳跃、倒退或跨协调器。
- 应用必须使用 `prepareAuthentication()` 返回的确切 target，并在重入后复核。Committing 不是已登录，不能分发账号私有用例。
- `QrLoginService` 对普通生命周期变化调用 port 的 `synchronize` 清空旧上下文；唯独 Committing 不走普通 clear-on-advance，由收据交接负责绑定。
- 基础设施先绑定凭据并使旧请求失效，再通知／取消；如果通知中发生退出／重启，提交确认失败。应用收到确认后才发布 Authenticated；发布中发生重入退出则不再发送过期的 signedIn。
- 任何排队结果仍携带其原 token，消费者必须再次验证，不把旧结果改贴成当前 token。

## 3. 应用与线程边界

`QrLoginService` 位于 `cirrus_application`，仅依赖 SessionCoordinator、IQrLoginGateway、domain 和 Qt Core。port 增加身份与提交结果；Cookie、JSON、HTTP、Qt Network、OpenSSL 留在基础设施。

当前 `begin()`／`poll()`／`cancel()` 是**所属线程内**的非阻塞调用；`poll()` 由调用方决定节奏，一次只能有一项远程认证操作，没有自动轮询、自动重试或完整登录总时限调度。单次 HTTP 仍受既有超时约束；收据提交是本地延迟事件，不再次联网。

Coordinator、service、gateway、client 必须在同一后端事件循环线程创建、使用和销毁。Coordinator 必须比 service 活得更久，正常装配也须让 gateway/client 覆盖 service 的生命周期；service 析构会取消仍在进行的认证。不要构造含值成员 CookieJar 的 client 后再 moveToThread，也不要从 GUI 跨线程直接调用同步 requestId API。

**尚未实现**网络 QThread 装配与 GUI 异步 facade；本轮的 ready 屏障是同线程认证发布顺序，不是已经实现跨线程交接。远程 logout、系统凭据存储、恢复、设备／匿名 bootstrap、QR 图片、播放和 UI 同样未包含。

## 4. 离线证据

- 应用专用 `qr_login_service` 套件只链接 application／Qt Core／Test，覆盖 ready 屏障、迟到完成、取消、失败回滚、准备及发布过程中的重入退出、二维码过期和析构。
- `auth_transaction` 走真实 C++ 加密／路由／传输适配／解码／Cookie／gateway／应用链，但使用 fake QNetworkReply 与合成 Cookie；覆盖精确 target、身份 response Cookie 更新、退出／到期清理、非法身份及 HTTP 失败、跨主机作用域、不同作用域凭据冲突、验证中的取消和错误提交目标。
- 并发回归明确断言 B 的 Cookie 响应先于 A 的身份响应完成，而最终交接的仍是 A 的请求快照；不依赖最终 jar 恰巧未变化的伪阳性。
- 正常提交后的收据重放被拒绝。60秒期限由单调时钟检查，但本轮没有等待60秒的计时边界测试。
- 已有 SessionCoordinator 测试更新为两阶段状态机；所有原有协议／传输／解码／Cookie／QR 回归保留。统计见[原生基础](native-foundation.md)。

这些测试不包含真实 socket/TLS、真实账号或官方响应样本。下一步仍需设备／匿名启动、安全持久化及恢复、轮询调度、线程装配和经明确授权的实测；XEAPI 与音乐业务另行推进。
