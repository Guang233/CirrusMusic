# 整歌单收藏／取消：原生 v2 前置调查

## 1. 状态与范围

维护者重新要求**收藏／取消收藏整个歌单**；它不是 `PlaylistCollection` 的歌曲成员编辑，也不是红心。一起听已明确暂停，草稿单独保留，不能借本项恢复一起听。

本页是**静态补证与待决契约，不是实现报告**。重新完整核对固定 `a8c781fd64faab17fedfd46e0615a2609307f163` 的 wrapper、v2 registrar、options、request 全文件，以及 config、home／TS 对应内容。业务路径明确，但原生 v2 issuer、SDK 许可／环境及真实服务要求仍未闭合。在现有限制下不能可靠交付；这不等于已证明原生实现不可能，或官方服务必然要求该固定第三方链。

**没有新增 Core variant／Facade 入口／operation／运行依赖或测试。主线仍82 business operation／44 CTest。** 没有执行参考 JS／Node／Watchman、下载动态 SDK、读取凭据／密钥环／私人文件、访问网易云业务／token issuer／CDN或发送写入。公共代码／文档研究不扩大这些授权。

## 2. 三层请求，不可混同

| 层 | 固定参考中可核对的事实 |
| --- | --- |
| 包装器 | `/playlist/subscribe`，home 文档 `t=1` 收藏、`t=2` 取消，`id` 为歌单 ID；这是参考服务路由，不是 Cirrus 请求地址 |
| TypeScript | `playlist_subscribe({t:SubAction,id})`；`SubAction.sub=1,unsub=0`，与 home 的取消值2不一致；generic Response 没有完整成功／权限 schema |
| 逻辑路径 | `query.t == 1 ? '/api/playlist/subscribe' : '/api/playlist/unsubscribe'`，显式 `createOption(query,'eapi')`；Cirrus 未来必须使用封闭正反枚举，不继承“所有其他t都是取消”或调用方crypto/domain覆盖 |
| 业务 data | 两动作均含 `id`；仅**严格数字** `query.t === 1` 增加 `checkToken: query.checkToken || APP_CONF.checkToken` |
| SDK 选择 | wrapper构造data之后强制 `query.checkToken='v2'`，options传入request；两动作都会先调用 `antiCheatTokenV2()` |
| Eapi wire | 固定默认 `interfacepc.music.163.com/eapi/playlist/{subscribe,unsubscribe}`；`e_r:false`；新SDK token置于加密payload的 `data.header['X-antiCheatToken']`，同一header又经 `createHeaderCookie()` URL编码成为HTTP Cookie，不是单纯顶层HTTP `X-antiCheatToken` |

特别注意两个不同的 token 位置：**业务 body.checkToken 与新鲜 SDK header token 不是同一个赋值来源**。前者在强制v2之前取query／config；后者来自每次SDK调用。不因为同名就把新鲜header token自动复制给body，也不复制固定config值当合法凭证。直接传入数字1与字符串"1"的wrapper分支不同；这里只说明函数代码，不断言所有服务入口都会怎样转换query类型。

TS的 `register_checktoken_v2({refresh?})` 也不是缓存策略的证明：固定registrar不读取该参数，每次调用取新token，缓存的是脚本文本／实例。home没有给出本项完整v2协议。未来必须补证body字段是否必要、正确来源和两个token的关系，不能靠截图、默认值或TS声明消除差异。

## 3. v2 registrar实际做了什么

固定 `module/register_checktoken_v2.js`：

1. axios下载config指定的 `tool.min.js`；不固定该远端脚本的版本／hash／独立许可。
2. JSDOM创建music.163.com网页环境，`runScripts:'dangerously'`、`resources:'usable'`；可动态加载Watchman和JSONP。`beforeParse`修改webdriver、chrome、languages、plugins；不移植其伪装环境行为。
3. `initWatchman({auto:true,productNumber})`后获取实例，调用 `getInstance().I(callback)`，再 `getToken(BUSINESS_ID,callback)`。产品号／业务号是集成标识，不是原生算法或本项目的服务许可。
4. SDK实例／初始化promise进程内复用；失败尝试关闭DOM。脚本下载10s、初始化15s、I等待15s、getToken等待15s是不同阶段，不是一个15s完整deadline或可取消／真实排空保证；`auto:true`的后台行为未知。
5. getToken超时／异常返回空字符串；包装响应仍 `status:200,body.code:200`，只有 `registered:!!token` 区分。request.js并未因空token停止业务请求，**这一失败续发不可照抄**。

这份文件只描述SDK调用桥，没有公开可直接移植的原生v2 HTTP请求／响应、token算法、指纹字段、签名向量、TTL或账号／设备绑定。不能自行猜 `/v2/b`、套用普通AES、固定／空／外部token，或以“不运行Node服务”掩盖实际运行JS SDK。

[已交付评论v3](comment-writes.md)是另经批准的固定原生GET／严格JSONP候选解析与两条XEAPI专用token边界；它不是本项v2等价物。当前 `commentV3Token`／allowlist不得扩大为通用token注入，亦不得改Weapi／旧路径／其他host或转发服务作为fallback。

## 4. 公共研究的证据等级

