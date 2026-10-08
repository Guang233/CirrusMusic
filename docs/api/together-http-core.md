# 一起听核心12项：HTTP-only

## 1. 批准范围与完成层级

维护者在[材料审计](together-protocol-material-audit.md)后明确按文档接入、不接 NIM，并选择核心12项。该授权仅覆盖此前一起听暂停中的 HTTP 子集。现在 registry **85→97 business operations，47→48 CTest suites**，新增 `together_http`。这是原账号、离线合成验证的 HTTP 适配，不是完整 FR-043／Active／双端同步／产品入口验收。

不接 NIM／SELFHOST／token／聊天事件／SDK／新依赖／邀请发送／朋友／GPS／RTC／多人／AI／商业功能；不自动播放、seek、改本地队列、听歌报告、轮询、心跳计时或重连。整歌单 v2、云盘后续、播客／声音下一阶段继续暂停，UI 最后。APK 检查被撤权，**从未读取、hash、检查、解包或执行**。

## 2. 路由、密码学与身份 profile

- 全部12项 **标准 Eapi**，包括新 Status。复用既有 AES-128-ECB／MD5／compact JSON／uppercase hex `params` form；不照材料 CBC 文字改密码学，不执行 JS。
- 新窄 `OfficialDestination::TogetherHttp` 仅允许下表12条逻辑路径，wire 为 `/eapi/listen/together/…`，host 为 `interface.music.163.com`，HTTPS。全局默认 Eapi 的 `interfacepc.music.163.com` 不变；没有试错 host、Weapi、旧字段或 SDK fallback。
- 保留既有 **PC compatibility metadata** 与其固定 UA，`header`／`e_r:false` 属协议层，不是业务 DTO。不是 Android 身份伪装；“文档 host＋既有 PC metadata”是**未实网证明的组合**。
- 原绑定账号／SessionToken 的 MUSIC_U；所有12项不采用响应 Set-Cookie。业务账号来自本地验证快照，不从 RoomInfo／command 用户字段反推身份。Cookie、JSON、crypto、HTTP 留在 infrastructure。
- 固定 api-enhanced 九wrapper中八项默认 Eapi，Status 显式 Weapi；这只是历史兼容性参照。本包 Status 明确按用户选择改为 Eapi，不混合运行两条路线，也未改 fixed lock／440 inventory。

## 3. 封闭请求表

路径均以 `/api/listen/together/` 开头。表列业务字段，外层 `params` 与协议 metadata 另算。JSON字符串不是 form 内逐字段明文 JSON，也不是普通对象直接替代。

| Typed action／operation | 后缀 | 封闭业务请求 |
| --- | --- | --- |
| Create／TogetherCreate | room/create | refer 固定 `songplay_more` |
| Check／TogetherCheck | room/check | roomId |
| Join／TogetherJoin | play/invitation/accept | roomId、inviterId、refer 固定 `inbox_invite` |
| Status／TogetherStatus | status/get | 空业务对象 |
| EndCheck／TogetherEndCheck | end/check | roomId |
| End／TogetherEnd | end/v2 | roomId |
| Heartbeat／TogetherHeartbeat | heartbeat | roomId、songId、playStatus、progress；本包不输出 playlistVersion |
| Play／TogetherPlay | play/command/report | roomId、commandInfo JSON字符串 |
| Queue／TogetherQueue | sync/playlist/get | roomId |
| ReplaceQueue／TogetherReplaceQueue | sync/list/command/report | **仅 command JSON字符串**；无顶层 roomId／playlistParam |
| ReconnectInfo／TogetherReconnectInfo | restore/reconnect/info | 空业务对象 |
| DeviceNotice／TogetherDeviceNotice | device/reconnect/notice | roomId |

`commandInfo` 支持 PLAY／PAUSE／NEXT／PREVIOUS／GOTO／PROGRESS；包含 formerSongId、targetSongId、playStatus、progress、clientSeq，共六个字段（含 commandType）。材料新增 triggerType／userId／ignoreUserId／serverSeq 的完整取值与权限语义尚未闭合，本包不伪造这些可选扩展、不混成 NIM 接收的 ignoreUserIds。`command` 仅 REPLACE：空 anchorSongId、anchorPosition=-1、clientSeq、displayList、randomList、单个原账号 `{userId,version}` 的 version 数组。它是当前窄请求候选，不是完整多成员版本向量或服务器 CAS 证明。

RoomId 为1–128 ASCII字母／数字／下划线／连字符，和独立 ChatRoomId 不互换。Song／账号规范正数字符串保持精度；counter 0–9007199254740991；progress 0–604800000 **毫秒**。替换列表各1–1000项，合法 SongId、多重集合一致，保留顺序与重复。这仅是结构约束，不证明来源、权限或服务端接受。局部预算不是官方房间／歌曲／时长上限。

## 4. 响应候选与刻意不映射项

根 code＋data 对象为本地闭合候选；不尝试 root/data／大小写／旧路径 alias。Create、Join、Status 只读 `data.roomInfo` 中少量已知字段；roomId必需，Join校验与请求一致。独立 chatRoomId、imType、roomType、mainDevice／roomCanJoin／roomEnd 为有限 metadata，不选择 IM 实现或发布加入状态。

