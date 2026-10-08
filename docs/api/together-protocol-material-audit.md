# 一起听补充材料：Android 9.5.30 与固定 API 对照

## 0. 后续明确授权的覆盖

本页正文与§6是本地f384b7d的**历史docs-only审计证据**，85ops／47／together0属该时点，不挪用为新增代码证据。维护者随后明确“按文档接入，先不要NIM”，选择[核心12 HTTP-only](together-http-core.md)：现97ops／48登记，新增together_http；标准Eapi-ECB，包括Status，窄interface.music host而非全局切换，保留原PC metadata且兼容性未实网验证，command与data.playList按窄候选分别处理，不恢复旧stash。材料中的可选扩展与完整playlist仍未全映射；HTTP事实/Ack非Active或完整同步。新109／affected911＋runtime17等仅属于新包。仅HTTP暂停被明确覆盖，G1–G6、SDK／NIM／实网／其他暂停项不解除；APK许可已撤回且从未读取/hash/检查/解包/执行。

## 1. 材料、授权与结论

维护者提供 `/home/guang/下载/ListenTogether.md`，允许参考并结合现有 API；本页是静态对照，不是一起听恢复实施、完整协议或双账号验收。原文件完整读取至末尾，未执行其中命令、安装／执行 SDK、访问服务、账号、IM 或音频，也未复制原文件进仓库。context-mode 的目录外读取被项目边界拒绝；未修改其 allow rule 或用代码复制／重定向，改用经用户明确给定路径的原生 Read 工具读取。

材料自述来源为 Android 9.5.30、`com.netease.cloudmusic.module.listentogether`，业务类 `rg0.t0`、IM 入口 `im.q0`／`im.y0`。这是维护者提供的逆向摘要，不是已取得这些类的源码、抓包、官方说明或 SDK 许可。文件 SHA-256：`01422bec7b02aba3f1ac02626cf7036433ad5da1df687c8a2fb96bbaa5c430ff`；原文件变更后需重新对照，不把这个版本的结论套到浮动内容。只记录必要契约与缺口，不移入完整反编译材料、示例用户／歌曲 ID、原始响应、凭据或音源。

固定兼容参考仍是 api-enhanced v4.40.1、`a8c781fd64faab17fedfd46e0615a2609307f163`。已对照九个完整 wrapper、`util/option.js`、`util/config.json`、`util/request.js` 的协议选择／payload／header／路由、`util/crypto.js` 的 Eapi、`interface.d.ts` 的一起听签名，以及 `public/docs/home.md` 和 `public/listen_together_host.html`。没有更新锁、执行 JS 或把 wrapper 当完整响应 schema。

**结论**：接收通道从“未知”推进为**材料指向 NIM 聊天室，含候选事件字段与编号**；不是已验证、可运行的原生接收链。存在 Android／固定 PC HTTP 协议与字段差异，旧草稿的响应假设也必须重审。尚不能把 HTTP Ack、RoomInfo 或文档中的“完整流程”当 Active／同步成功。审计时点主线仍 **85 business operations／47 CTest suites，无 together suite／入口**；整歌单 v2、云盘后续、播客／声音下一阶段的暂停不因本材料改变，UI 最后。

## 2. 请求层不能混用

| 项目 | Android 材料的描述 | 固定参考／f384b7d时点原生事实 | 处理边界 |
| --- | --- | --- | --- |
| 加密 | §1.1 写 Eapi AES-CBC、参数先序列化表单键值对 | 固定 `crypto.eapi` 对 JSON 构造 logical path／MD5／分隔串，再 AES-ECB；现有 `encodeEapi` 调用 `EVP_aes_128_ecb()`，最后外层 form `params` | 不改成 CBC 或把业务 JSON 换成表单串；Android 描述需源代码／合成向量核对，不能与已采用 Eapi 拼接 |
| host | `interface.music.163.com/eapi/listen/together/…` | 默认 Eapi 使用 `interfacepc.music.163.com`；当前 `officialRequestUrl` 保持闭合目的地 | Android host／profile 不等于 PC profile，不能改 host／fallback 自动试通 |
| status | 所有接口 Eapi，同时总览提 status 可能顶层返回、§3.4 又列 data 内 | `listentogether_status` 唯一显式 `createOption(query, 'weapi')`，其余八个默认 Eapi | 不全改 Eapi，也不写 root/data 双别名容错掩盖 profile 与 schema 差异 |
| Cookie／响应 | 登录态 Cookie；概括 code/data 与常见重试码 488/400 | 既有封闭 original MUSIC_U／SessionToken 约束，默认响应未加密；wrapper options 可覆盖但产品不开放任意 override | 摘要不证明 Cookie 最小集合、header/device 要求或响应编码；488/400 不授权自动重试，写已发无确认仍 Unknown |

