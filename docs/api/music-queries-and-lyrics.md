# 首批音乐查询与三语言／两级歌词

> 已实现歌曲搜索、歌曲详情、歌词的原生请求→应用用例→后台Facade闭环。模板UI不变；没有播放器、音源URL、歌词高亮／对齐器或缓存。本轮按维护者授权做了少量官方公开只读探测，不使用账号或本机凭据。

后续[四类资料／独立介绍](catalog-introduction-and-metadata-reads.md)在Song追加optional别名／译名／发行原值，复用songFrom及本原生命周期；歌曲百科基础为独立显式读，不生成通用歌曲description／完整作者署名，不替换六轨歌词或自动下游。本文7项和早期验证仍是本歌曲包时点。

## 1. 固定参考与请求契约

参考固定commit `a8c781fd64faab17fedfd46e0615a2609307f163` 的 `module/search.js`、`song_detail.js`、`lyric_new.js`、`util/option.js`及`public/docs/home.md`歌词格式说明。它们是实现参考，不是网易云官方规范；MIT来源保留于[NOTICE](../../third_party/api-enhanced/NOTICE.md)。

| 原生operation | 固定逻辑路径／协议 | 输入及边界 |
| --- | --- | --- |
| SearchSongs | `/api/cloudsearch/pc`，eapi（2026-11-08 起改用 PC 客户端的 `/api/cloudsearch/pc`，参考 cloudsearch.js，加 `total:true`；返回键相同，单曲直接带专辑封面） | 固定type=1、total:true，只查歌曲；关键词1～256 UTF-16单元且非空白，limit 1～100，offset 0～10000；响应≤2 MiB |
| SongDetails | `/api/v3/song/detail`，weapi | 1～100个不重复正整数ID；规范十进制字符串，最大signed-64-bit；响应≤4 MiB |
| SongLyrics | `/api/song/lyric/v1`，eapi | 一个歌曲ID；cp=false，tv/lv/rv/kv/yv/ytv/yrv均为0；响应≤4 MiB |

详情请求的`c`使用JSON字符串中的整数原文，不经double。client新增一般weapi分支；只有AccountIdentity仍要求MUSIC_U并创建一次性凭据收据。公开详情允许guest，已有匿名token时加入受限MUSIC_A。新音乐查询不采纳响应Cookie，不凭歌曲元数据更新已核验账号凭据；没有协议降级、自动重试或任意端点转发。

本歌曲首次接入时registry为7项业务operation（另有独立XEAPI公钥注册）；不因此修改440模块静态清单为“全已实现”。

## 2. 领域与应用边界

- `domain/music/music_types.h`：SongId／ArtistId／AlbumId、歌曲摘要、搜索分页、详情缺失列表、六轨歌词。仅Qt Core。
- `application/ports/music_gateway.h`：typed query/result，无HTTP／JSON；`MusicQueryService`负责输入、容量、允许的会话phase、取消与原token隔离。
- `infrastructure/netease/music/`：封闭operation适配、业务错误、JSON映射和LRC／YRC解析；具体provider字段不进入application。
- Runtime只增加服务装配，查询还是在原backend线程执行；Facade提供 `searchSongs`、`songDetails`、`lyrics`、`cancelMusic` 和 `musicCompleted(id, originalToken, result)`。

Facade最多16条未完成音乐查询（独立于账号命令邮箱），返回0表示未接收；只允许有效Guest／Authenticated快照，Authenticating／Committing／Expired不分发业务。传入调用方观察到的原token，不在Worker侧悄悄替换成新账号。关闭、登出和认证切换会取消待查询并丢弃旧payload；同一账号快速切歌时，调用方应取消旧ID并只消费当前请求ID，不让旧歌词覆盖新歌。

搜索保留服务端顺序、total与hasMore的未知状态；不根据“刚好满一页”猜下一页。详情规范到请求顺序，未返回ID单独列missing；额外ID、重复返回、非法ID拒绝。搜索`artists/album/duration`与详情`ar/al/dt`映射到同一摘要。未知时长与0分开；artist／album的0或缺失ID允许保留名称而无ID。元数据存在不代表可播放或可下载，尚未映射完整privileges。

封面只保存有界http(s) URL值，不发起抓取；将来图像加载仍需单独的来源／重定向策略，不能把模型里的URL视为自动信任。失败仅传MusicError、有限provider code／Retry-After毫秒，不传原响应或凭据。429没有自动重试；缺失歌曲不是自动推定“无版权”。

## 3. 歌词：语言与时间精度是两个维度

六个provider字段**分别保留**，不能按数组下标合并为一套行：

| 语言轨 | standard | enhanced |
| --- | --- | --- |
| original 原文 | lrc | yrc |
| translation 翻译 | tlyric | ytlrc |
| romanization 罗马音 | romalrc | yromalrc |

