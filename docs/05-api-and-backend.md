# 05 · 网易云官方 API 与客户端后端

> 状态：提案。**明确架构：客户端内置官方 API 直连，用户不配置端点，不部署第三方服务。** 本文的操作名是内部逻辑名，不是猜测出来的官方路径。

## 1. 责任边界

已按维护者指定的 api-enhanced 完成[固定版本静态审计](api/README.md)，记录 440 个模块的范围决定和协议依赖。它是第三方源码参考，不是官方契约证明；真实响应样本、加密向量和账号验证尚待完成。开发任务是原生 C++ 适配，不运行 Node 服务、不让用户填写 Base URL；UI 最后实施。

```text
应用用例
  → NetEaseGateway（领域请求 → 官方 DTO）
  → EndpointRegistry（内置方法、路径、协议类型、权限）
  → ProtocolSigner（官方要求的编码／加密／签名）
  → HttpTransport（TLS、Cookie、超时、取消、限流）
  → 网易云官方 API
  → ResponseDecoder → DTO 校验 → DomainMapper → 应用用例
```

生产端点随应用受控版本发布；mock origin 只能在测试构建通过注入替换。不得为便于调试留下普通用户可修改端点、绕过 TLS 或输出 Cookie 的隐藏设置。正常系统网络代理不等于用户配置 API 服务，两者应在设计中区分。

## 2. 接口接入交付物

每个 operation 登记以下内容，模板见[api-operation.yaml](templates/api-operation.yaml)：

- 官方方法、主机、路径、协议类型、版本依据和用途。
- 参数类型、默认值、范围、必填与条件必填、排序、字符编码及大 ID 表示。
- 请求正文序列化规则、签名／加密顺序、nonce／时间戳与时钟偏差处理。
- 会话 Cookie、CSRF、必要请求头与设备标识的真实要求；哪些信息不能持久化。
- 返回 envelope、业务成功判定、错误码、分页、权限和可能缺失字段。
- 真实但脱敏的成功、空、权限不足、登录失效、限流、部分成功和畸形样例。
- 重试安全性、是否官方支持幂等键、批量上限、速率、超时及资源有效期。
- 可用于开发的账号／数据、使用限制及契约测试责任人；不得把真实凭据提交 Git。

HTTP 200 不一定业务成功；HTTP 4xx/5xx 也可能有有用业务原因。未知业务码保留数值用于脱敏诊断，映射为 `ProviderFailure` 或 `ContractMismatch`，不能默认成功。

## 3. 操作覆盖表

业务到上游源码的候选映射见[接口表](api/selected-endpoints.md)，当前全部为 static-only、not-implemented；参数／返回的完整契约仍待逐项验证。不要求官方存在同名或一一对应接口，可由多个调用组合，但必须说明额外成本和部分失败语义。

| 内部逻辑操作组 | 用途 | 关联需求 |
| --- | --- | --- |
| auth.beginQr / pollQr / cancelQr | 扫码流程 | FR-003 |
| auth.restore / sessionStatus / logout | 恢复、会话校验、退出 | FR-004 |
| account.profile / privileges | 账号信息和权益快照 | FR-005 |
| search.query / suggest / hot | 分类搜索、建议与热搜 | FR-006、FR-007 |
| catalog.trackDetails / availability | 批量详情和可播信息 | FR-008 |
| catalog.album / albumTracks / artist / artistAlbums / artistTopTracks | 专辑与歌手 | FR-009 |
| playlist.details / trackIds / tracksPage | 歌单概要、完整曲目 ID 或分页 | FR-010 |
| playlist.create / update / remove | 歌单生命周期 | FR-011 |
| playlist.addTracks / removeTracks / reorder | 歌单编辑 | FR-012 |
| library.likes / setLike / collections / setCollection | 喜欢与收藏 | FR-013、FR-014 |
| discovery.recommendations / daily / charts | 音乐发现 | FR-015 |
| fm.nextBatch / feedback | 私人 FM 与反馈 | FR-016 |
| stream.resolve | 按当前账号与目标音质解析播放来源 | FR-017、FR-018 |
| lyrics.get | 逐行、逐字、翻译与音译 | FR-021、FR-022 |
| history.remote / playback.report | 远程历史与必要播放事件 | FR-024 |
| download.resolve / authorization | 下载来源与离线约束，不能假定等同 stream.resolve | FR-025、FR-026 |
| cloud.list / resolve | 云盘查询与播放 | FR-028 |
| cloud.prepareUpload / upload / finalize / remove / match | 上传与管理，依真实多阶段协议映射 | FR-029 |
| comments.list / like / create / reply / remove | 评论与互动 | FR-030、FR-031 |
| together.create / join / leave / heartbeat / command / syncQueue | 一起听；接收通道和同步协议尚待补证 | FR-043 |