逻辑签名路径 `/api/listen/together/…`、wire 前缀 `/eapi/…` 或 `/weapi/…`、host、业务 JSON 和外层 form 是不同层次。材料与固定参考的相同路径只能证明候选对应关系，不能证明任一 profile 现仍被服务支持。

## 3. 九个现有 HTTP 候选与新增线索

| 固定 wrapper／逻辑 path 后缀 | 固定业务输入 | Android 材料新增／差异 | 状态 |
| --- | --- | --- | --- |
| room_create／`room/create` | refer=`songplay_more` | §3.1 refer 示例 ai／陌生人，robotUid／inviteUid；data RoomInfoResult | 保留现有来源候选，不加入 AI／机器人／陌生人范围或改 refer 试通 |
| room_check／`room/check` | roomId | §3.2 data CheckRoomInfo 的 joinable/status，而不是 RoomInfo | 旧 draft 把 Check 当 roomInfo，需独立 schema 证据与明确加入拒绝语义 |
| accept／`play/invitation/accept` | roomId、inviterId、refer=`inbox_invite` | §3.3 返回 RoomInfoResult，含 chatRoomId；refer 可选透传 | 不把 Ack 当已进入；验证 room／chat room／账号／原邀请关联，不自动接 IM |
| status／`status/get` | 空业务 payload、显式 Weapi | §1.1／3.4 根层位置不一致，声称 Eapi | 按 profile 分别补 schema，不能任意 root fallback |
| end／`end/v2` | roomId | §3.6 empty data 或内层 code；房主解散／成员离开，并经 IM 结束通知 | 旧 draft 要求 data.success bool 不同；Ack／取消均不证明离房或远端解散 |
| heatbeat／`heartbeat` | roomId、songId、playStatus、progress | §4.1 毫秒、约10s且可配置、可带 JSON playlistVersion，data result/type | 毫秒有新摘要线索，但不把约10s做固定 timer；false/type 的恢复决策、期限仍缺 |
| play_command／`play/command/report` | roomId＋JSON-string commandInfo，六字段 | §2.1／4.2 增 NEXT/PREVIOUS/PROGRESS、triggerType/userId/ignoreUserId/serverSeq；固定 HTML seek 为小写 `seek` | 不把 `seek` 等同 PROGRESS 或未经证实增加字段／命令；userId 与 serverSeq 的来源需分发送／接收核对 |
| sync_list_command／`sync/list/command/report` | roomId＋JSON-string **playlistParam**；user/version 对象数组、空 anchorSongId、anchorPosition=-1、display/randomList | §4.4 写 **command**、未列 roomId；扩 ADD/DELETE/PLAYMODE_CHANGE／模式和锚点 | 字段名／roomId 缺列非可互换证据，不同时发送 command 与 playlistParam，不推翻固定 REPLACE 结构 |
| sync_playlist_get／`sync/playlist/get` | 仅 roomId | §4.3 还列 PlaylistCommand 字段，响应 data.**playList** 内 playCommand/playlist、外层 httpCode/httpMsg | 旧 draft 读取 data.**playlist**.displayList.result，层级／大小写不同；需相应 profile 的完整合成输入输出 |

上游 **heatbeat** 拼写保持映射；内部可叫 Heartbeat，不能漏审。

