# 账号补充读取：播客订阅、云盘列表与详情

## 1. 交付范围与证据

已接入三项只读后端，复用原 MusicQuery／MusicResult、账号／token／generation 屏障、16槽 facade 与 Runtime 后台线程；**仅合成候选 schema 验证，没有真实账号／服务／UI 验收**。

完整核对固定 `a8c781fd64faab17fedfd46e0615a2609307f163` 的 `module/dj_sublist.js`、`user_cloud.js`、`user_cloud_detail.js`、home.md对应完整章节、`util/option.js` 及 TS签名。三 wrapper 明确 Weapi；通用 options 允许的上游 crypto／domain／Cookie／代理等覆盖不移植。文档和 generic `Promise<Response>` 没有提供完整响应 schema；下面字段是受限合成候选，不当作真实形状证据。固定440模块及164／40／236历史审计不回写为运行支持名单。

本轮没有访问业务API、读取记忆凭据／真实密钥环、登录真实账号、重启已停−462探测、取头像／音源或操作私人文件；没有订阅／退订、上传、删除、匹配、下载、播放、报告、库同步或UI。

## 2. Core入口与固定wire

| Facade／operation | 官方逻辑路径 | 业务载荷 | 协议／响应上限 |
| --- | --- | --- | --- |
| `subscribedPodcasts(CatalogPage)`／SubscribedPodcasts | `/api/djradio/get/subed` | limit、offset、固定total:true | Weapi／JSON／4MiB |
| `cloudSongs(CatalogPage)`／CloudSongs | `/api/v1/cloud/get` | 仅limit、offset | Weapi／JSON／4MiB |
| `cloudSongDetails(QList<CloudSongId>)`／CloudSongDetails | `/api/v1/cloud/get/byids` | songIds **字符串数组** | Weapi／JSON／4MiB |

这是逻辑路径，不是上游 `/dj/sublist`、`/user/cloud`、`/user/cloud/detail` 服务包装路由。固定官方目标；无任意URL／uid／crypto／csrf／Cookie／total入口，无上游Node运行依赖。

- 两列表默认30／offset0，limit1..100、offset0..100000；这是Cirrus有界预算，不是已证服务器最大值。拒绝bool／小数／文本冒充数值，不自动翻页、抓整库或从短页推断结束。
- 详情1..100个规范、唯一CloudSongId，保持字符串与请求次序；不接受CSV／JSON文本／数值ID数组／重复，不照抄wrapper的隐式空白删除，不自动分批。wide ID仍字符串。
- Facade从已验证snapshot捕获accountId，调用方不选账号；非Ready／Guest／无效输入／满邮箱返回0。service检查原token、Authenticated及account相等，当前token＋异账号→InvalidRequest，旧token→StaleContext。
- Native client再次要求MUSIC_U，匿名MUSIC_A不足。三响应忽略Set-Cookie，不触发登录／退出／记忆更新。业务拒绝与HTTP失败保留MusicFailure；429合法Retry-After仅供调用方展示，无自动重试／fallback／挑战绕过。
- 受理ID、HTTP200或候选DTO成功不证明权益、完整库、上传完成或可播放。

## 3. 独立身份与候选映射

`src/domain/music/account_library_types.h`仅依赖Core值类型。`SubscribedPodcastsQuery`、`CloudSongsQuery`、`CloudSongDetailsQuery`都带原accountId；结果保留原query，完成信号带original token。JSON／provider字段／加密留在 `src/infrastructure/netease/music/account_library_mapper.*` 和关闭式 `api_operation.*`。

### 播客订阅

候选root.djRadios必须数组，最多请求limit；每项复用 `spoken_mapping::podcastFrom` 的独立PodcastId、名称／封面／描述及可选programCount，不取Voice／Program／Song ID。不含工作台、自己的创作列表或订阅者用户列表。保留provider次序／重复；root.count、hasMore、time可缺失／null为未知，count不得小于当页项数。time保留原值，不推断订阅日期／单位或跨页快照一致性。

### 云盘列表

候选root.data必须数组，最多请求limit；每项需要规范songId，映射为**独立CloudSongId**：

- songName为展示title，缺失／null为空展示占位，不声称真实名字为空；artist／album／fileName可未知，纯文本≤4096、合法UTF16／无NUL。
- fileSize／bitrate／addTime作为可选provider原数值，只接安全非负整数；不由此推算可下载格式、声学码率、创建时区或可播放时长。
- simpleSong若有必须对象；显式正id仅作为可选**SongId catalogReference**。缺失／null／0为未知引用，不把cloud id兜底成SongId。其余嵌套Song、作者／账号、文件标识、URL、MD5、权益和原JSON不发布。
- 根count／hasMore可未知；size／maxSize为providerUsedSpace／providerMaximumSpace，候选支持安全非负JSON整数或规范非负signed64十进制文本（含"0"，wide文本保留精度）；拒绝负数、溢出、bool、小数、空白、科学计数／前导0文本。该字符串兼容规则仍是候选，不是实测字段类型／字节单位证明。
- used>maximum可以是服务限额变化，不据此拒绝元数据或推断权益；没有“剩余空间”／文件大小合计的自动推导。

