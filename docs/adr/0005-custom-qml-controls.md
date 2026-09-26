# ADR-0005 · 自有控件体系与 Qt Templates

- 日期：2026-09-26
- 状态：自绘视觉为既定需求；具体工程路线 Proposed
- 负责人：UI／维护者

## 背景

默认 Qt 外观不符合产品目标，但从 Rectangle/MouseArea 重写所有行为会损失输入法、焦点、键盘和无障碍。

## 决策

建立 `Cirrus.Controls` 模块，首版显式使用 CButton/CSlider 等自有组件，基于 QtQuick.Templates，必要时组合 Basic。Tokens、控件、业务组件、页面分层；Gallery 和测试先于批量页面开发。

暂不要求第一天提供覆盖全部 Qt Controls 的全局 style。未来如采用完整 style，遵守 Templates 根类型、qmldir 和 fallback 规范。

## 备选

- 直接使用平台默认样式：不符合视觉需求。
- 全部原始图元自制交互：长期质量成本高。
- 引入整套不透明第三方 UI 库：可能限制风格、增加维护／许可风险，按组件需要审查而非默认采用。

## 后果

外观可独立演进，保留成熟交互基础。仍须实际测试可访问性，继承 Templates 不等于自动通过。Apple Music 仅作播放页体验参考，不要求仿制整套系统 UI。

## 验证与复议

M1 Gallery 验证按钮、输入、滑块、键盘、IME、明暗主题；M2 验证列表和播放器性能。若组件覆盖足够且需统一第三方控件外观，再评估完整 style。

详见[QML 与设计系统](../07-qml-and-design-system.md)。
