# 可采用接口清单（静态审计）

> 本表由 `inventory.json` 汇总。这里只表示移植范围，不表示已实现、已登录测试或具备稳定官方契约。

固定上游：[`a8c781fd64fa`](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/tree/a8c781fd64faab17fedfd46e0615a2609307f163)，包版本 `4.40.1`。

表中的路径是传给公共 request 的**逻辑路径**；真实 URL 还经过 weapi/eapi/xeapi 改写，详见[协议迁移](protocol-migration.md)。`default-eapi` 来自固定版本 `APP_CONF.encrypt=true`，不是永久默认规则。

参数是源码中观察到的 query 字段（包含直接访问和对象解构），不是完整的必填／类型／权限声明。继承的 Cookie 等共用参数不在每一行重复；Cirrus 不暴露 crypto/domain/headers 等任意透传参数。

需要人审的边界／缺陷以 AUD 编号引用[审计发现](gaps-and-hazards.md)。没有 request 调用不表示没有网络：可能是本地辅助或经插件／axios 执行，见协议文档。

## 协议基础（4 项，M1）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [batch](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/batch.js) | `/api/batch` | default-eapi | 无显式业务参数 | AUD-15 |
| [register_anonimous](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/register_anonimous.js) | `/api/register/anonimous` | xeapi | 无显式业务参数 | AUD-02 |
| [register_checktoken_v3](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/register_checktoken_v3.js) | 直接 HTTP／协议依赖，见源码和协议文档 | 独立链路 | 无显式业务参数 | AUD-03 |
| [register_xeapikey](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/register_xeapikey.js) | 直接 HTTP／协议依赖，见源码和协议文档 | 独立链路 | currentKeyVersion, deviceId | AUD-02 |

## 登录与会话（10 项，M1）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [captcha_sent](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/captcha_sent.js) | `/api/sms/captcha/sent` | weapi | ctcode, phone | — |
| [captcha_verify](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/captcha_verify.js) | `/api/sms/captcha/verify` | weapi | captcha, ctcode, phone | — |
| [countries_code_list](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/countries_code_list.js) | `/api/lbs/countries/v1` | default-eapi | 无显式业务参数 | — |
| [login_cellphone](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/login_cellphone.js) | `/api/w/login/cellphone` | weapi | captcha, countrycode, md5_password, password, phone, sca | — |
| [login_qr_check](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/login_qr_check.js) | `/api/login/qrcode/client/login` | default-eapi | key | AUD-04 |
| [login_qr_create](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/login_qr_create.js) | 本地二维码 URL／图像辅助 | 独立链路 | cookie, key, platform, qrimg | AUD-05 |
| [login_qr_key](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/login_qr_key.js) | `/api/login/qrcode/unikey` | default-eapi | 无显式业务参数 | — |
| [login_refresh](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/login_refresh.js) | `/api/login/token/refresh` | default-eapi | 无显式业务参数 | — |
| [login_status](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/login_status.js) | `/api/w/nuser/account/get` | weapi | 无显式业务参数 | — |
| [logout](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/logout.js) | `/api/logout` | default-eapi | 无显式业务参数 | — |

## 基础用户信息与权益（5 项，M2）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [user_account](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/user_account.js) | `/api/nuser/account/get` | weapi | 无显式业务参数 | — |
| [user_detail](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/user_detail.js) | `/api/v1/user/detail/${query.uid}` | weapi | uid | — |
| [user_level](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/user_level.js) | `/api/user/level` | weapi | 无显式业务参数 | — |
| [user_subcount](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/user_subcount.js) | `/api/subcount` | weapi | 无显式业务参数 | — |
| [vip_info_v2](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/vip_info_v2.js) | `/api/music-vip-membership/client/vip/info` | weapi | uid | — |

