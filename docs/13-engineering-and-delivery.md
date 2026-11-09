# 13 · 工程规范、构建与交付

> 状态：工程目标提案；第 3 节标明已提供的基础 presets，其余未明确实施的架构、测试、CI 和发布流程仍是目标。当前可用的 VS Code 操作见[17](17-vscode-development.md)。

## 1. 从现有模板迁移

建议分成可回退的小 PR，而不是一次把空工程改成无法运行的大目录树：

1. 冻结 Qt／编译器／app ID；明确 C++20、CMake 最低版本、必要 Qt 模块。
2. 建立基础库与 tests，保留现有根 QML URI `CirrusMusic`。
3. 将启动装配迁移到 `src/app/`，建立 application/domain 边界。
4. 用 C++ 单测／CLI 验证协议、会话、目录和播放后端，持续保留空窗口可启动。
5. 按契约和门禁逐项接入真实接口，再完成写操作和文件相关后端。
6. 后端验收后才创建 `Cirrus.Controls`、Gallery 和全部 UI；以最新路线图为准。

`importedcontent/` 仅是模板占位，若不采用 Figma-to-Qt 导入可在独立清理任务移除。不要把设计工具生成代码作为业务逻辑层长期维护。

## 2. CMake 规则

- 使用 target 级 include、compile options、link libraries；避免全局 include_directories 和无边界链接。
- 显式 `CXX_STANDARD 20`／`target_compile_features`、标准必需、关闭不需要的 compiler extensions。
- Qt 动态链接及插件部署策略在许可审查中记录；自有静态模块不等于静态链接所有 Qt。
- Qt 版本声明、CI Kit、Runtime 和文档一致；不写开发机绝对路径。
- QML 模块用 `qt_add_qml_module`，URI、资源前缀、输出目录及导入路径可解释；启用标准 Qt CMake policies。
- 通常使用 `/qt/qml/` 标准资源前缀，不到处手工 `addImportPath` 修补错误打包。
- 依赖通过明确 target 链接，QML module 依赖也声明，不以 IDE 自动找到为准。
- `BUILD_TESTING` 控制测试；正式安装不包含 fixtures、mock 配置、开发 Cookie 或测试签名源。

下例只是方向，不是完整可复制的 CMakeLists：

```cmake
cmake_minimum_required(VERSION 3.24)
project(CirrusMusic VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Qt6 6.11.2 REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2 Network Multimedia Sql DBus)
qt_standard_project_setup(REQUIRES 6.11)
include(CTest)
# 后续 add_subdirectory、target 依赖、QML modules、install、测试注册省略。
```

Qt 6.11.2 是当前建议验证基线，若 M0 调整需同步改上例。Widgets、Svg、Concurrent、Test 等按模块需要另行声明，不能只加头文件而漏链接和部署。

## 3. Presets 与开发命令

已提供共享 `CMakePresets.json`（schema 5，要求 CMake ≥ 3.24）、私有 `CMakeUserPresets.json` 的示例及 VS Code 工作区配置：

| 配置名 | 用途／当前状态 |
| --- | --- |
| linux-dev | 已提供：Debug、Ninja、C++20、compile_commands；已有10个后端离线 Qt Test 套件 |
| linux-release | 已提供：本地 Release 构建；不是发行包 |
| linux-dev-local / linux-release-local | 私有示例继承共享配置，指定本机 Qt Kit，实际私有文件不入库 |
| linux-asan | 待 M1 实现：Debug/RelWithDebInfo、ASan/UBSan |

系统／环境可发现 Qt 时可直接使用共享配置：

```bash
cmake --preset linux-dev
cmake --build --preset linux-dev
cmake --build --preset linux-dev --target all_qmllint
ctest --test-dir build/linux-dev --output-on-failure --no-tests=error
```

非系统 Qt 安装按[17](17-vscode-development.md)设置私有 preset。已有 configurePresets、buildPresets 和协议／会话／认证事务 CTest 用例，尚无 testPresets；当前用 `ctest --test-dir build/linux-dev-local --output-on-failure --no-tests=error` 执行。`BUILD_TESTING=OFF` 不构建测试；已有离线认证事务测试，但音乐业务／真实 API 验证仍未完成。VS Code 默认构建 all，而非仅空壳 app；提供先构建再运行的 `Cirrus: test` 任务。

构建目录、私有 preset、compile_commands 和本机缓存已加入忽略规则。Qt 6.11.2 下已验证聚合 target `all_qmllint`；未来新增模块应继续检查 QML 导入和生成类型。无测试时 CI 应失败，不能把 CTest 的“未发现测试”当成功。

## 4. 编码规范

### C++

- 类 PascalCase，函数／变量 camelCase，命名空间 `cirrus::<module>`；统一风格比个人偏好重要。
- 资源 RAII，非拥有指针说明生命周期；QObject parent 与智能指针不得重复拥有。
- QObject 析构、线程退出、Reply 清理都要有测试；不以 `QThread::terminate()` 正常关闭。
- Qt signal/slot 使用类型安全连接，回调带 context，避免捕获悬空 this。
- 不在 GUI 阻塞等待网络／SQL，禁止随意嵌套事件循环。
- 时间／ID／字节数有单位命名，空值显式表示；业务错误返回统一 Result。
- 公共接口注释说明所有权、线程、取消、失败和单位，而不是复述函数名。

### QML

