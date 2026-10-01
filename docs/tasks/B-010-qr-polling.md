# B-010 子任务：扫码轮询与总时限

> 状态：**待实现，可领取的下一任务契约**。属于 B-010／M1，不代表整个登录需求或 M1 完成。代码起点为 `1a60b74`，交接文档不改变该行为。

## 1. 目标与范围

在已有 `QrLoginService` 上补可选自动轮询和单次认证总时限。无需真实账号、UI、新运行库或新接口，即可离线验收。领取者先读[接手指南](../HANDOFF.md)和[认证事务](../api/authentication-transaction.md)。

当前 `begin()` 取 key，`poll()` 由调用方触发；收到803后自动验证身份并执行两阶段交接。单次 HTTP 有超时，但**整个认证过程没有总时限，尚无自动轮询**。

交付包括：应用层调度、可控时间测试、一次 Qt 定时器适配冒烟、文档更新。不做设备 bootstrap、持久化、线程迁移、GUI facade、二维码图片、真实接口验证、通用网络队列或其他登录方式。

## 2. 默认策略与明确语义

以下是**下一任务的保守工程默认，不是网易官方频率或二维码有效期声明**。维护者若调整，应同时改此契约、代码和测试，不从上游路径名猜测策略。

- 保留现有构造／手动调用兼容性；新增窄的值配置（例如 `QrLoginOptions`），自动轮询默认关闭，调用方显式开启。两种模式都受总时限约束。
- 默认轮询间隔 `3000 ms`，总时限 `180000 ms`；配置在一次认证开始后固定。建议限制间隔在 `[1000,60000] ms`，总时限在 `[间隔,600000] ms`，防止零延迟循环、溢出或无穷等待。
- 非法配置：begin 建立本次尝试后以 `AuthError::InvalidRequest` 终止回到 Guest，不发送网络请求、不留下活动计时器；失败 token 规则沿用 service。
- 自动模式：key 成功后等待一个完整间隔再发第一次 check；801／802 成功结果后重新等待一个间隔。按**上一次完成时间**排下一次，不用固定周期积压请求，不重叠。
- 自动模式下手动 `poll()` 也必须遵守最早允许时间和单飞限制，不能绕过节奏发双请求；返回 false 不产生错误信号。手动模式仍允许调用方主动 poll，但不会自行续轮询。
- 800：沿用二维码过期处理，停止调度、取消本次尝试，等待显式 begin 刷新；不自动取新 key。
- 803：立即停止 QR 轮询，只启动一次身份验证；**总时限继续覆盖身份确认和 Committing**，不能因803或 epoch 旋转重新计时。
- 所有 AuthFailure 继续作为本次尝试的终止错误，**包括429、网络错误和单请求超时**；第一版不增加错误重试。保留脱敏 `retryAfterMs`，不把缺失值当零延迟，不据此隐式重启登录。
- 从当前有效 begin 起记录单调时间绝对 deadline。达到／超过 deadline 尚未完成，就取消活动请求，以 `AuthError::Timeout` 回到 Guest并通知一次；不冒充服务端800。
- 超时回调和各关键成功回调都检查 deadline，特别是开始 verify、prepare／promote、发布 Authenticated 之前。即使定时器因事件循环繁忙晚执行，也不能让过期的成功抢先发布。
- 已成功发布且未过期的 Authenticated 不会被旧认证计时器退出；总时限不是账号会话有效期。

## 3. 生命周期与重入规则

