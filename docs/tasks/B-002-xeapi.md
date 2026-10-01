# B-002 子任务：XEAPI 编码、公钥状态与会话密钥

> 状态：**已实现**（离线验收）。属于 B-002／M1。它是匿名 bootstrap（`register_anonimous` 走 xeapi）和官方播放 URL（`song_url_v1` 默认 xeapi）的前置；不代表这两项已实现。

## 1. 目标与范围

在现有原生协议栈上补齐 XEAPI，离线可验收、无需真实账号、无新运行依赖（OpenSSL 3 已提供 X25519／HMAC-SHA256／AES-GCM）。依据锁定上游 `util/crypto.js`、`util/request.js`、`module/register_xeapikey.js`（`a8c781fd`），见[协议迁移](../api/protocol-migration.md) §3 与 [AUD-02](../api/gaps-and-hazards.md)。

分两步交付：

1. **协议层**（`cirrus_netease_protocol`）：B/S/R 请求编码、签名、公钥响应解密与校验；响应解密复用 `EapiAutoJson`。
2. **接入层**：传输层的 XEAPI 固定头和会话头捕获；公钥注册 operation 与原子公钥状态；SessionApiClient 的 xeapi 请求路径和按 epoch 隔离的会话密钥。

不做：匿名注册本身、设备 ID 生成／持久化、密钥环、真实接口验证、checktoken、其他 xeapi 业务接口。

## 2. 协议语义（以上游字节行为为准）

- 明文是封闭子集：仅 POST、表单 content type、无 URL query，固定为 `{"body":"<base64(form)>","queryString":"e_r=true"}`；表单沿用 URLSearchParams 规则并剔除 `e_r`。不暴露 method／contentType／query 选项。
- `B = AES-ECB(dynamicKey, mid(AES-256-ECB(staticKey, plaintext)))`；mid 为16字节随机异或、Base64、按 `(r[0]&15)%len` 左旋，前置随机字节。
- `S = ephPub(32) | iv(12) | AES-128-GCM(k, "b64(dynamicKey)|android|sk") | tag(16)`，`k = HMAC(HMAC(0³², shared), ephPub|0x01)[0:16]`；全零／小阶共享密钥视为失败。
- `R = AES-256-ECB(staticKey, "version|sessionId")`，仅在使用会话密钥时带 sessionId。
- 有会话密钥时 dynamicKey 即其字节（必须16/24/32字节可打印 ASCII），否则取16字节安全随机。随机取用顺序固定为 dynamic → mid → 临时私钥 → iv，以便注入测试。
- 公钥注册：POST `interface.music.163.com/api/gorilla/anti/crawler/security/key/get`（明文表单），HMAC 签名 `timestamp+nonce`（密钥为 Base64 文本本身的字节）。响应须 `code==200`、签名按**本次 nonce** 校验通过、AES-256-ECB 解密得到 `version`、32字节 `publicKey`、非空 `sk`；任何一项失败都不改变现有状态。上游"缺 sk 时沿用旧 sk"的分支在锁定版本中不可达，不迁移。

## 3. 状态与隔离

- 公钥状态是安装级协议状态，整体原子替换；缺失／校验失败时禁止发送 xeapi 业务请求（明确错误，不降级到 eapi）。
- `x-encr-ssid`／`x-encr-sskey` 是会话敏感状态：只在当前 epoch 成功的 xeapi 响应后采纳；epoch 推进即清空；旧 epoch 晚到响应不能覆盖新会话。不写日志、不出基础设施层。
- 传输层只接受类型化的 XEAPI 固定头（仅 xeapi 协议允许），不开放任意 header；会话头有长度上限，非 xeapi 或失败响应不返回。
- 持久化：公钥 `version/publicKey` 属非秘密安装状态，`sk` 与会话密钥按秘密处理。本任务只提供内存状态及可注入的加载／保存接缝，持久化在设备 profile 与存储任务中接入（见 §5）。

## 4. 验收矩阵

按实现时的验证结果勾选。

- [x] 固定输入的 B/S/R、签名、公钥解密与上游锁定实现逐字节一致（含会话密钥与空表单），签名和 R 另以 OpenSSL CLI 交叉验证；向量来源在测试中注明。
- [x] 非法公钥（长度、Base64、小阶点）、非法会话密钥、随机源失败、超限输入均返回明确错误且无部分输出。
- [x] 公钥响应：错误 code、缺字段、签名不匹配（含旧 nonce 重放）、解密失败、缺 sk 均拒绝且不改变旧状态；成功时整体替换。
- [x] 无公钥时 xeapi 请求以明确错误结束，不发网络请求。
- [x] 传输层：xeapi 固定头正确出现，其他协议拒绝该字段；会话头仅在成功 xeapi 响应返回，超长拒绝。
- [x] 会话密钥只被当前 epoch 采纳，epoch 推进清空，晚到响应不覆盖。
- [x] 现有套件全部通过 Debug/Release；新增测试被 CTest 发现；应用层不引入 Network／OpenSSL。（11 套件、306 项。）

## 5. 后续依赖：设备 profile 与存储选型

按[存储方案](../09-storage-cache-and-sync.md)与 [ADR-0006](../adr/0006-storage-and-secrets.md) 的成熟项目标准，而非图方便：

- **设备 ID 与公钥 version/publicKey**：安装级、非秘密、必须跨重启稳定，不随"重置设置"或清缓存变化，也不应随 dotfiles 同步到别的机器。因此不放 QSettings（AppConfigLocation 是用户偏好），也不放可重建的 `catalog.sqlite`；放 AppLocalDataLocation 下独立的 `state.sqlite`，经 DatabaseWorker 单线程访问，带版本化迁移。
- **`sk`、匿名 `MUSIC_A`、账号 Cookie**：秘密，进入系统密钥环（ADR-0006 选定 QtKeychain，引入前须维护者确认依赖）。密钥环不可用时只保留本次会话内存，不做明文回退（R-012）。
- DatabaseWorker（B-008 基础）随设备 profile 任务实现，并供后续音乐库复用。
