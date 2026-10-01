# B-010 子任务：系统密钥环与 XEAPI 公钥持久化

> 状态：**已实现**（离线验收；真实密钥环往返需手动开启）。属于 B-010（M1）。建立秘密存储接缝并先用于 XEAPI 公钥；匿名 `MUSIC_A` 与账号 Cookie 的持久化在匿名 bootstrap／账号恢复任务中复用本接口。

## 1. 选型

- 依赖：系统包 QtKeychain（Arch `qtkeychain-qt6` 0.17，维护者已同意）；`find_package(Qt6Keychain)`。Flatpak／AppImage 打包时作为模块构建（B-022）。
- **禁用明文回退**：显式 `setInsecureFallback(false)`；没有可用后端时返回 `Unavailable`，秘密只留本次进程内存（R-012）。不自制可逆“加密”文件。
- service 名由装配方传入（应用 ID 尚未冻结，B-004）；key 名限定 `[a-z0-9._-]{1,64}`，不含账号可识别信息。

## 2. 接口语义

- `SecretStore`：异步 read／write／remove；回调总是延后、只投递给仍存活的 context；错误分为 `NotFound`、`Unavailable`、`Denied`、`Failed`。线程亲和，不跨线程调用。
- 测试使用内存 fake；真实后端只做"无后端时不落明文、返回错误"的验证。写入真实用户密钥环的往返测试需显式开启（`CIRRUS_KEYRING_SMOKE=1`），默认跳过。

## 3. XEAPI 公钥持久化（XeapiKeyVault）

- 整个公钥记录（version、publicKey、sk）作为**一个**密钥环条目保存，保证原子性；不拆到 SQLite 与密钥环两处，避免跨存储不一致。（修订 [B-002 §3](B-002-xeapi.md) 中"公开部分放 state.sqlite"的初步设想。）
- 启动 `restore()`：合法记录才放入 ring，且不回写；若 ring 在读取期间已获得新公钥，则以新公钥为准、丢弃旧记录。记录损坏报错，不放入 ring。
- ring 变化时写入、清空时删除；同一时刻最多一个存储任务，期间的多次变化合并为最终状态。写入失败只上报，不影响内存中的使用。

## 4. 验收矩阵

按实现时的验证结果勾选。

- [x] 恢复：合法记录进入 ring 且不回写；不存在／损坏／后端不可用分别上报，ring 保持为空。
- [x] 恢复期间 ring 已有新公钥时不被旧记录覆盖。
- [x] 变化写入、清空删除；并发变化合并且无重叠存储任务；最终存储内容等于最终状态。
- [x] 存储不可用：上报失败，内存中继续可用，不产生明文文件。
- [x] 真实适配器在无后端（无 D-Bus 会话）时返回错误，配置目录中无明文回退文件。
- [x] 现有套件全部通过 Debug/Release；domain／application 不依赖 QtKeychain。（13 套件、329 项；运行时只加载项目 Qt 套件的 Core/DBus。）
