# 17 · VS Code 开发环境

> 状态：**已提供的工作区配置**，不是未来架构提案。已有原生后端，当前套件数量与定向验证见HANDOFF；单曲播放有无界面PoC，尚未接模板UI，没有CI或发布包。新接手者先读 [HANDOFF](HANDOFF.md)。

## 1. 工具与扩展

构建需要 CMake ≥ 3.24（Presets 工作流）、Ninja、C++20 编译器、OpenSSL ≥ 3、zlib、Qt6Keychain开发包，以及 Qt 6.10+ 开发 Kit（Core、Quick／QML、Network、Sql／QSQLITE、Multimedia及音频后端插件；模板需要 QtQuick.Layouts／QtQuick.Controls.Basic 模块，默认开启测试时还需 Test）。调试／编辑另需 GDB、clangd；Python 3 仅供可选的离线审计工具使用，不参与产品构建和运行。当前实测 Qt 为 6.11.2；发行工具链仍按 02/13 的后续流程冻结。

打开仓库根目录，安装 `.vscode/extensions.json` 推荐的扩展：

| 扩展 | 职责 |
| --- | --- |
| CMake Tools | 选择 Preset、配置／构建、获取实际可执行文件路径 |
| clangd | C++ 补全、跳转、诊断、格式化 |
| Microsoft C/C++ | `cppdbg`/GDB 调试；其 IntelliSense 在本工作区关闭，避免与 clangd 重复 |
| Qt Core / Qt C++ / Qt QML | Qt 安装集成、QML 语言服务与格式化 |
| EditorConfig | 统一缩进、编码和换行 |

这些设置仅作用于项目，不卸载或修改用户全局扩展。其他 QML 格式化扩展可以保留，但本工作区的默认 formatter 是 Qt 官方扩展。

## 2. 配置 Qt 路径

提交的 `CMakePresets.json` 不含个人路径，提供 `linux-dev` 和 `linux-release`。标准系统位置的 Qt 或环境中的 CMAKE_PREFIX_PATH 可直接被 CMake 查找。

非系统安装（例如 Qt Online Installer）使用私有 preset：

```bash
cp CMakeUserPresets.json.example CMakeUserPresets.json
```

将新文件中**两处** `/absolute/path/to/Qt/gcc_64` 改为实际 Qt Kit 目录。该文件已加入 `.gitignore`，不要提交。

本次配置已在当前机器创建此文件并指向现有 Qt 6.11.2；当前用户不必再次复制覆盖。新克隆／换机器时才执行上述步骤。

私有预设继承公共参数：

| configure/build preset | 构建目录 | 用途 |
| --- | --- | --- |
| linux-dev-local | build/linux-dev-local | 本机 Qt，Debug |
| linux-release-local | build/linux-release-local | 本机 Qt，Release |
| linux-dev | build/linux-dev | 系统／环境可发现的 Qt，Debug |
| linux-release | build/linux-release | 系统／环境可发现的 Qt，Release |

若不使用私有文件，也可以首次手工指定：

```bash
cmake --preset linux-dev -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.2/gcc_64"
cmake --build --preset linux-dev
```

这里的 Qt 路径只是常见安装布局示例，按实际情况替换。改变 Qt Kit／编译器后应使用新构建目录或清理对应 CMake cache，不能混用不兼容缓存。

## 3. 第一次打开 VS Code

1. 从仓库根目录执行 `code .`，只在信任该仓库时允许执行项目任务。
2. 执行 **CMake: Select Configure Preset**，本机选择 **Linux Debug (local Qt)**（`linux-dev-local`）。
3. 执行 **CMake: Select Build Preset**，选择对应的 `linux-dev-local`。CMake Tools 通常会自动匹配，但首次使用应确认。
4. 若未自动配置，执行 **CMake: Configure** 或任务 `Cirrus: configure`。
5. `Ctrl+Shift+B` 构建；`F5` 选择 **Cirrus: Debug (GDB)**；`Ctrl+F5` 使用同一启动配置无调试运行。

启动配置通过 CMake Tools 查询 `appCirrusMusic` 的真实路径，不绑定某个用户目录或 Debug/Release 路径。调试前任务会按当前选择的 preset 重新配置并构建；打 C++ 断点时应选 Debug，不用优化后的 Release 判断断点准确性。

这份配置支持 **C++/GDB 调试**，并未启用 QML JavaScript 运行时调试端口；QML 补全／诊断／格式化与运行时断点是两种能力。

## 4. 已提供的任务

通过 **Tasks: Run Task** 运行：

| 任务 | 行为 |
| --- | --- |
| Cirrus: configure | 使用当前 configure preset 配置 |
| Cirrus: build | 配置后构建 all，包含 app、后端库及已启用测试；默认构建任务 |
| Cirrus: test | 先完整构建，再在当前 configure preset 的构建目录运行 CTest；无测试时失败 |
| Cirrus: qmllint | 配置后构建 Qt 自动生成的 all_qmllint target |
| Cirrus: clean | 清理当前构建 preset 的产物，不删除源文件 |

任务不硬编码本机 Qt 路径。`Cirrus: test` 使用 CMake Tools 的 `${command:cmake.buildDirectory}`，要求 configure/build preset 匹配且 `BUILD_TESTING=ON`；不会固定读 Debug 或旧构建目录。当前没有 testPresets，CLI 使用下方 `ctest --test-dir`。`all_qmllint` 已实测通过。交接轮次验证了 JSON 配置及等价 CLI 命令，未人工操作 VS Code 图形任务；若扩展变量解析失败，可先使用 CLI。