## 首页推荐与榜单（19 项，M2）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [chart_detail](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/chart_detail.js) | `/api/chart/detail` | default-eapi | chartCode, targetId, targetType | — |
| [chart_song_detail](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/chart_song_detail.js) | `/api/chart/song/detail` | default-eapi | chartCode, targetId, targetType | — |
| [homepage_block_page](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/homepage_block_page.js) | `/api/homepage/block/page` | weapi | cursor, refresh | AUD-11 |
| [personalized](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/personalized.js) | `/api/personalized/playlist` | weapi | limit | — |
| [personalized_newsong](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/personalized_newsong.js) | `/api/personalized/newsong` | weapi | areaId, limit | — |
| [recommend_resource](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/recommend_resource.js) | `/api/v1/discovery/recommend/resource` | weapi | 无显式业务参数 | AUD-12 |
| [style_album](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/style_album.js) | `/api/style-tag/home/album` | weapi | cursor, size, sort, tagId | — |
| [style_artist](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/style_artist.js) | `/api/style-tag/home/artist` | weapi | cursor, size, tagId | — |
| [style_detail](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/style_detail.js) | `/api/style-tag/home/head` | weapi | tagId | — |
| [style_list](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/style_list.js) | `/api/tag/list/get` | weapi | 无显式业务参数 | — |
| [style_playlist](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/style_playlist.js) | `/api/style-tag/home/playlist` | weapi | cursor, size, tagId | — |
| [style_preference](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/style_preference.js) | `/api/tag/my/preference/get` | weapi | 无显式业务参数 | — |
| [style_song](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/style_song.js) | `/api/style-tag/home/song` | weapi | cursor, size, sort, tagId | — |
| [top_list](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/top_list.js) | `/api/playlist/v4/detail` | default-eapi | id, idx | — |
| [top_playlist](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/top_playlist.js) | `/api/playlist/list` | weapi | cat, limit, offset, order | — |
| [top_playlist_highquality](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/top_playlist_highquality.js) | `/api/playlist/highquality/list` | weapi | before, cat, limit | — |
| [top_song](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/top_song.js) | `/api/v1/discovery/new/songs` | weapi | type | — |
| [toplist](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/toplist.js) | `/api/toplist` | default-eapi | 无显式业务参数 | — |
| [toplist_detail](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/toplist_detail.js) | `/api/toplist/detail` | weapi | 无显式业务参数 | — |

## 每日推荐歌曲（独立来源）（4 项，M2）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [history_recommend_songs](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/history_recommend_songs.js) | `/api/discovery/recommend/songs/history/recent` | weapi | 无显式业务参数 | — |
| [history_recommend_songs_detail](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/history_recommend_songs_detail.js) | `/api/discovery/recommend/songs/history/detail` | weapi | date | — |
| [recommend_songs](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/recommend_songs.js) | `/api/v3/discovery/recommend/songs` | weapi | afresh | AUD-12 |
| [recommend_songs_dislike](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/recommend_songs_dislike.js) | `/api/v2/discovery/recommend/dislike` | weapi | id | — |

## 私人 FM（明确保留）（3 项，M2）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [fm_trash](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/fm_trash.js) | `/api/radio/trash/add` | weapi | id, time | AUD-13 |
| [personal_fm](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/personal_fm.js) | `/api/v1/radio/get` | weapi | 无显式业务参数 | AUD-13 |
| [personal_fm_mode](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/personal_fm_mode.js) | `/api/v1/radio/get` | default-eapi | limit, mode, submode | AUD-13 |

## 搜索与匹配（8 项，M2）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [cloudsearch](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/cloudsearch.js) | `/api/cloudsearch/pc` | default-eapi | keywords, limit, offset, type | AUD-11 |
| [search](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/search.js) | `/api/search/voice/get`；`/api/search/get` | default-eapi | keywords, limit, offset, type | AUD-11 |
| [search_default](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/search_default.js) | `/api/search/defaultkeyword/get` | default-eapi | 无显式业务参数 | — |
| [search_hot](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/search_hot.js) | `/api/search/hot` | default-eapi | 无显式业务参数 | — |
| [search_hot_detail](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/search_hot_detail.js) | `/api/hotsearchlist/get` | weapi | 无显式业务参数 | — |
| [search_match](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/search_match.js) | `/api/search/match/new` | default-eapi | album, artist, duration, md5, title | — |
| [search_suggest](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/search_suggest.js) | `/api/search/suggest/ + type` | weapi | keywords, type | — |
| [search_suggest_pc](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/search_suggest_pc.js) | `/api/search/pc/suggest/keyword/get` | default-eapi | keyword | — |

