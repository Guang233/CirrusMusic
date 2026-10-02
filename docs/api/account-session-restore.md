# 账号记忆、恢复与退出闭环

> 已实现一个完整的后端工作包：凭据快照编解码、系统密钥环保存／删除、身份复核、精确 token 交接、总时限与取消、退出后的旧写隔离。尚未装配到产品入口，未使用真实账号／密钥环验证，不代表应用已经能登录。

## 1. 应用使用方式

现有 `QrLoginService` 增加 `restore()` 与 `signOut()`，复用原有认证状态机和 deadline，而不是另建一套登录协调器。

- 默认 `QrLoginOptions::rememberSession=false`，只有装配方依据用户明确选择开启，才保存新扫码登录的账号。有效登录不依赖密钥环写入成功。
- `restore()` 是显式操作，没有启动时偷偷登录。密钥环恢复出记录后，状态仍是 Authenticating；先调用已有 weapi AccountIdentity，再核对返回的 accountId 与保存的 accountId。
- 只有身份匹配、请求快照收据提交成功，才通过 Committing→Authenticated 发布；不凭 MUSIC_U 或本地 nickname 直接认证，不跳过网络 ready 屏障。
- 成功恢复会重存已核验的快照，保留服务端返回的凭据轮换。整个恢复，包括密钥环读取，受现有总时限约束；超时和取消不会让晚到读取结果恢复登录。
- “仅本次登录”成功时清除此前记住的账号记录，避免下次悄悄恢复另一个旧账号。
- `signOut()` 不等待网络或密钥环，立即回到本地 Guest并清空当前账号 Cookie；保存记录的删除异步执行。已经认证的会话直接通过协调器登出／expire，也会触发删除；取消尚未完成的普通认证不等于删除旧记录。
- `savedSessionCompleted(context, SavedSessionResult)` 单独报告保存／删除结果：true为已保存，false为已删除，AuthFailure为存储失败。携带原操作token，不改贴成当前token；不能据它改变认证状态。界面将来要区分“本次登录有效”和“已经记住”。

接口仍只传领域身份、token和脱敏失败。Cookie、JSON与 SecretError具体存储细节不进入application／QML。

## 2. 基础设施与记录边界

带记忆能力的装配顺序：先创建 SessionApiClient、SecretStore、`AccountCredentialVault`，再用 `QrLoginGateway(client, vault)` 构造适配器。旧的无vault构造仍可用于纯内存登录；请求恢复或要求记住时会明确失败，不虚构持久化成功。

`AccountCredentialVault` 使用现有 SecretStore/QtKeychain，固定 entry为 `account-session`，一个活跃账号槽位；不是多账号选择器。本轮没有新运行依赖、SQLite账号secret表或明文fallback。其客户端和store必须活得更久，所有对象在同一所属事件循环线程创建／使用／销毁。

记录绑定 format、deviceId、accountId 和Cookie快照。总记录≤192 KiB，Cookie子记录≤128 KiB；最多128个Cookie、原始合计≤64 KiB、单项≤4 KiB。导入先在临时jar完成全部校验，再原子更新当前尝试：

- 限定已有官方HTTPS主机，不导入任意域或宽`.163.com`；host-only与Domain范围、Path、Secure、HttpOnly、过期语义保留。
- JSON字段、规范base64、Cookie名／控制字符／前缀、重复项和scope均检查。保留Qt正常化后的exact-host范围，不把host-only Cookie扩为全部官方主机。
- 过期Cookie剔除；没有当前身份URL可见的MUSIC_U或存在不同作用域MUSIC_U冲突，拒绝整条记录。设备不匹配或损坏记录不用于网络请求。
- accountId以规范正signed-64-bit十进制字符串保存；导入的ID只是待核验身份，不是授权证明。
- 加载只暂存到当前尝试的jar；身份查询仍捕获请求时快照，成功后通过原有一次性收据交接。撤销／替换会话仍依靠原token校验，不依靠最佳努力取消。

序列化／导入函数仅供基础设施使用；没有向应用公开全部Cookie枚举或导出功能。上述限额是逻辑记录校验限额，不声称控制系统密钥环后端读取时的全部内存分配。

## 3. 存储竞态与失败策略

- vault串行调度read/write/delete，同一entry只有一个writer。退出发生在write尚未完成时，先让旧write结束，再delete；旧write的“成功保存”通知被版本号丢弃，不得让注销记录复活。
- 新认证的保存可覆盖旧的待写快照，最终以最新期望状态为准。旧读取结果若发生revision或上下文变化，不再导入；重启一次恢复可排队等待旧read释放。
- 保存失败不撤销已经核验的内存登录，也不自动重试或转明文存储。同进程不能再从旧记录恢复；如果此前系统中已有旧账号记录，失败可能使旧记录仍留在后端，下次进程启动仍需身份复核，不能宣称已记住新账号。
- 网络错误不证明凭据被撤销，所以恢复网络失败保留记录。损坏／过期记录和已解码的身份不匹配被删除；301／302拒绝时清理候选，其他未知业务错误不自行猜测刷新协议。
- 删除失败会报告错误；同进程不再恢复旧记录。无法保证失败删除或进程在删除完成前退出后的跨进程擦除。
- SecretStore没有可撤销后端操作或统一超时；客户端恢复有deadline，但系统job可能仍在排队。要保证退出删除落地，需让vault和store保持存活到settled通知，不能直接销毁后宣称已经擦除。后续已完成[后台线程与关闭排空装配](backend-runtime.md)，但无法抹去系统后端永久不回调或进程被杀的限制。
- 同步信号槽可重入logout／new login；对象本身不能从自身完成信号中立即销毁（使用deleteLater／正常装配生命周期）。

## 4. 必要验证与剩余工作

账号恢复工作包（`4aedb18`）当时仅编译／执行受影响的6个Debug套件：account_restore、qr_login_service、qr_login_gateway、auth_transaction、session_api_client、session_cookie_store。共 **191 passed**，无失败／跳过（含数据行、init/cleanup）。

新account_restore套件17项，覆盖记住→新客户端恢复→身份验证→凭据轮换→退出；仅内存与存储失败；账号不匹配；删除失败后禁止同进程恢复旧记录；网络失败保留记录；退出压过read/write；旧read未完成时重启恢复；错误设备／格式／base64／scope／过期／超限记录。应用Core-only套件另补恢复的发布屏障与虚拟deadline回归，不真实等待180秒。

```bash
cmake --build --preset linux-dev-local --target \
  tst_account_restore tst_qr_login_service tst_qr_login_gateway \
  tst_auth_transaction tst_session_api_client tst_session_cookie_store --parallel 4
ctest --test-dir build/linux-dev-local \
  -R '^(account_restore|qr_login_service|qr_login_gateway|auth_transaction|session_api_client|session_cookie_store)$' \
  -V --no-tests=error
```

未重新跑全量／Release／QML／发行验证，未调用真实系统密钥环或官方接口。该时点登记15套件，最新数量见[接手指南](../HANDOFF.md)；历史完整验证与本轮定向验证分开记录，不累计旧结果伪称全量已通过。

后续[后台线程与非阻塞应用facade、启动／关闭装配](backend-runtime.md)也已完成；下一步接入首批音乐查询（搜索／详情／歌词）。它们不需要重新重写已验证的认证／存储协议，业务接口只依赖网络ready和稳定session边界。UI仍最后。
