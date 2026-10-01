# 干净源码接手验证记录

> 本文件由交接复验更新。当前执行流程见 [HANDOFF](HANDOFF.md)。复验使用同一开发机上的独立源码目录，不等同于全新操作系统或容器验证。

## 源码与隔离方式

生产代码基线：`1a60b74`。交接增量只修改文档和 `.vscode/tasks.json`，不修改产品或测试实现。

复验从 Git 暂存区导出完整源码到独立临时目录，包含本轮交接配置，不复制 `build/`、`CMakeUserPresets.json`、编译数据库、Qt Creator 缓存、上游 checkout 或本机日志。使用共享 `linux-dev`／`linux-release` presets，显式指定已安装的 Qt Kit。

导出时的 Git tree 为 `14fb99f3584b0f651a473a5cef74770019ec7198`（暂存区测试快照，不是交付 commit；当时本报告尚待填结果）。后续仅补文档结果／说明；产品源码、测试、CMake 与 VS Code 任务配置保持该快照内容。最终交付 commit 以接收方 `git rev-parse HEAD` 为准。

配置前清除了继承的 CMAKE_PREFIX_PATH、工具链／Qt 查找及 QML／插件／LD_LIBRARY_PATH 变量，并关闭 CMake 用户／系统 package registry；仍正常使用本机安装的开发包及默认编译器。

## 执行结果

实际环境：Linux x86_64，Qt6.11.2，GCC16.2.1，CMake4.4.3，Ninja1.13.2，OpenSSL3.6.4，zlib1.3.2；可选审计使用 Python3.14.7。这是实测版本，不是未经验证的最低兼容矩阵。

| 验证 | 结果 |
| --- | --- |
| 导出前检查 | 独立目录无 build、私有 preset、编译数据库、Qt Creator 缓存；无上游 checkout |
| Debug configure + build all | 通过，无编译／配置警告 |
| Debug CTest | 10套件；223 passed、0 failed |
| Release configure + build | 通过，无编译／配置警告 |
| Release CTest | 10套件；223 passed、0 failed |
| all_qmllint | 通过 |
| 独立 Release、BUILD_TESTING=OFF | configure／build通过 |
| 无上游 checkout 的清单校验 | 440模块：164 adopt、40 defer、236 exclude |
| Python 工具测试 | 15项通过 |
| 文档／工作区静态检查 | 169个本地 Markdown 链接目标存在；6个 JSON／示例配置解析通过；默认 all 和 test依赖／目录／空测试失败参数核对通过 |
| 测试副本一致性 | 49个源码／测试／工具／CMake／任务配置文件与工作区逐字节相同；46个编译单元均指向独立目录，无原仓库源码路径 |

两套 CTest 计数均由完整 `-V` 输出汇总，包含 Qt Test 数据行及 init/cleanup。没有修改原有测试以适配交接检查。

### 可重跑方式

在包含交接提交的仓库根执行，先确保要交付的变更已提交：

```bash
# Bash；只导出 HEAD 中已跟踪文件，不带任何本机忽略项
set -euo pipefail
clean=$(mktemp -d /tmp/cirrus-handoff.XXXXXX)
git archive HEAD | tar -x -C "$clean"
cd "$clean"
# 然后按 HANDOFF 的共享 preset 命令执行 Debug／Release、CTest、qmllint、Python 校验
```

本次为验证尚未提交的交接配置，采用等价的 `git checkout-index --all --prefix="$clean/"` 导出已暂存文件；这是验证操作，不要求接手者使用暂存区。

除 HANDOFF 命令外，本次 configure 还附加：

```text
-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF
-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF
```

无测试构建的等价命令：

```bash
cmake -S . -B build/no-tests -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DCMAKE_PREFIX_PATH=/absolute/path/to/Qt/gcc_64
cmake --build build/no-tests --parallel 4
```

无需在无测试构建上执行 CTest；接手者必须用启用测试的构建完成验收。

## 证据边界

- 不需要 Node.js、真实账号、Cookie 或上游服务；审计的默认校验不依赖参考 checkout。
- 本机原有编译器、Qt、OpenSSL、zlib 等安装仍被使用；没有证明自动安装依赖或跨发行版可移植性。
- VS Code JSON 与等价 CLI 命令可核验，但未人工操作 IDE 图形任务／调试会话。
- 不证明真实网络／TLS、官方协议、播放器、UI 功能或发行包可用。
- 本机原始日志在忽略目录 `build/workspace-checks/handoff-*.log`，结构检查为 `handoff-structure-check.json`；不随源码交付。接收方应重跑并保留自己的证据，而不是依赖发送者缓存。
- 本次没有重新下载参考仓库，没有推送远端、生成交付 bundle 或代接收方完成接收检查。