## 歌曲与播放来源（12 项，M2）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [check_music](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/check_music.js) | `/api/song/enhance/player/url` | weapi | br, id | AUD-10 |
| [simi_song](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/simi_song.js) | `/api/v1/discovery/simiSong` | weapi | id, limit, offset | — |
| [song_chorus](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_chorus.js) | `/api/song/chorus` | default-eapi | id | — |
| [song_copyright_rcmd](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_copyright_rcmd.js) | `/api/song/copyright/rcmd` | eapi | id, songid | — |
| [song_creators](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_creators.js) | `/api/song/creators` | default-eapi | id | — |
| [song_detail](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_detail.js) | `/api/v3/song/detail` | weapi | ids | — |
| [song_music_detail](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_music_detail.js) | `/api/song/music/detail/get` | default-eapi | id | — |
| [song_simi_get](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_simi_get.js) | `/api/link/position/show/resource` | eapi | id | — |
| [song_url](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_url.js) | `/api/song/enhance/player/url` | default-eapi | br, id | — |
| [song_url_v1](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_url_v1.js) | `/api/song/enhance/player/url/v1` | xeapi | id, immerseType, level, source, unblock | AUD-01 |
| [song_wiki_info](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_wiki_info.js) | `/api/link/page/parent/relation/construct/info` | eapi | id | — |
| [song_wiki_summary](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_wiki_summary.js) | `/api/song/play/about/block/page` | default-eapi | id | — |

## 歌词（2 项，M2）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [lyric](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/lyric.js) | `/api/song/lyric` | default-eapi | id | — |
| [lyric_new](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/lyric_new.js) | `/api/song/lyric/v1` | default-eapi | id | — |

## 歌单读取（9 项，M2）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [playlist_category_list](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_category_list.js) | `/api/playlist/category/list` | default-eapi | cat, limit | — |
| [playlist_catlist](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_catlist.js) | `/api/playlist/catalogue` | eapi | 无显式业务参数 | — |
| [playlist_detail](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_detail.js) | `/api/v6/playlist/detail` | default-eapi | id, s | — |
| [playlist_detail_dynamic](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_detail_dynamic.js) | `/api/playlist/detail/dynamic` | default-eapi | id, s | — |
| [playlist_detail_rcmd_get](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_detail_rcmd_get.js) | `/api/playlist/detail/rcmd/get` | default-eapi | id | — |
| [playlist_highquality_tags](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_highquality_tags.js) | `/api/playlist/highquality/tags` | weapi | 无显式业务参数 | — |
| [playlist_hot](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_hot.js) | `/api/playlist/hottags` | weapi | 无显式业务参数 | — |
| [playlist_track_all](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_track_all.js) | `/api/v6/playlist/detail`；`/api/v3/song/detail` | default-eapi | id, limit, offset, s | AUD-08 |
| [simi_playlist](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/simi_playlist.js) | `/api/discovery/simiPlaylist` | weapi | id, limit, offset | — |

## 歌单写操作（15 项，M3）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [playlist_cover_update](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_cover_update.js) | `/api/playlist/cover/update` | weapi | id, imgFile | AUD-14 |
| [playlist_create](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_create.js) | `/api/playlist/create` | weapi | name, privacy, type | — |
| [playlist_delete](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_delete.js) | `/api/playlist/remove` | weapi | id | — |
| [playlist_desc_update](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_desc_update.js) | `/api/playlist/desc/update` | default-eapi | desc, id | — |
| [playlist_name_update](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_name_update.js) | `/api/playlist/update/name` | default-eapi | id, name | — |
| [playlist_order_update](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_order_update.js) | `/api/playlist/order/update` | weapi | ids | AUD-09 |
| [playlist_privacy](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_privacy.js) | `/api/playlist/update/privacy` | default-eapi | id | AUD-09 |
| [playlist_subscribe](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_subscribe.js) | `/api/playlist/${path}` | eapi | checkToken, id, t | AUD-03 |
| [playlist_tags_update](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_tags_update.js) | `/api/playlist/tags/update` | default-eapi | id, tags | — |
| [playlist_track_add](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_track_add.js) | `/api/playlist/track/add` | weapi | ids, pid | — |
| [playlist_track_delete](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_track_delete.js) | `/api/playlist/track/delete` | weapi | id, ids | — |
| [playlist_tracks](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_tracks.js) | `/api/playlist/manipulate/tracks` | default-eapi | op, pid, tracks | AUD-07 |
| [playlist_update](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_update.js) | `/api/batch` | default-eapi | desc, id, name, tags | AUD-09 |
| [playlist_update_playcount](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_update_playcount.js) | `/api/playlist/update/playcount` | default-eapi | id | — |
| [song_order_update](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_order_update.js) | `/api/playlist/manipulate/tracks` | default-eapi | ids, pid | AUD-09 |

