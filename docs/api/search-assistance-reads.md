# 搜索建议与热搜辅助读取

## 1. 交付范围与证据

维护者批准继续后端搜索辅助小包。只接三个封闭操作：**web实体搜索建议、简略热搜、详细热搜**。Qt/C++，原read16、session token／generation／取消和后台关闭排空复用；无新运行库、线程、SDK／JS执行、通用Manager、Node／代理服务、UI、持久化搜索历史／缓存或实网授权。此前−462窗口保持停止，没有账号／真实凭据／私人文件或服务请求。

完整审阅固定a8c781fd64faab17fedfd46e0615a2609307f163：search_suggest.js、search_hot.js、search_hot_detail.js及对照search_suggest_pc.js，home.md简略／详细热搜、搜索建议及PC建议四章节；interface.d.ts对应4overload／SearchSuggestType，完整option.js、config.json及request.js519行。无默认checkToken／SDK链。参考wrapper／generic Response只建立请求候选，不是官方规范、响应schema／免登录／今天服务支持证明；本包响应均合成候选，无网页／客户端执行或真实热搜／评论／歌曲内容fixture。历史440模块／164-40-236分类不改作实时registry。

不采用mobile关键词建议 `/api/search/suggest/keyword`、新PC建议 `/api/search/pc/suggest/keyword/get`、默认搜索词、多重匹配；不是这些变体失败后的fallback。没有歌单内搜索、Podcast／Voice／Artist评论绑定、视频／MV／动态／工作台搜索动作。四类建议的typed ID不证明服务数字互异、可播或可编辑。

## 2. Core值、Facade与协议

`domain/music/search_assist_types.h`新增SearchSuggestionsQuery{keywords}／HotKeywordsQuery{}／HotKeywordDetailsQuery{}及三个结果，扩充原MusicQuery／MusicResult。Facade：searchSuggestions(QString)、hotKeywords()、hotKeywordDetails()，结果经musicCompleted(id, originalToken, MusicResult)／cancelMusic。不增加UI命令或内部自动搜索逻辑。

- 建议keywords合法UTF16、非空／非纯空白、≤256UTF16、无NUL；保留引号／反斜杠／前后空格／Unicode，不规范化、补缺省词、截断或本地扩写。这是本地预算，不是服务限制。
- 原Facade Ready、有效Guest／Authenticated token、最多16槽；输入无效／非Ready／容量拒绝返回0、无派发。非零是本地受理，结果不是完整搜索／服务可达／权利证明。
- Service／Gateway验证原token，失效／取消返回原身份，不改标新账号；输入改变不会偷偷发新查询／取消或丢弃其他同账号合法请求。乱序结果保留独立requestId及原query，未来UI须显式取消或按ID选最新输入，不把最后到达当最新。
- 不自动读实体详情、作者／图片／歌词、统计、下一页，或加入播放／报告／收藏／评论写／cache／SQL。用户后续明确选中ID才能调用既有独立入口，建议预览不是Song／Album完整DTO或播放身份。

| operation | 官方逻辑path（非Node wrapper） | protocol／wire host | 精确业务payload | JSON预算 |
| --- | --- | --- | --- | --- |
| SearchSuggestions | `/api/search/suggest/web` | Weapi／music.163.com | s:原keywords文本 | 512KiB |
| HotKeywords | `/api/search/hot` | 默认Eapi／interfacepc.music.163.com | type:数值1111 | 512KiB |
| HotKeywordDetails | `/api/hotsearchlist/get` | Weapi／music.163.com | 空对象 | 1MiB |

都使用固定JSON解码，无猜测密文／弱root重试。Pc header及e_r:false由既有协议层固定；不带入public options的crypto／type／e_r／token／header／domain／Cookie／proxy／realIP覆盖。内部suggest仅keywords一个string，其他两操作仅空params；额外s／type／分页／uid／SDK键均拒绝。Guest不要求MUSIC_U只是本地可提交，不是服务匿名支持证据；Authenticated请求取原Cookie snapshot。响应Set-Cookie不采纳，不能以热搜cookie轮换账号，不触发v3 issuer或协议降级。

