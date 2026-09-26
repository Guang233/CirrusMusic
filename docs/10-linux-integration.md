# 10 · Linux 桌面集成

> 状态：提案。Linux 不是单一桌面；Wayland、X11、GNOME、KDE 与沙箱环境分别验证，不能靠开发机“看起来正常”外推。

## 1. 支持矩阵

| 平台层 | 首版目标 | 边界 |
| --- | --- | --- |
| 架构 | x86_64 | aarch64 在有真实硬件与 CI 后加入 |
| 桌面 | GNOME Wayland、KDE Plasma Wayland | GNOME 不依赖托盘扩展；KDE 不依赖私有协议 |
| 兼容会话 | 至少一个 X11 桌面实测 | 不是所有发行版组合承诺 |
| 音频 | 主流 PipeWire 桌面及实际 Qt 支持路径 | 可能经 PulseAudio 兼容层；不得宣称 Qt 必然直接使用 PipeWire |
| 安装形式 | Flatpak 主交付；AppImage 候选 | 两者各有真机安装／运行测试，不能只测裸二进制 |
| 最低发行环境 | M0 根据 Qt ABI 与 Runtime 固定 | 未确认前不写“支持任意 glibc 版本” |

发布记录要列实际发行版、桌面版本、会话、GPU、音频服务、Qt 后端和包格式。

## 2. 应用身份与桌面入口

产品显示名为 Cirrus；CMake 工程／构建 target 可继续使用 CirrusMusic／appCirrusMusic。正式 app ID 使用维护者拥有命名空间的反向域名，例如 `io.github.<owner>.Cirrus` 只是占位，不能原样发布。

M0 冻结以下一致值：app ID、desktop 文件名、图标 ID、Qt organization/applicationName、D-Bus 名称、Flatpak ID、AppStream 元数据 ID。改变它们会影响路径、权限和桌面识别，必须有迁移说明。

`.desktop` 需正确 Name、Comment、Exec、Icon、Categories、StartupNotify；仅在实现了 URI 参数处理后声明对应 `%U`/scheme。AppStream 提供简介、截图、发布说明、许可证和项目链接，不使用未拥有的官方标识。

## 3. MPRIS 2.2

实现 session bus 上的标准对象 `/org/mpris/MediaPlayer2`，候选总线名 `org.mpris.MediaPlayer2.Cirrus`。核心实现 `org.mpris.MediaPlayer2` 与 `org.mpris.MediaPlayer2.Player`，可选 TrackList/Playlists 接口不作为首版必需。

| MPRIS 能力 | Cirrus 映射 |
| --- | --- |
| Play/Pause/PlayPause/Stop/Next/Previous | 调用同一个 PlaybackCoordinator，不模拟按钮点击 |
| PlaybackStatus | 依据实际媒体状态与用户意图映射 Playing/Paused/Stopped；Buffering 不假装新曲已开始 |
| LoopStatus | None/Track/Playlist → repeat Off/One/All |
| Shuffle | QueueService 的顺序模式 |
| Volume | 线性音量，内部 UI 感知曲线单独处理；不暗中放大超过产品范围 |
| Rate、MinimumRate、MaximumRate | 不支持倍速时全部按规范报告 1.0，不能暴露假能力 |
| Position | 内部毫秒转为有符号 64 位微秒 |
| Metadata | 稳定 track object path、title、artist 数组、album、length、artUrl（可用时） |
| CanSeek/CanGoNext/CanGoPrevious 等 | 依据当前媒体和队列实时计算 |

关键规范：

- track object path 基于 QueueEntryId，使用 D-Bus 合法字符编码，不直接塞含连字符 UUID 或外部大 ID。
- Position 自然递增时**不发送** `PropertiesChanged` 风暴；seek 等不连续位置变化发送 `Seeked`。
- `SetPosition` 的 TrackId 与当前项不匹配时忽略；超出范围的绝对位置按规范处理，不作用于下一首。
- `Seek` 是相对偏移，跨越曲尾／曲首的行为按规范实现并测试，不能简单混用 UI 的 clamp 逻辑。
- `mpris:artUrl` 不含 Cookie 或签名秘密；使用可被桌面实际读取的安全封面位置。沙箱内私有路径不一定能被宿主读取，需验证导出／公共封面策略；不可用就省略。
- `Raise` 在 Wayland 需要合理激活上下文；桌面拒绝抢焦点时不要循环置顶。
- `OpenUri` 只接受已声明且验证的格式；外部 D-Bus 输入和 CLI 输入同样不可信。
- 系统媒体元数据会被其他本地程序读取。可提供隐藏封面／曲目信息的隐私模式，说明取舍。

验收用实际桌面媒体控件与 `playerctl` 等诊断工具；后者不是运行依赖。

