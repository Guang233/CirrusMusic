#pragma once

#include <QString>

#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwrContext;

namespace poc
{

// One file decoded to interleaved stereo float at its own sample rate. Start/end padding
// (MP3 encoder delay, LAME header) is trimmed by libavcodec from the demuxer's skip data.
class Decoder
{
  public:
    Decoder() = default;
    ~Decoder();
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    bool open(const QString& path);
    [[nodiscard]] int sampleRate() const { return m_rate; }
    [[nodiscard]] qint64 durationMs() const { return m_durationMs; }
    // Position of the next frame read() returns, in source frames.
    [[nodiscard]] qint64 position() const { return m_position; }
    // Appends up to about `frames` frames (2 floats each); false at the end.
    bool read(std::vector<float>& out, qsizetype frames);
    // Sample-exact: seeks a little earlier and drops frames up to `ms`.
    bool seek(qint64 ms);

  private:
    bool decodeSome();
    void convert(const AVFrame* frame);

    AVFormatContext* m_format = nullptr;
    AVCodecContext* m_codec = nullptr;
    SwrContext* m_convert = nullptr;
    AVPacket* m_packet = nullptr;
    AVFrame* m_frame = nullptr;
    int m_stream = -1;
    int m_rate = 0;
    qint64 m_durationMs = 0;
    qint64 m_position = 0;
    qint64 m_dropUntil = -1; // after a seek: source frame to drop up to
    std::vector<float> m_pending;
    bool m_inputDone = false;
    bool m_done = false;
};

} // namespace poc