- Check：joinable bool及有限提示；不是 RoomInfo。false 仍是成功获取一个不能加入的判断，不自动发 Join。
- Status：inRoom bool；false 允许无 roomInfo。它是 HTTP 自述，不是本地同步状态。
- EndCheck／Heartbeat：result bool。Heartbeat 没有固定10秒 timer，也不更新播放时钟。
- End：空 data 或数值 inner code200；不接受旧 `success:true` 假设，不证明成员退出或远端房间消失。
- Play／ReplaceQueue：GeneralResult 的 result 必须 bool；显式 false／失败 inner code 被拒绝。DeviceNotice 缺完整业务响应定义，空 data 仅表示 HTTP envelope Ack，不是设备切换已成功。
- Queue：严格 `data.playList`，其中 playlist 必须对象；仅提取有限 playCommand 字段。**playlist完整内部 schema 未给出，不生成 songs／队列 DTO，返回 `playlistContentUnmapped=true`**，不接受旧 `data.playlist.displayList.result`。
- ReconnectInfo：有限 canReconnect／roomId／mainDevice／needDeviceReconnectConfirm／supportMultiDeviceReconnect 事实，不自动恢复、确认设备或重新加入。

默认 response budget 64KiB，Queue 512KiB；JSON字符串请求256KiB。纯局部限额。文本按原文有限 plain text，不是 HTML／动作／生成说明；不暴露原始 JSON、Cookie 或 IM token。缺失、错型、分数 counter／progress、业务 room 不匹配和旧 alias 归 InvalidResponse；HTTP200不代替 decode/schema 验证。以上 synthetic schema 不能当官方完整模型或已实网成立的响应。

## 5. 原上下文、取消与四结果

[Core DTO](../../src/domain/music/together_types.h)及[application port](../../src/application/ports/together_gateway.h)不依赖 JSON／Network／Gui。[Facade](../../src/application/backend/backend_facade.cpp)→原 worker→[Gateway](../../src/infrastructure/netease/music/together_gateway.cpp)→SessionApiClient，仍用原生命周期、queued delivery／反向销毁；没有第二播放器／后台 owner。

每层所有12项共享 **8 pending slots**，不另拆“4读／8写”。原账号、SessionToken、Facade generation、UUID、kind、调用方 roomGeneration 关联；重复 pending UUID拒绝。roomGeneration仅本地请求关联，**不是已验证 room、serverSeq、服务器版本或自动房间切换 fence**。调用方需要撤销旧意图时显式取消。UUID不等于服务幂等键，counter不等于 CAS。

收到账号／phase／token变更通知及完成时当前快照均校验；即使token相同而账号／phase改变也作废。取消、退出、client-loss、close-drain不复活旧回调。事件结果只回原调用方上下文，payload中的账号不授权更新本地身份。

| outcome | 含义 |
| --- | --- |
| NotSent | 未交业务 transport，或本地拒绝且明确未提交 |
| Acknowledged | 接收到本契约接受的 HTTP／业务事实；不等于 Active／同步／远端效果 |
| Rejected | 可识别 provider失败或明确业务否定；保留有限 code |
| Unknown | 已交 transport后的取消／超时／断开／schema失败或保守无法判定 |

Facade已派发但 Gateway尚未发送时也可能保守 Unknown；不伪造撤销服务效果。无自动 retry／fallback／outbox／乐观状态／补偿／刷新／readback；Retry-After只是有限提示，不启动调度。

## 6. 本轮离线证据

- 新 `together_http`：**109 pass**＝init/cleanup2＋12 contracts＋nativeEach1（实际解密12请求）＋55 malformed＋bounds1＋provider5＋cancel24＋guards1＋same-token/token scope6＋slots/loss/outcomes1＋Facade1。首次99/1只因原生断言遗漏既有profile `e_r:false`；明确断言后保留业务精确比较，未改算法／生产payload／负例／fatal规则；旧失败日志留存，不累加复跑。
- 最终15受影响 Debug suites **911 pass**：together_http109、protocol40、crypto18、transport22、decoder19、cookie21、api24、facade8、report18、collections189、search_assist117、comment_writes134、entity_search44、recommendation_history86、daily_playlists62；0fail／skip。
- 最终 runtime选择14函数＝15数据行＋init/cleanup **17 pass**，0fail／skip。新 togetherHttpThroughThread 明确Guest零API→合成登录→12 HTTP→错误旧queue层→hung读close；17总requests、14results、原token／caller thread、wrongThread0、created=destroyed，play/report/collection/comment/music请求零，本地队列为空。不是双账号／双端同步。
- 配置与affected build exit0、无warning；刷新compile DB；16 changed C++ fresh all-server LSP0；14 actual application objects／**6992 Ninja依赖，0 forbidden**；模板入口／QML未改。format／diff-check0、fixed440/public hash＋Python15通过；824本地引用路径missing0、变更文件有限Cookie／signed-query扫描flags0（不检查anchors／非全面安全），具体忽略日志JSON见 HANDOFF。
- `build/workspace-checks/together-http-*` 留ignored日志。既存cpp-check两文件输出解析问题任务92仍独立待查，不能把unparsed cache当clean。没有全48套件／Release／最低Qt／真实账号／服务／SDK／NIM／音频／keyring／UI证据。

## 7. 留待后续

[审计G1–G6](together-protocol-material-audit.md)依旧约束完整同步：原生 transport/token/envelope、SELFHOST选择、seq/version作用域/reset/echo/丢洞/回放、时间基准／权限／焦点／preview／掉线／多设备、Linux与许可。仅新HTTP授权，不自动开启这些能力。固定 stash `9907ac4a9ad83b9cefb79fea9d3337b56b91ba6f` 仍保留，不pop/drop/apply；其历史153／858＋13不是本轮911＋17或主线完整FR-043证据。
