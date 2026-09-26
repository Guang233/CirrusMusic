# 16 · 外部规范与参考资料

> 查阅日期：2026-09-26。只列本次实际查阅的资料。在线 `qt-6` 文档会随版本变化；本次页面显示 Qt 6.11/6.11.2，实施时以锁定版本再次核对。

## 1. Qt 官方资料

| ID | 资料 | 用途与结论边界 |
| --- | --- | --- |
| REF-QT-01 | [Customizing Qt Quick Controls](https://doc.qt.io/qt-6/qtquickcontrols-customize.html) | 自绘 Controls 与完整自定义 style 的区别；Templates 根类型和 fallback 规则。不能据此省略键盘／无障碍测试 |
| REF-QT-02 | [Defining QML Types from C++](https://doc.qt.io/qt-6/qtqml-cppintegration-definetypes.html) | 声明式类型注册、QML_ELEMENT/UNCREATABLE/SINGLETON 等；支持清晰 C++/QML 边界 |
| REF-QT-03 | [Threads and QObjects](https://doc.qt.io/qt-6/threads-qobject.html) | QObject 线程亲和性、事件循环、queued connections；网络对象不能任意跨线程调用 |
| REF-QT-04 | [QSqlDatabase](https://doc.qt.io/qt-6/qsqldatabase.html) | SQL 连接线程约束、连接生命周期；较新版本的 moveToThread 也有约束，不代表可并发共享连接 |
| REF-QT-05 | [QNetworkAccessManager](https://doc.qt.io/qt-6/qnetworkaccessmanager.html) | 异步 HTTP 和所属线程；业务 deadline／重试仍由应用实现 |
| REF-QT-06 | [Qt Multimedia](https://doc.qt.io/qt-6/qtmultimedia-index.html) | FFmpeg 后端、平台差异和许可说明；需要对目标平台测试 |
| REF-QT-07 | [QMediaPlayer](https://doc.qt.io/qt-6/qmediaplayer.html) | 实际播放状态、媒体状态、seek、后端相关能力；不自动赋予应用 gapless/DSP 等产品保证 |
| REF-QT-08 | [FFmpeg attribution](https://doc.qt.io/qt-6/qtmultimedia-attribution-ffmpeg.html) | 当前 Qt 所附 FFmpeg 及许可文本。本次页面为 FFmpeg 7.1.3，不表示所有 Qt／发行版都用同一构建 |
| REF-QT-09 | [Qt Quick performance](https://doc.qt.io/qt-6/qtquick-performance.html) | 虚拟化、图像、绑定和渲染性能的设计依据 |
| REF-QT-10 | [Accessibility for Qt Quick](https://doc.qt.io/qt-6/accessible-qtquick.html) | 可访问属性与控件语义；最终要在 Linux 屏幕阅读器验证 |
| REF-QT-11 | [Qt Quick Test](https://doc.qt.io/qt-6/qtquicktest-index.html) | QML 测试工具；不替代真实图形和音频环境测试 |
| REF-QT-12 | [qmllint](https://doc.qt.io/qt-6/qtqml-tooling-qmllint.html) | QML 静态检查、类型和警告，不等于业务正确性证明 |
| REF-QT-13 | [QTP0001](https://doc.qt.io/qt-6/qt-cmake-policy-qtp0001.html) | QML 默认资源前缀 `/qt/qml/` 的 CMake policy |
| REF-QT-14 | [Qt for Linux Deployment](https://doc.qt.io/qt-6/linux-deployment.html) | Linux 运行时库／插件部署；开发机可运行不代表安装包完整 |
| REF-QT-15 | [Qt Licensing](https://doc.qt.io/qt-6/licensing.html) | Qt 的多种许可途径和模块差异；实际义务需按发布构建核对 |

## 2. Linux 桌面规范

| ID | 资料 | 用途与边界 |
| --- | --- | --- |
| REF-LINUX-01 | [MPRIS 2.2](https://specifications.freedesktop.org/mpris-spec/latest/) | 桌面媒体服务接口总规范 |
| REF-LINUX-02 | [MediaPlayer2.Player](https://specifications.freedesktop.org/mpris-spec/latest/Player_Interface.html) | Position 微秒、Seek/SetPosition、Seeked、PropertiesChanged 和能力字段规则 |
| REF-LINUX-03 | [GlobalShortcuts portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html) | Wayland 下快捷键应经 portal 和权限流程；接口版本与桌面实现均需探测 |
| REF-LINUX-04 | [Secret Service specification](https://specifications.freedesktop.org/secret-service-spec/latest/) | Linux 密钥服务接口背景。latest 当前指向草案，不假设所有桌面已实现新草案；通过实际后端验证 |
| REF-LINUX-05 | [Flatpak Sandbox Permissions](https://docs.flatpak.org/en/latest/sandbox-permissions.html) | 权限最小化、文件系统与 D-Bus 边界；不直接复制宽权限示例作为最终 manifest |

## 3. 持久化与依赖

| ID | 资料 | 用途与边界 |
| --- | --- | --- |
| REF-DATA-01 | [SQLite Write-Ahead Logging](https://www.sqlite.org/wal.html) | WAL 的文件系统约束、checkpoint 和备份注意点 |
| REF-DEP-01 | [QtKeychain upstream](https://github.com/frankosterfeld/qtkeychain) | 系统凭据存储封装候选；锁定版本后再审查后端与 fallback 行为 |

TagLib、libmpv、OpenSSL 具体版本和打包配置属于 M0/M4 依赖验证任务；本次未对它们的每个版本或许可证组合做实测，不将其写成已验证兼容矩阵。

## 4. 官方网易云 API 资料的来源原则

此项目以**维护者提供的网易云官方 API 资料**为准。当前仓库尚无这些资料，因此本套文档没有声称任何具体官方路径、签名算法、参数或返回字段已经验证，也没有将某个第三方 API 仓库当成官方契约。

交接后建立内部索引：operation → 官方资料版本／样例 → 脱敏 fixture → signer/DTO/mapper → 需求 → 测试。某些资料如不宜公开，应在仓库只保留非敏感契约摘要与测试向量，不提交凭据或私人数据。

## 5. 可验证结论与待验证结论

文档资料支持：Qt 自定义控件路线、线程约束、QML 类型注册、MPRIS 单位与通知规则、Flatpak 权限和 SQLite WAL 原理。

仍需实测：Cirrus 的实际官方请求、账户流程、CDN 鉴权、可播格式、设备切换、首声延迟、GPU 性能、Qt/Runtime 组合、沙箱密钥环以及真实无障碍体验。引用官方文档不等于这些产品指标已经通过。