1. 为一次认证维护独立的本地调度 generation；新 begin／cancel／外部登出／离开本次认证／析构使旧调度失效。
2. 认证中 attempt→prepared target 是同一事务，不重置 deadline。允许调度记录跟踪该确切 target，但**不允许把旧网络结果改贴新 token**。
3. 定时器执行必须确认仍属于当前事务和当前 Authenticating／Committing 状态；只检查捕获的旧 attempt token 会导致 Committing 阶段漏超时，应专项测试。
4. 清理计时器、请求 ID 和状态后再向外通知。challenge/status/failed/signedIn 或 SessionCoordinator::changed 的槽可能同步 cancel、begin、signOut；返回后必须重新验证归属。
5. deadline 和网络结果同时到达时以当前单调时间判定；`now >= deadline` 超时优先，失败最多一次。若超时发生在凭据已经绑定但尚未发布期间，现有 Guest 推进必须清掉新绑定凭据。
6. 不引入嵌套事件循环、阻塞 sleep 或跨线程直接调用；定时器在 service 所属线程创建和销毁。

## 4. 实施入口与测试方式

优先修改：

- `src/application/account/qr_login_service.{h,cpp}`：调度状态与生命周期；配置／时间抽象确有需要再加小文件。
- `tests/unit/qr_login_service_test.cpp`：扩展现有 FakeGateway，记录请求次数、context、取消和完成。
- `tests/unit/auth_transaction_test.cpp`：保留完整快照／凭据绑定安全回归；至少增加总时限跨 Committing 的验证。
- `src/CMakeLists.txt`／`tests/CMakeLists.txt`：仅在增加实际源码／测试时登记。
- `docs/api/authentication-transaction.md`、`native-foundation.md`、`HANDOFF.md`：同步实现状态与实际测试数。

生产采用 Qt Core 的单调时钟和单次定时器；为测试提供窄的可控时间／调度接缝，不建立通用 Manager 或让 application 依赖 Network。主要用例推进虚拟时间，**不能真实等待180秒或60秒来验证边界**；保留少量有上限的实际 Qt 事件循环测试确认定时器连线。Fake 的完成仍须遵守异步、原 token、按值结果契约。

不承诺单调时钟跨系统休眠的墙钟语义；跨休眠／平台恢复策略另行验证。本任务至少证明事件循环延迟执行时的 deadline 校验。

## 5. 验收矩阵

以下全为待实现者完成的验收项，本交接轮次未代勾：

- [ ] 默认手动模式无自动 check；自动模式按配置启动，非法配置无网络访问。
- [ ] key 等待期间无 check；key 成功后的首次等待和801／802后的等待准确，边界前不发、边界到达只发一次。
- [ ] 慢请求／延迟完成不积压定时器；自动与手动触发并用仍只有一个远程请求。
- [ ] 800停止；803只核验一次，之后没有QR check；真实错误和429不自动重试。
- [ ] key／轮询／身份核验／Committing 各阶段均受同一个 deadline；不存在每轮／每次epoch刷新总时限。
- [ ] deadline恰好到达、过期成功先于timeout事件被处理、timeout与结果同轮抵达，都不误登录或重复失败。
- [ ] 新 begin、取消、外部登出、prepare／publish中的重入、旧定时器晚到，都不影响新尝试或已认证账号。
- [ ] 绑定后、发布前超时回退会清凭据；原有精确token、快照A/B、作用域冲突测试仍通过。
- [ ] service销毁后无回调或网络请求；仍遵守依赖对象生命周期。
- [ ] 应用层测试仍仅依赖 domain／Qt Core／Test，不引入 Qt Network、JSON、OpenSSL、QML。
- [ ] 现有10套件全部保留并通过 Debug/Release；新增测试被 CTest 正确发现，更新统计，不硬凑旧223数量。
- [ ] `BUILD_TESTING=OFF`、qmllint、Python审计及格式／文档链接检查通过；没有 UI、真实凭据、新运行依赖或官方兼容性宣称。

建议提交分两步：先以 fake 时间固定调度／边界用例，再完成应用实现和 Qt 适配；最终交付不能留下红测试。完整回归命令见[接手指南](../HANDOFF.md)。

若真实契约要求额外验证 SDK、协议切换或超出当前单线程边界，应记录证据并请维护者决策；不要为了让这个任务“完成”扩大范围。