## 艺人（13 项，M2）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [artist_album](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/artist_album.js) | `/api/artist/albums/${query.id}` | weapi | id, limit, offset | — |
| [artist_desc](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/artist_desc.js) | `/api/artist/introduction` | weapi | id | — |
| [artist_detail](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/artist_detail.js) | `/api/artist/head/info/get` | default-eapi | id | — |
| [artist_detail_dynamic](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/artist_detail_dynamic.js) | `/api/artist/detail/dynamic` | default-eapi | id | — |
| [artist_list](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/artist_list.js) | `/api/v1/artist/list` | weapi | area, initial, limit, offset, type | — |
| [artist_new_song](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/artist_new_song.js) | `/api/sub/artist/new/works/song/list` | weapi | before, limit | — |
| [artist_new_song_playall](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/artist_new_song_playall.js) | `/api/sub/artist/new/works/song/playall` | eapi | 无显式业务参数 | — |
| [artist_songs](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/artist_songs.js) | `/api/v1/artist/songs` | default-eapi | id, limit, offset, order | — |
| [artist_top_song](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/artist_top_song.js) | `/api/artist/top/song` | weapi | id | — |
| [artists](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/artists.js) | `/api/v1/artist/${query.id}` | weapi | id | — |
| [simi_artist](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/simi_artist.js) | `/api/discovery/simiArtist` | weapi | id | — |
| [top_artists](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/top_artists.js) | `/api/artist/top` | weapi | limit, offset | — |
| [toplist_artist](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/toplist_artist.js) | `/api/toplist/artist` | weapi | type | — |

## 专辑（6 项，M2）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [album](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/album.js) | `/api/v1/album/${query.id}` | weapi | id | — |
| [album_detail_dynamic](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/album_detail_dynamic.js) | `/api/album/detail/dynamic` | weapi | id | — |
| [album_new](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/album_new.js) | `/api/album/new` | weapi | area, limit, offset | — |
| [album_newest](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/album_newest.js) | `/api/discovery/newAlbum` | weapi | 无显式业务参数 | — |
| [album_privilege](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/album_privilege.js) | `/api/album/privilege` | default-eapi | id | — |
| [top_album](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/top_album.js) | `/api/discovery/new/albums/area` | weapi | area, limit, month, offset, type, year | — |

## 个人音乐库与播放记录（14 项，M2）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [album_sublist](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/album_sublist.js) | `/api/album/sublist` | weapi | limit, offset | — |
| [artist_sublist](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/artist_sublist.js) | `/api/artist/sublist` | weapi | limit, offset | — |
| [digitalAlbum_purchased](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/digitalAlbum_purchased.js) | `/api/digitalAlbum/purchased` | weapi | limit, offset | — |
| [likelist](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/likelist.js) | `/api/song/like/get` | default-eapi | uid | — |
| [recent_listen_list](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/recent_listen_list.js) | `/api/pc/recent/listen/list` | default-eapi | 无显式业务参数 | — |
| [record_recent_album](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/record_recent_album.js) | `/api/play-record/album/list` | weapi | limit | — |
| [record_recent_playlist](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/record_recent_playlist.js) | `/api/play-record/playlist/list` | weapi | limit | — |
| [record_recent_song](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/record_recent_song.js) | `/api/play-record/song/list` | weapi | limit | — |
| [song_like_check](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_like_check.js) | `/api/song/like/check` | default-eapi | ids | — |
| [song_purchased](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_purchased.js) | `/api/single/mybought/song/list` | weapi | limit, offset | — |
| [user_playlist](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/user_playlist.js) | `/api/user/playlist` | weapi | limit, offset, uid | — |
| [user_playlist_collect](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/user_playlist_collect.js) | `/api/user/playlist/collect` | default-eapi | limit, offset, uid | — |
| [user_playlist_create](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/user_playlist_create.js) | `/api/user/playlist/create` | default-eapi | limit, offset, uid | — |
| [user_record](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/user_record.js) | `/api/v1/play/record` | weapi | type, uid | — |