每轨都包含：Missing／Empty／Present／Malformed、实际PlainText／Lines／Words精度、独立providerOffsetMs、行／字时间及纯文本credits。`enhanced`是字段版本，不是“必定逐字”；真实样本确认`ytlrc`／`yromalrc`可以仍是LRC格式。某语言缺失、某精度缺失、行数不同、时间不同都不补造、不强制对齐；不把原文逐字时长复制给翻译或罗马音。旧klyric未额外适配，不猜其未核验格式。

- LRC支持`[mm:ss]`、1～3位小数（点或冒号分隔）、一行多个时间标签、CRLF／BOM、同时间多行、空文本行与纯文本降级。
- 稳定按时间排序；同时间行保留，结束时间取下一个不同时间；最后一行结束未知。纯文本行无虚构时间。
- YRC按`[行起点,行时长](字起点,字时长,标记)文本`解析，时间单位毫秒，字起点为**绝对时间**。字起点单调且位于行范围，结束不超过行尾；允许相邻字时长重叠，不拉伸补齐，未知标记不当作权限或样式。
- 每轨≤512 Ki UTF-16单元、≤10000行、≤30000字片段；单行≤16384字符，单行LRC时间标签≤128。时间范围≤7天，UTF-16／NUL／溢出等受限检查。HTTP解码还有上面的总响应限额。
- 不支持／损坏行增加malformedLines并跳过；已有有效行仍可用，全坏轨标Malformed；调用方可明确回退到同语言standard或纯文本，不伪造逐字信息。
- JSON型署名元数据只提取`c[].tx`为credits，忽略内嵌图片／导航链接，不作为演唱时间行。
- 域模型偏移正值表示延后显示；LRC offset正值表示提前，因此转换为相反号并单独保留，不直接改写时间戳。未来播放时再加用户偏移。
- nolyric只保存providerNoLyric；instrumental只接受显式pureMusic布尔。空轨、无字段、接口失败均不能猜成纯音乐。

当前只负责获取与内容时间轴。三语言行对齐、最佳轨选择、媒体时钟定位、seek、高亮、用户偏移设置、磁盘缓存／导出分别留给后续工作，不在这一轮伪装成已实现。

## 4. 本轮验证，保持有界

离线仅编译受影响目标，执行4个Debug套件（`QT_FATAL_WARNINGS=1`）：music **26**、backend_facade **7**、session_api_client **24**、auth_transaction **38**，合计 **95 passed**、0失败／跳过。另只执行backend_runtime的 `musicQueriesThroughThread` 一个用例（Qt报告3 pass，含init/cleanup），没有重跑其5秒密钥环等待等不相关用例，也未跑全量／Release矩阵。

覆盖三语言独立轨、逐行／逐字与格式识别、重复时间／空行／重叠／偏移／坏时间／超限、未知分页、宽ID与遗漏详情、封闭payload、429、取消／切会话、共享weapi身份分支回归，以及真实后台线程到Facade的一条完整音乐查询链。测试歌词全部合成，不把曲库原文写入fixture。

```bash
cmake --build --preset linux-dev-local --target tst_music tst_backend_facade \
  tst_backend_runtime tst_session_api_client tst_auth_transaction --parallel 4
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(music|backend_facade|session_api_client|auth_transaction)$' -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime musicQueriesThroughThread
```

### 授权的公开只读探测

2026-10-02（执行环境UTC日期），本工作包通过临时、被git忽略的原生C++探测器调用SessionApiClient（不是Node代理），共4次：歌词结构一次；随后搜索、详情、歌词各一次核对完整映射。仅发送临时非硬件deviceId，无账号／本机密钥环或保存Cookie，不调用注册／登录／写操作。只记录结构摘要，未保存完整歌词／响应体／Cookie。

- 搜索“海阔天空”，limit=2、offset=0：HTTP200／code200，2首，result含songs/songCount/hasMore，映射成功。
- 公开歌曲`1824020871`详情：HTTP200／code200，1首、missing=0，映射成功。
- 同一歌曲歌词：HTTP200／code200，六字段均存在；lrc 55行、yrc 45行（Words）、tlyric 55行、ytlrc 30行、romalrc 55行、yromalrc 45行；除yrc外均Lines。六轨malformedLines均0。

这只能证明**本次环境、少量样本**下三项查询的协议与结构可用，不证明全曲库、地区／登录权限、XEAPI注册、账号安全存储、媒体URL或播放兼容。原始歌词不会作为公开文档引用；仓库只保留上述结构事实，临时探测器和日志留在ignored build目录。

## 5. 下一工作包

后续[官方歌曲URL解析与无界面Qt播放PoC](stream-resolution-and-playback.md)已完成首个单曲闭环，后续[队列与六轨歌词时钟](queue-and-lyric-clock.md)也已接入。[专辑／艺人／歌单只读目录](catalog-queries.md)也已接入并离线验证；每日推荐、FM及个人库仍未实现；UI仍最后。不要为播放成功引入第三方解锁，也不要把元数据查询成功当作音源授权。