## 3. 严格候选响应与展示边界

业务必须code200；HTTP200不覆盖403／429／−462或解析失败。mapper直接比较JSON数值200（符合既有read mapper）；字符串code／malformed拒绝，不把write decoder兼容当所有读取schema约定。optional缺失／null与显式空分开，不伪造完整性／零值。

### web建议

必须root.result对象及result.order数组≤16项。各order项非空合法纯文本≤64UTF16；仅songs／artists／albums／playlists映射为SuggestionGroup枚举，保留支持组次序／重复；未知组只置unsupportedGroups=true，不传原组名／内容／ID或执行动作。未知数据本体不递归解码，但总体仍有JSON传输预算。

四个optional数组各≤20行，保留provider次序／重复（包括同ID不同标题），不交叉合并／去重／重排；已在order声明的支持组必须有显式数组，不能缺失／null。未在order声明的数组若存在仍严格解码，不据order反推完整性。缺失／null为未知，[]为已知空；合法候选无命中需显式order:[]，**result:{}不当空成功**，真实服务空结果格式未验证，受限时返回InvalidResponse待补证。

每行必须object、规范positive signed64 ID与必需name（合法UTF16、无NUL、≤4096，可显式空）。JSON数值ID≤2^53−1，宽ID必须规范字符串；四个SearchSuggestion<ID>只有各自typed ID／title，不冒充完整Song、继承试听／权益／来源或把artist当verified owner。附属artist／album／playcount等未纳入，不猜跨实体绑定。order重复不是再次请求指令；unsupported不声明“无其他结果”。

### 简略热搜

必须root.result.hots数组≤100；每行object、必需first合法非空keyword≤256（同输入规则）。输出HotKeyword{keyword}保留顺序／重复／外部空格，不根据位置或second／third猜排名、分值／播放计数或精确热度。合法hots:[]可空成功，root/data兜底和缺数组不允许。

### 详细热搜

必须root.data数组≤100；每行object、必需searchWord同keyword规则。optional score／iconType只接受安全非负JSON整数（含0），分别providerScore／providerIconType原值；缺失／null未知，不解析数字字符串／小数／负值／宽double，也不称精确排名／实时总数／稳定业务icon枚举。content optional合法纯文本≤4096；未知与显式空可区分。

iconUrl按既有display metadata策略：≤2048、合法http(s)／host、无credentials／fragment，否则空QUrl；仅展示引用，不是访问授权／SSRF或白名单保证，绝不自动下载。url／alg等跳转／跟踪字段不发布；javascript／file链接不能变成客户端动作。所有标题／热词／说明为不可信纯文本，不渲染HTML、自动搜索／打开URL／生成补充文案或日志记录原正文。实际分值含义／单位／图片兼容／刷新频率和排名语义均未实证。

## 4. 账号、取消与失败

原MusicQueryService／MusicGateway生命周期不改：Guest／Authenticated可读，旧token／意图generation屏障、16槽、SessionApiClient全局32槽和transport限额继续。cancel／signOut阻断晚到、返回originalToken；service失效可StaleContext，**Facade close立即统一Canceled**并清空snapshot／generation，后端晚到不能覆盖。client销毁返回Network、确实取消reply，不做用户可见假空列表。

429保留合法整数Retry-After秒0..86400→ms，但不自动重试／定时刷新；403／−462原ProviderRejected，不推断挑战原因／强迫登录／换host或token绕过。超过group／order／word／body预算整结果失败、不截断，也不自动分页拆请求。合成响应受这些条件约束，不保证恶意JSON解析沙箱或未来service兼容；失败不扩宽默认规则。

## 5. 实现、验证与交接

