# 有界歌单封面更新

## 1. 交付与授权边界

维护者确认封面更新独立包，并明确允许基础设施显式链接Qt6::Gui做图片解码／编码。domain/application仍Core-only；没有Node、WebEngine、第三方图片库或新SDK。JPEG插件可用性属于部署要求，不存在时校验失败而非格式伪成功。Qt／插件的分发许可仍需打包验收，api-enhanced MIT不授予服务、图片或SDK权利。

仅离线合成验证：**没有读取私人文件、真实凭据、真实账号或真实上传**；UI、文件选择／目录扫描、PNG／其他格式、裁剪／缩放、缓存／同步及可靠核验不在本包。旧−462窗口未恢复。

完整固定参考a8c781fd64faab17fedfd46e0615a2609307f163：module/playlist_cover_update.js、plugins/upload.js、util/fileHelper.js、home.md对应说明、TS ImageUploadConfig／函数、public/playlist_cover_update.html及options／config／request链。三个必要阶段为申请NOS凭据→原始图片POST→提交封面。两API明确weapi，createOption默认不启用checkToken；不推定未来服务无挑战，整歌单收藏v2 SDK门禁不变。

注意：文档／TS／示例的imgSize／imgX／imgY没有被当前wrapper／plugin使用，不能当已证服务裁剪契约；网页multipart/form-data只是Node包装入口，真实NOS上传为raw bytes。上游忽略res2上传正文，最终wrapper外层总200，均不照抄；不得把外层／HTTP200当业务成功。

## 2. 显式输入、图片与内存

原CollectionChange增加第14种 `PlaylistCoverReplace{playlist, jpeg}`，通过原mutateCollection／8槽／eventId／原账号-token-generation链。不接受路径、URL、文件名、token或docId输入，不自动读取文件。Core仅检查规范PlaylistId、JPEG有界起终标记／输入≤1MiB；**Core envelope不是完整图片验证或权益证明**。

基础设施准备阶段：

- 1MiB输入／输出本地预算；方形1..1024像素，不声称服务最大值。
- 有界marker预检≤4096 markers，完整segment边界、单8bit baseline或progressive frame、scan／entropy stuffing／EOI；拒绝其他JPEG形式、坏边界、尾随数据。它不是像素解码器或安全沙箱。
- Qt QImageReader完整解码，尺寸匹配／方向自动转换；不猜裁剪或自动缩放。
- 转RGB32后复制到**新像素图**，JPEG质量85重编码；不拷贝text／EXIF／ICC／原文件名。改变编码字节，不保证与原文件逐字节相同；色彩／方向／真实图像兼容性仍待验证，解码不是版权／内容安全证明。
- 只有准备成功才申请上传凭据。普通坏／非方形／超限／codec不可用输入NotSent，不静默改格式或透传原图。

输入和准备图片仅进程内、Qt值传递；在途共用8槽，隐式共享不等于安全擦除。取消／结束／关闭释放持有者，但不保证allocator、调用方原图或已排队副本不可恢复；消费者仍须清理旧账号内容。不建临时文件／outbox／持久任务或图片缓存。

## 3. 固定协议与凭据边界

| 阶段 | 固定请求 | 业务载荷／验证 |
| --- | --- | --- |
| CoverAllocate | Weapi `/api/nos/token/alloc`，Default／JSON64KiB／MUSIC_U | bucket yyimgs、ext jpg、filename cirrus-cover.jpg、local false、nos_product数字0、return_body `{"code":200,"size":"$(ObjectSize)"}`、type other；内部必须空参数 |
| NOS upload | HTTPS POST `nosup-hz1.127.net/yyimgs/<objectKey>?offset=0&complete=true&version=1.0` | 原始准备JPEG、Content-Type image/jpeg、x-nos-token；不是multipart，不带MUSIC_U／Cookie／Authorization／Referer |
| PlaylistCoverCommit | Weapi `/api/playlist/cover/update`，Default／JSON64KiB／MUSIC_U | 目标id、仅本次分配的coverImgId规范字符串；不接受数值／额外键／自由字段 |

alloc／commit走原SessionApiClient；Cookie只在原官方账号API请求，Set-Cookie忽略。固定NOS独立同线程manager，只允许封闭allocation和字节，不开放generic URL/header接口；禁自动Cookie读写、认证复用、缓存和redirect，不忽略TLS错误或改host重试。没有自定义代理入口。Qt网络默认系统环境及部署边界仍另验收。

allocation候选schema：code200及required root.result.objectKey／token／docId。objectKey仅1..512 ASCII `[A-Za-z0-9_./-]`，严格整串匹配，拒空segment、`.`／`..`、百分号、query／fragment／冒号／反斜线／控制符；token为1..4096可见无空白ASCII；docId为正规范signed64，JSON number只接受safe integer。不是SongId／PlaylistId、不是调用方凭证，不输出到Core。严禁根层／URL兜底、浮点舍入或自行生成ID。

NOS候选回执：HTTP200、严格有界JSON对象／合法code，成功200时**必需size规范字符串恰等于准备字节长度**，否则Unknown，不推进commit。它来源于固定return_body，不证明实际存储完整性／持久性／权限。正文≤16KiB、header≤64KiB，identity编码；15s transfer／20s总deadline本地限制。服务器Location、Cookie、URL及正文不进入Core或日志。

候选字段无真实响应证据，schema／token有效期／docId绑定／JPEG接受范围／服务效果尚未实证；不复制宽松fallback或无限上传／重试。