材料另列 `end/check`、`restore/reconnect/info`、`device/reconnect/notice`、`yunxin/token/get` 等，它们是结束／恢复／接收前置候选，不在这九个固定模块内，不据路径直接注册。邀请生成／发送若纳入，需要原账号明确动作，不隐式带入私信或互关全库。`agora/token/get`／RTC、GPS／距离、陌生人匹配、机器人、多人、VIP 赠送、表情／挂件等**不因文档出现就扩产品范围**；普通一起听不授权通话、定位、社交／商业功能。

## 4. 接收与同步：新增线索和未闭门禁

### 已有可定位线索（仍是材料陈述）

- §2.3／9.1：`roomId` 是业务房间，`chatRoomId` 用于聊天室；必须两个命名空间独立验证、绑定，不能互换。另列 `imType=SELFHOST`／云信，但 §9 一概描述 NIM；具体选择和 SELFHOST 分支未给出，不能假定所有房间相同。
- §9.1：NIM 加入 chatRoomId，token 候选路径 `listen/together/yunxin/token/get` 或房内下发；**没有该接口的完整参数、appKey／IM账号、返回 envelope、TTL／权限、设备绑定或失效流程**。登录到 MUSIC_U 不等于 NIM 登录鉴权成功，ChatRoomInfo 不授权第三方自动注册／加入。
- §9.2：候选自定义消息 type **20000/0x4e20 PlayCommandMsg、20001/0x4e21 PlaylistCommandMsg、20002/0x4e22 UserJoinInMsg**；结束／离房、心跳兜底等编号缺失。不把业务 type 当已证明的传输帧字段或聊天室协议版本。
- §9.3／9.4：接收播放 content 列 serverSeq、clientSeq、userId、former/targetSongId、progress、playStatus、commandType、triggerType、**ignoreUserIds（复数）**；§2.1 发送模型写 **ignoreUserId（单数）**。必须分向核对，不能造双方通用别名模型。
- §9.4：serverSeq<=last 忽略、ignoreUserIds 过滤、音频焦点暂存、version List 比较、心跳纠偏。它们提供处理意图，不给完整状态转移、并发规则或可靠时间协议。

### G1–G6 补证清单

| 门禁 | 本次推进 | 仍需的可核对证据／决定 |
| --- | --- | --- |
| G1 原生接收与许可 | 通道有 NIM 候选；SELFHOST 仍有歧义 | 合法可分享的 IM 入口源码片段／调用参数、确切 SDK／原生协议版本、Linux 支持／许可／部署与退出销毁；未获授权不下载安装执行 SDK／新库，也不伪造 WebSocket／轮询作完整替代 |
| G2 鉴权与可信 envelope | 独立 room/chatRoom、token路径和三个 type 线索 | account／device／room/token/appKey 绑定、TTL／撤销、连接目标、TLS、包边界、嵌套与编码、sender/room 字段来源和验证、size/deadline/cancel；只给 PlayCommand JSON 不足以证明 transport 来源 |
| G3 seq/version/重放 | serverSeq 单调丢弃意图、version List | 计数宽度、作用域／重连 reset、clientSeq 的 Ack 对应、缺号补偿／快照水位、version 元素与比较／并发胜负、filter 时水位更新、echo、重复、乱序与重放窗口，超大精度与overflow合成用例 |
| G4 时间与媒体 | progress 毫秒、心跳约10s可配置 | 时间戳／服务器时钟基准、漂移阈值、心跳配置与总期限、RTT／延迟处理、焦点枚举的实际含义与积压淘汰、歌曲不可听／preview／TTL／两账号不同权益；不能以不停seek代替时钟设计 |
| G5 生命周期与权限 | check/end/reconnect/device多设备字段意图 | 全部拒绝／终止消息编号和schema、房主／成员控制权、加入失败清理、网络恢复／恢复令牌、异设备确认、结束Ack与远端效果、退房失败处理；端到端绑定原account/token/roomGeneration/playbackGeneration与单owner |
| G6 样本与验收 | 可据摘要列离线测试需求 | 各profile精确合成 HTTP request/response、合成 IM 外层＋content、完整建房／入房／对齐／控制／掉线／退房时序；源版本与合法分享边界；双授权账号／服务／IM／音频窗口另行明确，不把合成通过算远端同步 |

