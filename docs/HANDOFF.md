# 开发接手指南

> 接手入口，不需要阅读此前聊天。代码基线为认证事务 `1a60b74`，其后增加 B-010 扫码轮询与总时限（见 `git log`）；[验证记录](handoff-verification.md)对应的是 `1a60b74` 时点。交付方应提供**包含本文件的完整提交**，并告知 `git rev-parse HEAD`，不要只发送这个旧基线号。

## 1. 先理解现在的状态

**这是已建立后端基础和离线认证事务的项目，不是已经可用的音乐客户端。** `main.cpp`／`Main.qml` 仍是 Qt 模板，后端尚未接入入口。

| 已实现且有离线测试 | 尚未实现／尚未证明 |
| --- | --- |
| C++20／Qt 分层 targets、weapi/eapi、form、路由、HTTP、gzip／JSON 解码 | XEAPI、真实 socket/TLS／官方接口兼容性 |
| epoch 隔离、内存 Cookie、取消及晚到结果保护 | 设备／匿名 bootstrap、系统安全存储、账号恢复 |
| eapi QR key／QR check、weapi AccountIdentity，共3个远程 operation | 其他音乐业务接口、播放器、云盘／下载、一起听 |
| `QrLoginService` 身份验证、快照收据、两阶段凭据交接、可选自动轮询与认证总时限 | 网络线程装配、GUI facade、UI |
| Debug/Release：10套件、275项 Qt Test pass（含数据行及 init/cleanup） | CI、发布包、跨发行版兼容性、真实账号验收 |

上游440模块（164采用候选、40待定、236排除）只是静态审计；`inventory.json` 的 `not-implemented/static-only` 是审计时点标记，不作为运行注册表或实时实现进度。

## 2. 第一次接手：按此顺序执行

### A. 确认拿到了正确代码

```bash
git status --short
git log -5 --oneline
git rev-parse HEAD
```

交付方当前只进行了本地提交，不能假设远端含最新代码。仓库地址／分支由维护者提供，不凭空填写公共地址或自行推送。若不能走远端，可由交付方在确认无敏感内容后交付 Git bundle：

```bash
# 交付方：只打包需要交接的 master 历史，不打包工作目录、缓存或其他分支
# 以下命令是交付选项，本轮没有代为发布或生成交付包。
git bundle create ../CirrusMusic-handoff.bundle master
# 接收方
git clone -b master /path/to/CirrusMusic-handoff.bundle CirrusMusic
```

目录副本也可用于构建，但正式接手应保留 Git 历史。无需获取任何账号密码或真实 Cookie。

### B. 准备工具链

- Linux；当前实测 x86_64。CMake ≥3.24、Ninja、支持 C++20 的编译器。
- Qt 开发 Kit：模板要求 ≥6.10，**实际验证的是6.11.2**；需要 Core、Quick／QML、Network、Test 及 QML lint 工具；模板还使用 `QtQuick.Layouts`、`QtQuick.Controls.Basic`，不能只装 Qt Core 开发包。未证明6.10或其他发行版兼容。
- OpenSSL ≥3 Crypto、zlib 的开发头和库。仅有运行库不足以编译。
- Python 3：可选审计校验，不是应用构建／运行依赖。不需要 Node.js。
- GDB、clangd、clang-format、VS Code 扩展是调试／编辑工具，不是运行依赖。不要盲目复制本机编译器版本作为最低版本要求。

发行版若没有匹配 Qt，请使用已有受信任 Kit；本项目还没有自动安装依赖或发行 Runtime。不要为凑依赖悄悄降 Qt、引入 WebEngine 或服务代理。

### C. 完整构建并测试，而不是只编译空窗口

以下使用共享 preset，无需本机私有文件。将 Qt 路径替换为接收机器的实际路径；若系统能发现正确 Qt，可省略该参数。

