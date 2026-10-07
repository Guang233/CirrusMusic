# 每日推荐、私人 FM 与一起听

这些能力均在本轮范围内，但属于不同的来源／会话，不为复用歌单页面而伪造相同远程协议。以下“客户端规则”是设计约束；响应字段和真实权限仍需验证。

## 1. 每日推荐歌曲

源码：`module/recommend_songs.js`；官方逻辑路径 `/api/v3/discovery/recommend/songs`，weapi，观察到 `afresh` 参数。上游文档说明需要登录。

相关模块：

| 模块 | 作用 | 注意 |
| --- | --- | --- |
| recommend_songs | 获取日推歌曲 | 与普通歌单详情分开 |
| recommend_songs_dislike | 日推不感兴趣 | `/api/v2/discovery/recommend/dislike`，resId、resType=4、sceneType=1；属于写操作 |
| history_recommend_songs | 历史日推索引 | [两项只读契约](daily-recommendation-history.md)已合成接入，Weapi空业务／原account/read16、candidate data.dates；会员／window仍待证 |
| history_recommend_songs_detail | 指定历史日推内容 | 仅原日期DTO成员／source token private factory、Weapi date，不用PlaylistId；data.songs候选非dailySongs fallback，无cache／自动读／反馈写 |
| recommend_resource | 每日推荐歌单 | [独立原账号读取](daily-recommended-playlists.md)已合成接入，Weapi空业务；不是每日歌曲／普通personalized替代，root.recommend候选，无日期cache／自动下游 |

最新历史增量85ops／47登记、新86／839＋thread15；ISO calendar是原服务标签不是本机推荐日或权限。home详情date必选来自索引，TSoptional/wrapper空fallback不搬，拒绝不伪空；前三项与第四项后续均暂停。本节以下snapshot/cache是设计，不作为本包已有cache或UI。

客户端规则：

- 定义 `DailyRecommendationSnapshot`：账号范围、日期键／获取时间、曲目和推荐元数据；返回中的字段可选性要先建 fixture。
- 列表展示可以复用歌曲列表组件，来源类型仍为 DailyRecommendations，不能执行“删除远程歌单”等不存在的操作。
- 缓存按账号和日期隔离。跨日／重新登录／手动刷新都要重新确认；不能以普通歌单详情缓存覆盖日推。官方日期边界及时区尚待验证。
- `afresh` 的影响和不感兴趣后的替换语义须用真实样本确认，不因为它们“返回歌曲”就自动重试。
- “保存为歌单”必须是用户明确触发的新建普通歌单＋添加曲目的复合写流程；部分成功时回读，不自动新建多个歌单。

测试门禁：未登录、正常日推、空集合、跨日、刷新／晚到响应、切账号、反馈失败及历史权益不可用。

## 2. 私人 FM

| 模块 | 官方逻辑路径 | 源码行为 |
| --- | --- | --- |
| personal_fm | `/api/v1/radio/get` | weapi，空业务 payload |
| personal_fm_mode | `/api/v1/radio/get` | 默认 eapi；mode、subMode=query.submode、limit 默认 3 |
| fm_trash | `/api/radio/trash/add` | weapi；songId、alg=RT、time 默认 25 |

私人 FM 与传统电台／消费播客是不同来源；消费播客／声音范围已由后续决策纳入并另包读取，不能将FM接口当节目播放协议。不同 FM mode 是否都对当前账号开放、允许值、返回批量大小、time 单位和限流策略都未验证，不把源码默认值当服务器保证。

客户端规则：

- 定义 `FmSession` 与续批缓冲，不把一次返回的几首歌当固定远程歌单，也不在队列末尾无限循环旧批次。
- 续批去重、有界预取、取消与 sessionEpoch 隔离。耗尽且请求失败时明确暂停自动推进。
- 喜欢、普通跳过、“不喜欢／垃圾桶”是三个不同意图；点下一首不能自动调用 fm_trash。
- 不把默认 time=25 记成用户真实收听时长；播放上报与 FM 反馈各自建立字段契约。
- 切出 FM 恢复普通队列的行为由应用层定义；普通重复播放／随机模式不能直接套在流式推荐上。

