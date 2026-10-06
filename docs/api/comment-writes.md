# 评论点赞、发布、回复与删除后端

## 1. 范围、授权与证据

维护者确认五动作一起接入，并在核对门禁后选择**全部接入封闭原生v3前置链**。只有静态／合成离线验证：无真实账号、密钥环、评论、易盾／网易云业务访问、私人文件、SDK／JS执行、UI、推送或发布；此前−462窗口不恢复。不新增运行库／服务，domain/application仍Qt Core-only。

完整核对固定a8c781fd64faab17fedfd46e0615a2609307f163的comment.js、comment_like.js、comment_add.js、comment_reply.js、comment_delete.js、register_checktoken_v2/v3.js、options／config／request.js全519行，home.md点赞／旧统一接口／新版三动作章节，以及TS全部comment overload、对应函数、CommentType／Action／RequestBaseConfig。固定参考是兼容代码，不是官方规范或真实schema／权限／现行支持证明，历史440／164-40-236不改作运行注册表。

旧统一comment接口强制Eapi+v2 Watchman SDK，**不采用／降级**。新版发布／回复明确XEAPI+v3，v3注册器是固定官方易盾HTTPS GET而不是Watchman SDK；按维护者本次许可新增窄原生HTTP实现，不执行JSONP／JS／SDK、使用Node/WebEngine、接受Core／配置／外部传入token或空token、固定config token／通用转发。整歌单收藏的v2 SDK门禁保持延期，不能换成此次v3链。

home新版回复示例错写/comment/add，删除错写/comment；旧删除说明把content混写为ID。遵循实际module独立逻辑路径和cid／commentId映射，不把文档示例、TS generic Response或读取成功当写成功证据。

## 2. Core值、入口与资源范围

新 `domain/music/comment_write_types.h`：

- CommentReaction{target, CommentId, liked}为点赞／取消；CommentPublish{target, content}；CommentReply{target, parent CommentId, content}；CommentDelete{target, CommentId}。
- CommentMutation{eventId, CommentChange}有4封闭variant／5动作，原CommentTarget独立SongId／PlaylistId／AlbumId／ProgramId／PodcastId。IDs为正规范signed64十进制；eventId非空，本地关联／在途去重而非服务幂等键。
- **发布／回复仅歌曲、歌单、专辑、传统节目**。新文档／TS没有建立Podcast容器发布支持，config map的7不单独证明可以发布。点赞／取消／删除含已文档映射的PodcastId候选。无VoiceId／ArtistId、MV／视频／动态／工作台映射，不强转ID或传任意thread。
- 评论文本≤4096 UTF16本地预算，合法UTF16、非空非仅空白，拒NUL／C0（允许TAB/LF/CR）／DEL；保留合法外部文本／空格／引号／换行／Unicode，不规范化、裁剪、自动补内容、清空或正文日志。4096不是服务最大值；不渲染富文本／自动打开链接。
- 删除单个显式目标，permission由服务器判断。公开author ID／liked标记不是已验证账号／本人所有权证明；不接受isOwner或请求uid，不批删／自动删，不保证审核或删除立即全网生效。

`BackendFacade::mutateComment(CommentMutation)`／commentWriteCompleted(id, originalToken, CommentWriteResult)／cancelComment，独立Core port ICommentWriteGateway，**五动作共用独立8槽**（不是原collection／report槽的合并事务）。非Ready／Authenticated、无效账号／输入、容量／UUID冲突返回0，无派发／完成信号；非零ID是本地受理，不是远端提交或创建ID。

Native gateway再次核对原account／token／Authenticated；同token异账号InvalidRequest，旧tokenStaleContext。调用方generation、切号／同token账号或阶段变化／close、晚到交付隔离原结果。无新增线程／Manager、读取变体／隐式写；正文与原DTO副本仅进程内有界传递，不进SQL／outbox／cache。取消释放本端持有者，不保证调用方副本／allocator安全擦除，消费者需清理旧账号内容。

## 3. 五个固定业务请求

| operation | 官方逻辑path | protocol／响应 | 业务payload |
| --- | --- | --- | --- |
| CommentLike | `/api/v1/comment/like` | Weapi／JSON64KiB | threadId、commentId |
| CommentUnlike | `/api/v1/comment/unlike` | Weapi／JSON64KiB | threadId、commentId |
| CommentPublish | `/api/resource/comments/add` | XEAPI／EapiAutoJson64KiB | threadId、content、resourceType文本0、expressionPicId文本-1、bubbleId文本-1 |
| CommentReply | `/api/v1/resource/comments/reply` | XEAPI／EapiAutoJson64KiB | threadId、commentId、content、resourceType文本0 |
| CommentDelete | `/api/resource/comments/delete` | XEAPI／EapiAutoJson64KiB | threadId、commentId |

