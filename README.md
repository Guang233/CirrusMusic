# Cirrus

**Cirrus** 是面向 Linux 桌面的第三方网易云音乐客户端项目，仓库／工程名称为 **CirrusMusic**。计划使用 C++ 与 Qt Quick/QML，构建以歌曲播放、音乐发现和音乐库管理为中心的桌面体验。

> 当前状态：Qt 空项目骨架 + VS Code 开发环境 + 开发技术文档 + 固定版本 API 静态审计 + C++ 协议基础及 Qt Test，尚未实现可用的音乐客户端功能。本文中的产品目标不是已交付功能；本项目不是网易云音乐官方客户端。

## 开发文档

从 **[技术文档总览](docs/README.md)** 开始阅读。

- [产品范围与需求](docs/01-product-requirements.md)
- [技术栈](docs/02-technology-stack.md) · [系统架构](docs/03-system-architecture.md)
- [网易云 API 与客户端后端](docs/05-api-and-backend.md)
- [API 审计与移植清单](docs/api/README.md)：440 个上游模块，164 个采用候选、40 个待定、236 个排除；尚未真实验证
- [已实现的 C++ 协议基础与测试](docs/api/native-foundation.md)
- [播放引擎](docs/06-playback-and-audio.md) · [自定义 QML 控件体系](docs/07-qml-and-design-system.md)
- [实施路线与任务拆解](docs/14-roadmap-and-backlog.md)
- [待确认问题](docs/15-risks-and-open-questions.md) · [API 交接清单](docs/checklists/api-handoff.md)

## VS Code 与当前骨架构建

完整说明见 **[VS Code 开发指南](docs/17-vscode-development.md)**。需要 CMake ≥ 3.24、Ninja、C++20 编译器、OpenSSL ≥ 3、zlib 开发包及 Qt 开发 Kit（含 Network，测试另需 Test）；模板要求 Qt 6.10+，当前已验证 Qt 6.11.2。

首次克隆时复制 `CMakeUserPresets.json.example` 为 `CMakeUserPresets.json`，填写其中两处 Qt Kit 路径。本次已为当前机器配置好私有文件，不必重复覆盖；个人路径不会提交。

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --parallel
ctest --test-dir build/linux-dev-local --output-on-failure --no-tests=error
./build/linux-dev-local/appCirrusMusic
```

在 VS Code 中安装推荐扩展，选择 **Linux Debug (local Qt)** configure preset 和对应 build preset；`Ctrl+Shift+B` 构建，`F5` 使用 **Cirrus: Debug (GDB)** 调试。另有 QML lint 与 clean 任务。

Debug/Release 构建、qmllint、clangd 检查、GDB main 断点和 offscreen 启动冒烟已验证。C++ 协议、网络、响应解码和会话隔离的 6 个测试套件已通过 Debug/Release CTest；当前仍没有 CI、真实 API／完整业务测试或正式发布包；未来规范见[工程与交付](docs/13-engineering-and-delivery.md)。

## 边界

- Cirrus 内置对接网易云官方 API，用户无需配置 API 地址或部署服务。客户端 C++ 层负责协议适配、请求签名／加密与会话管理；参考 api-enhanced 源码但不运行 Node.js 服务。
- 产品代码使用 C++/Qt；`tools/` 中 Python 仅用于离线接口审计校验，不是客户端运行依赖。
- 后端优先、UI 最后；包括每日推荐歌曲、私人 FM 和一起听。明确排除视频、电台／播客、云贝、动态和云小编。
- 所有音质、下载、离线和账号操作受服务端能力、账号权限及适用条款约束，不提供付费／版权限制绕过功能。
- 上游参考材料的 MIT 通知见 [third_party/api-enhanced](third_party/api-enhanced/NOTICE.md)。项目自身许可证尚待维护者确认；未添加许可证不等于允许自由复制分发。依赖许可与发布门禁见[安全、隐私与合规](docs/11-security-privacy-and-compliance.md)。
