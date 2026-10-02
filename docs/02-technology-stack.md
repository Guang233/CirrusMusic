# 02 · 技术栈与依赖策略

> 状态：提案。技术选型以可靠性、Linux 可交付性和单人／小团队可维护性为主要约束。

## 1. 推荐基线

| 层 | 选择 | 理由与边界 |
| --- | --- | --- |
| 语言 | **C++20**，QML 中仅使用轻量展示 JavaScript | Qt 集成成熟；不把状态机、加密、数据库和网络搬到 QML |
| Qt | **Qt 6.11.2 作为首个验证基线** | 与当前本机 Kit 一致；不是“永远使用此补丁版本”，升级需走回归门禁 |
| 构建 | CMake ≥ 3.24，Ninja，CMake Presets | 显式依赖、IDE/CLI/CI 同源配置；不沿用模板的低版本作为产品承诺 |
| 编译器 | 首个 CI 组合 GCC 13；第二组合 Clang 18 | 均以实际 Qt Kit ABI 及发行环境验证为准，不混用不兼容二进制 |
| UI | Qt Quick、Qt Quick Controls 的 **Templates/Basic 基础**、Qt Quick Layouts | 重写外观，复用成熟交互语义；见 ADR-0005 |
| 客户端后端 | 模块化单体 C++，应用服务 + 领域对象 + 适配器 | 无独立业务服务器，无微服务；见 ADR-0002 |
| 官方 API | Qt Network；内置 NetEaseGateway 与协议签名适配器 | 客户端直连官方接口，用户无 API 配置；见 ADR-0003 |
| 密码学 | 已引入 OpenSSL 3.x `libcrypto`，本机验证 3.6.4 | 使用 EVP；协议离线向量及少量公开weapi/eapi/XEAPI调用已通过，真实账号仍待验证，不启动外部脚本 |
| 压缩响应 | 已引入 zlib，当前本机 1.3.2 | gzip 流式解压／CRC／输出限额；系统开发包，发布时锁定版本与许可，未 vendoring 源码 |
| 音频 | **Qt Multimedia / QMediaPlayer + QAudioOutput**，封装可替换接口 | 首版适配成本低；gapless/DSP/复杂鉴权必须先做 PoC；见 ADR-0004 |
| 持久化 | Qt SQL + SQLite；QSettings 保存非敏感偏好 | 可迁移、可事务、可测试；见 ADR-0006 |
| 凭据 | QtKeychain 封装系统密钥环 | 访问 Secret Service/KWallet 等实际后端；不可用时仅内存会话，不回退明文 |
| Linux IPC | Qt DBus | MPRIS、portal 能力探测和桌面集成 |
| 图标／图片 | 许可明确的 SVG 图标集；Qt SVG/图片插件按实际资源选择 | 不默认引入网络字体和远程图标；封面异步加载并限额 |
| 本地标签 | M4 评估并锁定 TagLib 2.x 的维护版本 | 仅读取首版所需标签／封面；部署和许可证纳入清单 |
| 测试 | Qt Test、Qt Quick Test、CTest、QSignalSpy | 与 Qt 生命周期、信号和 QML 交互一致 |
| 静态检查 | clang-format、clang-tidy、qmllint、qmlformat | 版本随工具链固定，禁止不同格式化器互相覆盖 |
| Linux 发布 | Flatpak 优先；AppImage 次之；原生包后续 | 运行时一致性优先；见 ADR-0007 |

Qt 的浮动文档在本次检索时显示 6.11.2。已有模板的 `REQUIRES 6.10` 是**当前事实**，本方案实施时应将版本声明与选定基线一致化；在未建立 6.10 构建／运行测试前，不宣称兼容 6.10。若目标 Flatpak Runtime 不提供此基线，应在 M0 选择可维护的 Qt 版本或评估自构建成本并更新 ADR，不能把 Runtime 的 Qt 当成任意可替换依赖。

## 2. Qt 模块使用清单