## 4. 阶段、取消与结果

CollectionResult新增optional CoverStep：Unobserved／Preparing／Allocating／Uploading／Committing，表示尝试／最后可观察阶段，**不证明请求已到服务器、提交、无副作用或进度百分比**。普通写不携带；Facade丢失阶段时Unobserved。只有合法原上下文Committing回执可以完成Acknowledged，上传或分配200不完成整个动作，不发布createdPlaylist或远端URL。

每个后续阶段前重新验证原账号／token／Authenticated；共享8槽与在途UUID去重，UUID不是服务幂等键。发送前cancel阻止后续，发送后cancel／timeout／切号保守Unknown；任何先前阶段已分发，不能把后续未分发失败称整个workflow NotSent。Rejected注明阶段，不保证对象未部分写入。Facade意图失效立即Unknown/Unobserved；Gateway仍保留原token、停止后续阶段和释放传输。

申请可能产生分配资源，NOS成功而commit未发／失败可能留下未绑定对象；**没有已证删除／撤销接口、回滚或孤儿清理承诺**。commit码200是Ack而非封面实际展示、权限／审核、CDN传播、长期效果或隐私证明。无盲重试、自动refresh／读核验／库更新／播放／报告、伪原子“图片+其他编辑”。不清空已有封面。

API client销毁停止在途NOS，不再提交；正常close先销毁gateway及其Uploader／manager、abort并销毁reply／timer，再继续原资源排空。停止是本机资源释放，不是服务端撤销。backend factory只在后台线程构造，不借测试seam绕过真实账号权限。

## 5. 验证与交接

Qt6.11.2 Debug，LC_ALL=C.UTF-8／QT_FATAL_WARNINGS=1。最新10套件：playlist_cover_writes57、playlist_tag_writes41、playlist_writes122、playlist_order59、collections189、session_api_client24、backend_facade8、http_transport22、response_decoder19、library21，共**562 pass**；9条选定线程数据行**11 pass**（含init/cleanup），0fail/skip。登记41→42套件、registry72→74（NOS是窄传输步骤、独立公钥链另计），不是全42／Release／真实业务验收；旧563＋11不累加。

新suite覆盖JPEG去元数据／1024／方形／坏结构／预算、2官方path／Weapi／7固定alloc参数／宽ID与严格键、31坏allocation行（路径控制／header token／wide numeric）、真实生产三阶段+NOS raw body/host/query/token且无账号Cookie、8 NOS失败行及final403／512／缺码／字符串200、4阶段发送前后cancel×logout共8行、Guest／MUSIC_U缺失、NOS期间API client销毁（此前分配已发／API id0仍Unknown/Uploading、原token/eventId与NOS取消排空，无续发commit）、8槽／UUID、非commit的提前Ack拒绝及close Unobserved；没有逐次Weapi独立解密声明，固定业务payload另独立断言。

线程：完整合成登录→alloc／NOS／commit一次Ack，第二次alloc／挂起NOS close→Unknown/Unobserved，共8请求，无final重放／分类／详情读取／播放／报告。JPEG由测试生成，无真实图片；原token／eventId、调用线程／同线程工厂与created=destroyed／wrongThread0；close不证明远端回滚。

15改动C++路径fresh all-server active LSP clean；真实14 application对象／6950 dependency entries，基础设施／JSON／非Core Qt／OpenSSL泄漏0、UI差异0。Gui只PRIVATE基础设施及图片测试，不拿链接声明代替真实依赖。首次fixture列表类型错误修为QStringList；首次恶意JPEG导致Qt警告／fatal中止，生产增加有界marker预检，坏fixture保留且fatal设置不改，最后全组通过。旧enum／QImage／PCH诊断由真实构建／正确Gui target／DB刷新／主动复查解决；必要包含、Q_OBJECT／moc不删，普通方法／factory误报按精确rule标记，无源码suppression。

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --target tst_playlist_cover_writes tst_playlist_tag_writes \
  tst_playlist_writes tst_playlist_order tst_collections tst_session_api_client tst_backend_facade \
  tst_http_transport tst_response_decoder tst_library tst_backend_runtime --parallel 4
cp build/linux-dev-local/compile_commands.json build/compile_commands.json
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(playlist_cover_writes|playlist_tag_writes|playlist_writes|playlist_order|collections|session_api_client|backend_facade|http_transport|response_decoder|library)$' -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  playlistCoverThroughThread playlistTagsThroughThread playlistWritesThroughThread playlistOrderThroughThread \
  collectionsThroughThread libraryThroughThread playbackThroughThread destructionStillDrains
```

ignored日志cover-final-{build,focused,thread}.log、cover-{boundaries,format,diff-check,fixed-hash,links,secrets}.log，以及修复记录cover-tests／cover-fix／cover-marker-fix-*；本机cache和凭据不交付。本地独立完整提交、不推送／发布／生成bundle；clang-format dry-run／git diff --check／fixed440通过，667个本地Markdown文件引用、missing0（不验证外链／anchor）；有限live-looking Cookie模式扫描0 findings，不当全面秘密保证。FR-011部分／域16／TC-PLAYLIST-COVER／R-025及索引同步，UI、格式扩展、真实schema／权限／效果／可靠核验／新真实访问均另确认。参见[基础写](playlist-management-writes.md)、[标签／单向公开](playlist-tags-and-publication-writes.md)及[后台生命周期](backend-runtime.md)。
