# ADR-0004 · Qt Multimedia 首选与可替换播放后端

- 日期：2026-09-26
- 状态：**Superseded**（2026-11-08 被 [ADR-0008](0008-own-audio-engine.md) 替代：维护者把无缝等音质能力定为硬需求，触发下文复议条件）
- 负责人：播放／维护者

## 背景

首要需求是稳定听歌、seek、队列和 Linux 设备集成。Qt Multimedia 集成成本低，但真实来源鉴权、格式、无缝和 DSP 不能仅靠 API 名称推断。

## 决策

默认以 QMediaPlayer + QAudioOutput 实现 IPlaybackBackend。官方来源解析在播放器之外；应用层管理权限、队列、用户意图、generation 和错误恢复。

M0 验证真实 HTTPS/CDN、headers、Range、过期 URL、格式、设备与安装包。无缝、crossfade、EQ、位完美输出不作为默认后端已具备能力。

## 当前落地（2026-10-02）

维护者明确批准Qt6::Multimedia依赖。已有[官方XEAPI音源、受控CDN缓冲、固定load身份和Qt单曲播放](../api/stream-resolution-and-playback.md)，公开30秒试听MP3及合成WAV静音PoC通过。

当前不让QMediaPlayer直接访问远程URL：先由无账号凭据的独立Qt Network manager校验TLS／每跳CDN并有界写入临时设备，再由独立媒体对象解码。最多64MiB／45秒，完整缓冲后起播，不宣称低延迟流式、离线缓存或所有高解析文件可播。MP3／FLAC为当前provider白名单，AAC／外部引用容器暂不开放。

队列／自动恢复／歌词时钟、完整音质／设备／打包验证仍未完成；本次结果不覆盖所有M0退出条件，因此保留Proposed和原复议门禁。

## 备选

- libmpv：真实协议或高级音频需要时优先评估，新增事件桥接和许可／打包成本。
- 自写 FFmpeg 解码＋输出：控制强但时钟、缓冲、设备和测试成本过高，首版不选。
- 自有 GStreamer 管线：保留专项选择，不同时维护多套完整实现。

## 后果

最小接口允许更换实现，但不能抽象抹平全部差异，必须发布 capabilities。媒体对象的旧信号与异步 URL 回包都要被 token 隔离；禁止只在收到事件时贴上最新 generation。

## 复议条件

P0 真实流不稳定、需要 Qt 无法安全提供的鉴权传输、关键 codec／设备不支持，或 gapless/DSP 被提升为硬需求。触发时先重做 PoC 和 ADR，不在 QML 页面旁路播放。

详见[播放系统](../06-playback-and-audio.md)。