Default固定官方host：Weapi music.163.com、XEAPI interface3.music.163.com。所有业务要求原MUSIC_U（MUSIC_A不足），XEAPI必须有效原生协议key；key缺失不自动注册／降级。前置与实际发送均检查所需凭据；响应Set-Cookie不采纳／不更新身份，XEAPI既有会话密钥生命周期仍原epoch隔离。逻辑路径不是Node wrapper路由。

内部strict adapter只接受kind（Core次序0..4）、id和相应comment／content，精确数量／类型；拒任意uid／header／checkToken／threadId／crypto／时间戳／图片字段／resourceType覆盖。**写wrapper类型0/2/3/4/7与读取私有resourceType4/0/3/1/14不同**；不直接复用数字。目标前缀按Core类型生成R_SO_4_／A_PL_0_／R_AL_3_／A_DJ_1_／A_DR_14_，ID保留宽字符串，不转double。固定resourceType:"0"是新版写body字段，不是调用方选择命名空间。

## 4. 原生v3前置链、秘密与门禁

只对Publish／Reply：原账号及MUSIC_U／XEAPI key前置成功→一次新token申请→原账号上下文复核→一次业务写。Like／Unlike／Delete不调用issuer。SDK token、凭据和原生加密细节不进入Core／UI。

- 独立同线程CommentV3Issuer，固定GET `https://ac.dun.163yun.com/v3/b?pn=YD00000558929251`，无用户host／pn／Cookie／Authorization／x-music-u／Referer，manual cookie读写／auth复用／redirect，禁缓存、identity编码，不忽略TLS错误或切域重试。
- 本地8在途／10s transfer／15s总deadline、正文16KiB、header64KiB。没有后台预取／缓存／复用／持久化，两个显式动作各自申请；不推断服务token时效、账号绑定、反作弊验收或生成算法。
- 严格候选：有界合法UTF8、整串 `null(<JSON array>)`（允许尾分号／外围空白），array恰3项；第一项**数值200**，第二项非负safe integer，第三项非空1..4096可见无空白ASCII。不执行回调，拒root object、其他回调、尾随程序、坏码／宽数字／空token／控制符／Unicode header，不能用源码宽regex substring和失败返回空继续发送。实际响应尚未采集，候选格式限制失败会拒绝，不fallback。
- token只在基础设施临时value／Pending／受控HTTP header；SessionApiClient不把它置入payload。**只允许两个已登记XEAPI add/reply路径的X-antiCheatToken**，其他op携带或这两op缺失／坏token均不发；Transport再限制protocol／path／destination，不把它开放为任意xeapiHeaders。并不证明普通可构造的内部bytes有密码学来源；可信边界是原生issuer及封闭Gateway，Core／配置无token入口。
- 获取失败、取消／旧账号、协议key不可用、API client丢失等禁止后续业务。源码空token、跨号／缓存token、旧Eapi-v2统一reply／delete或改crypto不是fallback。key在token等待期间失效，实际XEAPI发送仍拒绝；不伪成功。
- 临时buffer尽力清除／对象结束释放，但Qt隐式共享、已交付值及网络栈副本不保证安全擦除。无token／正文／原响应日志，许可／真实反作弊兼容仍需独立授权验收，MIT不授权服务或SDK使用。

## 5. 结果、取消与部分效应

CommentWriteResult仅eventId、CommentWriteOutcome／Error、CommentWriteStep、optional NetEase providerCode／retryAfterMs；无新评论ID、raw JSON／msg／正文／作者／token／URL／推断所有权。**generic响应不证明创建身份**，发布Ack不创建本地评论、不按相同正文猜ID／对账。

Step为Token／Writing／Unobserved，是最后本地阶段而非server进度或已收件证明。Token阶段完成不能Ack；Facade拒绝非Writing Ack／错generation／token／账号，取消统一Unknown/Unobserved并保留原eventId/token，晚到不能复活。结果四种：

- NotSent：已知没有将**评论业务写**交给transport。token HTTP可能已请求／分配，不声称全流程零网络或无issuer侧效应。
- Acknowledged：原账号上下文中业务码200（既有decoder允许合法字符串200）；不是可见／审核通过、内容完整性、权限、最终计数或删除效果证明。
- Rejected：合法非200业务码；仍不承诺绝无部分效应。
- Unknown：业务可能已发后cancel／timeout／HTTP／decode／失效，或Facade不知道派发阶段；不是没发／失败可重试／撤销成功。

