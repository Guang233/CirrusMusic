# 常规发现读取与复杂百科门禁

## 1. 本轮选择与范围

维护者明确先补发现类，并选择“新碟、艺人、相似推荐＋复杂百科；只接证据充分的消费端内容，缺结构明确待补证”。代码起点本地master53f8f3e（97operations／48注册）。不恢复whole-playlist-v2/cloud/spoken/fullTogether暂停，不读已撤权APK、不动固定stash，初始合成包不执行实网/SDK/新运行依赖，不接模板UI。后续维护者仅授权公开搜索／发现／复杂百科metadata的有界响应窗口，见§5；未扩张其他范围。

9个常规读取均固定参考v4.40.1/a8c781fd64faab17fedfd46e0615a2609307f163的Weapi/JSON/default destination：

| native operation | module／logical path | 业务payload | strict response候选 |
| --- | --- | --- | --- |
| LatestAlbums | album_newest `/api/discovery/newAlbum` | 空 | root.albums |
| NewAlbums | album_new `/api/album/new` | area/limit/offset/total:true | root.albums，optional total/hasMore |
| AlbumCharts | top_album `/api/discovery/new/albums/area` | area/limit/offset/type/year/month/total:false/rcmd:true | root.weekData／root.monthData至少一组存在；各组optional、有别于[]，分别保序；optional hasMore，不合并／假排序 |
| ArtistBrowse | artist_list `/api/v1/artist/list` | type/area/initial/limit/offset/total:true | root.artists，optional more |
| PopularArtists | top_artists `/api/artist/top` | limit/offset/total:true | root.artists，optional more |
| ArtistTopSongs | artist_top_song `/api/artist/top/song` | id | root.songs，候选本地最大50，不视为全部艺人歌曲 |
| SimilarSongs | simi_song `/api/v1/discovery/simiSong` | songid/limit/offset | root.songs |
| SimilarArtists | simi_artist `/api/discovery/simiArtist` | artistid | root.artists |
| SimilarPlaylists | simi_playlist `/api/discovery/simiPlaylist` | songid/limit/offset | root.playlists；**seed是SongId，不是PlaylistId** |

这些字段/预算是严格本地候选，wrapper/home/通用TS Response不是官方完整schema或实网证明。不试root/data/大小写alias、不换host/协议；未知metadata不造完整性或权益。ArtistTopSongs独立固定50候选，不把既有ArtistSongs Hot有界排序伪作同接口。

## 2. typed输入与结果

闭合9类variant query，不暴露任意JSON/path/host；原MusicQuery/Service/Facade/Gateway/worker/read16/token-generation/cancel-close路线，新增单一typed discover入口，不新增Manager/worker/owner。Guest可提交仅为本地策略，不是服务免登录证明；有认证时沿用现有会话请求快照，但读取不采纳response Cookie。

CatalogPage limit1..100/offset0..100000为本地预算。区域ALL/ZH/EA/KR/JP闭合；AlbumCharts year1900..9999、month1..12必须caller明确给出，不读系统当前年月。type new/hot闭合。Artist type -1/1/2/3、area -1/7/96/8/16/0按文档闭合；initial只-1/0或A..Z的ASCII65..90，不任意Unicode/空值/大小写正规化。type/area输出规范数字字符串（TS类型），initial输出精确数值。

每响应2MiB；latest/similarArtists最大100，普通paged列表不超过请求limit，ArtistTopSongs50。SimilarSongs单批本地最大100（非请求limit），保完整返回顺序与重复、不截成假页；DiscoverySongs.requestedPaginationUnverified=true表示caller limit/offset未证明服务分页，不能自动翻页或推导next/total。既有songFrom已经支持legacy artists/album/duration，本次不添加字段别名或改共享解析器。AlbumCharts每个存在的分组最大500／合计1000；week/month为optional QList，missing未知、[]明确空、present null/object错误，至少一组必须存在，不把组当同页/完整榜。其他根数组仍必需且必须array，明确[]有效；missing/null/object/超限/元素坏类型失败，不空成功。保重复/顺序/宽规范ID，name必需且bounded plain text；复用Album/SearchArtist/Song/DiscoveryPlaylist摘要，拒绝未知alias产生假文本。图像只元数据，不自动加载；不取歌曲详情/音源/播放/报告/写/cache/更多页。optional total/hasMore/more有错类型则拒绝，缺失保未知；total非完整库/CAS或权限。

## 3. 复杂百科本轮审计结果：未注册入口