| 模块 | 引入时间 | 职责 |
| --- | --- | --- |
| Core | M1 | 值类型、对象生命周期、事件循环、线程、设置 |
| Gui / Qml / Quick | M1 | 窗口、图形、QML 运行时与场景图 |
| QuickControls2 | M1 | 控件基础与样式集成；QML Templates/Layouts 也需正确部署 |
| Network | M0/M2 | API、图片、下载 HTTP；不同信任域隔离管理器和 Cookie |
| Multimedia | 已批准并接入M2 | [无界面Qt单曲播放](api/stream-resolution-and-playback.md)，默认输出；设备切换／完整发行门禁未做 |
| Sql | M1 | SQLite 驱动及迁移 |
| DBus | M3 | Linux 系统集成；非 Linux 构建用 stub |
| Concurrent | 需要时 | 有界 CPU/文件任务；不替代有线程亲和性的网络／SQL worker |
| Svg | 按资源需要 | SVG 解码／图标；不盲目加载不可信远程 SVG |
| Test / QuickTest | M1，仅测试 | C++/QML 测试 |
| LinguistTools | M1/M4，构建时 | 翻译提取、编译，不是运行时联网翻译 |
| Widgets | M3，可选 | 只有选择 QSystemTrayIcon 时引入；主 UI 仍是 QML |

如果启用 `QSystemTrayIcon`，入口必须使用 `QApplication` 而非仅 `QGuiApplication`，并隔离 Widgets 依赖。若最终改为直接实现 StatusNotifierItem，则保持 `QGuiApplication`；M3 不得混用两套托盘实现。详见 [Linux 集成](10-linux-integration.md)。

## 3. 音频后端决策矩阵

| 方案 | 优点 | 成本／风险 | 决定 |
| --- | --- | --- | --- |
| Qt Multimedia | Qt 生命周期一致、部署体系可复用、基础播放足够 | 实际协议／格式／设备支持依赖 Qt 后端；没有由本项目验证的 gapless 保证 | 已接入，有静音试听MP3／WAV PoC；尚非完整发行验收 |
| libmpv | 成熟媒体能力、丰富播放控制 | 新增事件桥接、构建和许可证审核；Linux 打包复杂度更高 | PoC 失败或高级音频成为硬需求时评估 |
| 直接 FFmpeg + 自建音频输出 | 控制力强 | 自行承担解码、时钟、缓冲、重采样、设备和大量测试 | 首版不选 |
| GStreamer 自有管线 | Linux 管线扩展性强 | 插件矩阵和平台差异增加维护面 | 特定需求的备选，不与默认实现并行维护 |

**禁止混淆**：Qt Multimedia 内部可能使用 FFmpeg，不等于 Cirrus 已有 FFmpeg 底层任意扩展能力。Qt 的媒体线程和网络不会自动复用应用的 `QNetworkAccessManager`、CookieJar 或缓存。[Qt 媒体资料](16-references.md)仅证明 API 存在，真实流、鉴权及打包仍须验证。

## 4. 第三方依赖准入

引入依赖必须记录：目的、上游、固定版本／提交、SHA-256、许可证、维护活跃度、传递依赖、构建方式、运行时体积、CVE 跟踪及可替代方案。

- 优先 Qt 内建功能，但不能为了“零依赖”自制密码学或不成熟的媒体管线。
- 禁止 `FetchContent` 指向浮动 `main`／`master`；CI 不在每次构建中取未知最新版本。
- 测试依赖也要固定；离线／受控环境应能从已验证源包重建。
- Qt/FFmpeg 的构建配置改变可能改变许可条件；不是“所有 FFmpeg 都同一种许可证”。
- 首个正式发布前确认 Cirrus 自身许可证。本文不擅自替维护者授予许可。

## 5. 当前不引入

不引入 Electron、WebEngine 全页面 UI、Node.js 常驻 API 中间层、Python 运行时、Redis、云端自建账号系统、通用 DI 框架、跨进程事件总线、插件 ABI 和多 Provider 市场。

登录如确实依赖浏览器，应优先系统浏览器加受约束回调；只有确认官方流程需要且不存在更简单方式时，单独评审 WebEngine。不得为了一个不确定登录路径预先把整个 Chromium 打进包。

## 6. 升级策略

1. 锁定当前工具链与依赖清单；生成可重建基线。
2. Qt 补丁升级跑 API fixture、音频格式、设备切换、QML、MPRIS 和安装包测试。
3. Qt 次版本升级先在独立分支验证，不只检查“能编译”。
4. 安全修复可加速流程，但不能取消数据迁移与播放回归。
5. 每次稳定发布公布实际 Qt、音频后端、编译器、运行时和支持平台，避免宽泛的“支持所有 Linux”。
