# 资源评论只读包

## 1. 范围与证据

本包实现五类显式读取／八个封闭业务 operation，复用原 MusicQueryService、原 session token、16 槽 facade mailbox 和后台线程。**后端合成契约，不是实网或 UI 验收**；没有发送网易云请求、读取真实凭据／账号／评论，或调用点赞／发布／删除／通知／用户评论历史接口。

固定参考仍为 `a8c781fd64faab17fedfd46e0615a2609307f163`，完整核对 `comment_music.js`、`comment_album.js`、`comment_playlist.js`、`comment_dj.js`、`comment_hot.js`、`comment_new.js`、`comment_floor.js`、`comment_info_list.js` 及 `public/docs/home.md` 的对应完整章节。wrapper 与其文档不是官方 API 规范：统计章有响应字段示例，其余响应没有被本包真实采集，下面 mapper 是**明确受限的合成候选 schema**，不能把 fixture 当服务实际字段证明。默认协议同时核对 `util/option.js`、配置及 request 选择规则；新版默认 Eapi，不把无显式 crypto 当成 Weapi。

### 资源身份

| Core 身份 | 私有 type／thread 前缀 | 普通读取 | 热门／新版／楼层／统计 |
| --- | --- | --- | --- |
| SongId | 4／`R_SO_4_` | 是 | 是 |
| PlaylistId | 0／`A_PL_0_` | 是 | 是 |
| AlbumId | 3／`R_AL_3_` | 是 | 是 |
| ProgramId（传统节目） | 1／`A_DJ_1_` | 是 | 是 |
| PodcastId（播客容器） | 14／`A_DR_14_` | **无已确认普通 wrapper，拒绝** | 是，响应仍仅候选 |

`CommentTarget` 是上述独立类型的 variant；`CommentId` 单独建模。**没有已确认 ArtistId 或 VoiceId 评论绑定，不开放、不转换为 ProgramId／SongId**。也不推断 ProgramId 和 VoiceId 的服务数字一定不同。排除视频／动态／创作者工作台类型，即使通用 wrapper 有其分支。原数字 type 和 thread 拼接仅在基础设施，不能由调用方传任意资源类型／线程字符串／host／crypto／uid／csrf／e_r。

## 2. 调用入口与 wire 契约

`BackendFacade` 提供 `comments(CommentsQuery)`、`hotComments(HotCommentsQuery)`、`sortedComments(SortedCommentsQuery)`、`commentFloor(CommentFloorQuery)`、`commentInfo(CommentInfoQuery)`；接受并分配 requestId 不等于远端业务成功。按 `musicCompleted(id, originalToken, MusicResult)` 获取结果，`cancelMusic` 沿用原取消入口。不自动请求下一页、楼层、统计、作者、头像或媒体，也不入队／播放／报告／写缓存。

以下是实际官方**逻辑路径**（传输层构造对应 weapi/eapi 地址），不是上游 `/comment/*` 服务包装地址；均固定官方目标、JSON 解码、不采纳读取响应 Set-Cookie：

| operation | 逻辑路径 | 协议 | 业务 payload | 响应限额 |
| --- | --- | --- | --- | --- |
| SongComments | `/api/v1/resource/comments/R_SO_4_<SongId>` | Weapi | rid、limit、offset、beforeTime | 4MiB |
| AlbumComments | `/api/v1/resource/comments/R_AL_3_<AlbumId>` | Weapi | 同上 | 4MiB |
| PlaylistComments | `/api/v1/resource/comments/A_PL_0_<PlaylistId>` | Weapi | 同上 | 4MiB |
| ProgramComments | `/api/v1/resource/comments/A_DJ_1_<ProgramId>` | Weapi | 同上 | 4MiB |
| HotComments | `/api/v1/resource/hotcomments/<固定前缀><ID>` | Weapi | 同上 | 4MiB |
| SortedComments | `/api/v2/resource/comments` | Eapi／既有 Pc profile | threadId、pageNo、pageSize、cursor、sortType、showInner | 4MiB |
| CommentFloor | `/api/resource/comment/floor/get` | Weapi | parentCommentId、threadId、time、limit | 4MiB |
| CommentInfo | `/api/resource/commentInfo/list` | Weapi | resourceType（固定十进制文本）、resourceIds（JSON 字符串数组文本） | 1MiB |

