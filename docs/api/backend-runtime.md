# 后台装配、非阻塞入口与关闭排空

> 已实现并通过离线线程／生命周期测试。`main.cpp` 和 `Main.qml` 仍为模板；没有自动启动官方网络、访问真实账号或把后台入口注册给QML。原后台装配工作包没有新增运行依赖；后续[播放增量](stream-resolution-and-playback.md)已获准接入Qt Multimedia。

## 1. 实际边界

| 层／入口 | 职责 |
| --- | --- |
| `application/backend/backend_facade.*` | 调用方线程的Core-only入口，命令序号、有界邮箱、会话值快照、晚到事件过滤；不持有Cookie、HTTP或SQL对象 |
| `infrastructure/runtime/backend_runtime.*`／`cirrus_backend` | 装配现有协调器、服务、client、密钥环、匿名启动和安装库；只处理生命周期，不实现音乐业务 |
| `infrastructure/secrets/tracked_secret_store.*` | 包装现有SecretStore，记录真实后端回调是否落地，包括接收者已销毁的操作和回调内派生的后续操作 |
| AccountCredentialVault／XeapiKeyVault | 原有串行写入队列；新增只读idle状态供关闭屏障检查，不改变账号／密钥协议 |

构造Runtime时只启动一个空后台事件循环。仅无资源的Worker壳移到线程；网络manager、client（含值成员CookieJar）、协调器、定时器、vault、QtKeychain job都在线程内部创建／使用／销毁。SQL继续由既有DatabaseWorker的独立执行线程负责；网络线程不执行SQL查询，调用方不等待后台或数据库线程。

Runtime公开头只依赖application／Core和前置声明；具体Network、Sql和secrets依赖留在装配实现。没有跨线程返回QObject、jar或原始JSON的getter。

## 2. 启动与命令契约

装配方提供 `BackendConfig{databasePath, keychainService}`：绝对安装数据库路径、确定的应用密钥环service名。没有代选app ID或用户可配置API地址。测试工厂在后台线程执行；不得返回在GUI线程预先创建的QObject。空工厂选择原生实现；返回空指针为配置失败，不暗中改走真实网络。

`runtime.facade()` 是调用方线程对象，只在该线程调用；不能再moveToThread。流程：

1. `start()`：Dormant→Starting，一次性；同一实例重复调用不会重复启动。
2. 打开／迁移安装库并取得稳定deviceId；失败报告Configuration／Storage，进入Failed，不无声生成新身份或临时明文配置。
3. 装配账号服务和匿名bootstrap，恢复／注册XEAPI公钥、恢复／注册匿名token。**不自动恢复账号**。
4. 匿名启动成功为Ready；网络／服务端导致匿名能力失败则报告GuestBootstrap并以降级Ready开放QR登录。Ready仅表示调用入口已就绪，不保证匿名能力或所有业务接口可用。
5. QtKeychain保存失败通过 `persistenceWarning(ProtocolKey/AnonymousToken)` 单独报告，不能因内存启动成功宣称记录已保存。账号保存仍用 `savedSessionCompleted(originalToken, result)`。

Ready以后支持beginLogin(options)、restore、poll、cancel、signOut。默认扫码登录仅本次，记住仍需显式选择。恢复继续执行服务端身份复核与原Committing屏障，不把线程装配当成新的认证实现。

- 每条接收的命令返回非零本地ID，结果通过acknowledged／领域信号异步给出。ID不是provider requestId或认证凭据。
- 未就绪、关闭中、非法options或已有32条未确认命令时返回0；调用方必须处理拒绝，尤其不能把未接收的signOut当成成功。close始终可用，不受邮箱上限影响。
- ack的true只表示命令被后端接受（poll还需满足节奏），不表示已经登录或存储成功。close会以false结束尚未观察到的ack；**false不保证该命令未发出或没有副作用**。以独立会话／存储结果为准。
- begin／restore／cancel／signOut提交时立即推进调用方generation，清空本地缓存快照；已排到GUI队列的旧session／signedIn不能复活新意图。后端继续执行原token检查，不能用generation替代epoch屏障。
- 存储通知保留原token，不用于切换账号；即使来自旧会话或关闭期间也可报告。只有当前generation且token匹配的认证事件能交付。
- `snapshot()`只读本线程缓存；权威新状态由sessionChanged到达。未得到有效token时不可发起私有业务。
- deliver／finishClose及Requested信号仅为装配接线，不是UI命令。不要从对象自身信号槽同步删除Runtime／Facade，使用deleteLater并保持正常所有权。

后续[五类收藏订阅写](collection-writes.md)新增独立Core port／8槽mutateCollection／completion，原账号／token／generation跨线程值传递；取消／切号／close保守Unknown并异步保留原token。gateway在线程内构造，先于client/session销毁，无新线程／运行依赖或自动查询／播放／报告。整歌单收藏v2 SDK门禁经维护者确认延期，不装配未验证SDK。本文158＋1是原后台包历史，收藏包335＋9是其历史；后续[歌单创建／改名／描述写](playlist-management-writes.md)仍共享该port／8槽／线程与关闭屏障，只有合法原账号创建Ack交付ID。最新404＋9见新契约，不累加为全量；无创建自动重试／同名读对账或新运行依赖。

## 3. close不是signOut

正常程序关闭保留账号记忆、匿名token和公钥。先signOut再close才会请求删除账号记录；不调用匿名reset，不因程序退出删除安装级身份。

关闭顺序：

