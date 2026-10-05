# api-enhanced 来源与许可

Cirrus 的接口审计参考：

- 项目：NeteaseCloudMusicApiEnhanced/api-enhanced
- 仓库：https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced
- 固定 commit：`a8c781fd64faab17fedfd46e0615a2609307f163`
- 许可：MIT，原文见 [LICENSE](LICENSE)
- 原版权声明：Copyright (c) 2013-2022 Binaryify

当前使用范围：接口名称、协议和请求结构的静态分析，`docs/api/inventory.json` 中的源码表达式片段，以及 `src/infrastructure/netease/protocol/protocol_crypto.cpp` 中 weapi/eapi 协议常量与流程、`protocol/eapi_request_profile.cpp` 中请求 profile／UA／Cookie 结构、`api/api_operation.cpp` 和 `auth/qr_login_gateway.cpp` 中 QR／账号查询路径、参数与业务状态映射，以及 `api/session_api_client.cpp` 中身份查询的 weapi 请求 profile／浏览器 UA、匿名 MUSIC_A fallback，以及 `protocol/xeapi_crypto.cpp`／`protocol/xeapi_request_profile.cpp` 中 XEAPI 协议、匿名用户名生成与请求结构的 C++ 改写（路径均位于 `src/infrastructure/netease/` 下）；新增音乐查询还参考 `module/search.js`、`song_detail.js`、`lyric_new.js`的路径／参数，以及 `public/docs/home.md` 的YRC结构说明，原生实现位于 `api/api_operation.cpp` 和 `music/`；测试歌词使用合成内容，不包含曲库原文。官方音源还参考 `module/song_url_v1.js` 的XEAPI官方分支，改写位于 `api/api_operation.cpp` 和 `music/stream_resolver.cpp`；不包含其解锁分支或依赖。目录查询还参考 `module/album.js`、`artist_detail.js`、`artist_songs.js`、`artist_album.js`、`playlist_detail.js`、`playlist_track_all.js` 的路径、参数与ID切片补详情流程；原生改写位于 `api/api_operation.cpp`、`music/catalog_mapper.cpp`、`music/music_gateway.cpp`，响应测试仅使用合成数据。每日推荐／私人FM读取还参考 `module/recommend_songs.js`、`personal_fm.js` 的weapi路径及参数、`public/docs/home.md`的登录要求，原生改写位于 `api/api_operation.cpp` 和 `music/`；响应映射只有合成数据验证，未引入反馈或听歌上报。个人库读取还参考 `module/user_playlist.js`、`likelist.js`、`album_sublist.js`、`artist_sublist.js` 的路径、参数及文档；原生改写位于 `api/api_operation.cpp`、`music/library_mapper.cpp` 和现有查询链，按产品范围将includeVideo固定为false；无真实账号响应或库内容fixture。显式听歌上报还参考 `module/scrobble.js` 的startplay／play日志结构，以及 `util/request.js`、`util/config.json` 的固定clientlog目标与osx兼容profile；原生改写位于 `api/api_operation.cpp`、`protocol/official_route.cpp`、`protocol/eapi_request_profile.cpp`、`music/listening_report_gateway.cpp`。两个动作独立显式调用，不复制上游总成功包装，不自动触发／重试，不含真实上报fixture或scrobble_v1变体。云端最近听过／听歌排行还参考 `module/record_recent_song.js`、`user_record.js` 的weapi路径、limit／uid／type参数及 `public/docs/home.md` 的weekData／allData选择；原生改写位于 `api/api_operation.cpp`、`music/cloud_history_mapper.cpp` 和现有查询链。完整响应schema没有上游样本证据，仅合成数据验证；不含真实账号记录、实听时长推算或自动报告。榜单列表／内容摘要还参考 `module/toplist.js`、`toplist_detail.js` 的路径、空业务载荷及默认eapi／显式weapi选择，`public/docs/home.md` 的榜单歌单兼容说明；原生改写位于 `api/api_operation.cpp`、`music/chart_mapper.cpp` 及原查询链。响应只有合成fixture，摘要文本不生成SongId；歌曲分页／播放复用已有链，不含上游过时top_list或toplist_detail_v2分支、真实媒体或库内容。不包含上游服务或其 Node.js 运行依赖。未来原生实现若复制／改写上游受保护表达，须保留必要版权与许可通知，并在分发材料中包含此通知和 MIT 原文。

这里的 MIT 文件仅适用于该参考项目的相应材料，不是 Cirrus 自身许可证。它也不授予网易云商标、曲库、账号、接口服务或独立第三方 SDK 的使用权。

特别是上游 v2 token 模块运行时下载的 Watchman 脚本，不能据此认定同样受 MIT 许可授权。依赖、远程 SDK 和服务条款需分别审查；当前没有将其下载、打包或执行。
