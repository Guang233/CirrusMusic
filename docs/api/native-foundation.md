# 已落地的 C++ 协议与传输基础

## 实现范围

产品代码使用 C++20/Qt；Python 仅用于开发期接口清单校验，不参与客户端构建或运行。

### 协议库：cirrus_netease_protocol

源码：`src/infrastructure/netease/protocol/`，依赖 Qt Core、OpenSSL 3 Crypto 和 zlib。

- `form_encoding.*`：UTF-8 form 编码，保留字段顺序／重复名／空值，正确处理空格、百分号与孤立 UTF-16 surrogate。
- `official_route.*`：api/weapi/eapi/xeapi 默认官方 HTTPS 主机和路径改写；拒绝外部 URL、查询串、片段、路径穿越和控制字符。
- `protocol_crypto.*`：weapi 双层 AES-128-CBC + raw RSA；eapi 的 MD5／分隔符／AES-128-ECB 编码，以及二进制 eapi 响应解密。
- `response_decoder.*`：有界 gzip、UTF-8／JSON object 校验和可选业务码提取；解码成功不等于业务成功，不吞掉损坏／超限数据。
- `eapi_request_profile.*`：固定 pc 请求 profile、显式 Cookie／User-Agent、`e_r=false`；设备 ID 由装配方提供；匿名 bootstrap 见[安装级启动记录](anonymous-bootstrap.md)。
- `xeapi_crypto.*`：XEAPI B/S/R 编码（AES-256-ECB／随机中间变换／X25519＋HMAC 派生＋AES-128-GCM）、注册签名与常数时间校验、公钥响应解密校验、匿名注册用户名；拒绝小阶点和非法会话密钥。
- `xeapi_request_profile.*`：固定 android xeapi 兼容 profile（`x-*` 头、Cookie 子集，不发送追踪类随机 Cookie）；`openssl_primitives.*` 为库内共享 AES／HMAC 辅助。
- 生产随机数使用 OpenSSL RAND_bytes，base62 用拒绝采样避免偏差；测试可注入随机源，但没有用户可配置的固定 seed/key。
- 编码输入有大小上限；随机失败、无效输入、加解密失败返回明确错误，不使用固定密钥 fallback，不把解密失败当明文成功。

API 层另有 `xeapi_key_ring.*`（安装级公钥状态，整体原子替换，`changed()` 为持久化接缝）和 `xeapi_key_registrar.*`（单飞公钥注册，签名必须匹配本次 nonce，失败不改旧状态）；`SessionApiClient` 的 xeapi 路径无公钥时以 `MissingProtocolKey` 结束、不降级协议，`x-encr-ssid/sskey` 只被当前 epoch 采纳并在 epoch 推进时清除。