issuer错误归TokenUnavailable／NotSent，第三方码不冒充NetEase providerCode；业务429保留合法Retry-After信息但不重试，−462不推断原因或绕过。Gateway发送前cancel可确认NotSent，业务发送后Unknown；token等待期间client销毁NotSent/Token、业务挂起时client销毁Unknown/Writing，issuer真取消排空。close先销毁评论gateway及issuer／reply／timer，再原vault/client/key/session资源排空，不是远端撤销。

5动作是一包交付，不是batch／原子事务；每次显式动作独立完成，无自动点赞后发布、回复后删除、whole-library／作者／头像读取、refresh、乐观修改、计数推算、播放／报告、重试、flush、persistent outbox或可靠核验。未来UI权限确认／纯文本／审核和Unknown体验仍未验收。

## 6. 合成验证与交接

Qt6.11.2 Debug，LC_ALL=C.UTF-8／QT_FATAL_WARNINGS=1。最终10套件：comment_writes134、comments149、xeapi23、http_transport22、session_api_client24、backend_facade8、collections189、playlist_writes122、playlist_cover_writes57、bootstrap32，共**760 pass**；选定9条线程数据行**11 pass**（含init/cleanup），0fail/skip。登记42→43套件、74→79业务operation，v3 issuer是独立窄前置传输、原公钥链另计；非全43／Release／真实服务、Sdk、UI验收，原封面562＋11与新suite初次129＋3不累加。

新suite：4variant／5动作、文本／Unicode／4096、23资源模式及native请求／原Cookie不轮换，独立固定path／payload／严格键／宽ID、真实XEAPI头／加密响应、17坏issuer行、60响应模式行、24三阶段cancel×logout行、Guest／MUSIC_U／foreign-account／missing-key、直接transport双重token白名单／实际XEAPI缺Cookie阻断、client毁于token／business两行、same-token账号／Guest／错generation／晚到Ack、8槽／UUID、fresh2token不复用／issuer8容量发送前取消。请求业务密文不声明逐次独立解密，既有crypto互操作另有向量；普通type映射不是实网成功证明。

线程 `commentWritesThroughThread`：完整合成key／匿名安装与登录，五动作独立（unlike403保留Rejected）、坏token无业务派发、挂起reply业务和另一publish token期间close→2Unknown/Unobserved，**14总请求／8结果**。原token/eventId／调用线程，created=destroyed、wrongThread0、真实资源排空；无额外read／collection／play／report、queue空，无真实正文／私有记录。

22改动C++ paths fresh all-server15s／concurrency4 LSP全部clean，真实14 application对象6957 dependency entries：基础设施／JSON／非CoreQt／OpenSSL穿透0，UI0。无需新库／图片依赖扩展。新moc在AUTOMOC生成/DB刷新后存在且fresh clean；普通enum初始化／std::function factory的bit-field启发式误报已Read并精确标记，无源码suppression。一个真实未用session_types包含改为声明owner QMetaType，port保留直接SessionToken owner；配对TransportRequest token字段缺失已补并fresh clean，不能称未完成的配对编辑可编译。此前3path probe一项inconclusive不当clean，后续22全部确认；邻接music_gateway.h／collection_operation.cpp另2path fresh all-server也clean。

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --target tst_comment_writes tst_comments tst_xeapi \
  tst_http_transport tst_session_api_client tst_backend_facade tst_collections tst_playlist_writes \
  tst_playlist_cover_writes tst_bootstrap tst_backend_runtime --parallel 4
cp build/linux-dev-local/compile_commands.json build/compile_commands.json
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(comment_writes|comments|xeapi|http_transport|session_api_client|backend_facade|collections|playlist_writes|playlist_cover_writes|bootstrap)$' -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  commentWritesThroughThread commentsThroughThread playlistCoverThroughThread playlistWritesThroughThread \
  collectionsThroughThread libraryThroughThread playbackThroughThread destructionStillDrains
```

ignored日志comment-write-final-{build,focused,thread}.log及comment-write-{boundaries,format,diff-check,fixed-hash,links,secrets}.log、静态audit／first／review记录单列。不交付本机cache／凭据；完整docs／FR-030-031部分／域17／TC-COMMENT-WRITE／R-026／路线图／handoff／indexes／NOTICE同步，本地独立提交不推送／发布／bundle。clang-format dry-run、git diff --check及fixed440／公共文件hash通过；**690本地Markdown文件引用、missing0**（排除代码，不查外链／anchor），改动文件有限live-looking Cookie长值模式hit0。证据单列，不当全面秘密／当前服务保证。真实issuerschema／TTL／绑定、NetEase权限／审核／响应／删除效果／Podcasts发布、UI与可靠核验仍另授权；旧v2门禁不绕过。参见[评论读取](comment-reads.md)、[后台生命周期](backend-runtime.md)、[写结果基础](collection-writes.md)。