固定song_wiki_info.js明确Eapi `/api/link/page/parent/relation/construct/info`，positionCode固定songWikiMainPosition，extJson为JSON字符串`{"states":{"playingResource":{"current":原SongId,"scene":"songWiki"}}}`。home音乐百科说明只给SongId与“响应复杂按需提取”；没有完整响应schema/有界块类型/链接节点/内容资源绑定/空值与未知块实例；TS无专用类型。**请求已知不等于能把返回当可用复杂百科**。

既有SongWikiSummary只支持已闭合的基础纯文本block/songTag/songBizTag/language/bpm，unsupportedContent继续诚实报告，不借它的data.blocks结构套用另一个path。UGC creator get源不用于消费百科，原排除保留。不解包raw JSON/HTML/动作、自动访问link/page/url/下载谱/取相关资源或编造署名；不加假的unavailable或纯HTTP200“完整百科”registration。

门禁：需要有权分享的source片段/合成响应，明确root与SongId对应、blocks/sections/资源类型与顺序、optional/unknown/空/坏节点、纯文本与link（显示非动作）区分及可执行预算。无需真实账号/token/Cookie/版权音频。§5已取得一次root.data.blocks/rnData结构观察，但只采样前2块的key/type，未核对Song绑定、块类值及其余块语义；不能闭合门禁。完整消费百科仍G，非已完成FR008/009。

## 4. 初始合成验证与交付（331b4fb历史）

TC-DISCOVERY-BROWSE：新[fixture](../../tests/support/synthetic_discovery_browse.h)及[unit](../../tests/unit/discovery_browse_test.cpp)最终165 pass／0fail／0skip；9合同及精确pre-crypto业务payload、越界／非整数／extra／缺参数，90坏schema、预算／未知分页／宽ID与重复顺序／原text，36取消及epoch前后send、provider5／Cookie不采纳／read16／Facade原query及content-kind／close。9个实际SessionApiClient HTTP请求检查原Weapi path、host与加密form（不是逐个解密实际Weapi请求或实网证明），且不触发下游。

[原线程runtime](../../tests/unit/backend_runtime_test.cpp)新discoveryBrowseThroughThread：Guest一次＋账号9次、别名假空拒绝、hung read关闭，15requests／12results／原token、线程与创建销毁对称；队列／play／report／write／Together无自动动作。最终选15runtime函数，16数据行＋init/cleanup共18 pass，0fail/skip。

最终22个affected Debug套件合计1167 pass（含新165），0fail/skip；不累加first123／新线程first3或历史其他包。真实build0／warnings0，15changed C++ fresh all-server LSP0、format0；cirrus_application全部14实际对象（含MOC）6998 Ninja依赖／Qt Network-Sql-Multimedia-Gui-QML-Quick、crypto／JSON细节等禁入0；entry／QML不变。106唯一operations／49show-only注册，其中discovery_browse1；登记不是全套执行。

首轮测试build因fixture漏QJsonObject直接include与headers.append重载歧义失败；仅补真实include与std::pair后重新build成功。新测试.moc及改测试名后的stale MOC均用实际AUTOGEN生成和编译数据库刷新解决，保Q_OBJECT/.moc，不压diagnostic。domain规则将普通enum赋值误判bitfields，经GCC独立syntax／真实compiler／LSP核对后逐项false-positive，不写抑制。坏schema与fatal warnings保留；#92旧cpp-check output parsing仍独立未闭。

fixed440／21公共参考hash／Python15、format/worktree及staged diff、local references与有限literal-query扫描分开核对，最终计数见[HANDOFF](../HANDOFF.md)；ignored日志discovery-browse-*，不是anchors或全面安全审计。本地提交、不push。没有完整49／Release／minQt／真实账号／服务响应／权益／SDK／keyring／音频／UI验收；复杂百科保持G且不注册，原暂停与APK禁读不变。

## 5. 后续公开响应窗口与修正

[2026-10-08公开窗口](discovery-public-response-verification.md)原native链11次metadata读，SimilarArtists业务301后停止，SimilarPlaylists未发；其余10次HTTP/code200独立于mapper接受。其中月榜monthData[]但weekData缺失、相似歌曲5条超过请求2导致原映射拒绝，已用optional分组／100有界未知分页批修正，不添加共享songFrom别名。复杂百科取得data.blocks6/rnData部分结构，绑定／块类值及余块仍未补证，不注册入口。

修后离线188／22affected1190／选runtime18，0fail/skip；5fresh all-server LSP0，14实际application对象6998逐对象／601唯一依赖禁入0，UI不变。首次runtime选择错函数名exit1保留后按可执行文件修正重跑；未重发实网，旧§4证据保留为历史、不累加。只有这个公开metadata窗口取代旧“不执行实网”，其他权限、paused、APK和固定stash约束全部不变。
