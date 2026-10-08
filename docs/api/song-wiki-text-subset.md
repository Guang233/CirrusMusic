# 歌曲百科：受限纯文本子集（非完整百科）

## 1. 授权、证据与身份边界

接续本地 `6f63c244a280b1f743fa9a3cf1ef89b43240f35a`。维护者明确继续补证，并选择接入两种纯文本子块，允许原请求关联，必须标明 **“内容身份未独立验证”**，不宣称完整百科。旧 [11次发现窗口](discovery-public-response-verification.md) 的 SimilarArtists301停止不被重试；本次是另行确认的、仅百科公开元数据窗口。

2026-10-08，四次串行单次请求：搜索“稻香”→其原搜索SongId百科；搜索“海阔天空”→其原搜索SongId百科。全部 HTTP200/business200，结束 UTC `2026-10-08T07:13:21Z`，无重试、换路由或错误后继续。仅既有原生 C++/Qt Eapi/Weapi/默认TLS/transport；不登录、读凭据、采用返回Cookie、取音频/CDN、写、SDK或运行上游JS。原上限四次、单次15s/2MiB；首非成功即停。原paused/APK禁读/固定stash、UI最后全部保留。

两份响应外层块分别6/7；只保留技术discriminator、结构类型/计数与比对bool，合并出现 `songDetailNewSongWiki`、`songWikiArtistInfo`、`songWikiSimSongs`、`songWikiMoreVersions`、`songWikiSimPlaylist`、`songWikiPodcast`、`songWikiFirstListen`。主块内十种子块均采样，存在歌曲信息、基础信息、MV、评论、曲谱、娱乐、奖项、歌词等异质节点；一般数组只采前三项，不证明完整数组契约。

主块rnData.songName与原搜索title、albumId与原搜索AlbumId均匹配。但主块内blockInfo.songId不满足规范正ID；相关节目里的相同SongId也不是主资源身份回显。**描述性匹配不证明主百科内容SongId；opaque encodeId不猜解。** I/S是下面的严格有限映射；L只是两份原结构观察与描述匹配，未在实现后再次访问服务；完整百科、独立身份、全部块和所有节点形状仍G。

## 2. 闭合请求与原来源工厂

固定参考 song_wiki_info 的 Eapi logical path `/api/link/page/parent/relation/construct/info`，不是wrapper route。唯一内部参数id规范正signed64字符串；拒绝数值/空/非规范/额外参数。生成业务payload只有：

- `positionCode: "songWikiMainPosition"`
- `extJson`为紧凑JSON字符串，内容`states.playingResource.current`为原SongId字符串，`scene:"songWiki"`。

沿原default Eapi/JSON/PC profile/interfacepc.music.163.com、AES-ECB、header/e_r:false，不另造crypto、host、issuer或fallback；response limit 2MiB，response Cookie不采用。Guest/Authenticated仅本地提交资格，不证明免登录、权限或服务兼容。

[SongWikiTextQuery](../../src/domain/music/song_wiki_text_types.h)只能由 `fromSong(sourceContext, originalSong)` 工厂形成，保原SongId、非空title（≤4096 UTF-16 units）、原AlbumId与有效SessionToken。无专辑ID/坏ID/超限/空title拒绝；不trim或重写Unicode。工厂是本地结构来源，不是签名收据，调用方须传原读取结果与其token，不能把任意拼出的Song当服务身份证据。新 `BackendFacade::songWikiText(query)` 复用 [MusicQuery/Result](../../src/application/ports/music_gateway.h)、read16、Service/Gateway/原worker/token-generation/cancel-close；没有新owner或自动账户加载。旧source token不得借新token重用；同token私有账户/认证态屏障保持，不扩大取消公共读取。

原source query、returned observedSongName/AlbumId及result-kind在Facade交付再关联；错误种类/错原Song/标题/专辑拒绝。DTO只有typed query、原显示文本、受限sections、unsupportedBlockCount、metadataOmitted，以及单值 `SongWikiIdentity::NotIndependentlyVerified`。**关联不是独立内容身份验证、权益/播放绑定或完整制作署名。** 无Core JSON、URL字段/动作、opaque ID或provider对象。

## 3. 严格响应子集与预算

[专用mapper](../../src/infrastructure/netease/music/song_wiki_text_mapper.cpp)不复用旧SongWikiSummary或UGC/root/data alias：

- code必须数值200，data必须object，data.blocks必须array≤32；每外层块是object/positionCode非空string≤80。
- 恰一 `positionCode == bizCode == songDetailNewSongWiki`，rnData必须object；缺主块或重复主块失败，不生成空百科。
- rnData.songName必需string≤4096，与source title精确相等；albumId由现有canonical providerId映射并等于原AlbumId，数字仅安全整数，字符串保宽ID。rnData.blocks必需array≤32；每项object/blockCode非空string≤80。
- **只接** `wikiSubBlockSongInfoVo` 与 `wikiSubBlockBaseInfoVo`；blockInfo必需object，blockName必需string≤256，desc可选string≤4096；wikiSubElementVos必需array≤100。
- 每element object/title必需string≤256；content可选string≤4096，wikiSubMetaVos可选array≤16；每meta object/text必需string≤4096。至少content或meta数组存在；显式空字符串/[]合法，missing不当空，已出现的null/object/错类型或超限拒绝整结果，不截断成假成功。
- 所有保留name/label/desc/content/meta text合计≤131072 **UTF-16 units**，不是响应字节数。独立JSON响应2MiB预算也检查。保原次序、重复、空值和Unicode，不合成散文、合并署名、推测role/pattern/日期含义；文本中的HTML/URI样子仍只是不可信显示文字，未来UI必须plain text，不能解析为动作。
- 未支持的外层/内层块计数并跳过，其rnData/blockInfo不解释；仍校验外层object/discriminator和整体预算。只含未知子块/明确[]可产生零supported sections，但不是“百科为空/完整”。已知纯文本子块的明确空array保留空section；row缺内容则拒绝。