## 喜欢、收藏与真实播放上报（4 项，M3）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [album_sub](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/album_sub.js) | `/api/album/${query.t}` | weapi | id, t | — |
| [artist_sub](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/artist_sub.js) | `/api/artist/${query.t}` | weapi | id, t | — |
| [like](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/like.js) | `/api/radio/like` | weapi | id, like | — |
| [scrobble](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/scrobble.js) | `/api/feedback/weblog` | eapi | cookie, id, sourceid, time | AUD-16 |

## 下载与下载记录（6 项，M4）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [song_cloud_download](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_cloud_download.js) | `/api/cloud/dowonload` | eapi | id | AUD-06 |
| [song_downlist](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_downlist.js) | `/api/member/song/downlist` | default-eapi | limit, offset | — |
| [song_download_url](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_download_url.js) | `/api/song/enhance/download/url` | default-eapi | br, id | — |
| [song_download_url_v1](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_download_url_v1.js) | `/api/song/enhance/download/url/v1` | default-eapi | id, level | AUD-06 |
| [song_monthdownlist](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_monthdownlist.js) | `/api/member/song/monthdownlist` | default-eapi | limit, offset | — |
| [song_singledownlist](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_singledownlist.js) | `/api/member/song/singledownlist` | default-eapi | limit, offset | — |

## 云盘读取与歌词（3 项，M4）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [cloud_lyric_get](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/cloud_lyric_get.js) | `/api/cloud/lyric/get` | eapi | sid, uid | — |
| [user_cloud](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/user_cloud.js) | `/api/v1/cloud/get` | weapi | limit, offset | — |
| [user_cloud_detail](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/user_cloud_detail.js) | `/api/v1/cloud/get/byids` | weapi | id | — |

## 云盘上传与管理（5 项，M4）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [cloud](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/cloud.js) | `/api/cloud/upload/check`；`/api/nos/token/alloc`；`/api/upload/cloud/info/v2`；`/api/cloud/pub/v2` | default-eapi | songFile | AUD-14 |
| [cloud_match](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/cloud_match.js) | `/api/cloud/user/song/match` | weapi | asid, sid, uid | — |
| [cloud_upload_complete](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/cloud_upload_complete.js) | `/api/upload/cloud/info/v2`；`/api/cloud/pub/v2` | default-eapi | album, artist, bitrate, filename, md5, resourceId, song, songId | AUD-14 |
| [cloud_upload_token](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/cloud_upload_token.js) | `/api/cloud/upload/check`；`/api/nos/token/alloc` | default-eapi, weapi | bitrate, fileSize, filename, md5 | AUD-14 |
| [user_cloud_del](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/user_cloud_del.js) | `/api/cloud/del` | weapi | id | — |

## 歌曲／歌单／专辑评论读取（8 项，M2）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [comment_album](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/comment_album.js) | `/api/v1/resource/comments/R_AL_3_${query.id}` | weapi | before, id, limit, offset | AUD-11 |
| [comment_floor](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/comment_floor.js) | `/api/resource/comment/floor/get` | weapi | id, limit, parentCommentId, time, type | AUD-11 |
| [comment_hot](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/comment_hot.js) | `/api/v1/resource/hotcomments/${query.type}${query.id}` | weapi | before, id, limit, offset, type | AUD-11 |
| [comment_info_list](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/comment_info_list.js) | `/api/resource/commentInfo/list` | weapi | id, ids, type | AUD-11 |
| [comment_music](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/comment_music.js) | `/api/v1/resource/comments/R_SO_4_${query.id}` | weapi | before, id, limit, offset | AUD-11 |
| [comment_new](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/comment_new.js) | `/api/v2/resource/comments` | default-eapi | cursor, id, pageNo, pageSize, showInner, sortType, type | AUD-11 |
| [comment_playlist](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/comment_playlist.js) | `/api/v1/resource/comments/A_PL_0_${query.id}` | weapi | before, id, limit, offset | AUD-11 |
| [user_comment_history](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/user_comment_history.js) | `/api/comment/user/comment/history` | weapi | limit, time, uid | AUD-11 |