## 5. 语言服务和格式化

### C++

Presets 启用 `CMAKE_EXPORT_COMPILE_COMMANDS` 和 C++20。CMake Tools 配置成功后，把当前编译数据库复制到 `build/compile_commands.json`，clangd 固定读取此位置，因此切换 preset 不必手改 Qt include 路径。

- 不提交机器相关 `c_cpp_properties.json` 或编译数据库。
- 本次已经为当前 Debug 构建生成并复制数据库。
- 如果以后只在 CLI 配置而不经过 CMake Tools，需要手工刷新副本，例如：

```bash
cp build/linux-dev-local/compile_commands.json build/compile_commands.json
```

- clangd 的 query-driver 仅允许常规 `/usr/bin/c++`、`g++`、`clang++`。使用其他可信编译器时，显式增加其路径；不要使用任意可执行文件通配符。
- `.clang-format` 提供 C++ 格式规则；当前未为引入格式配置而重写已有模板源码。

### QML

Qt 官方扩展使用 qmlls 提供补全、跳转、检查和格式化。先完成一次 CMake configure/build，使生成的 QML 模块信息可用。

Qt 6.10+ 无需设置已弃用的 `QT_QML_GENERATE_QMLLS_INI`。不手写固定 `buildDir` 或跨机器不成立的 QML import 路径；Qt 会生成构建元数据，扩展与当前 CMake 工程集成。

若扩展选择了错误的 qmlls，先检查 Qt 安装识别和扩展输出；必要时在**个人用户设置**里设置 `qt-qml.qmlls.customExePath` 为实际 Qt Kit 的 `bin/qmlls`，不要将个人绝对路径写进共享 settings.json。

## 6. 命令行等价流程

准备好私有 Qt preset 后：

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --parallel
cmake --build --preset linux-dev-local --target all_qmllint
ctest --test-dir build/linux-dev-local --output-on-failure --no-tests=error
./build/linux-dev-local/appCirrusMusic

cmake --preset linux-release-local
cmake --build --preset linux-release-local --parallel
ctest --test-dir build/linux-release-local --output-on-failure --no-tests=error
```

Release 这里只是本地优化构建，不代表已完成 Flatpak/AppImage 打包、依赖部署和发布验收。

已有协议、会话与认证事务离线 Qt Test，详见[实现记录](api/native-foundation.md)。`BUILD_TESTING` 默认开启；只构建应用时可在 configure 加 `-DBUILD_TESTING=OFF`。Python 的接口清单校验是可选开发工具，不参与客户端构建／运行。

## 7. 本次验证范围

实际环境：Qt 6.11.2、CMake 4.4.3、Ninja 1.13.2、GCC 16.2.1、clangd 22.1.8、GDB 17.2。此记录说明当前开发机事实，不替代拟定的 CI／发行兼容矩阵。

已执行：

- Debug/Release 的 configure 与 build。
- `all_qmllint`。
- clangd 对 main.cpp 的检查，使用真实编译数据库，0 errors。
- GDB batch 启动 Debug 程序并命中 main.cpp 的 main 断点。
- Debug/Release 在 offscreen + 软件渲染下运行 3 秒，未出现 QML 加载错误，随后由验证脚本正常发送终止信号。
- JSON、CMake preset 解析和 clang-format 配置读取检查。

以上调试／运行冒烟为早期模板验证记录；当前后端测试与干净源码接手复验见[交接验证记录](handoff-verification.md)。本机日志位于忽略目录 `build/workspace-checks/`，不随仓库发布。尚未人工操作 VS Code 图形调试会话，也未做真实 Wayland/X11 窗口视觉验收；这些不能由 offscreen 冒烟替代。

## 8. 常见问题

- **找不到 Qt6Config.cmake**：确认选择 `linux-dev-local` 而不是未配置 Qt 路径的公共 preset，检查 CMAKE_PREFIX_PATH 指向 Kit 根目录。
- **C++ 提示找不到 Qt 头文件**：执行 CMake: Configure，检查 `build/compile_commands.json` 是否是当前配置；必要时重启 clangd。
- **Debug 启动失败／找不到程序**：确认推荐扩展、configure/build preset 和构建任务成功；不要手动改 launch.json 为过时二进制路径。
- **QML 模块标红**：先完整构建，再检查 Qt QML 扩展日志和 qmlls 版本；不要通过随意增加 import path 掩盖构建问题。
- **出现 UTF-8 locale 警告**：使用系统已安装的 UTF-8 locale；自动化验证使用 C.UTF-8，不修改用户全局 locale。
- **新机器没有本机 preset**：复制示例并修改路径；私有配置按设计不会进入首个 commit。

## 9. 参考

- [CMake Tools 的 Presets 工作流](https://github.com/microsoft/vscode-cmake-tools/blob/main/docs/cmake-presets.md)
- [CMake Tools 的调试变量](https://github.com/microsoft/vscode-cmake-tools/blob/main/docs/debug-launch.md)
- [Qt QML Language Server](https://doc.qt.io/qt-6/qtqml-tooling-qmlls.html)
- [QT_QML_GENERATE_QMLLS_INI 的弃用说明](https://doc.qt.io/qt-6/cmake-variable-qt-qml-generate-qmlls-ini.html)