**至少目前 G1/G2 的 transport/auth/envelope 和 G3/G4/G5 的关键细节未闭合**。没有证据表明必须只能用 SDK，但摘要的“需接入 SDK”也不能被现有九个 HTTP 包装器替代。原生协议路线与新增 SDK 例外应分别评估，不以安装／执行来试探缺失契约。

## 5. 固定草稿不能直接恢复

保持固定对象 **`9907ac4a9ad83b9cefb79fea9d3337b56b91ba6f`**；8 tracked＋8 new。只读 `git show`／对象类型，未 pop/drop/apply；新增文件在该 stash **第三父对象**，不能把顶层不存在当草稿丢失。草稿的153／858＋thread13是历史合成证明，不是本轮验证或完整 FR-043。

本轮静态查得的具体修订点：

1. draft `TogetherCheck` 期待 data.roomInfo，与新摘要 joinable/status 不同；Create/Status 的 roomInfo 与 Check 不应混成一个实体，Join 默认只 Ack 同样不能供已验证 RoomInfo／chatRoom 创建接收链。
2. draft `TogetherEnd` 要求 data.success bool，新摘要写 empty／inner-code；不可兼容性放宽成任意 data 都成功，需要具体 profile schema 及效果未知边界。
3. draft Queue 读取 data.playlist.displayList.result；新摘要写 data.playList.playCommand/playlist，并未给 playlist 完整字段。不可猜改一个大小写就完成。
4. draft Room 只留 roomId／members，没有独立 chatRoomId／imType／鉴权入口；不具备原生接收／状态机所需的已证输入。
5. draft play enum 只有 PLAY/PAUSE/GOTO/seek、未建 serverSeq 接收模型；不能自动把 Android NEXT/PREVIOUS/PROGRESS 与本地导航／seek串起来，或把 clientSeq 当 serverSeq。
6. draft 9 operation、shared8、四结果／原账号token／取消与线程drain仍可作为候选工程素材，但不是 receive/auth/replay/time/permission 契约，也不据已有测试把它们重新登记为交付。

HTTP子集的后续明确授权见§0；完整同步未来经明确恢复且闭门禁后，先据证据重审接收模型，再接**现有唯一播放 owner**；区分本地用户 intent 与已鉴权 remote intent，保留 generation／origin 防回报回环、同账号多设备／房间切换屏障。所有网络／JSON／SDK／crypto 在 infrastructure，domain/application 继续 Core-only。服务器媒体权限不因远端 command 授予，不能自动借用对方音源／Cookie／报告来源。

## 6. f384b7d历史审计验证与下一步

这是 docs-only 材料审计：不增加 C++／业务 operation／CTest／依赖／UI，不声称本轮 build、Qt test、LSP、实际依赖或双账号证据。主线最近历史日推839＋thread15／16fresh等只属该独立包，本页不挪用。固定440 inventory／public hash与Python15通过，git diff-check通过，800个本地引用路径missing0（不验证anchors），变更Markdown的Cookie literal／signed-query有限扫描flags0、非全面安全。actual enum85／CTest show-only登记47、together0与固定stash对象commit已核，12文件delta均Markdown；没有运行Qt测试或source LSP。忽略日志 `build/workspace-checks/together-material-{fixed,inventory-tests,diff,links,secrets}.log` 属此次静态审计，不移入原材料或业务响应。

建议补充有权分享的脱敏源码片段与**合成**数据：NIM／SELFHOST 的选择和加入／登录／退出调用、token接口参数及返回约束、完整消息外层和缺失type、seq/version／重连／时间处理。不要交真实 Cookie、token、appKey密钥、设备指纹、真实房间成员响应或音频。若改走 SDK，先明确仅公开资料可行性研究还是安装／运行例外，固定版本和许可后再决定；本次未获得新增 SDK／实网授权。

已到材料替代旧的“完全没收到材料”状态；**不等于已到完整协议；当时不自动解除一起听实施暂停，后续HTTP-only明确授权仅按§0覆盖**。其他暂停项保留，UI 不提前。