## 评论写操作（5 项，M3）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [comment_add](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/comment_add.js) | `/api/resource/comments/add` | xeapi | content, id, type | AUD-03 |
| [comment_delete](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/comment_delete.js) | `/api/resource/comments/delete` | xeapi | cid, id, type | AUD-11 |
| [comment_like](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/comment_like.js) | `/api/v1/comment/${query.t}` | weapi | cid, id, t, threadId, type | AUD-11 |
| [comment_reply](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/comment_reply.js) | `/api/v1/resource/comments/reply` | xeapi | cid, content, id, type | AUD-03 |
| [comment_report](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/comment_report.js) | `/api/report/reportcomment` | default-eapi | cid, id, reason | AUD-11 |

## 一起听（9 项，M3）

| 上游模块 | 官方逻辑路径／性质 | 协议提示 | 观察到的输入 | 审计项 |
| --- | --- | --- | --- | --- |
| [listentogether_accept](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/listentogether_accept.js) | `/api/listen/together/play/invitation/accept` | default-eapi | inviterId, roomId | AUD-17 |
| [listentogether_end](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/listentogether_end.js) | `/api/listen/together/end/v2` | default-eapi | roomId | AUD-17 |
| [listentogether_heatbeat](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/listentogether_heatbeat.js) | `/api/listen/together/heartbeat` | default-eapi | playStatus, progress, roomId, songId | AUD-17 |
| [listentogether_play_command](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/listentogether_play_command.js) | `/api/listen/together/play/command/report` | default-eapi | clientSeq, commandType, formerSongId, playStatus, progress, roomId, targetSongId | AUD-17 |
| [listentogether_room_check](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/listentogether_room_check.js) | `/api/listen/together/room/check` | default-eapi | roomId | AUD-17 |
| [listentogether_room_create](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/listentogether_room_create.js) | `/api/listen/together/room/create` | default-eapi | 无显式业务参数 | AUD-17 |
| [listentogether_status](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/listentogether_status.js) | `/api/listen/together/status/get` | weapi | 无显式业务参数 | AUD-17 |
| [listentogether_sync_list_command](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/listentogether_sync_list_command.js) | `/api/listen/together/sync/list/command/report` | default-eapi | commandType, displayList, randomList, roomId, userId, version | AUD-17 |
| [listentogether_sync_playlist_get](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/listentogether_sync_playlist_get.js) | `/api/listen/together/sync/playlist/get` | default-eapi | roomId | AUD-17 |

## 待定项

待定不是自动排除，也不进入默认移植队列。验证后修改清单并补测试；当前业务主路径有替代或可独立推进。