测试门禁：小批量／空批次／重复歌曲、连续快速跳过、喜欢失败、垃圾桶结果未知、账号失效、切出与切回、晚到续批不能污染普通队列。

## 3. 一起听：HTTP 模块不是完整同步协议

接续搜索辅助包acf822e，维护者先要求一起听，现明确因固定接口不全**搁置至最后**，表示已逆向整套接口；收到完整材料再重新核对接收／同步／身份／序号契约，不自动恢复不全参考。已编写但未提交的9 HTTP候选草稿完整保存在本地WIP stash，撤出主线，**未作为API或完整一起听交付**。该暂停时点主线82 business operation／44 CTest不变（最新每日歌单包独立增至83／45，非一起听恢复）；没有真实账号／服务／SDK／JS／新依赖／UI授权。草稿曾153新suite／10受影响858＋11线程数据行13 pass（0fail/skip），仅草稿历史，不挪作主线／FR-043证明；后续材料不能仅凭“整套”推已兼容／授权。

共 9 个模块，全部列为采用目标但带 `together-receive-contract` 门禁。上游拼写为 **listentogether_heatbeat**，Cirrus 内部使用 Heartbeat，保留映射避免漏项。

| 上游模块 | 观察到的业务内容 |
| --- | --- |
| listentogether_room_create | 创建房间，refer=songplay_more |
| listentogether_room_check | roomId 检查 |
| listentogether_accept | roomId、inviterId，refer=inbox_invite |
| listentogether_status | 当前一起听状态 |
| listentogether_end | roomId 结束会话 |
| listentogether_heatbeat | roomId、songId、playStatus、progress |
| listentogether_play_command | roomId + JSON 字符串 commandInfo：commandType、progress、playStatus、formerSongId、targetSongId、clientSeq |
| listentogether_sync_list_command | roomId + JSON 字符串 playlistParam：commandType、user/version 数组、固定 anchor 字段、randomList、displayList |
| listentogether_sync_playlist_get | roomId 获取同步列表 |

### 已找到的证据

- `public/docs/home.md` 的一起听章节列出这些操作，并明确说明从机模式“待整理”。
- `public/listen_together_host.html` 演示建房／加入／退出和主机命令，维护 clientSeq，并使用 REPLACE 队列命令。
- 命令与队列字段必须序列化成 JSON 字符串，而不是直接嵌套 object。
- 这些文件没有提供经验证的双端实时接收／恢复方案。不能从 REST 接口名称推断存在某个 WebSocket 地址。

### 必须补齐的协议缺口

1. 邀请链接／标识的合法生成与解析，未加入／已加入／房间失效的判断。
2. 远端播放事件的传输方式、订阅鉴权、事件格式、顺序和重放窗口。
3. 心跳周期、超时规则、成员／房主变更、服务器时间基准。
4. progress 单位和基准、clientSeq 的作用域及确认方式；不得只按 HTML 示例猜完整协议。
5. 队列 version、并发编辑、随机序列／展示序列的对应关系。
6. 从机暂停／seek 权限、歌曲不可播放、双方曲目权限差异、断线恢复与房间终止语义。

### 原生客户端设计约束

- 使用 `TogetherSession` 状态机：Idle、Creating/Joining、Active、Reconnecting、Leaving、Ended/Failed；名称是本地设计，不冒充服务器枚举。
- 所有异步结果绑定账号 epoch、房间 generation 和播放 generation。结束／切账号后不得复活旧房间。
- 远端命令有来源标记，应用到播放器后不能再次回报成新的本地用户命令而形成回环。
- 不盲重发 play command；依据已验证的 seq/version/确认规则处理重复和乱序。
- 在本地播放后端稳定后再接同步调节，不以反复 seek 代替时钟与漂移方案。
- UI 以后只暴露已通过能力门禁的状态；现在不制作房间页面，也不引入私信功能作为隐式依赖。

完成标准不是“9 个 HTTP 请求能发送”，而是两名授权测试用户能完成建房、加入、同步播放／暂停／切歌／队列变化、网络中断恢复和退出，且有脱敏证据。此项暂不阻塞普通歌曲、日推和 FM 后端。
