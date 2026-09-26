# 官方协议的原生迁移边界

本页仅描述固定版本中观察到的实现和 Cirrus 的设计约束；没有真实服务验证。源码校验值及固定链接见 [upstream-lock.json](upstream-lock.json)，端点选择见 [inventory.json](inventory.json)。

## 1. 分清三层地址

- `/song/url/v1`：上游 Express 包装路由，Cirrus 不访问它。
- `/api/song/enhance/player/url/v1`：业务模块传入公共 request 的逻辑路径。
- `https://interface3.music.163.com/xeapi/song/enhance/player/url/v1`：按当前 XEAPI profile 改写后的实际请求地址。

`util/option.js` 默认 crypto 为空，由 `util/request.js` 依 `APP_CONF.encrypt` 选 eapi 或 api。当前配置为加密开启、普通响应加密关闭，但 **XEAPI 始终单独处理二进制加密响应**，不能被普通 `encryptResponse=false` 覆盖。

| 模式 | 当前默认主机／路径改写 | 数据形态与迁移要求 |
| --- | --- | --- |
| api | `interface.music.163.com` + `/api/…` | POST form；不等于公开无鉴权接口 |
| weapi | `music.163.com` + `/weapi/…` | JSON、双层 AES-CBC、反转随机密钥的 raw RSA，表单字段 params/encSecKey |
| eapi | `interfacepc.music.163.com` + `/eapi/…` | 逻辑路径参与 MD5 摘要，拼接指定分隔符后 AES-ECB，输出 hex params |
| xeapi | `interface3.music.163.com` + `/xeapi/…` | B/S/R 字段；公开密钥状态、X25519、HMAC-SHA256 派生、AES-ECB/GCM 和随机中间变换 |
| linuxapi | `music.163.com/api/linux/forward` | method/url/params 包装后 AES-ECB；已盘点，但不是因为程序运行 Linux 就必须选它 |

公共 request 在固定版本中统一发送 POST，Content-Type 为表单。HTTP 方法不能用于判断业务幂等性。`scrobble` 有明确 `clDomian`（上游配置原拼写）覆盖，注册密钥／token 和上传还有独立链路；必须逐 operation 定义主机 profile，不允许调用方任意传 `domain`。

## 2. C++ 技术边界

已引入 Qt Network 与 OpenSSL 3 EVP，实现 weapi/eapi 编码和有界异步传输，详见[实现记录](native-foundation.md)。已有 zlib 有界 gzip 解码及内存会话／Cookie 隔离基础，见[分层说明](session-isolation.md)；完整登录链路和 XEAPI 尚未实现。

- 使用系统安全随机源；不要迁移 weapi 的 Math.random 随机实现。
- 精确固定 UTF-8、JSON 字段形态、form 转义、PKCS#7、hex 大小写、Base64 和 RSA raw padding，禁止以“同样是 AES/RSA”代替字节一致性。
- XEAPI 的 HMAC 密钥文本／Base64 处理、派生上下文、随机中间变换、GCM tag 拼接都须以向量验证，不能拿普通 HKDF 默认参数替代源码行为。
- Qt 的 `qUncompress` 不是任意 gzip 的通用替代。限制密文长度、解密后长度和解压后长度，错误不得回落为成功的原始字节。
- 模块中的大 ID、JSON-stringified 数组、布尔字符串与空值须单独建契约；领域 ID 用字符串，禁止穿过 QML double 或 JavaScript Number 丢失精度。
- 不把 JS 引擎、CryptoJS、Axios、node-forge、jsdom、上游 server 或通用 URL 转发器纳入程序。

首批测试覆盖公开算法向量和固定协议向量。随机源／时钟可注入，但固定随机数只能用于测试。与上游对照生成的 fixture 必须标记来源，不得宣称它是独立的官方契约证明。

## 3. 启动与会话不是“有 Cookie 就能调”

上游 `main.js`/`server.js` 负责启动环境；request 层会读取全局临时目录的 `anonymous_token`、`xeapi_public_key`，依赖全局 deviceId，并维护进程全局 NMTID、XEAPI sessionId/sessionKey。这些不能照搬到多账号桌面程序。

原生状态边界：