如果某一能力无需远程操作（例如本地取消轮询），登记为 `local-only`，不要为了表格整齐发出不存在的请求。

## 4. 协议与加密模块

`ProtocolSigner` 接收规范化参数、端点描述、只读会话材料、时间／随机源，输出可发送的请求；它不访问 QML、不写数据库、不自行重试。

- 已观察到 weapi/eapi/XEAPI 等协议，详见[原生迁移边界](api/protocol-migration.md)；以固定源码和验证向量为准，不把静态观察当实测。v2 浏览器 SDK 不自动纳入原生依赖。
- Canonicalization、字段顺序、编码、填充、Base64／十六进制大小写都属于契约，必须有固定向量测试。
- 生产使用密码学安全随机源；测试可注入确定性随机源，不能通过配置在正式构建启用固定 nonce。
- 重试前重新生成协议要求更新的时间戳／nonce／签名，不能盲目重发过期签名。
- 官方协议规定的公开常量与真正秘密要区分。嵌入桌面二进制的常量不能作为防止用户提取的安全边界。
- 设备标识仅按必要协议要求生成和存储，不收集无关硬件指纹；生命周期和重置行为写入隐私说明。
- 签名测试日志只记向量编号和校验结果，不记真实会话正文。

## 5. 会话管理

状态：`Guest → Authenticating → Authenticated → Expired`；`LockedCredentialStore` 是独立可恢复状态。不存在“读取到 Cookie 就已登录”的假设，恢复必须进行轻量服务校验。

1. CookieJar 绑定 profile 和 sessionEpoch，遵守域、路径、安全标志及到期规则；每个账号独立存储。
2. 敏感持久化通过密钥环，普通 DB 只保存凭据引用和校验时间。
3. 登出立即使本地会话失效并递增 epoch，取消私有任务、清理内存来源和私有展示，再尽力执行服务端退出；远程失败不能阻止本地退出。
4. 官方若存在 refresh，采用 single-flight 刷新；不存在时不得编造 refresh token 流程，应转为重新登录。
5. 同一批并发请求触发一次到期提示，不能弹出多个登录框。
6. QR 轮询按服务要求的间隔、期限执行，取消／关窗／新二维码出现时使旧轮询失效。
7. 登录失败、额外验证和风控保持原始语义，不自动切换身份或重复刷验证码。

## 6. 网络调度、超时与取消

以下为初始上限，正式值以官方限制和 M0 测量取更保守者：

| 项目 | 默认预算 |
| --- | --- |
| 常规 API 并发 | 总计最多 6，同主机最多 4；播放来源／用户操作优先于预取 |
| 登录轮询 | 一个活跃轮询链，不并行堆积 |
| 下载 | 2 个流；网络较差时可减到 1 |
| 图片 | 单独有界队列，不挤占登录／播放解析 |
| 普通元数据请求 | 不活动超时 8 s，单次总时限 15 s；需要单独总时限定时器 |
| 下载／上传 | 不套用 15 s 总时限；初始无进度超时 30 s，可暂停／取消 |
| 安全读取重试 | 最多 2 次重试（总共最多 3 次尝试），退避起点 500 ms、带抖动；服从 Retry-After |
| UI 搜索建议 | 防抖初值 250 ms；新 query 取消旧请求并递增 generation |

Qt 的 transfer timeout 与端到端 deadline 不完全相同，不能只设置一个数就宣称涵盖 DNS、连接、正文和重试。用户动作总预算初值 30 s；Retry-After 超出预算时返回明确等待状态，不继续占住 loading。

