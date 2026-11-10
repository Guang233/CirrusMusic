# 用户主页读取

维护者 2026-11-10 把网易云的用户页纳入范围（设计：[docs/ui/pages/user.md](../ui/pages/user.md)）。这一页记五个读取（§1–§4）和关注／取消关注两个写操作（§5）。当前 114 个业务 operation。

## 1. 操作

都不需要登录（`requiresAccountCookie` 为假），断连可重发。用户 ID 由调用方给（`UserProfileQuery`），不是当前账号。

| operation | 参考模块 | 逻辑路径 | 协议 | payload |
| --- | --- | --- | --- | --- |
| UserDetail | user_detail | `/api/v1/user/detail/{uid}` | Weapi | 空 |
| UserPlaylists | user_playlist | `/api/user/playlist` | Weapi | uid／limit／offset／includeVideo:false |
| UserFollows | user_follows | `/api/user/getfollows/{uid}` | Weapi | offset／limit／order:true |
| UserFans | user_followeds | `/api/user/getfolloweds`（**不带 uid**） | Eapi | userId／time／limit／offset:0／getcounts:"true" |
| UserRanking | user_record | `/api/v1/play/record` | Weapi | uid／type（1 最近一周，0 所有时间） |

**粉丝列表和参考项目不同**：参考项目现在的路径 `/api/user/getfolloweds/{uid}`（Eapi 和 Weapi 都试过）不认 limit／offset／time，总是给最前面 30 个。老版本的写法是逻辑路径不带 uid，它认参数：limit 照给，`time` 是游标——下一页的 `time` 是上一页最后一行的 `time`，offset 保持 0（offset 20 会再跳过 20 个）。

## 2. 实测（2026-11-10，游客）

钩子 `build/research-scripts/user-probe-hook.patch`（已撤，没提交）。一个音乐人（陈粒，uid 37179178），以及一首热门歌评论区里的 5 个普通用户。日志只打键名、类型和数字，没打昵称、签名、URL。

- 五个读取游客都能读。
- 关注、粉丝不公开：code 400，`message`"用户隐私无权限查看"（5 个人里 1 个）。
- 听歌排行不公开：code -2，`msg`"无权限访问"。用户详情的 `peopleCanSeeMyPlayRecord` 事先就能说明。
- 不存在的 uid（99999999999999）：用户详情 code 404。
- 别人的排行条目 `playCount` 是 0，只有 `score`。

## 3. 映射（`user_profile_mapper.cpp`）

和其他读取同一套严格规则：类型不对、ID 不对、行数超过 limit，就整页 `InvalidResponse`，并记一条 `music.invalid_why`（reason 只是检查项的名字）。

- **UserProfile**：`profile.userId` 必须是请求的 uid；`nickname` 必须有。`avatarUrl` 和 `signature` 可选，`null` 当作空。可选数字：`level`（顶层）、`follows`、`followeds`、`playlistCount`。可选布尔：`followed`、`peopleCanSeeMyPlayRecord`（→ `rankingOpen`）。`profile.artistId` 不是 0 的才算音乐人（→ `artist`）。
- **UserList**：关注读 `follow[]`，粉丝读 `followeds[]`，每行字段同上。另有 `more`；粉丝还有 `size`（总数）。粉丝每行要有正的 `time`，并且新的在前；`nextCursor` 是最后一行的 `time`。
- **UserPlaylists**、**UserRanking**：响应和账号自己的 `MyPlaylists`、`ListeningRanking` 同形，直接用 `mapLibraryResponse`、`mapCloudHistoryResponse`，被查看的用户放在 account 的位置。创建的和收藏的歌单靠 `creatorId == uid` 区分。`accountLikedSongs` 指的是对方的"喜欢的音乐"。
- **不公开**：在 gateway 里转成 `UserProfilePage{query, UserProfileHidden{}}`，不算失败。关注、粉丝看 code 400，排行看 code -2。用户详情没有"不公开"，404 照常是 `ProviderRejected`，带 providerCode。
- facade（`BackendFacade::userProfile`）用 `matchingUserProfilePage` 核对查询种类、uid 和内容种类。

## 4. 测试

`tst_user_profile`（`tests/unit/user_profile_test.cpp`）覆盖以下内容，合成数据按 §2 的实测形状，放在 `tests/support/synthetic_user_profile.h`：
- 合同、payload 严格性、四种映射、内容匹配；
- 游客经真实 client 读取，其中两类不公开、404。

## 5. 关注、取消关注（写）

| operation | 参考模块 | 逻辑路径 | 协议 | payload |
| --- | --- | --- | --- | --- |
| UserFollow | follow（t=1） | `/api/user/follow/{uid}` | Weapi | 空 |
| UserUnfollow | follow（t≠1） | `/api/user/delfollow/{uid}` | Weapi | 空 |

- 路径里带 uid，所以合同在 `user_profile_operation.cpp`（`collectionOperationContract` 不带参数）；变更是 `domain::UserFollow`，走 `CollectionMutation`／收藏 gateway，规则和收藏相同：要账号 Cookie、不重发、code 200 才 Acknowledged，其余 Rejected（带 providerCode），没看到回应是 Unknown。
- **未实测**：成功的回应和风控（像评论那样）都还没见过，等维护者登录试。
- 测试：`tst_user_profile` 的 `followWrites`（合同、payload、要账号、不重发）。

## 6. 歌手 → 用户

歌手详情（`/api/artist/head/info/get`）的 `data.user.userId` 映射到 `ArtistDetails::userId`（2026-11-10 实测：音乐人有，`data.user` 缺 userId 时没有；userId 不是合法 ID 或 `data.user` 不是对象时整页拒绝）。测试：`tst_catalog` 的 `artistSchemasAndUnknownPagination`。
