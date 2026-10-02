# 安装级匿名启动与重置

> 原生链路及定向离线回归已实现，尚未装配到产品入口，也未调用真实匿名注册／密钥环服务。匿名 token 不是已登录账号，不代表账号恢复已完成。

## 链路

`AnonymousBootstrap` 位于 `cirrus_bootstrap`，依赖已有 SessionApiClient、XEAPI key ring／registrar／vault 和 SecretStore；全部在同一所属线程使用，协作者必须比 bootstrap 活得更久。

1. 优先恢复安装级 XEAPI 公钥；缺失时使用既有公钥注册流程，无协议降级或自动重试。
2. 从系统密钥环读取 `anonymous-token`。JSON 记录含 format、deviceId、token；设备必须匹配。超过8 KiB、非法版本或无损 ASCII 校验失败的记录不使用。
3. 无可用记录时调用封闭 `AnonymousRegister` operation：XEAPI `/api/register/anonimous`（官方路径保留上游拼写），username 由既有固定设备 ID 算法构造。
4. 成功 HTTP、合法解密／JSON、业务200且合法 MUSIC_A 后，采用为安装级内存 token，并异步写密钥环。写失败发出 `persistFailed`，只保留内存，不降级明文文件。

匿名响应的 MUSIC_A 由可信 registry origin 提取，不写入账号 Cookie jar。同名冲突、非法域、过期或非可打印 token 被拒绝。普通账号 epoch 推进清除账号凭据／XEAPI会话，但不删除安装级匿名 token。

eapi／XEAPI 请求只在缺少 MUSIC_U 和目标 MUSIC_A 时采用匿名 fallback；它不覆盖已有账号凭据。此行为参考锁定实现，还需真实服务验证；不是对所有接口可匿名调用的保证。

## 上下文与重置

- 每次 start 捕获确切 SessionToken；异步恢复／注册采纳前复核客户端上下文，不能因回调到达时取得新 token 就把旧任务当新请求。
- reset 立即清空内存 token，取消在途匿名 API 请求，拒绝新 start，等待已有恢复／注册／写入回调结束后再删除 token 记录。不能先 delete 再让旧 write 落盘。
- 重复 reset 合并。只有删除回调完成后才释放单飞槽，并发出 `finished(Canceled)`；这表示重置流程结束，不表示成功启动。删除失败另发 `persistFailed`，同进程以后忽略旧记录并重新注册。
- 删除失败或在删除完成前退出进程，**不能保证跨进程擦除**。依赖没有超时／取消接口的 SecretStore 回调时，reset可能一直等待；系统后端长期无响应的超时策略尚待另行设计。
- 析构取消本对象的匿名 API 请求，不取消共享安装级公钥工作，也不宣称能撤销已经交给密钥环的写入。需要确保持久删除时，应保持对象和协作者存活到重置确认。
- 同一设备／client／store仅允许一个 bootstrap 编排 token；不应同时从其他路径独立 restore vault或改写同一个记录。这不是多实例存储仲裁器。
- 从密钥环恢复成功只说明记录有效且设备匹配，没有联网确认 token 当前是否仍可用；尚未实现撤销探测和自动续期。

## 本轮验证范围

按风险只执行受影响的 Debug 编译及3个套件：

```bash
cmake --build --preset linux-dev-local --target tst_bootstrap tst_session_api_client tst_xeapi --parallel 4
ctest --test-dir build/linux-dev-local -R '^(bootstrap|session_api_client|xeapi)$' -V --no-tests=error
```

结果：bootstrap **32 passed**，session_api_client **24 passed**，xeapi **23 passed**，无失败／跳过（包含Qt Test init/cleanup及数据行）。没有本轮完整 Debug／Release、QML或发布验证，不把旧完整测试报告自动视为此增量证据。

必要回归覆盖：新注册及恢复、其他设备／损坏记录、Guest fallback；重置发生在预分发、公钥恢复／注册、token读取、匿名注册和持久化写入阶段；删除失败后不恢复旧token、上下文过期不采纳、析构取消请求。JSON持久化断言按对象语义比较，不依赖字段输出顺序。

下一步：账号凭据持久化／恢复的独立契约，以及后台线程与非阻塞 facade装配。保持UI不变；真实验证另行授权。