列表保留provider顺序／重复；空数组可成功，缺失／null／坏数组或坏行整页InvalidResponse。若根uid出现必须匹配捕获账号；不拿播客作者／文件署名当认证身份。其他未知字段被忽略，不做root／字段fallback。

### 云盘详情

同样要求root.data数组，最多请求ID数，每项songId属于请求且不能重复／ unsolicited。按**请求次序**重构返回songs，缺失项单列missing（未知，不冒充已删除／无版权），不靠simpleSong.id匹配请求。显式合法 `[]` 代表所有请求项未知；字段缺失不是空成功。

独立CloudSongId是安全边界，不断言服务数字必与SongId不同。simpleSong.id即使数值不同／相同都只作元数据引用，不能推出它是音轨／音源绑定。固定云盘文档提到另调用旧 `/song/url`，但没有本包可复用native XEAPI v1／身份／权限／过期／试听证据；**没有开放cloud播放或把cloud id投给现有SongStream**。后续需要单独验证可信绑定及原账号音源，不能做强转／旧协议降级／曲库解锁。播客声音播放仍遵守[原门禁](spoken-playback-contract.md)。

## 4. 隐私与生命周期

数据是当前账号私有元数据，fileName只是展示文本，不解析成本地路径、不访问文件、不打开URL／渲染HTML。文本、作者引用和配额不进SQL、共享缓存、日志或诊断包。没有自动封面／详情补齐／音源获取／入队／报告。跨页没有原子快照保证，不能把分批返回称作完整当前库。

signOut／切号／close立即旧意图屏障，取消和迟到事件保留原token，后台关闭真正排空；已经交给消费者的DTO副本不会自动擦除，消费者仍须清理旧账号可见内容。单次读取不改变记忆凭据或订阅／云盘状态。

## 5. 验证与复跑

Qt6.11.2 Debug，`LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1`；七个受影响套件：account_library49、library21、podcast_reads55、comments149、music26、session_api_client24、backend_facade8，**332 pass**。另选accountLibrary／library／podcast／playback（queue／FM）／destruction六条线程数据行共**8 pass**，均无fail/skip，包含data rows和init/cleanup，不是全36套件／Release／真实账号矩阵。

新suite有31个有效JSON坏schema行；三条独立固定路径／payload，数字／分页／最多100 IDs／wide字符串、未知／零值／重复列表、详情顺序与missing、独立cloud／catalog引用、配额规范与溢出、文本上限。三类Guest／MUSIC_A不足／异账号拒绝，三类native双请求验证原Cookie／无响应轮换、不自动下游读取，三类cancel／stale／429／−462无重试以及16槽facade／晚到屏障。Weapi检查实际固定路由／form，加密互操作沿用原向量；不声称已经独立解密所有Weapi业务payload或验证真实服务。

线程测试使用临时SQL／fake secrets／合成完整登录，验证三项读取跨线程返回原账号／token；列表hasMore不自动翻页，cloudID4／catalog引用99不混为身份，挂起cloud列表close取消及资源排空。没有使用真实登录／文件／声卡。

构建时一次CMake域头注册替换未命中，mapper source成功应用后已补齐正确注册位置，未重复应用。clangd旧header size／preamble及旧enum视图级联，经真实编译、刷新compile DB与active诊断解决；没有删必要includes／Q_OBJECT／moc，也没有suppression。

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --target tst_account_library tst_library tst_podcast_reads \
  tst_comments tst_music tst_session_api_client tst_backend_facade tst_backend_runtime --parallel 4
cp build/linux-dev-local/compile_commands.json build/compile_commands.json
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(account_library|library|podcast_reads|comments|music|session_api_client|backend_facade)$' -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  accountLibraryThroughThread libraryThroughThread podcastReadsThroughThread playbackThroughThread destructionStillDrains
```

12个修改C++路径fresh active LSP无诊断；实际Ninja依赖检查14个application对象／6917 entries，无基础设施／JSON／Network／Multimedia／Sql／Quick／Qml／Gui／OpenSSL泄漏，UI差异0。clang-format dry-run、git diff --check、固定440模块校验通过；524个本地Markdown文件引用有效（排除fenced／inline code，不验证外链或anchor），live-looking Cookie模式扫描0 findings而非全面秘密保证。

最终日志在ignored `build/workspace-checks/account-library-final-{build,focused,thread}.log`及account-library-{boundaries,format,diff-check,fixed-hash,links}.log，不交付本机cache／凭据；registry51→54，CTest35→36。实网schema、配额单位／类型、cloud音源／权益／catalog绑定、订阅／收藏／云盘写、完整库同步和UI仍未交付，另立明确授权窗口与契约。下一小包可调查显式喜欢／收藏／订阅写及不确定结果，不自动发送真实写入。
