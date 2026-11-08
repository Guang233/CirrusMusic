# ADR-0008 · 自研音频引擎：FFmpeg 解码＋QAudioSink 回调输出

- 日期：2026-11-08
- 状态：Accepted（维护者 2026-11-08 评审设计并确认 PoC 听感与设备切换）
- 决策者／实施者：维护者／当前助手
- 关联需求／任务／风险：[音频引擎与音频缓存](../tasks/audio-engine.md)；[06 播放与音频系统](../06-playback-and-audio.md)
- 替代／被替代：替代 [ADR-0004](0004-playback-backend.md)

## 背景

维护者 2026-11-08 把播放质量定为产品第一优先：边下边播、持久音频缓存、预取下一首、无缝播放是上线必需，音量均衡也算；淡入淡出和均衡器上线后紧接着做。这触发了 ADR-0004 的复议条件（"gapless/DSP 被提升为硬需求"）。`QMediaPlayer` 一次只播一个来源、没有可修改的音频处理环节，满足不了无缝和音效；现有的"整首下载到临时文件再起播"（64 MiB／45 秒）起播慢、Hi-Res 易超限、不缓存。

## 决策

- 播放引擎自己实现：FFmpeg（libavformat/libavcodec/libavutil/libswresample）解码，自己的处理链（音量均衡、均衡器、淡入淡出、音量渐变、限幅），Qt 6.11 `QAudioSink` 的回调接口输出（Linux 原生 PipeWire）。
- 引擎实现现有的 `IPlaybackBackend`，新增预载下一首、`Advanced`（采样边界切到下一首）、`SourceExpired`/`refresh`（签名地址过期）和 `capabilities()`；令牌隔离、意图、反串歌等既有约定不变，媒体层仍看不到歌曲身份。
- 前面是按范围下载、边下边播、落盘缓存的音频存储，替换 `MediaBuffer`；每次播放仍先在线解析（缓存不是离线许可），默认 2 GB、按账号分区。
- FFmpeg 以 LGPL 构建动态链接：Linux 开发用系统库（pkg-config），Flatpak 用运行时的 FFmpeg，Windows/macOS 打包带 LGPL 共享库（未验证）。
- 不包含：离线下载（另行设计）、倍速与变速不变调、独占／位完美输出（架构不堵死，暂不做）。

## 备选方案

| 方案 | 收益 | 代价／风险 | 采用／拒绝理由 |
| --- | --- | --- | --- |
| `QMediaPlayer`＋缓存层 | 改动小 | 无缝、音效做不了 | 拒绝：不满足硬需求 |
| libmpv | 无缝、均衡器现成 | 时钟和事件不归我们；淡入淡出不行；构建、许可、打包重 | 拒绝，作为退路 |
| GStreamer | 能力全 | Windows/macOS 打包麻烦 | 拒绝 |
| 自研 FFmpeg＋`QAudioSink` | 全部能力可控，缓存直接接入 | 解码、时钟、设备、测试都要自己负责 | **采用** |

## 后果

- 正面：无缝、音效、缓存、时钟都在自己手里；音频播放不再加载 Qt 的 FFmpeg 媒体插件。
- 负面：代码量和测试量明显增加；设备与平台差异由我们处理；多一个 FFmpeg 依赖（与 Qt 自带那份 soname 不同，可共存）。
- 许可：只用 LGPL 构建的 FFmpeg，动态链接，不启用 GPL／nonfree 组件。
- 安全：FFmpeg 解析不可信的网络数据；只接受 MP3/FLAC（与现有白名单一致），下载仍走受控 CDN 规则；FFmpeg 安全更新跟随系统／运行时。

## 验证

阶段 A PoC（`tools/audio-poc/`，结果见 [audio-engine.md §6.1](../tasks/audio-engine.md)）：WAV/FLAC 衔接逐采样相同（44.1 kHz 与重采样到 48 kHz）；MP3 衔接与 ffmpeg 分别解码拼接逐采样相同；拖动误差 ≤ 1 帧；CPU < 0.2%；真实输出无欠载；维护者确认听感、暂停／拖动无爆音、插拔设备正常（2026-11-08）。之后各阶段的退出条件见 audio-engine.md §6。

## 复议条件

Windows/macOS 上 `QAudioSink` 回调或 FFmpeg 打包不可行；出现必须由系统框架处理的受保护音频来源；FFmpeg 许可或安全维护成本不可接受。触发时先评估 libmpv 退路，引擎接口（`IPlaybackBackend`）保持不变。

## 参考

- Qt 6.11.2 `qaudiosink.h`、`qtaudio.h`（`start(callback)`，本机安装，2026-11-08 查阅）。
- FFmpeg 8.x（系统 pkg-config 版本 libavcodec 63）。
