# B-008 子任务：SQLite 存储基础与设备 profile

> 状态：**已实现**（离线验收）。属于 B-008／B-010（M1）。为匿名 bootstrap 提供稳定设备 ID，同时建立后续音乐库复用的 DatabaseWorker；不包含音乐库 schema、缓存或密钥环。

## 1. 选型（按成熟项目标准）

| 数据 | 位置 | 理由 |
| --- | --- | --- |
| 设备 ID（安装级、非秘密、须跨重启稳定） | AppLocalDataLocation 下 `state.sqlite` | 不是用户偏好，不放 QSettings（AppConfigLocation 常被 dotfiles 同步到别的机器，会造成多机同 ID）；不放可重建的 `catalog.sqlite`（清缓存不应改变设备身份） |
| 秘密（匿名 `MUSIC_A`、账号 Cookie、XEAPI `sk`） | 系统密钥环（QtKeychain，系统包） | ADR-0006；后续任务接入，不可用时只留内存，不明文回退 |

依据 [存储方案](../09-storage-cache-and-sync.md) §1–§2、§6 与 [ADR-0006](../adr/0006-storage-and-secrets.md)。新增 Qt SQL 模块（Qt 套件自带 QSQLITE），无第三方依赖。

## 2. DatabaseWorker 语义

- 每个数据库一个 worker，独立 QThread 拥有唯一连接；调用方只提交任务、经排队回调拿结果，不跨线程共享 QSqlDatabase。context 对象销毁后回调丢弃。
- 打开顺序：建目录（0700）与文件（0600）→ `QLockFile` 防同库多进程写（陈旧锁自动回收）→ `quick_check` → pragma（`foreign_keys=ON`、`busy_timeout=3000`、`synchronous=FULL`；本地文件系统用 WAL，NFS/SMB/FUSE 等回落 DELETE）→ 迁移。
- 迁移：版本递增、每版 SHA-256 校验存于 `schema_migrations`；已应用迁移校验和不符即拒绝；库版本高于程序已知版本即拒绝写入（`NewerSchema`），不降级。每版一个事务，完成后 `foreign_key_check`。升级已有库前检查磁盘空间并 `VACUUM INTO` 一致备份（保留最近一份）。
- 析构：已排队任务执行完后在 worker 线程关闭连接、释放锁再退出；未执行的回调不再调用。

## 3. 设备 profile

- `state.sqlite` v1：单行 `installation(device_id, created_at)`。
- 首次在事务内生成：26 字节安全随机数的大写 hex（52 字符，与锁定上游 `generateDeviceId` 格式一致，不采集硬件信息）。
- 已存在则原样返回；存储值不合法报 `Corrupt`，**不静默重新生成**（避免设备身份漂移）。数据库不可用时由装配方决定（本任务不做会话级回退）。

## 4. 验收矩阵

按实现时的验证结果勾选。

- [x] 新库：目录／文件权限、锁、pragma、v1 迁移与校验记录正确；重开不重复迁移。
- [x] 已应用迁移被篡改、库版本更高、`quick_check` 失败、锁被占用均返回明确错误且不写入。
- [x] 升级路径生成一致备份；迁移中途失败回滚，原数据与版本不变。
- [x] 任务在 worker 线程执行、回调回到 context 线程；context 销毁后不回调；析构排空并释放锁。
- [x] 设备 ID：首次生成格式正确、重开稳定、两个进程级 worker 不能同时持锁、非法存储值报错不重生成。
- [x] 现有套件全部通过 Debug/Release；应用层不依赖 Qt SQL。（12 套件、317 项。）