1. 安装／设备 profile：稳定设备标识、明确的客户端 header profile，不随机采集真实硬件隐私。
2. 匿名 session：`register_anonimous` 对应的匿名凭证，与登录账号凭证分离。
3. 账号 session：Cookie jar、CSRF、uid、凭证保管、sessionEpoch。
4. XEAPI 公钥状态：`register_xeapikey` 直接 POST `/api/gorilla/anti/crawler/security/key/get`，验响应签名并解密；需要 version/publicKey/sk。更新必须保持状态一致，缺 key 或签名失败不能继续发业务请求。
5. XEAPI 会话密钥：来自 `x-encr-ssid` / `x-encr-sskey`，作为敏感会话状态处理；在账号变更时失效并测试晚到响应不能覆盖新 session。

不保留上游共享 `/tmp` 文件／进程全局可变状态。持久化凭证进入安全存储或明确批准的降级模式；协议缓存与账号绑定信息不得混在普通缓存里。认证 header 不进入日志、测试快照或媒体 CDN 请求。

## 4. Cookie、重定向和响应

上游会移除 Set-Cookie 的 Domain 以适配自己的代理服务，并在各加密模式间重新组装 Cookie。原生客户端不处在同样的代理场景：必须保留／验证 Secure、HttpOnly、Path、Domain、过期语义；跨官方主机的必要凭证映射由内部 profile 明确定义，不能做万能 Cookie 广播。

- 接口、图片 CDN、媒体 URL、NOS 上传分别使用凭证策略；媒体 URL 查询签名也属于敏感信息。
- 只允许 HTTPS 和批准的重定向策略；不因服务端返回 upload host 就无条件向任意地址上传本地文件。
- 不迁移任意 IP／X-Forwarded-For 伪装、不禁用 TLS 验证。
- 将 HTTP status、业务 code、认证状态、解析错误分别保留。上游把 302、800–803 等业务码包进 HTTP 200，是包装器行为，不是“全部成功”。
- QR 等待／过期、账号失效、限流、版权／会员限制、网络失败必须区分。
- 对敏感操作不自动重放。超时后的“结果未知”需要重新读取状态或人工确认，不能统一 retry 三次。

## 5. 反作弊 token 的真实依赖

### v3

`register_checktoken_v3` 直接 GET 配置的易盾主机 `/v3/b?pn=…`，解析 JSONP 形态返回。它不是普通 request 模块；可作为原生 HTTP 解析候选，但必须严格校验而不是执行响应文本。空 token 应返回明确的能力／验证错误，不伪装注册成功。

### v2：待决策，不承诺原生可用

`register_checktoken_v2` 会获取 `tool.min.js`，用 jsdom 执行并允许继续加载 Watchman 脚本／JSONP，再向 SDK 请求 token。上游中的浏览器指纹修改不迁移。

`playlist_subscribe` 强制 `query.checkToken='v2'`。因此“收藏歌单”需求保留，但受 `native-checktoken-v2` 门禁约束。不能把 v3 token 擅自替代 v2，也不能把远程 SDK 当作仓库 MIT 许可下的 C++ 算法。

先调查是否存在可验证的官方原生路径。若确实需要浏览器验证桥或额外运行组件，应暂停该操作并征求维护者同意；**当前不引入 WebEngine，不执行动态远程 JS，不改变纯原生／无 Node 原则**。

## 6. 上传是独立协议链

云盘完整流程由 `cloud.js`、`plugins/songUpload.js` 和相关模块组成：

1. 从真实文件计算 MD5、字节数、格式和安全文件名。
2. `/api/cloud/upload/check` 查询处理状态；不要把“存在同 hash”当作任意内容使用授权。
3. `/api/nos/token/alloc` 分配 token（上传插件使用 weapi）。
4. GET `https://wanproxy.127.net/lbs?...` 查上传节点。
5. 向批准的 NOS 节点流式 POST 文件字节，附上传 token、MD5、长度等。
6. `/api/upload/cloud/info/v2` 提交元数据，再 `/api/cloud/pub/v2` 发布。

`cloud_upload_token`/`cloud_upload_complete` 是拆分流程入口，不表示可以省掉实际文件上传。上传、校验、发布分别记录状态；失败不能从头重放整个链。分片续传、取消后的残留处理和文件去重语义仍待验证。

歌单封面也经 `plugins/upload.js` 先分配 NOS token、上传图片，再 `/api/playlist/cover/update`。必须验证真实图片类型／尺寸，不能仅复制固定 jpg 扩展假设。

## 7. 不迁移的服务器设施

Express 路由、CORS、服务端缓存、HTTP 302 包装、任意 API 透传、代理隧道实现、第三方音源解锁和通用解密调试接口均不是客户端功能。系统代理如后续需要，使用 Qt／平台的明确策略实现；不得暴露供业务层指定任意主机、header、crypto 的逃生接口。
