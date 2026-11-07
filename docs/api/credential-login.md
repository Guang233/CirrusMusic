# 手机号与邮箱登录

> 已接入后端（2026-11-07，维护者要求加入扫码以外的登录方式，见 [UI 接入 D8](../ui/integration.md)）：手机号＋短信验证码、手机号＋密码、邮箱＋密码，以及发送短信验证码。复用扫码登录的认证状态机、身份复核和精确 token 交接。离线只做了构建和既有测试回归，没有新增测试（维护者约定）；真实接口用维护者账号在 UI 接好后验证。

## 1. 接口（锁定参考 `a8c781fd`）

| 原生 operation | 参考模块 | 官方逻辑路径 | 加密 | 固定字段 | 调用方字段 |
| --- | --- | --- | --- | --- | --- |
| SmsCodeSend | captcha_sent.js | `/api/sms/captcha/sent` | weapi | `secrete: music_middleuser_pclogin` | `ctcode`、`cellphone` |
| CellphoneLogin | login_cellphone.js | `/api/w/login/cellphone` | weapi | `type:"1"`、`https:"true"`、`remember:"true"`、`secureCaptcha:""` | `countrycode`、`phone`，`captcha` 或 `password` 二选一 |
| EmailLogin | login.js | `/api/w/login` | eapi（参考默认） | `type:"0"`、`https:"true"`、`rememberLogin:"true"` | `username`、`password` |

- 字段严格封闭：区号 1–4 位数字（不以 0 开头），手机号 4–15 位数字，验证码 4–8 位数字，密码是 32 位小写 MD5 十六进制，邮箱是可打印 ASCII、恰好一个 `@`、≤254 字符。其他字段、类型或长度一律 InvalidRequest，不发请求。
- 响应 JSON 上限：发送验证码 64 KiB，两种登录 256 KiB。
- 参考模块给登录成功响应拼了 `cookie` 字符串、替换 `avatarImgId_str`、把 502 改写成"账号或密码错误"，这些都不属于官方正文，不照搬。登录正文里的 account／profile 不用于认证。
- 不接 `captcha_verify`（登录接口自己校验验证码）、`cellphone_existence_check`、注册、`login_refresh`、远程 `logout`。

## 2. 流程

- **登录**：`QrLoginService::beginCredential(CredentialLogin)` 开始一次认证尝试，和扫码共用 `QrLoginOptions`（总时限、记住登录），不轮询。登录接口返回 `code=200` 时，响应 Cookie 和 QR 803 一样只暂存进本次尝试；然后照常调用 AccountIdentity 复核身份，再 prepare → 精确 token 交接 → Authenticated，记住登录时写入密钥环。
- 同一个尝试 token 只允许一次登录：已经申请过二维码或已经授权的 token 再提交登录会被拒绝。
- **发送验证码**：`sendSmsCode(SmsCodeRequest)` 在 Guest／Expired 会话，或扫码尝试正在等待扫码时受理（登录框左边扫码、右边手机号同时可用）；登录、身份复核、交接进行中时拒绝。不推进会话、不停止播放、不作废其他请求（Facade 不递增 generation）。它不暂存 Cookie，所以在网关里独立于扫码／身份的单飞请求运行，同一时间最多一个。结果是 `smsCodeSent(token, variant<bool, AuthFailure>)`；会话 token 在等待中变了（如二维码过期）时结果被丢弃，调用方自己清掉等待状态。
- **Facade**：`beginCredentialLogin(login, options)`、`sendSmsCode(request)`；输入非法时直接返回 0。结果复用 `signedIn`／`failed`，发送验证码用新的 `smsCodeSent`。

## 3. 失败与安全验证

- 非 200 的业务码放在 `AuthFailure::providerCode`，`ProviderRejected`（429 为 RateLimited）。不重试、不换主机或加密方式。
- 常见业务码（社区资料，未经本仓库实测）：密码错误 502，验证码错误 503，需要安全验证 8821，风控 -462。服务端要求滑块等安全验证时，不内嵌网页、不尝试绕过；UI 提示改用扫码或短信验证码（维护者决定）。
- `secret` 是短信验证码或密码的 MD5，绝不是明文密码：调用方用 `domain::passwordDigest()` 在 UI 线程算好，之后不再持有密码。验证码和摘要只随请求走，不记日志、不缓存、不进密钥环；Qt 字符串是隐式共享的，不保证内存被清零。
- 网易的密码 MD5 是协议要求，不是安全存储；传输仍走 HTTPS 和 weapi／eapi 加密。

## 4. 改动位置

- `domain/account/credential_login_types.h`：`CredentialLogin`、`SmsCodeRequest`、校验函数、`passwordDigest()`。
- `api_operation.*`：三个 operation 的契约、封闭载荷、`acceptsResponseCookies`（两种登录 200 时暂存）。
- `ports/qr_login_gateway.h`：`requestCredentialLogin`、`sendSmsCode`（默认返回 0＝不支持，测试替身不受影响）及两个完成信号。
- `qr_login_gateway.*`、`qr_login_service.*`、`backend_facade.*`、`backend_runtime.cpp`：转发与状态机接入。
