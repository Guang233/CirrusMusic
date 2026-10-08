# 发现读取公开响应窗口与映射修正

## 1. 授权与请求边界

维护者授权自行测试公开发现／复杂百科metadata，ID可从已有搜索取得；不必限定curl。2026-10-08T06:16:20Z结束一次串行窗口，代码／mapper基线为本地331b4fb。预先上限15次，实际11次，间隔1s，单次15s超时、2MiB响应上限；首个非成功即停止，不重试或换协议／host。不是账号、权限、版权或长期兼容性验收。

ignored C++开发探针复用真实SessionApiClient／HttpTransport、既有Weapi/Eapi crypto、JSON decoder、当前music mapper和固定官方默认路由。2个已有typed搜索获取Song／Artist原ID；9个注册发现读取沿用typed request builder，复杂百科仅一个固定song_wiki_info request诊断，**没有注册操作／DTO／产品入口**。普通curl只作首页HEAD连通辅助，不传业务body。

未读取账号Cookie/token/keyring，不登录／注册、不访问音频／CDN／写／SDK／暂停功能／禁读APK；不采纳响应Cookie。Eapi沿用已有PC设备profile元数据，不是账号凭据或账号支持证明。ID只在内存用作原seed，未导出值。未修改DNS／IP／proxy／TLS验证／redirect策略；不自动加载图片、链接、更多页或相关资源。

此前Node lookup得到198.18.x.x后，曾**仅凭DNS结果**误判不可达，实际尚未发业务请求；该推论已撤回。普通curl默认HTTPS首页HEAD exit0／HTTP200／TLS验证0，证明当时可达而非API兼容。保留原ignored推论记录及superseding更正；没有绕过实际工具拒绝，也不确定Fake-IP的具体实现。

## 2. 原基线观察（不是修后实网验收）

每个已发送请求transport／JSON decode均无错误，HTTP均200。业务码与mapper结果独立记录：

| 顺序 | 操作 | code／结构摘要 | 331b4fb mapper |
| --- | --- | --- | --- |
| 1 | SearchSongs，稻香／limit2／offset0 | 200；root.result，取得原SongId | 接受 |
| 2 | SearchArtists，周杰伦／limit2／offset0 | 200；root.result，取得原ArtistId | 接受 |
| 3 | SongWikiInfoCandidate，原搜索SongId | 200；root.data.blocks6 | 未注册，不运行产品mapper |
| 4 | LatestAlbums | 200；root.albums12 | 接受 |
| 5 | NewAlbums，ALL／limit2／offset0 | 200；root.albums2、total | 接受 |
| 6 | AlbumCharts，显式2025／12／ALL／hot／limit2／offset0 | 200；monthData[]、hasMore bool，缺weekData | InvalidResponse |
| 7 | ArtistBrowse，type/area/initial均-1／limit2／offset0 | 200；artists2、more bool | 接受 |
| 8 | PopularArtists，limit2／offset0 | 200；artists2、more bool | 接受 |
| 9 | ArtistTopSongs，原搜索ArtistId | 200；songs50、more bool | 接受；非全部艺人歌曲 |
| 10 | SimilarSongs，原搜索SongId／limit2／offset0 | 200；songs5，legacy artists/album/duration | InvalidResponse，先被请求limit上限拒绝 |
| 11 | SimilarArtists，原搜索ArtistId | **301**；code/message | 拒绝并立即停止窗口 |

SimilarPlaylists没有发送，仍无本窗口L；相似艺人301不能冒充免登录成功或证明登录后行为。探针exit1为预设非成功停止，不是Qt测试结果。没有继续测试、登录、请求匿名发行器或协议／host fallback。

complex wiki的`data`包含blocks、blockCodeOrderList、callbackParameters、cursor、demote、hasDoubleFlow、hasMore；前2块采样为bizCode/channelCode/positionCode/rnData等key/type。第一个rnData见songName/albumId/albumName/artistName/encodeId/coverImage/blocks等，第二个见artists/totalArtistInfo。**只保类型／长度／数量，未保块类值、文本／链接／encodeId／callbackParameters值；未核对Song资源绑定或其余4块的语义**。6个块不是6个已支持章节，不套用旧SongWikiSummary或UGC schema；复杂百科门禁仍G。

## 3. 两项生产修正

- AlbumCharts：`DiscoveryAlbumCharts.week/month`改为optional QList，至少一组必须存在。缺失是未知，[]是明确空；present null/object／坏元素／超500仍失败。两组分别保序，不互相补空、不合并假榜；optional hasMore保留原bool，不推导下一页。
- SimilarSongs：单批本地最大100，不再假定响应不超过请求limit；完整保序／重复、不截断成假页。`DiscoverySongs.requestedPaginationUnverified=true`仅标记该操作caller limit/offset未获服务分页证明，不造total/hasMore/next。既有共享songFrom已支持artists/album/duration，**本次未修改共享解析器或增加别名**；mp3Url、动作／报告／权益等未采用。

路径、payload、106操作／49注册、单一Facade入口、read16、原query/content-kind/token/generation/cancel-close、Cookie不采纳及Qt Core-only边界不变。未知／不合法数组仍拒绝；不为complex wiki加假registration。

## 4. 离线回归与当前证据

新增fixture只重建结构差异，所有ID／文本人工合成，不保存服务全文或URL。保留原90坏schema、36取消／epoch等测试；缺必需chart组的负例明确改为“两组都缺”，增加单组缺失、[]与null区别、500/501、bool错型。相似批新增5/100/101、legacy credit／duration错型、missing array、错误seed、重复宽ID、未知分页；三请求native synthetic链含301且不重试／自动登录。

- 新discovery_browse最终 **188 pass／0fail／0skip**；本轮23新增用例不与旧165重复累计。
- 重编译后22个affected Debug套件 **1190 pass／0fail／0skip**（包含188）；不是完整49／Release／minQt矩阵。
- 15个选定runtime函数：16数据行＋init/cleanup **18 pass／0fail／0skip**。discoveryBrowseThroughThread保留15requests／12results，增加month-only未知week与5歌超请求limit交付断言，原token／原query／线程／资源销毁／无下游动作仍检查。
- 首次runtime选择误写musicThroughThread导致exit1／Unknown test function，未跑测试、未计证据；依据真实可执行文件-functions改为musicQueriesThroughThread后重跑通过。失败日志保留，未改／删测试或抑制diagnostic。
- 真build0／warnings0；5个changed C++ fresh all-server LSP0／format0。cirrus_application全部14实际对象含MOC：6998逐对象依赖项／601唯一依赖文件，禁入Qt Network/Sql/Gui/Multimedia、JSON、crypto、keyring、storage、infrastructure细节0；main.cpp/Main.qml不变。
- fixed440／Python15、参考hash、文档links／有限Cookie-signed-query literal扫描与worktree/staged diff分别核对；最终交付见[HANDOFF](../HANDOFF.md)。#92旧cpp-check输出解析问题仍独立，不冒充修复。

响应仅在内存解析，ignored证据只存bounded key/type/count/string-length/integral及必要状态，无原值／账号凭据／Cookie／签名URL／服务全文／音频。单样本不能建立完整schema、分页、空值全景、免登录保证、PC profile长期兼容或音乐权益。修后只做合成回归，**没有重发实网**。106 operations／164 adopt模块／49注册／44 FR不同计量，不能据此宣布完整产品或FR完成；本地交付，不push。

相关：[发现合同](discovery-browse-reads.md)、[覆盖台账](backend-coverage-audit.md)、[风险与问题](../15-risks-and-open-questions.md)、[测试](../12-quality-and-testing.md)。
