# 已落地的 C++ 协议与传输基础

## 实现范围

产品代码使用 C++20/Qt；Python 仅用于开发期接口清单校验，不参与客户端构建或运行。

### 协议库：cirrus_netease_protocol

源码：`src/infrastructure/netease/protocol/`，依赖 Qt Core、OpenSSL 3 Crypto 和 zlib。

- `form_encoding.*`：UTF-8 form 编码，保留字段顺序／重复名／空值，正确处理空格、百分号与孤立 UTF-16 surrogate。
- `official_route.*`：api/weapi/eapi/xeapi 默认官方 HTTPS 主机和路径改写；拒绝外部 URL、查询串、片段、路径穿越和控制字符。
- `protocol_crypto.*`：weapi 双层 AES-128-CBC + raw RSA；eapi 的 MD5／分隔符／AES-128-ECB 编码，以及二进制 eapi 响应解密。
- `response_decoder.*`：有界 gzip、UTF-8／JSON object 校验和可选业务码提取；解码成功不等于业务成功，不吞掉损坏／超限数据。
- 生产随机数使用 OpenSSL RAND_bytes，base62 用拒绝采样避免偏差；测试可注入随机源，但没有用户可配置的固定 seed/key。
- 编码输入有大小上限；随机失败、无效输入、加解密失败返回明确错误，不使用固定密钥 fallback，不把解密失败当明文成功。

form 编码遵循 [WHATWG URL Standard](https://url.spec.whatwg.org/#application/x-www-form-urlencoded)。JSON 使用 QJsonDocument Compact；各 operation 仍须明确数组、布尔、大 ID 和空字段的序列化，不能照搬 JS 隐式转换。

协议常量和流程参考固定上游；版权与许可见 [NOTICE](../../third_party/api-enhanced/NOTICE.md)。这些算法是服务兼容要求，不是推荐用于新协议的现代密码设计。

### 传输库：cirrus_netease_network

源码：`src/infrastructure/netease/network/http_transport.*`，依赖协议库及 Qt Network。

- 异步 POST，返回独立 requestId；结果区分 HTTP 状态、网络错误、超时、取消、响应超限和重定向拒绝。
- 仅使用内置官方默认主机／路径；请求正文上限 2 MiB，响应默认上限 4 MiB、最大 32 MiB。
- 分发后的总时限定时器不随 readyRead 重置；主动取消和超限会 abort；完成信号最多一次，支持启动前取消和对象销毁。
- 禁止自动重定向、自动 Cookie 读写、缓存保存和认证复用；不忽略 TLS 错误，不做隐式重试。
- 保留受限正文、HTTP status、network error 和 Retry-After；不打印正文、Cookie 或密码材料。

传输层不负责 operation registry、业务 code 判断、重试调度、账号 epoch、认证 headers、Cookie 保存或响应 JSON 映射。其安全默认并不表示完整登录链已经可用。

### 领域／应用与凭据基础

新增 `cirrus_domain` 的会话值类型、`cirrus_application` 的 SessionCoordinator，以及 `cirrus_netease_session` 的内存 Cookie 存储。应用层不依赖 Qt Network 或具体协议；凭据留在基础设施。线程／依赖边界和仍待连接的登录链路见[会话隔离说明](session-isolation.md)。

## 测试与构建

CTest 已登记六个无需账号、无需真实 API 的套件：

| 套件 | 范围 | Qt Test 汇总 |
| --- | --- | --- |
| netease_protocol | form、Unicode、大 ID 字符串、路由和非法路径 | 40 passed |
| netease_crypto | 合成向量、无效密文／输入、随机失败与有界拒绝采样 | 10 passed |
| http_transport | 注入的 fake QNetworkReply：成功、429、TLS 错误、重定向、超时、超限、取消、乱序和销毁 | 13 passed |
| response_decoder | 明文／加密／gzip、CRC、超限、坏 UTF-8／JSON 和业务码边界 | 19 passed |
| session_coordinator | epoch、取消、换号、过期和同步重入 | 10 passed |
| session_cookie_store | 域／路径／Cookie 属性、原子更新、限额与旧账号晚到响应 | 19 passed |

合计 111 passed（包含数据行及初始化／清理）。加密向量由合成 JSON、固定测试密钥和公开协议常量构成；AES/MD5 通过 OpenSSL CLI 独立组装，raw RSA 另以大整数模幂计算。它们不是官方响应样本，不证明当前服务可用。

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --parallel
ctest --test-dir build/linux-dev-local --output-on-failure --no-tests=error
```

当前开发机 Qt 6.11.2、OpenSSL 3.6.4、zlib 1.3.2：Debug/Release 构建和六个 CTest 套件通过，新增 C++ 源文件主动 LSP 检查无诊断，QML lint 通过。`BUILD_TESTING=OFF` 不构建测试；测试不安装。

另有 15 项 Python 清单工具测试和 440 个模块／公共文件 hash 核对通过；这是开发工具证据，不计为产品协议测试。网络测试使用替身，未做真实 socket/TLS 或真实账号集成验证。

## 未实现与下一步

尚未实现 XEAPI/X25519／密钥获取、登录网关、会话与真实请求的绑定、凭证持久化、operation registry、DTO、网络调度或真实 API 调用；应用空窗口不变，基础库尚未接入应用业务。

下一步连接有上下文的登录请求／会话链路，并补 XEAPI，再接首批真实操作。浏览器 SDK 和一起听仍遵守[审计门禁](gaps-and-hazards.md)，不得以局部协议测试通过宣称这些业务已支持。