新文件：domain/music/search_assist_types.h、music/search_assist_mapper.{h,cpp}、tests/support/synthetic_search_assist.h、tests/unit/search_assist_test.cpp。原API registry／payload、MusicQuery/Result／gateway／mapper和Facade接线，Runtime通用read装配直接复用，不加新factory／后台service。业务79→82，CTest43→44；v3、公钥、NOS窄传输另计。

最终Qt6.11.2 Debug／QT_FATAL_WARNINGS=1／LC_ALL=C.UTF-8：11 affected套件**631 pass**（search_assist117、entity_search44、music26、catalog22、catalog_metadata66、comments149、comment_writes134、session_api_client24、backend_facade8、response_decoder19、http_transport22），选10条线程数据行**12 pass**（含init/cleanup），0fail/skip。不是全44／Release／真实服务／密钥环／UI／声学验收；此前评论760＋11、初次115＋3均单列，不累计成新总数。

新117：四类独立宽ID／同数字跨类型、顺序重复／未知与[]、unsupported组不转发、strict3path/payload/keys、关键词256与20/16/100、score/content／图／纯文本；88个坏schema数据行、6 Guest／Auth双请求native行（Weapi form／原Cookie不轮换，简略热搜实际Eapi解密与固定Pc e_r:false/header）、6 cancel×stale、9 code403/429/−462无fallback、3client销毁与3raw超限、2并发建议乱序保持query/id、Facade16与取消／晚到／close、不自动play/report/write。

线程searchAssistThroughThread：先Guest热搜→合成登录→四型建议／详细热搜／重复简略热搜→坏suggest schema→挂起热搜close；**9API请求／6read结果**、原Guest／Authenticated token／caller线程、created=destroyed／wrongThread0／真正排空。6显式命令对应6结果，无其他search／详情／图／play/report/comment/collection或queue变化；不以这些替身证明匿名或账号服务支持。

13 changed C++加邻接collection_operation.cpp／backend_runtime.h，fresh all-server15s／concurrency4共15 clean；真实14 application对象**6963 dependency entries**，基础设施／JSON／非CoreQt／OpenSSL穿透0、UI0。新moc保留，经CMake注册／AUTOMOC生成、实际编译与DB刷新后fresh clean；不移除Q_OBJECT或伪suppression。初次4fixture失败已修：Plan已自带Retry-After再append会成3,3，保留生产严格解析；close应Caller Canceled不是StaleContext，仅修fixture预期，最终全组成功／fatal与malformed案例保留。

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --target tst_search_assist tst_entity_search tst_music \
  tst_catalog tst_catalog_metadata tst_comments tst_comment_writes tst_session_api_client \
  tst_backend_facade tst_response_decoder tst_http_transport tst_backend_runtime --parallel 4
cp build/linux-dev-local/compile_commands.json build/compile_commands.json
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(search_assist|entity_search|music|catalog|catalog_metadata|comments|comment_writes|session_api_client|backend_facade|response_decoder|http_transport)$' -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  searchAssistThroughThread entitySearchThroughThread musicQueriesThroughThread catalogMetadataThroughThread \
  commentsThroughThread commentWritesThroughThread libraryThroughThread playbackThroughThread destructionStillDrains
```

ignored search-assist-final-{build,focused,thread}.log、boundaries／format／diff-check／fixed-hash／links／secrets及audit／first／fix／review记录只作本地证据；不交付缓存或秘密。FR-007部分／域18／TC-SEARCH-ASSIST／R-027／roadmap／HANDOFF／indexes／native/runtime／NOTICE同步，本地独立提交、不push／publish／bundle。clang-format dry-run／git diff --check／固定440与公共文件hash通过；709本地Markdown文件引用missing0（排除代码、不核验外部URL／anchor），changed范围有限live-looking Cookie pattern hit0，不作全面秘密保证。实网候选schema／空结果／服务权限／真实分值／频率、UI／debounce／持久历史、mobile／pc变体和默认词／多重匹配仍另包／授权；下载等其他后端不因本包完成而已交付。参见[实体搜索](entity-search-reads.md)、[后台生命周期](backend-runtime.md)。
