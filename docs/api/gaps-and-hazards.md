# 静态审计发现与契约缺口

所有源码观察都针对 [固定版本](upstream-lock.json)。`module/…` 文件的不可变链接在 [接口表](selected-endpoints.md)／[清单](inventory.json)中，公共文件见 lock。**没有实际调用证据**；下述处理是 Cirrus 的迁移要求，不宣称已修复上游。

## AUD-01：音源主路径变更且夹带解锁分支

`module/song_url_v1.js` 默认选择 xeapi，并包含第三方 unblock/source 处理。只采用官方分支，不开放解锁选项，不带入第三方音源依赖。质量 level、可用权限、URL 到期、试用片段、格式和无音源原因要逐个验证，不能承诺高音质必可播放。

## AUD-02：匿名与 XEAPI 启动依赖

`util/request.js` 依赖启动时的匿名 token 文件、公钥文件和全局设备信息；`util/xeapiKey.js` 在更新响应缺 sk 时会保留旧 sk。原生实现必须拥有独立的启动状态机、原子 key 更新和账号隔离，不能只移植 song_url_v1 模块。

## AUD-03：token v2 不是纯 HTTP 接口

`register_checktoken_v2` 下载浏览器 SDK 并在 jsdom 中执行；列为待定。`playlist_subscribe` 强制使用 v2，因此收藏歌单保留需求但暂不承诺可用。`register_checktoken_v3` 是另一条 HTTP/JSONP 链，不能假定二者互换。

同时，playlist_subscribe 用 `query.t == 1` 选路径，却用 `query.t === 1` 决定一个 payload 字段，字符串／整数输入会产生不同分支。C++ API 应暴露显式 bool/enum，不迁移这种歧义。`comment_add`/`comment_reply` 的 token 需求也要按源文件与公共 options 逐条确认。

## AUD-04：QR 检查异常分支

`login_qr_check` 的 catch 引用 try 块内部声明的 result，发生异常时会再次失败。原生实现必须保留原始网络／协议错误，并将 QR 等待、扫码、授权成功和过期视为业务状态，不做通用 HTTP 错误吞并。

## AUD-05：本地辅助与上游响应包装

`login_qr_create` 生成扫码链接／可选 QR 图像，没有官方请求；不应增加虚构网络 operation。`login_qr_key`、`login_status` 等改变原始返回 envelope。C++ 直连时解析的是官方响应，不是 Node 包装后的 data/body 嵌套。

## AUD-06：下载不同于播放 URL

`song_download_url_v1` 在 vivid 分支改变客户端 OS/appver；`song_cloud_download` 是云盘下载来源。不要把所有分支统一成播放 URL，也不要把设置客户端版本当作获得权限的保证。下载授权、会员限制、URL 过期和文件校验必须独立处理。

## AUD-07：歌单曲目写重试

`playlist_tracks` 对业务码 512 有重复 trackIds 后再请求的逻辑，且返回包装特殊。不要直接复制。请求超时、业务拒绝、重复曲目和服务器实际成功必须区分；不确定时回读歌单，而不是继续添加或删除。

## AUD-08：track_all 并不等于真正的 all

`playlist_track_all` 先读详情、截取 trackIds，再取 song_detail；默认 limit=1000、offset=0。必须支持分页和分批歌曲详情，保留原排序、缺失占位及去重策略，不能只取第一页却展示“全部”。

## AUD-09：写接口输入和能力不对称

`playlist_update` 手拼含用户名称／描述的 JSON 字符串，特殊字符可能破坏结构；原生实现用 JSON 编码器。它通过 batch 执行多个更新，需处理部分成功。

`playlist_order_update` 和 `song_order_update` 对 ids 形态要求不同；输入 schema 不能仅写“ID 列表”。`playlist_privacy` 固定 privacy=0，只说明公开隐私歌单，不能据此宣称支持任意公开↔私有切换。创建时隐私参数与修改隐私是不同契约。

## AUD-10：可用性失败不等于无版权

`check_music` 基于旧音源接口并把失败简化为“暂无版权”。原生错误模型至少区分认证失效、会员／版权限制、区域限制、限流、过期 URL、网络和解析错误；不能因请求异常给用户错误版权结论。

## AUD-11：混合资源接口必须收窄

search/cloudsearch、首页 block、通用评论和用户评论历史可能涉及视频、电台、动态等资源。搜索首批允许歌曲／专辑／艺人／歌单；评论只允许歌曲／歌单／专辑的受支持类型，不能接受任意 threadId。响应同样过滤，不能只约束请求。

## AUD-12：每日推荐与推荐歌单不可混淆

recommend_songs、recommend_resource、history_recommend_songs 系列不是同一资源。日推登录要求、日期／时区、afresh 行为、历史权益及 dislike 后替换均需补实测，见[独立契约](special-contracts.md)。

## AUD-13：FM 是流式来源，垃圾桶是写操作

personal_fm 与 personal_fm_mode 共用逻辑路径但协议／参数不同。fm_trash 是显式负反馈，不等于 next。续批、模式权限和时间参数待确认，不复制固定时长作为真实行为。

## AUD-14：上传插件不在模块直接调用清单内

云盘／歌单封面需要 NOS token、真实字节上传、元数据提交等多步流程。AST 的 module requestCalls 不覆盖插件内部 axios 调用；“无 request 调用”不能作为本地 operation 判断。上传节点校验、大小／格式限制、失败恢复和取消语义必须补齐。

## AUD-15：batch 不作为通用透传

上游 batch 会收集任意 `/api/` 开头的输入 key。原生只允许内部已登记 operation，保留每项结果；禁止让 UI 指定任意路径或使用 batch 绕开业务权限和重试规则。

## AUD-16：上报不等于一次普通写请求

scrobble 顺序发送 startplay 和 play 两条 weblog，并强制 osx profile；第二条失败可能留下第一条成功。根据真实播放事件生成上报，保护用户隐私，不伪造时长、刷量或同时发送多个变体；需要独立的会话去重和部分成功记录。

## AUD-17：一起听缺完整接收／同步链

9 个模块已登记，不能因而宣称“完整一起听可用”。从机事件通道、心跳／时间单位、权限、seq/version 和恢复方案尚待证据。详见[一起听契约](special-contracts.md)，未验证前 capability 保持关闭。

## 横向缺口

- 尚无官方返回 fixture：所有 required/optional/null、字符串／数字 ID、分页、权限和错误字段必须在实现相应 operation 前验证。
- 尚无当前账号真实调用：源码路径存在不证明服务器今天仍支持。受限能力不静默切到非官方服务。
- 主机和 Cookie 策略不能复制服务包装器的 Domain 删除／全局凭证逻辑。
- 真正的本地扫描、队列、歌词解析、下载任务、缓存和 Linux 集成属于客户端能力，不存在一个上游端点能替代。
- 当前只确认仓库 MIT；远程 SDK、服务条款、商标和曲库授权不因参考仓库开源而转移。

## 需要维护者决策时的停止点

1. v2 token 若找不到可验证的原生路径：是否允许独立的浏览器验证桥，需要另行批准。现在没有添加该依赖。
2. 一起听的协议补证若需要真实双账号测试：由维护者提供合法测试方式；不请求把密码或完整 Cookie 粘贴进对话／仓库。
3. 若端点仅能通过被排除的 SDK、第三方音源或未经批准的数据采集工作：保持能力受限，先询问，不擅自扩大范围。