## 4. 托盘、通知与关闭行为

推荐先将 `QSystemTrayIcon` 封装在可选 `TrayAdapter` 中。启用时链接 Qt Widgets 并使用 QApplication；禁用构建可保留 QGuiApplication。不要仅因为界面是 QML 就忽略这个入口差异。

- 启动检查托盘是否实际可用，不能只检查 API 对象创建成功。
- 无托盘时关闭窗口默认退出；若用户选择后台模式，必须仍有可理解的重新打开和退出方式。
- 不依赖 GNOME 扩展；通知功能独立于托盘是否存在，可用 D-Bus／portal Notifications 适配器。
- 首次更改“关闭到后台”解释行为，菜单始终有退出。
- 通知可禁用；切歌通知去重／限频，不在每次缓冲状态变化弹通知。
- 休眠、锁屏和账号退出不泄露私有消息；通知标题和图片可按隐私设置隐藏。

## 5. 快捷键与窗口行为

| 能力 | 推荐实现 | 降级 |
| --- | --- | --- |
| 应用内快捷键 | Qt Shortcut/Action，统一命令表 | 始终可用，尊重文本输入焦点 |
| 媒体键 | 优先桌面对 MPRIS 的路由 | 不同时全局抢占同一个媒体键造成双触发 |
| 全局自定义快捷键 | 探测 xdg-desktop-portal GlobalShortcuts 版本与后端 | 不支持／拒绝授权时仅应用内快捷键 |
| 窗口拖动／缩放 | 系统标题栏优先；自绘时使用系统 move/resize 请求 | 不用手写绝对坐标模拟所有合成器行为 |
| 桌面歌词置顶／穿透 | 按平台能力提供实验特性 | Wayland 无统一保证时使用普通独立歌词窗 |
| 激活已有窗口 | 合法桌面 activation token/平台机制 | 拒绝激活时显示可恢复入口，不强抢焦点 |

不要把 X11 的全局键盘监听／窗口坐标方案直接搬到 Wayland。Portal 方法也有版本差异，例如配置快捷键界面并非所有版本都存在；逐项探测而非只判断“有 portal”。

## 6. 文件选择与沙箱

- 首选 portal 文件／目录选择，处理用户取消、授权撤销和文档访问令牌。
- 只申请用户选择的目录，不为扫描本地音乐索要整个 HOME 或 host 文件系统。
- Flatpak 初始权限目标：网络、实际需要的音频和渲染设备、Wayland（必要时 X11 fallback）、精确 D-Bus 名称。不得不经审查授予完整 session/system bus。
- MPRIS 的 bus own-name、托盘协议、密钥环后端、通知和 portal 权限分别验证；不是“打开 D-Bus”一个开关解决全部。
- `--socket=pulseaudio` 等具体权限以最终 runtime 和音频实现为准；记录其潜在录音权限面，不因产品不录音就假设权限没有风险。
- 下载目录读写授权与本地扫描只读授权分开，拒绝权限时保持其他功能可用。
- 应用默认不监听本机 HTTP 端口；需要登录回调时另行审查精确回环监听、随机 state、期限和关闭时机。

## 7. 单实例与外部启动

建议 `QLockFile + QLocalServer` 的受限 IPC，或经验证的 D-Bus 单实例方案，二选一形成正式实现。首版可用前者：锁与 socket 放 XDG_RUNTIME_DIR 下应用私有位置；检查 stale lock 后安全恢复。

第二次启动仅传白名单动作（激活、打开已验证文件／官方分享链接），消息设置长度上限，验证来源权限，不传可执行脚本或 shell 命令。异常锁不能无限阻止启动，也不能强行杀掉未知 PID。

## 8. 设备、休眠和屏幕

- QMediaDevices 监控默认输出和设备变化；设备消失默认暂停而非突然切到扬声器，恢复由用户确认或明确设置控制。
- 休眠前保存位置；唤醒校验会话和来源有效期，不无条件大音量自动恢复。
- 不为普通音乐播放禁止系统锁屏／休眠；如提供阻止休眠选项，使用标准机制并解释影响。
- 多屏不同 DPR 下重新计算图片尺寸与窗口约束；不把物理像素当逻辑尺寸。
- 软件渲染或 GPU 特性不足时降低背景效果，核心播放和文字界面仍可使用。

## 9. 发布验证

至少覆盖：全新用户首次启动、无托盘、密钥环锁定、portal 缺能力、系统主题改变、100–200% 缩放、蓝牙拔插、休眠唤醒、双实例、离线启动、AppImage 宿主缺插件以及 Flatpak 权限被拒绝。结果录入 [TC-LINUX 与发布检查表](checklists/release.md)。