form 编码遵循 [WHATWG URL Standard](https://url.spec.whatwg.org/#application/x-www-form-urlencoded)。JSON 使用 QJsonDocument Compact；各 operation 仍须明确数组、布尔、大 ID 和空字段的序列化，不能照搬 JS 隐式转换。

协议常量和流程参考固定上游；版权与许可见 [NOTICE](../../third_party/api-enhanced/NOTICE.md)。这些算法是服务兼容要求，不是推荐用于新协议的现代密码设计。

### 传输库：cirrus_netease_network

源码：`src/infrastructure/netease/network/http_transport.*`，依赖协议库及 Qt Network。

- 异步 POST，返回独立 requestId；结果区分 HTTP 状态、网络错误、超时、取消、响应超限和重定向拒绝。
- 仅使用内置官方默认主机／路径；请求正文上限 2 MiB，响应默认上限 4 MiB、最大 32 MiB。
- 分发后的总时限定时器不随 readyRead 重置；主动取消和超限会 abort；完成信号最多一次，支持启动前取消和对象销毁。
- 禁止自动重定向、自动 Cookie 读写、缓存保存和认证复用；不忽略 TLS 错误，不做隐式重试。
- 保留受限正文、HTTP status、network error 和 Retry-After；不打印正文、Cookie 或密码材料。
- 显式 Cookie ≤16 KiB、User-Agent ≤1 KiB，拒绝控制字符；成功响应的 Set-Cookie 按原边界返回，每条 ≤4 KiB、最多64条／合计64 KiB。使用 QHttpHeaders，避免 legacy rawHeaderPairs 合并重复项。

传输层不负责 operation registry、业务 code 判断、重试调度、账号 epoch、认证头构造、Cookie 保存或响应 JSON 映射。其安全默认并不表示完整登录链已经可用。

### 领域／应用与凭据基础

`cirrus_domain` 提供会话／扫码值类型；`cirrus_application` 包含 SessionCoordinator、QrLoginService 和窄 IQrLoginGateway port；`cirrus_netease_session` 保管当前 epoch 的内存 Cookie。应用层不依赖 Qt Network 或具体协议；凭据留在基础设施。见[会话隔离说明](session-isolation.md)。

### 请求与扫码适配

`cirrus_netease_api` 连接 protocol/network/session，捕获每项请求的 token 与可信 origin，提供封闭 QR／AccountIdentity operation 契约、编码／解码、Cookie 暂存、失效和取消；最多32个在途 HTTP 请求。

`cirrus_netease_auth` 实现应用 port：取 key、本地扫码 URL、800～803 状态映射、同一尝试的 key 绑定和单远程请求限制。原始 JSON／Cookie 不进入应用层。**803 不会直接认证 SessionCoordinator**。weapi 账号身份确认、请求时 Cookie 快照、一次性收据、Committing→Authenticated 精确交接与同线程发布屏障已实现；大数值 ID 和跨作用域凭据冲突保守拒绝。详见[扫码请求链路](qr-auth-request-chain.md)及[认证事务](authentication-transaction.md)。[账号记忆／恢复／退出](account-session-restore.md)也已实现，仍必须复核身份后使用原有交接，不从本地记录直接认证。

## 测试与构建

下表是密钥环增量时的13套件完整验证基线；随后新增bootstrap与account_restore，当前登记15套件。账号恢复本轮按风险只重跑6个受影响Debug套件／191 pass，结果见[账号恢复记录](account-session-restore.md)；匿名启动的较早定向结果见[匿名启动记录](anonymous-bootstrap.md)，不声明重新完成完整矩阵：

| 套件 | 范围 | Qt Test 汇总 |
| --- | --- | --- |
| netease_protocol | form、Unicode、大 ID 字符串、路由和非法路径 | 40 passed |
| netease_crypto | 合成向量、无效密文／输入、随机失败与有界拒绝采样；XEAPI B/S/R、签名、公钥解密、小阶点与非法会话密钥 | 18 passed |
| http_transport | fake reply、429／TLS／重定向、取消／乱序／销毁、显式头、重复 Cookie 和限额 | 22 passed |
| response_decoder | 明文／加密／gzip、CRC、超限、坏 UTF-8／JSON 和业务码边界 | 19 passed |
| session_coordinator | epoch、两阶段认证、取消、换号、过期和同步重入 | 12 passed |
| session_cookie_store | 域／路径／属性、原子更新、限额、旧账号响应及冲突凭据查询 | 21 passed |
| session_api_client | profile、Cookie 链与失效、HTTP 已成功后的切号、取消／并发／封闭 registry | 24 passed |
| xeapi | 匿名用户名向量、签名校验、传输层固定头白名单与会话头捕获、公钥注册（nonce 重放／错误拒绝且不改旧状态）、xeapi 请求路径与按 epoch 的会话密钥 | 23 passed |
| storage | 目录／文件权限、进程锁、pragma／WAL、迁移校验和、更高版本拒绝、升级备份与失败回滚、损坏文件、线程与回调生命周期、设备 ID 生成与稳定性 | 11 passed |
| secrets | XEAPI 公钥整条密钥环持久化：恢复不回写、不覆盖更新的公钥、损坏／不可用上报、变化合并无重叠；无 D-Bus 时真实适配器报错且无明文回退文件（另有默认跳过的真实密钥环往返） | 12 passed, 1 skipped |
| qr_login_gateway | 官方与包装 envelope、QR 状态、错误、key 绑定、queued 值结果和销毁 | 28 passed |
| qr_login_service | Core-only 应用事务、发布屏障、失败回滚、取消／晚到／重入／析构；自动轮询节奏与总时限（虚拟时钟＋Qt 定时器冒烟） | 61 passed |
| auth_transaction | 身份 schema、快照隔离、精确交接、错误作用域／收据、登出／到期／重入、总时限跨 Committing | 38 passed |

该历史完整基线合计 329 passed（包含数据行及初始化／清理）。XEAPI 向量由锁定上游函数在固定随机输入下生成（开发期 Node 脚本，不随产品分发），签名与 R 字段另经 OpenSSL CLI 交叉验证。加密向量由合成 JSON、固定测试密钥和公开协议常量构成；AES/MD5 通过 OpenSSL CLI 独立组装，raw RSA 另以大整数模幂计算。它们不是官方响应样本，不证明当前服务可用。

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --parallel
ctest --test-dir build/linux-dev-local --output-on-failure --no-tests=error
```

密钥环增量记录的开发机 Qt 6.11.2、OpenSSL 3.6.4、zlib 1.3.2：当时 Debug/Release 构建和十三个 CTest 套件通过，C++ 源文件主动 LSP 检查无诊断，QML lint 通过。匿名启动增量只进行了受影响 Debug 目标编译／3套件回归；账号恢复增量只编译／执行受影响6套件并检查变更文件诊断。`BUILD_TESTING=OFF` 不构建测试；测试不安装。

另有 15 项 Python 清单工具测试和 440 个模块／公共文件 hash 核对通过；这是开发工具证据，不计为产品协议测试。网络测试使用替身，未做真实 socket/TLS 或真实账号集成验证。

## 未实现与下一步

[匿名 bootstrap](anonymous-bootstrap.md)与[账号凭据持久化／恢复／退出](account-session-restore.md)已实现并有定向离线回归；尚未实现网络线程装配与 GUI facade、通用调度或真实 API 验证。registry 当前覆盖两个 eapi QR operation、一个 weapi 身份 operation 和一个 xeapi 匿名注册 operation（其 MUSIC_A 暂不入账号 jar），不代表目录／播放业务已支持；应用空窗口不变，后端尚未装配到产品入口。

扫码自动轮询与认证总时限已实现（[B-010 子任务](../tasks/B-010-qr-polling.md)）。XEAPI 编码、公钥注册与会话密钥已实现（[B-002 子任务](../tasks/B-002-xeapi.md)）。下一步补后台线程和非阻塞启动／关闭装配，再开展受控实测和音乐业务。浏览器 SDK 和一起听仍遵守[审计门禁](gaps-and-hazards.md)，不得以局部协议测试通过宣称这些业务已支持。