| 模块 | 分类 | 暂缓原因 |
| --- | --- | --- |
| [audio_match](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/audio_match.js) | song | 听歌识曲未纳入本轮且有不同服务／上传链，另行评估。 |
| [banner](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/banner.js) | home | 可选首页轮播，可能指向视频／活动／外链，需内容类型白名单；不影响音乐主路径。 |
| [captcha_safe_sent](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/captcha_safe_sent.js) | auth | 账号安全验证分支，只有已验证登录流程确实需要时接入。 |
| [captcha_sent_v1](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/captcha_sent_v1.js) | auth | 验证码发送新变体；与 captcha_sent 做真实契约比较后再决定替换或保留。 |
| [cloud_import](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/cloud_import.js) | cloud_write | 直接导入不同于真实文件上传；必须验证真实文件元信息与账号授权，不能伪造文件或批量搬运。 |
| [device_kickoff](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/device_kickoff.js) | account | 踢设备是账号敏感写操作，不随基础登录顺带实现。 |
| [device_list](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/device_list.js) | account | 设备会话管理超出简单用户信息，独立确认后追加。 |
| [deviceinfo_center_upload](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/deviceinfo_center_upload.js) | protocol | 设备信息上传是否必需尚无证据；不自动迁移额外指纹采集。 |
| [homepage_dragon_ball](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/homepage_dragon_ball.js) | home | 移动端导航入口不是桌面必需数据；如使用必须排除被禁业务入口。 |
| [like_v1](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/like_v1.js) | library_write | XEAPI + v3 token 的喜欢变体；优先验证基础 like，必要时切换而非盲加三套逻辑。 |
| [listen_data_realtime_report](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/listen_data_realtime_report.js) | library | 听歌统计增强，不当作必需播放上报自动发送。 |
| [listen_data_report](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/listen_data_report.js) | library | 听歌统计报表不属于最小播放历史。 |
| [listen_data_song_play_rank](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/listen_data_song_play_rank.js) | library | 统计维度增强，基础历史和排行已覆盖。 |
| [listen_data_today_song](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/listen_data_today_song.js) | library | 今日统计增强，待产品需要。 |
| [listen_data_total](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/listen_data_total.js) | library | 总时长统计增强，待产品需要。 |
| [listen_data_year_report](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/listen_data_year_report.js) | library | 年度听歌统计增强，非首批必需。 |
| [login](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/login.js) | auth | 邮箱密码登录候选；先扫码／手机验证，需单独验证当前可用性和错误语义。 |
| [music_first_listen_info](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/music_first_listen_info.js) | library | 回忆坐标增强，非首批必需。 |
| [playlist_import_name_task_create](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_import_name_task_create.js) | playlist_write | 服务端文字／链接导入是独立能力，待本地导入方案确定后追加。 |
| [playlist_import_task_status](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_import_task_status.js) | playlist_write | 与服务端导入任务配套，暂不进入首批。 |
| [playlist_mylike](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_mylike.js) | playlist | mlog 命名的按时间喜欢列表，资源语义尚不清楚，不能按文件名当普通歌单采用。 |
| [playlist_subscribers](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playlist_subscribers.js) | playlist | 收藏者列表属社交展示增强，不是歌单播放／编辑必需。 |
| [playmode_intelligence_list](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playmode_intelligence_list.js) | home | 心动模式不是私人 FM；可作为后续播放策略增强。 |
| [playmode_song_vector](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/playmode_song_vector.js) | home | 心动／相似策略辅助数据，先验证必要性。 |
| [register_checktoken_v2](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/register_checktoken_v2.js) | protocol | 上游依赖 jsdom 并动态下载执行 Watchman 浏览器 SDK，不是可直接照搬的原生 HTTP 接口；纯 C++ 替代或验证桥需单独决策，禁止暗中引入 Node/WebEngine。 |
| [related_playlist](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/related_playlist.js) | playlist | HTML 抓取而非结构化官方 API，优先 playlist_detail_rcmd_get。 |
| [relay_play_state_submit](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/relay_play_state_submit.js) | protocol | 接力播放不等同一起听，协议目的需单独确认。 |
| [scrobble_v1](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/scrobble_v1.js) | library_write | 独立 clientlog 编码／上报链，待真实上报必要性与字段验证；不与 scrobble 重复发送。 |
| [search_multimatch](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/search_multimatch.js) | search | 混合搜索增强；基础四类搜索已覆盖，需额外资源类型过滤。 |
| [song_like](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_like.js) | library_write | 另一喜欢写接口需 uid，待与 like 比对幂等和账号语义。 |
| [song_lyrics_mark](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_lyrics_mark.js) | lyrics | 歌词摘录／歌词本不是播放歌词，后续独立评估。 |
| [song_lyrics_mark_add](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_lyrics_mark_add.js) | lyrics | 歌词摘录写功能未纳入本轮。 |
| [song_lyrics_mark_del](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_lyrics_mark_del.js) | lyrics | 歌词摘录写功能未纳入本轮。 |
| [song_lyrics_mark_user_page](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_lyrics_mark_user_page.js) | lyrics | 个人歌词本未纳入本轮。 |
| [song_red_count](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/song_red_count.js) | song | 喜欢数量展示增强，不影响核心歌曲能力。 |
| [toplist_detail_v2](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/toplist_detail_v2.js) | home | 榜单摘要候选版本，需与 toplist_detail 比较字段／可用性。 |
| [user_detail_new](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/user_detail_new.js) | account | 与 user_detail 并存的候选，先对齐字段及精度，不同时暴露两套用户实体。 |
| [verify_getQr](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/verify_getQr.js) | auth | 额外身份验证分支；不得将其等同普通扫码登录。 |
| [verify_qrcodestatus](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/verify_qrcodestatus.js) | auth | 额外身份验证状态，待真实验证流程样本。 |
| [vip_info](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/a8c781fd64faab17fedfd46e0615a2609307f163/module/vip_info.js) | account | 权益旧变体，优先 vip_info_v2；需对照返回再决定是否 fallback。 |

## 排除项

所有排除模块及理由保存在 `inventory.json`，不在这里重复数百行。验证器保证 440 个模块均有决定，私人 FM 例外和视频／电台等禁入规则不会被简单前缀筛选覆盖。
