# ADR-0007 · Linux 优先与 Flatpak 主交付

- 日期：2026-09-26
- 状态：Proposed，Runtime 与 app ID 尚待确认
- 负责人：发布／维护者

## 背景

Qt、QML 插件、音频后端、FFmpeg 和宿主 ABI 组合会影响安装可靠性。目标是填补 Linux 客户端空白，而不是同时维护所有平台。

## 决策

首版目标 x86_64 Linux，核心验收覆盖 GNOME Wayland、KDE Wayland 和至少一个 X11 环境。Flatpak 为主交付候选，AppImage 次选；DEB/RPM/ARM64 后续按维护资源增加。

M0 确认维护中的 Runtime/SDK 是否支持选定 Qt 基线；不预设某个分支已具备 Qt 6.11.2。最小权限、portal、MPRIS、密钥环和编解码器在真实包内验证。

## 备选

- 只发裸二进制：依赖安装和 ABI 问题转嫁用户，不选。
- 首版所有原生包：测试矩阵扩张过快，不选。
- 默认开放 host 文件系统／所有 D-Bus：方便但权限过宽，不选。

## 后果

获得较一致的运行时，同时承担沙箱适配和 runtime 生命周期管理。AppImage 需独立宿主 ABI／插件测试；一个格式成功不能替代另一个格式验收。Qt 或 Runtime 决策变化必须同步 ADR-0001。

## 验证与复议

M0 最小包启动和音频验证；M4 干净安装、升级、文件授权、密钥环、MPRIS 与卸载保留数据规则通过。目标用户平台、Runtime 生命周期或包维护资源变化时复议。

详见[Linux 集成](../10-linux-integration.md)及[工程交付](../13-engineering-and-delivery.md)。