- 公共搜索找到Rust原生客户端的playlist订阅列表，只是发现线索，不证明它实现相同v2 issuer、支持当前服务或有完整token算法。尝试读取固定 [ncm-api-rs 133b65b](https://github.com/SPlayer-Dev/ncm-api-rs/tree/133b65bfe482e41ebccf018870d3fce07bf58eb3) 时，fetch工具拒绝github.com解析到 `198.18.0.67`（`198.18.0.0/15` fake-IP／内部地址）。**未取得该源码，不作实现判断**；没有改SSRF白名单、代理、DNS或换CLI／备用地址绕过。此工具限制不是网易云拒绝、SDK不支持或原生不可行的证据。
- [Cocos易盾文档](https://service.cocos.com/document/zh/neteaseyidun.html)写“支持全平台：Android、iOS、H5”，并要求各平台集成SDK／初始化。这是第三方游戏集成文档，不是网易云Watchman v2协议、Linux客户端支持证明或本项目业务号授权。没有因为出现getToken名称就替换实现，也不能由未列Linux推断易盾绝无Linux产品。
- 搜索提供者的生成回答／“Rust native”标签不是完整源文件证据，更不是本项目许可或实时可用性证明。没有执行第三方客户端、访问token服务或更新上游lock。

MIT通知仅覆盖对应参考材料，不自动覆盖运行时下载的Watchman、网易云服务／商标／曲库或其他项目。见[NOTICE](../../third_party/api-enhanced/NOTICE.md)。

## 5. 接入前门禁与所需材料

| 门禁 | 必须补齐；目前均未完成 |
| --- | --- |
| G1 原生等价 | 有来源／版本的原生v2请求与响应／算法或可用Linux SDK头文件、环境／依赖／许可；可公开且脱敏的合成向量。不提供真实Cookie、token、指纹、私人响应或账号材料 |
| G2 token绑定 | 产品／业务标识的使用依据；新鲜度／TTL／复用、设备／账号／epoch绑定，字符集／大小／错误码与取消。普通字符串或正HTTP200不能证明来源／权限 |
| G3 业务载荷 | body.checkToken的来源与必要性、subscribe／unsubscribe差异、加密header＋Cookie携带方式和Pc/Eapi响应格式；不能将SDKtoken与body字段猜合并 |
| G4 隐私与架构 | issuer不收到原账号MUSIC_U、认证header或私有库；所需采集逐项说明。不得伪装浏览器指纹、执行任意JSONP，SDK／新运行库／验证桥需单独明确批准和许可检查 |
| G5 生命周期 | 原账号／token／generation、原collection8槽、UUID关联、取消／close／切号／client loss与真实排空；取得token后发业务前重检；issuer失败不发业务，无token持久化／复用／外部注入 |
| G6 结果与验收 | 分开前置GET/SDK与业务写的发送边界、NotSent/Ack/Rejected/Unknown，合法业务拒绝／HTTP错误／缺schema／超时；Ack不证明已入库、取消不等于回滚。真实账号／写／issuer验证需另外明确授权 |

未来倾向独立 `PlaylistSubscription` 类型复用已有Collection链，但**这只是设计候选**，未新增类型或注册一个永远TokenUnavailable的“可用”API。member编辑不代替整歌单订阅，creator／subscribed元数据不证明拥有权或当前写权限。不会自动读取详情／整库／图片、刷新库、重试Unknown、乐观改状态、持久outbox或播放／报告。

## 6. 待实现测试族

`TC-PLAYLIST-SUB-V2`关联FR-014、B-015、Q-013／R-028。**以下是未来验收清单，未实现／执行，不增加CTest或pass数**：

- 两动作独立固定路径／载荷，规范wide PlaylistId；明确action拒绝非法t，验证body分支并独立解密Eapi检查header／Cookie／e_r。
- Guest／Committing／缺MUSIC_U、旧token及同token异账号，8槽、在途UUID重复与全链总deadline。
- issuer坏token／空／超限／错误码／超时／失联／redirect／取消，不发业务、不泄漏凭据或自动fallback。
- token获得前后切号、business发送前后cancel、close、资源销毁与乱序；保持原事件／账号，KnownNotSent与Unknown分离，晚到Ack不复活。
- 业务200但错误schema、403／429／-462／5xx等、Retry-After仅展示、无重试／自动刷新；fake线程创建销毁相等与真正排空。
- 另获明确授权的真实service验证与UI验收独立报告；合成成功不推定可收藏／已可见。

## 7. 本轮交接与决定点

本次调查时点只有源码／公共资料调查和文档更新，无C++／UI／build／LSP／Qt测试或真实访问新证据。当时82／44不变；历史集合335＋9、评论760＋11、搜索631＋12等不可挪作本项交付。一项GitHub资料获取受工具限制；默认不绕过。

维护者起初选择提供原生v2资料，随后明确暂无材料，选择**暂缓整歌单收藏、推进其他后端**。需求和G1–G6保留、不当完成，未来需实际可核对原生材料与明确恢复；不继续催本轮资料，不自动评估／安装／下载／执行SDK、实网或改协议。后续[每日推荐歌单](daily-recommended-playlists.md)新增83／45及新62／affected753＋thread14是独立读取包证据，不证明v2门禁闭合。一起听／云盘维持此前暂停，下载以实际可听播放音源为准、不额外查询下载资格，后端先行／UI最后。
