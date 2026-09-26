# Cirrus

**Cirrus** 是面向 Linux 桌面的第三方网易云音乐客户端项目，仓库／工程名称为 **CirrusMusic**。计划使用 C++ 与 Qt Quick/QML，构建以歌曲播放、音乐发现和音乐库管理为中心的桌面体验。

> 当前状态：Qt 空项目骨架 + VS Code 开发环境 + 开发技术文档，尚未实现音乐客户端功能。本文中的产品目标不是已交付功能；本项目不是网易云音乐官方客户端。

## 开发文档

从 **[技术文档总览](docs/README.md)** 开始阅读。

- [产品范围与需求](docs/01-product-requirements.md)
- [技术栈](docs/02-technology-stack.md) · [系统架构](docs/03-system-architecture.md)
- [网易云 API 与客户端后端](docs/05-api-and-backend.md)
- [播放引擎](docs/06-playback-and-audio.md) · [自定义 QML 控件体系](docs/07-qml-and-design-system.md)
- [实施路线与任务拆解](docs/14-roadmap-and-backlog.md)
- [待确认问题](docs/15-risks-and-open-questions.md) · [API 交接清单](docs/checklists/api-handoff.md)

## VS Code 与当前骨架构建

完整说明见 **[VS Code 开发指南](docs/17-vscode-development.md)**。需要 CMake ≥ 3.24、Ninja、C++ 编译器及 Qt 开发 Kit；模板要求 Qt 6.10+，当前已验证 Qt 6.11.2。

首次克隆时复制 `CMakeUserPresets.json.example` 为 `CMakeUserPresets.json`，填写其中两处 Qt Kit 路径。本次已为当前机器配置好私有文件，不必重复覆盖；个人路径不会提交。

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --parallel
./build/linux-dev-local/appCirrusMusic
```

在 VS Code 中安装推荐扩展，选择 **Linux Debug (local Qt)** configure preset 和对应 build preset；`Ctrl+Shift+B` 构建，`F5` 使用 **Cirrus: Debug (GDB)** 调试。另有 QML lint 与 clean 任务。

Debug/Release 构建、qmllint、clangd 检查、GDB main 断点和 offscreen 启动冒烟已验证。当前仍没有 CI、业务自动化测试或正式发布包；未来规范见[工程与交付](docs/13-engineering-and-delivery.md)。

## 边界

- Cirrus 内置对接网易云官方 API，用户无需配置 API 地址或部署服务。客户端 C++ 层负责协议适配、请求签名／加密与会话管理；具体接口资料尚未放入此仓库。
- 所有音质、下载、离线和账号操作受服务端能力、账号权限及适用条款约束，不提供付费／版权限制绕过功能。
- 项目许可证尚待维护者确认；未添加许可证不等于允许自由复制分发。依赖许可与发布门禁见[安全、隐私与合规](docs/11-security-privacy-and-compliance.md)。