忽略 `id/songId/encodeId/jumpUrl/imgUrl/coverImage`、pattern/style/log、edit/upload/user contribution信息；不回传这些字段、不独立提升其身份含义。其他艺人/相似歌曲/版本/歌单/节目/首听及MV、评论、曲谱、娱乐、奖项、整块歌词均unsupported。不访问图/链接、不级联detail/page/audio、入队/播放/报告/写、cache/SQL/自动翻页/重试或定时任务。既有六轨歌词与唯一media owner不变。

## 4. 代码与合成验收

新增 `SongWikiText` 一个Eapi operation；107枚举/50注册，26人工family、39E/63W/5X，44FR不增；module/operation/entry/产品能力不是同一单位。全百科仍G；原 [catalog introduction](catalog-introduction-and-metadata-reads.md) 的旧summary路径/模型不变。

[合成fixture](../../tests/support/synthetic_song_wiki_text.h)没有服务文本/原ID/URL；[新测试](../../tests/unit/song_wiki_text_test.cpp)最终 **88 pass/0fail/0skip**（含init/cleanup）：51坏结构、8预算数据行、factory/空与未知/保序重复/宽ID/未验证身份、合同与10坏参数、实际native Eapi解密/PC host/path/extJson/e_r、5业务错误不重试、4取消send前后/epoch、read16容量、过期原来源零发送、Facade错kind/原query/描述值与close。恶意synthetic Set-Cookie后直接SessionApiClient账号读Cookie失败/未提交transport，证明不被Guest业务前置检查掩盖。仅一百科native请求，无自动下游。

[线程](../../tests/unit/backend_runtime_test.cpp)新songWikiTextThroughThread：Guest一次→合成登录→原新token一次→坏结构→hung close，7requests/4results，旧source token本地拒绝；caller线程/原id-token、identity/unsupported保真、创建销毁对称、队列/play/report/write/comment/Together零自动动作。

本轮23受影响Debug套件 **1278 pass**（含88），16选runtime函数17data+init-cleanup **19 pass**，0fail/skip；LC_ALL=C.UTF-8/QT_FATAL_WARNINGS=1。不是全50/Release/minQt/账号/媒体/SDK/keyring/服务或产品验收，旧188/1190/79/78等不累加。真实build exit0/warnings0；13 changed C++ format0、18 changed+邻接fresh all-server LSP0；14实际application对象含MOC，7004逐对象/602唯一VALID Ninja deps，Network/Sql/Gui/Multimedia/QML/Quick/JSON/crypto/keyring/storage/infrastructure禁入0；UI0。

保留首syntax AlbumId无operator==失败（改规范.value比较）、两轮78/1 fixture失败（Authenticating按原顺序Busy；空新Guest才测Stale，最初容量竞态推测撤回）、append initializer歧义build失败。使用真实std::pair/include、AUTOGEN/MOC和实际compile DB，不删Q_OBJECT/.moc/坏schema/fatal。普通strong enum初始化被误判bitfields，经GCC实际Qt-Core flags及可取成员地址static_assert证明为普通成员，标false-positive但不写suppression；header-change PCH诊断经主动fresh复查/真实build解决。#92既有cpp-check输出parse仍独立未闭。

## 5. 交接、隐私与未闭项

忽略目录 `build/workspace-checks/wiki-structure-*` 保留原生探针/编译及脱敏自测日志、`wiki-structure-observations.json`；仅有界技术结构/equality bool/synthetic skeleton。首自测fixture括号编译失败保留，修后0、脱敏自测0请求。没有保存服务全文/原文本-ID-URL/encodeId/Cookie/账号凭据/音频。内存buffer清理不是secure erasure。产品不依赖或执行这些开发工具。

本轮验证日志 `wiki-text-*`：unit/runtime/selected tests/build/实际deps/固定参考/Python/格式/文档路径与有限literal检查。后两类不是anchors或全面安全检查，最终见[HANDOFF](../HANDOFF.md)。固定v4.40.1/440/21 reference hash/Python15及MIT边界不变；MIT不授予服务/音乐/商标/Android/NIM/SDK权利。只本地提交，不push/publication/bundle。

剩余门禁：主资源SongId独立内容绑定、剩余块/节点的全部语义与真实边界、服务兼容/完整性/rights。纯文本子集不开放完整百科/UGC/MV/私密首听。账号等级与收藏计数仍只推荐审计候选，不获账号实网权限；持久队列/历史与启动恢复、下载/本地导入/Linux集成仍后续，UI最后，不自动恢复v2/cloud/spoken-next/fullTogether。