```bash
cmake --preset linux-dev -DCMAKE_PREFIX_PATH=/absolute/path/to/Qt/gcc_64
cmake --build --preset linux-dev --parallel 4
ctest --test-dir build/linux-dev --output-on-failure --no-tests=error
cmake --build --preset linux-dev --target all_qmllint

cmake --preset linux-release -DCMAKE_PREFIX_PATH=/absolute/path/to/Qt/gcc_64
cmake --build --preset linux-release --parallel 4
ctest --test-dir build/linux-release --output-on-failure --no-tests=error
```

预期 CTest 有10个套件且全部通过；少于10个先排查错误目录／`BUILD_TESTING`，不要把“无测试”当成功。275是当前 Qt Test 明细计数，不是275个独立产品功能。

VS Code：按 [17](17-vscode-development.md) 创建私有 preset，选择匹配的 configure/build preset；`Ctrl+Shift+B` 现构建 `all`，`Cirrus: test` 先构建再跑 CTest。后端没有链接到空壳 app，**只构建 `appCirrusMusic` 不能验收后端改动**。

仅 CLI 配置后，clangd 需要刷新：

```bash
cp build/linux-dev/compile_commands.json build/compile_commands.json
```

源码入口运行仍只显示模板窗口，不能用它测试真实登录或播放。

### D. 可选审计与固定参考源码

```bash
python3 tools/validate_api_inventory.py
python3 -m unittest discover -s tools/tests -p 'test_*.py'
```

预期440模块校验及15项 Python 测试通过；不需要上游 checkout。若任务需要核对源码，另行获取锁定参考：

```bash
# 需要 GitHub 网络访问；目标目录须不存在，已有目录先检查而非覆盖
mkdir -p build/reference
git clone --no-checkout https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced.git build/reference/api-enhanced
git -C build/reference/api-enhanced checkout --detach a8c781fd64faab17fedfd46e0615a2609307f163
python3 tools/validate_api_inventory.py --upstream build/reference/api-enhanced
```

以 `docs/api/upstream-lock.json` 为准，不自动更新到 upstream HEAD；不执行 `npm install` 或运行参考服务。若固定提交无法取得，保留阻塞证据并联系维护者，不静默替换版本。`build/reference/` 中此前临时提取脚本不是产品或测试依赖，不需要复制。

## 3. 最小阅读与代码导航

顺序：本页 → [实现记录](api/native-foundation.md) → [系统架构](03-system-architecture.md) → [认证事务](api/authentication-transaction.md) → [已完成的轮询任务契约](tasks/B-010-qr-polling.md)。遇到协议疑问再查 [API 审计](api/README.md)和[门禁](api/gaps-and-hazards.md)，不必先读完440模块。

| 入口 | 看什么 |
| --- | --- |
| `src/CMakeLists.txt`、`tests/CMakeLists.txt` | 实际依赖方向、测试 target；不是架构图中的未来空模块 |
| `src/domain/account/session_types.h` | Token、phase、身份值；token 不是凭据 |
| `src/application/account/session_coordinator.*` | lifecycle 与重入安全的两阶段转换 |
| `src/application/account/qr_login_service.*` | begin/poll/cancel、自动轮询与总时限、身份确认、精确 target 提交 |
| `src/application/account/login_scheduler.*` | 单调时钟＋单次定时器窄接缝；测试替身在 `tests/support/fake_login_scheduler.h` |
| `src/application/ports/qr_login_gateway.h` | 窄 port、按值结果、脱敏错误；无 JSON／Cookie |
| `src/infrastructure/netease/api/api_operation.*` | 目前3项封闭注册契约 |
| `src/infrastructure/netease/api/session_api_client.*` | 捕获 token/origin、快照和一次性收据 |
| `src/infrastructure/netease/auth/` | QR 映射、身份 schema、port 实现 |
| `tests/unit/qr_login_service_test.cpp` | Core-only fake port 与虚拟时钟用例 |
| `tests/unit/auth_transaction_test.cpp` | 从实际协议代码到应用的合成网络认证链 |
| `tests/support/` | fake HTTP、解码测试辅助；不是产品运行组件 |