取消调用 transport abort，并把结果标记 `Canceled`；底层完成信号仍可能到达，接收端再次校验 requestId、epoch、generation。断网检测只能作为提示，最终以请求结果为准。

## 7. 重试与写操作一致性

| 场景 | 行为 |
| --- | --- |
| 安全读取的连接错误、部分 5xx | 在次数与总预算内重试 |
| 429 | 遵守 Retry-After，降低调度速率，显示等待，不并发绕过 |
| 会话失效 | 单次恢复／重新登录；不无限重试原请求 |
| 权限不足、下架、地区限制 | 不重试；返回可解释原因 |
| JSON 契约不匹配 | 不重试风暴；记录契约失败、关闭受影响能力 |
| 创建歌单／评论／上传提交超时 | `OutcomeUnknown`；优先读取对账，不盲重放 |
| 设置喜欢等“看似幂等”的操作 | 只有契约确认确为 set 状态才按幂等处理；toggle 不自动重试 |
| 官方支持幂等键 | 重试沿用同一操作键，并验证其作用域和有效期 |

同一歌单、收藏实体的写入串行化。快速连续点击可以合并为最终用户意图，但必须跟踪服务端已确认状态。不要将本地 mutationId 当成官方已接受的幂等凭证。

## 8. 错误模型

`AppError` 最小字段：category、operation、safeMessageKey、retryable、suggestedAction、requestId、providerCode?、retryAfter?。原始正文和凭据不属于此对象。

| category | UI 行为 |
| --- | --- |
| NetworkUnavailable / Timeout | 展示可用缓存，允许手动重试；不伪装空结果 |
| AuthRequired / SessionExpired | 一次性引导登录，保持可恢复的本地操作上下文 |
| AccessDenied / CopyrightRestricted / PreviewOnly | 展示原因，保留元数据，不循环换账号／换域 |
| RateLimited | 显示等待与恢复时刻 |
| NotFound / Removed | 列表标记失效，不立刻抹掉用户队列记录 |
| ContractMismatch / UnsupportedCapability | 功能级降级并提示更新；不能导致整个 UI 崩溃 |
| Conflict / PartialFailure / OutcomeUnknown | 保留局部结果，重新读取或用户确认 |
| DiskFull / PermissionDenied / DeviceUnavailable | 引导磁盘、目录或设备操作 |
| Canceled | 通常不显示错误提示 |

## 9. 分页、缓存与能力

- 页请求绑定完整 queryKey（关键词、分类、排序、过滤、账号范围），旧页面不能追加到新查询。
- 远程分页变动时去重只用于目录结果，不能无差别去掉允许重复的队列／歌单项。
- `total` 可未知；“加载全部”显示进度、可取消，并遵守接口批次限制。
- 元数据可 stale-while-revalidate；权限、临时播放地址、会话不能按长期元数据缓存。
- 能力表来自已映射契约和运行时结果。官方不存在 capabilities endpoint 时由内置 registry + 安全探测维护，不假造健康检查接口。
- 应用启动不强行请求全站数据；首页、搜索和播放按需加载。

## 10. 媒体信任边界

API 请求和媒体 CDN 请求分开。不得将官方登录 Cookie／Authorization 自动传给媒体域、封面域或重定向目标。

`QMediaPlayer` 使用自己的媒体加载链路，无法假定它继承 API 会话。M0 必须验证真实授权来源是否可直接播放、是否要求 headers、是否支持 Range 和重定向。若需要受控字节传输，评估带 seek 的本地源适配或更换后端；不得临时打开任意 URL 的本地 HTTP 代理逃避设计。

## 11. 契约验收

一个 operation 合入真实实现前必须具备：登记项、脱敏 fixture、签名向量（如适用）、DTO/mapper 测试、错误映射、取消／超时用例及最少一次受控真实服务验证。没有真实资料的 operation 可提供 fake 实现供后端单测／CLI harness 使用，但必须标为 mock，不能构造看似真实的路径或返回字段混进生产；UI 最后才接入。
