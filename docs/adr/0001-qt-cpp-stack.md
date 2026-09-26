# ADR-0001 · C++20 与 Qt Quick/QML 技术基线

- 日期：2026-09-26
- 状态：Qt Quick/QML 为既定约束；具体版本与依赖为 Proposed
- 负责人：维护者

## 背景

项目已有 Qt Quick 空模板；本机 Kit 为 Qt 6.11.2，模板最低设置为 6.10。Linux、现代自绘界面、音频和长期维护是主要目标。

## 决策

以 C++20 实现业务／基础设施，QML 实现界面。首个验证基线为 Qt 6.11.2、CMake ≥ 3.24、Ninja、GCC 13，Clang 18 作为第二编译组合。Qt 版本与 Runtime 在 M0 一致冻结，正式构建不依赖本机绝对路径。

## 备选

- Qt Widgets 作为主 UI：不符合既定 QML 方向；只允许托盘等受控可选依赖。
- Electron/WebEngine 主 UI：增加运行时与第二技术栈，不选。
- 全部业务放 QML/JavaScript：测试、类型和并发边界较弱，不选。
- 首版覆盖多个 Qt 次版本：增加验证面，暂不承诺。

## 后果

获得原生 Qt 生态和单一语言后端；承担 Qt 插件、FFmpeg、Linux ABI 和许可管理。Qt 核心值类型可进入 domain，但 Network/Sql/QML 不得进入领域规则。

## 验证与复议

M0 验证目标 Runtime、编译器 ABI、音频和最小安装包。若 Qt 6.11.2 无可维护的发布路径，可调整版本并更新文档／CI，不静默保留冲突约束。安全维护期、官方 Qt 升级或目标平台改变时复议。

详见[技术栈](../02-technology-stack.md)。