1. Facade进入Stopping，立刻拒绝新命令并过滤旧成功事件。
2. 后台先退役单曲播放协调器／resolver／Qt媒体实例和临时设备，再销毁报告／收藏写gateway与查询／匿名bootstrap／公钥registrar／账号service和gateway，取消自己的网络工作，停止定时器和新的存储生产者。
3. 保留client、两个vault、tracked store及其后端。已排队但未发出的vault写入仍会flush；接收者销毁不等于后端写入取消。
4. 等待tracked store实际pending归零、account vault的read／dirty／busy为空、key vault的dirty／busy为空；完成回调所派生的新写／删除也纳入屏障。检查在下一事件循环轮次执行，不从完成信号里同步删除vault。
5. 销毁vault/client/ring/session/store，关闭DatabaseWorker，排空其已有SQL工作并释放进程锁。SQL join发生在后台线程，**不是GUI线程**。
6. 网络线程退出之后才给Facade发送stopped。实例不复用；需要再次启动时创建新Runtime。集成测试通过同一路径重新打开数据库验证锁已经释放。

`stopped(persistenceFailed)` 的true表示本次Runtime曾观察到写入／删除失败（含关闭前），不是“仍有操作未完成”；调用方不可宣称持久擦除或永久记住成功。false也不是跨发行版／真实密钥环保证。异常终止、断电、被杀进程不在排空承诺内。

QtKeychain没有统一可取消job或完成时限：若后端永久不回调，Runtime保持Stopping；5秒后closeDelayed发出一次，不循环催促、不调用terminate、不虚报stopped。启动时的密钥环读取同样可能等待系统解锁；本工作包未伪造统一超时。调用方事件循环仍可响应，最终强制退出的产品策略需要单独决策。

Runtime析构只排队请求close，不阻塞调用方，不直接销毁运行中的QThread；Worker和thread自行排空回收。但这不是提前终止进程的许可：产品退出流程必须在事件循环仍运行时先close并等stopped，不能等aboutToQuit之后才指望异步排空。

## 4. 离线验证

后台装配工作包（`0f8dd24`）当时针对7个受影响Debug套件，启用 `QT_FATAL_WARNINGS=1`（跨线程QObject／定时器警告即失败）：

| 套件 | pass | skip |
| --- | ---: | ---: |
| backend_facade（新） | 6 | 0 |
| backend_runtime（新） | 17 | 0 |
| account_restore | 17 | 0 |
| qr_login_service | 63 | 0 |
| bootstrap | 32 | 0 |
| secrets | 12 | 1 |
| storage | 11 | 0 |
| 合计（含数据行、init/cleanup） | **158** | **1** |

新增测试涵盖：真实QThread＋fake HTTP／密钥环的启动→扫码→记住→关闭→新实例恢复；对象创建／调用／销毁线程及调用方通知线程；邮箱上限、旧事件generation屏障、ready通知重入关闭；数据库打开中关闭／进程锁冲突／配置拒绝／匿名网络降级；QR请求中关闭；读取不返回及5秒延迟提示；账号与匿名写入排空；注销删除不得越过旧写；删除失败／匿名保存失败报告；提前析构Runtime仍排空；原接收者销毁后仍追踪真实写入。

```bash
cmake --build --preset linux-dev-local --target \
  tst_backend_runtime tst_backend_facade tst_account_restore \
  tst_bootstrap tst_secrets tst_storage tst_qr_login_service --parallel 4
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(backend_runtime|backend_facade|account_restore|bootstrap|secrets|storage|qr_login_service)$' \
  -V --no-tests=error
```

真实keyring往返默认跳过；现有无D-Bus适配器失败检查不等于真实桌面keyring成功验证。没有本轮全量／Release／QML／真实socket或官方账号兼容性证明；没有修改模板入口。该时点登记17套件，最新数量见[接手指南](../HANDOFF.md)，旧工作包的结果不可相加伪称全量验证。

## 5. 下一步

后续[搜索／歌曲详情／三语言歌词](music-queries-and-lyrics.md)已接入Facade；其16条有界查询、原token与generation隔离、关闭取消继续复用本装配。[官方歌曲URL与单曲Qt播放](stream-resolution-and-playback.md)也已接入：共享32条命令邮箱，播放快照脱敏且保留固定LoadToken，媒体对象在后台创建／销毁。后续[队列与六轨歌词时钟](queue-and-lyric-clock.md)也已接入：Runtime拥有调用方Core-only `ListeningSession`（`listening()`），在Facade之前销毁它。使用队列时由它独占播放命令生产，仍通过异步Facade驱动后台；无需重做已建立的认证／装配基础。[日推读取／私人FM条目供给](discovery-and-fm.md)继续复用查询通道；Runtime另拥有调用方Core-only `PersonalFmSource`（`fm()`），同样先于Facade销毁。Source不是播放器；后续[FM连续播放](fm-continuous-playback.md)已将它注入ListeningSession，由后者独占播放命令和模式切换。析构先ListeningSession、再FM供给，最后关闭Facade，避免依赖提前销毁。[个人库四项只读查询](personal-library.md)继续复用Facade／MusicQuery和后台派发，无新的线程或控制器。后续[本地观测累计／会话历史](local-listening-history.md)的计量在媒体线程完成，历史留在调用线程ListeningSession，跨线程只传原load累计值；未新增线程／SQL任务。后续[显式报告](explicit-listening-reports.md)有独立写port、8条在途、原账号校验和明确写结果；后台gateway先于client销毁，复用线程，无自动报告／重试。Runtime只增加对应窄业务入口，不扩成集协议、缓存和播放器于一身的Manager。真实官方访问、桌面keyring验证和app ID落地仍需要授权／决策；UI保持最后。