Guest／Authenticated 原上下文均可提交资源读取，不另要求 MUSIC_U；若已登录，既有 client 只使用请求时原 jar/profile。**可提交不保证服务免登录或可访问**。403／业务拒绝按原失败模型返回，不登录、不换 host／协议、不绕过挑战；也不顺带读取账号评论历史。Pc envelope 的 `e_r:false` 与 header 由既有协议层固定注入，业务调用方不能控制。

### 有界分页／严格输入

- ID 必须规范正 signed64 十进制字符串；响应数字 ID 只接受 ≤2^53−1，宽 ID 用规范字符串。评论作者 ID 不是已验证 AccountId。
- 普通／热门：limit 默认20、1..100；offset 默认0、0..100000；before 默认0、0..9007199254740991，传 beforeTime。缺失／未知分页不推断完整列表。
- 新版：page 默认1、1..10000，limit 默认20／1..100，`(page−1)*limit≤100000`。推荐固定 sortType99，cursor 为数值 offset（不是旧 sortType1）；热度 sortType2，cursor 文本 `normalHot#offset`；时间 sortType3，第一页 before0、后续显式正 before，cursor 十进制文本。非时间排序不接受非0 before。`showInner` 默认true，显式false原样保留，不复制上游 `showInner || true`。
- 楼层：parent 为独立规范 CommentId，limit 默认20／1..100；time 缺失→固定−1 sentinel，显式0仍为0，非负显式值≤上述安全整数。不从 root.time 自动造下一次调用。
- 统计：1..100 个同命名空间、规范、唯一目标；异构／重复拒绝，不偷偷去重。provider 缺项单独 `missing`，不是零评论。resourceIds 保持请求 ID 为字符串，即使宽 ID。
- 内部 adapter 再次严格验证 JSON 参数数量、字段类型、整数／bool、顺序及枚举，不把文本／bool／小数当整数，不照搬 JS 真值默认或任意透传。

## 3. 合成候选响应与隐私

所有成功先要求业务 code200；错误／缺字段不能包装成空成功。

| 结果 | 候选结构与保留字段 |
| --- | --- |
| CommentsPage | root.comments 必须数组；可选 hotComments／topComments 数组（各≤100），total／more |
| HotCommentsPage | root.hotComments 必须数组；total／more 可选，不借 root.comments 兜底 |
| SortedCommentsPage | root.data 对象内 comments 必须数组（**不以 pageSize 为上限**：2026-11-10 游客实测推荐排序请求 20 条回 26 条，原映射整页拒绝；改为最多 100 条，本产品防护上限）；totalCount／hasMore／cursor 可选，返回 sortType 若有必须匹配查询 |
| CommentFloorPage | root.data 对象内 comments 必须数组；ownerComment 若有必须匹配 parent；可选 parentCommentId 回显、totalCount／hasMore／time |
| CommentStatistics | root.data 必须数组，每项 resourceId 必须属于请求且不重复；可选 commentCount／likedCount／shareCount／musicianSaidCount、commentCountDesc、liked／commentUpgraded |

主数组不超过调用 limit，统计不超过请求项数。数组缺失／null／非数组／坏行／矛盾总数整页 InvalidResponse；已存在的可选数组坏类型同样拒绝。合法 `[]` 可成功；可选缺失／null 为未知，不默认为0／false／空数组。主评论与附属展示区保留各自 provider 顺序／重复，不自动合并去重或推算全量。

每条主评论需规范 commentId，可选 content（≤4096、合法UTF16、无NUL）、user（公开 userId／nickname／安全头像引用）、time（原 provider 安全非负整数）、likedCount／liked。缺失或null内容可表示未知／删除占位，但本包不判断删除原因。user若存在但nickname缺失，用空文本作展示占位；不能据此断言真实名字为空。`beReplied` 引用≤10项，引用ID可未知；`showFloorComment` 的 replyCount 和≤10个非递归 CommentExcerpt 预览可未知，不递归发布任意JSON。若回显 threadId／resourceId／resourceType，需匹配原 target；楼层 owner 另匹配 parent。头像无法安全表示时为空，不下载。

普通／热门／新版 `nextBefore` 仅从**最后一行已知 time**得出；楼层 `nextTime` 亦如此，root.time 另作 providerTime 保留，不混同。无最后行time则不给候选cursor；返回原 providerCursor 只展示／留作补证，不把任意文本当后续请求。time 不推断本地时区、真实发布时间单位或听歌时长；countDescription不解析成精确计数，未知／缺项不伪造0。