- 属性有明确类型，使用 required property 表达外部依赖；不要让所有数据都变成 var。
- 对外命令用 signal，页面尽量不跨多个父级寻找对象。
- 视觉值来自 tokens；业务状态来自 ViewModel；不在 onCompleted 偷偷发全局网络请求。
- 避免一个超大 Main.qml；页面、组件、控件各守边界。
- 新增控件同时添加 Gallery 和测试；禁止以“暂时 MouseArea”永久替代交互语义。

### 日志与错误

全应用日志见 [tasks/logging.md](tasks/logging.md)：分类表（§4.1，`src/logging/categories.h`）、级别（§4.2）、格式与写法（§4.3–§4.4）、脱敏（§5）、约定（§9）。日志在边界脱敏；用户看到翻译后的错误，诊断记录稳定英文 eventName。错误必须处理、传递或明确说明忽略理由。

## 5. 分支与评审

首个提交按维护者要求收录 Qt 模板、技术文档和 VS Code 开发配置。后续建议短分支 + 小 PR，维护者决定主分支名称，不强行更名现有 master；提交、推送和发布分别遵循用户授权。

PR 模板至少包含：需求／任务 ID、设计变化、测试证据、截图（若 UI）、线程和权限影响、迁移、依赖／许可变更、已知限制。

评审重点：状态唯一性、异步过期结果、API 字段泄漏、账号隔离、错误语义、GUI 阻塞、可访问性和部署影响。涉及账号／数据删除／下载／更新的变更必须做专门安全检查。

## 6. CI 设计

| 阶段 | 内容 | 失败策略 |
| --- | --- | --- |
| 静态 | C++ 格式、QML lint、文档相对链接、依赖边界、秘密扫描 | PR 阻塞 |
| 构建 | 固定 Qt + GCC；第二编译器定期或每 PR | 主组合阻塞 |
| 自动测试 | unit、contract、SQLite、QML 基础、端到端 fake | 阻塞；保存脱敏日志 |
| Sanitizer | ASan/UBSan；并发核心专项 | 按频率执行，新增问题阻塞合并／发布 |
| 包构建 | Flatpak，后续 AppImage；哈希与依赖清单 | 发布阻塞 |
| 包验收 | 干净 runtime 启动、QML/import、codecs、MPRIS、密钥环 | 发布阻塞 |
| 真实协议 | 显式触发的受控账号冒烟 | 发布需最近有效证据，服务故障单独记录 |
| 真机质量 | GPU、音频设备、Wayland、无障碍、长期播放 | 稳定版阻塞 |

未经信任的 PR 不运行带秘密的工作流。缓存 key 包含 Qt、编译器、架构和依赖锁，避免混用 ABI。CI 保留版本、命令、退出码及报告路径；不能只保留一张绿色截图。

## 7. 打包与运行时依赖

### Flatpak（首选）

- M0 选择能满足 Qt 基线的维护中 Runtime/SDK 分支，固定 manifest 和 sources 哈希；目前尚未确认具体分支。
- 无匹配 Runtime 时重新评估 Qt 基线或自构建成本，不伪称运行时已具备所需 Qt。
- 最小权限、portal 文件选择、精确 D-Bus、密钥环访问与音频能力逐项验证。
- 不把测试数据、开发工具、完整 SDK 和构建凭据带入运行包。
- 更新遵循 Flatpak 机制；回滚应用版本不代表可以回滚已迁移用户数据库。

### AppImage（次选）

- 固定构建环境与最低宿主 ABI；收集 Qt QML modules、platform plugins、Multimedia 后端、FFmpeg 及 TLS 运行依赖。
- 不随意打包宿主 glibc、GPU 驱动等应由系统提供的组件。
- 在没有开发版 Qt 的干净系统测试 Wayland/X11、图片插件、音频格式和证书路径。
- 发布哈希、签名和依赖说明；不要只发一个无法验证来源的可执行文件。

DEB/RPM、AUR 等在核心发行流程稳定后增加；每种格式有维护责任，不把打包脚本存在等同正式支持。

## 8. 版本与发布

推荐语义版本 `MAJOR.MINOR.PATCH`，预发布 `-alpha.N/-beta.N/-rc.N`。产品版本、DB schema version、API adapter revision 分开，API 变更不一定需要改数据库。

发布步骤：冻结范围 → 更新需求覆盖／风险 → 跑测试矩阵 → 构建可追溯产物 → 签名与 SBOM／许可证 → 干净安装和升级测试 → 发布说明与已知限制 → 小范围试用 → 稳定渠道。

发布说明明确哪些 P1 因官方能力或桌面限制降级；禁止把 mock 截图作为已可用功能宣传。线上问题可关闭受影响 UI 能力，但不引入可从网络执行任意代码的“热修复”。协议修复优先通过可验证应用更新交付。

## 9. 开发者入门完成标准

**当前可执行标准**：按 [HANDOFF](HANDOFF.md) 获取完整提交 → 配置自己的 Qt 路径 → 完整构建后端和测试 → 通过 Debug/Release CTest → 找到认证 fixture → 理解 token／凭据边界 → 参照[轮询与总时限任务](tasks/B-010-qr-polling.md)的契约格式领取下一任务。干净源码复验证据见[验证记录](handoff-verification.md)。不需要维护者私人账号或自建 API 服务。

**未来产品阶段标准，尚未具备**：运行 fake 数据应用、播放本地样本、打开 Gallery、启用受控真实协议测试。当前没有这些入口，不把它们列作新人今天必须完成的步骤。
