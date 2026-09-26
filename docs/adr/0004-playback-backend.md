# ADR-0004 · Qt Multimedia 首选与可替换播放后端

- 日期：2026-09-26
- 状态：Proposed，M0 PoC 通过后才能采纳为正式后端
- 负责人：播放／维护者

## 背景

首要需求是稳定听歌、seek、队列和 Linux 设备集成。Qt Multimedia 集成成本低，但真实来源鉴权、格式、无缝和 DSP 不能仅靠 API 名称推断。

## 决策

默认以 QMediaPlayer + QAudioOutput 实现 IPlaybackBackend。官方来源解析在播放器之外；应用层管理权限、队列、用户意图、generation 和错误恢复。

M0 验证真实 HTTPS/CDN、headers、Range、过期 URL、格式、设备与安装包。无缝、crossfade、EQ、位完美输出不作为默认后端已具备能力。

## 备选

- libmpv：真实协议或高级音频需要时优先评估，新增事件桥接和许可／打包成本。
- 自写 FFmpeg 解码＋输出：控制强但时钟、缓冲、设备和测试成本过高，首版不选。
- 自有 GStreamer 管线：保留专项选择，不同时维护多套完整实现。

## 后果

最小接口允许更换实现，但不能抽象抹平全部差异，必须发布 capabilities。媒体对象的旧信号与异步 URL 回包都要被 token 隔离；禁止只在收到事件时贴上最新 generation。

## 复议条件

P0 真实流不稳定、需要 Qt 无法安全提供的鉴权传输、关键 codec／设备不支持，或 gapless/DSP 被提升为硬需求。触发时先重做 PoC 和 ADR，不在 QML 页面旁路播放。

详见[播放系统](../06-playback-and-audio.md)。