评论、引用与作者都是不可信外部文本：当前只按值返回，不渲染HTML／链接／富文本，不记录原响应／正文／作者资料，不入SQL／共享缓存。未来UI必须显式纯文本或安全转义，不能把例如 `<b>` 当执行指令或自动打开URL。liked只属于原上下文，不能证明认证／点赞权限；旧token／注销／关闭仍隔离。已经交给调用方的DTO副本不会被系统自动擦除，消费者需同步清理可见内容。服务端删除／审核／地区／账号权限与楼层分页语义仍需明确授权的受控验证。

## 4. 文件与生命周期

- Core 值：`src/domain/music/comment_types.h`；独立 author／CommentId／五种查询与结果，不引入JSON或网络。
- 关闭式 wire adapter：`src/infrastructure/netease/api/comment_operation.{h,cpp}`，由原 `api_operation.*` 注册／校验；不是可配置端点或泛型Manager。
- 候选映射：`src/infrastructure/netease/music/comment_mapper.{h,cpp}`，由原 music mapper／gateway 分派。
- `application/ports/music_gateway.h` 与 `backend/backend_facade.*` 复用既有请求／结果／取消、generation和原token；service／runtime无需新并发机制。
- 429保留Retry-After，不自动重试；−462等业务拒绝原样ProviderRejected，不能改成登录必需／版权结论或挑战绕过。取消与stale返回原token，不重标为新身份。signOut立即意图屏障，完成通知沿用排队交付；close真实排空资源。
- 无额外 runtime 依赖、Node服务、UI、spoken播放／报告适配或账号写入。registry43→51，CTest34→35；原440模块审计hash及164／40／236历史归类保持不变。

## 5. 验证与剩余门禁

`tests/unit/comments_test.cpp`：149 pass（包括data rows及init/cleanup），34个有效资源／模式组合，32个有效JSON坏schema案例；身份、边界、plain-text、未知／零值、宽ID、重复展示、统计missing；68条Guest／Authenticated双请求native数据行；八operation取消／换token／429，−462无fallback，16槽facade与晚到隔离。Weapi检查固定路由／form及独立payload；Eapi额外解密实际wire业务payload、固定e_r:false。基础加密互操作沿用已有向量；不能据这些请求替身声明官方服务兼容。

`commentsThroughThread` 验证五种显式读取、调用方线程／原token、非自动分页／楼层／媒体／报告以及hung floor close取消和后台排空。首次测试31fail已完整复核：30条新版wire测试漏算既有Pc envelope的e_r:false，另1条facade错误要求异步completion同步发出；仅修测试预期并增强StaleContext／旧token断言，production profile与意图屏障未改变。只记最后成功结果，不累计失败／旧运行。

最终受影响七套件共328 pass、另选六条线程数据用例共8 pass，无fail/skip；14个变更C++路径刷新编译库后主动LSP均clean。Ninja实际14个application编译对象依赖未见infrastructure／Network／Multimedia／Sql／Quick／Qml／Gui／JSON／OpenSSL泄漏，UI delta0；clang-format dry-run／diffcheck通过，505个本地Markdown文件链接有效（不查外链／anchor，排除代码语法），固定440模块hash校验通过。详情见[接手指南](../HANDOFF.md)和[测试策略](../12-quality-and-testing.md)。不是全35套件／Release／真实账号／真实服务／UI验收。

复跑范围：

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --target tst_comments tst_podcast_reads tst_entity_search \
  tst_catalog tst_music tst_session_api_client tst_backend_facade tst_backend_runtime --parallel 4
cp build/linux-dev-local/compile_commands.json build/compile_commands.json
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(comments|podcast_reads|entity_search|catalog|music|session_api_client|backend_facade)$' -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  commentsThroughThread podcastReadsThroughThread entitySearchThroughThread playbackThroughThread destructionStillDrains
```

随后[五动作独立写](comment-writes.md)已明确批准原生v3前置并合成接入，不改变本读取包／数字type／原149与328＋8历史证据；独立8槽、原account-token-generation与Unknown，不自动刷新读取结果／计数、从author推断owner或生成创建ID。未交付：真实评论schema／权限／时间单位／完整分页证据、Voice／艺人评论绑定、UI、持久化、账号评论历史／通知、抱抱写及Podcast发布／回复；真实issuer格式／TTL／绑定、审核／删除效果也另验收。实网和真实账号验证必须另行授权，不沿用已停−462窗口，不读取记忆凭据，也不为了测试自动登录。后续可推进已确认的订阅／云盘只读或显式收藏／歌单写设计小包；未知写入结果／协议门禁另立契约，不由本包读取成功推导。