## 4. 不可破坏的设计约束

- domain／application 仅 Qt Core 值与用例；不引入 Qt Network、JSON、OpenSSL、具体 NetEase 实现或 QML。当前不需要额外运行时。
- `803 ≠ Authenticated`。身份查询验证后 prepare 到 **Committing**；凭据交给返回的**确切 target**并确认后，才能 complete 到 Authenticated。Committing 不可分发账号私有用例。
- 身份请求前捕获 Cookie 快照，不能在身份 A 返回后复制已更新成 B 的当前 jar。普通 epoch 前进清凭据，不自动继承。
- 所有完成信号带原 token；取消只是优化，消费者必须校验上下文。重入退出／换号优先于旧成功结果。
- 当前所有认证对象在同一个后端事件循环线程构造、使用、销毁；不得直接跨线程调用同步返回 requestId 的 API。client 含值成员 CookieJar，不能构造后整体 moveToThread。
- service 必须先于其引用的 SessionCoordinator 销毁；正常装配让 gateway/client 覆盖 service 生命周期。不要假设给多个对象设置同一 QObject parent 就保证正确析构顺序。
- 凭据不进入应用层、QML、普通配置或日志；UUID 收据不能持久化。大数值 ID 不安全时拒绝，不浮点舍入补救；不同作用域 MUSIC_U 冲突不猜选。
- 后端先行，UI 最后。每日推荐歌曲独立建模；私人 FM／一起听包含在范围，视频/MV、播客/电台、云贝/云豆、动态、云小编排除。不引入第三方音源解锁。

## 5. 接下来做什么，什么时候需要询问

[B-010 子任务：扫码轮询与总时限](tasks/B-010-qr-polling.md)已实现并离线验收，可作为任务契约写法的范例。

下一步：设备／匿名 bootstrap（领取前先按同样格式写任务契约并请维护者确认） → 安全存储／恢复与线程装配 → 受控真实认证验证；XEAPI 可独立规划，再扩展音乐查询与播放后端。详细顺序见[路线图](14-roadmap-and-backlog.md)。不要一次把这些全部塞进一个 Manager 或单次提交。

| 待决事项 | 安全默认／询问时机 |
| --- | --- |
| 安全存储实现与新增依赖 | 目前仅内存；新增 QtKeychain、系统桥接等实现／依赖前确认，不明文 fallback |
| app ID、自身许可证、发行 Runtime | 由维护者决定；不阻碍后端单测，不擅自填公司域名或发布许可证 |
| 真实账号与音源验证 | 必须明确授权并安排脱敏流程；不在聊天或仓库提供凭据 |
| XEAPI、v2 checktoken SDK、一起听接收链 | XEAPI 未实现；SDK 与一起听有证据门禁，不暗加 Node/WebEngine，不以 v3 替代 v2 |
| 播放后端 | QMediaPlayer/QAudioOutput 仍待真实来源 PoC，不把方案当已落地 |

当前没有指定新的接手人／问题负责人；由维护者分配。详见[风险与决策](15-risks-and-open-questions.md)。

## 6. 交付与接收检查

- [ ] 交付方告知完整提交、分支和传递方式；接收方确认含本页及下一任务文件。
- [ ] 从干净源码配置／构建／测试，而非复用发送者的 build 目录；结果与[验证记录](handoff-verification.md)比较。
- [ ] 接收方找到 service、port、native adapter 及其测试，能说明为何803不是登录完成。
- [ ] 领取单一任务，记录验收项；新依赖／真实访问先获授权。
- [ ] 每个增量附完整测试结果、剩余风险并更新本页和实现记录；提交、推送、发布分别授权。

不交付：`CMakeUserPresets.json`、Qt Creator 私有配置、Qt 安装目录、编译数据库、构建缓存、本机日志、真实凭据。以上接收勾选项尚待实际接手者执行，不因本机复验而代勾。
