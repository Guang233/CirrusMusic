#include "decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <QDebug>

#include <algorithm>

namespace poc
{
namespace
{
constexpr qint64 kPrerollMs = 300; // decode this much before a seek target
}

Decoder::~Decoder()
{
    swr_free(&m_convert);
    av_frame_free(&m_frame);
    av_packet_free(&m_packet);
    avcodec_free_context(&m_codec);
    avformat_close_input(&m_format);
}

bool Decoder::open(const QString& path)
{
    if (avformat_open_input(&m_format, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(m_format, nullptr) < 0)
        return false;
    const AVCodec* codec = nullptr;
    m_stream = av_find_best_stream(m_format, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (m_stream < 0 || !codec)
        return false;
    // Only the audio stream is read; cover art and others are dropped by the demuxer.
    for (unsigned i = 0; i < m_format->nb_streams; ++i)
        if (int(i) != m_stream)
            m_format->streams[i]->discard = AVDISCARD_ALL;
    m_codec = avcodec_alloc_context3(codec);
    if (avcodec_parameters_to_context(m_codec, m_format->streams[m_stream]->codecpar) < 0 ||
        avcodec_open2(m_codec, codec, nullptr) < 0)
        return false;
    m_rate = m_codec->sample_rate;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&m_convert, &stereo, AV_SAMPLE_FMT_FLT, m_rate, &m_codec->ch_layout,
                            m_codec->sample_fmt, m_rate, 0, nullptr) < 0 ||
        swr_init(m_convert) < 0)
        return false;
    m_packet = av_packet_alloc();
    m_frame = av_frame_alloc();
    const AVStream* stream = m_format->streams[m_stream];
    if (stream->duration != AV_NOPTS_VALUE)
        m_durationMs = av_rescale_q(stream->duration, stream->time_base, {1, 1000});
    else if (m_format->duration != AV_NOPTS_VALUE)
        m_durationMs = m_format->duration / 1000;
    return true;
}

void Decoder::convert(const AVFrame* frame)
{
    const int count = swr_get_out_samples(m_convert, frame->nb_samples);
    const size_t at = m_pending.size();
    m_pending.resize(at + size_t(count) * 2);
    auto* out = reinterpret_cast<uint8_t*>(m_pending.data() + at);
    const int made =
        swr_convert(m_convert, &out, count, const_cast<const uint8_t**>(frame->extended_data),
                    frame->nb_samples);
    m_pending.resize(at + size_t(std::max(made, 0)) * 2);
}

bool Decoder::decodeSome()
{
    while (true) {
        const int got = avcodec_receive_frame(m_codec, m_frame);
        if (got == 0) {
            if (m_dropUntil >= 0) {
                // The frame's first sample, in source frames from the stream's start.
                const AVStream* stream = m_format->streams[m_stream];
                const qint64 start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
                const qint64 pts = m_frame->best_effort_timestamp;
                if (pts != AV_NOPTS_VALUE) {
                    const qint64 first = av_rescale_q(pts - start, stream->time_base, {1, m_rate});
                    const qint64 skip = m_dropUntil - first;
                    if (skip >= m_frame->nb_samples) {
                        av_frame_unref(m_frame);
                        continue;
                    }
                    const size_t before = m_pending.size();
                    convert(m_frame);
                    if (skip > 0)
                        m_pending.erase(m_pending.begin() + qsizetype(before),
                                        m_pending.begin() + qsizetype(before + size_t(skip) * 2));
                    m_dropUntil = -1;
                    av_frame_unref(m_frame);
                    return true;
                }
            }
            convert(m_frame);
            av_frame_unref(m_frame);
            return true;
        }
        if (got == AVERROR_EOF) {
            m_done = true;
            return false;
        }
        if (got != AVERROR(EAGAIN))
            return false;
        if (m_inputDone)
            return false;
        const int read = av_read_frame(m_format, m_packet);
        if (read < 0) {
            m_inputDone = true;
            avcodec_send_packet(m_codec, nullptr); // drain
            continue;
        }
        if (m_packet->stream_index == m_stream)
            avcodec_send_packet(m_codec, m_packet);
        av_packet_unref(m_packet);
    }
}

bool Decoder::read(std::vector<float>& out, qsizetype frames)
{
    while (qsizetype(m_pending.size() / 2) < frames && !m_done) {
        if (!decodeSome())
            break;
    }
    const qsizetype take = std::min<qsizetype>(frames, qsizetype(m_pending.size() / 2));
    out.insert(out.end(), m_pending.begin(), m_pending.begin() + take * 2);
    m_pending.erase(m_pending.begin(), m_pending.begin() + take * 2);
    m_position += take;
    return take > 0 || !m_pending.empty() || !m_done;
}

bool Decoder::seek(qint64 ms)
{
    const AVStream* stream = m_format->streams[m_stream];
    const qint64 start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    const qint64 from = std::max<qint64>(0, ms - kPrerollMs);
    const qint64 ts = start + av_rescale_q(from, {1, 1000}, stream->time_base);
    if (av_seek_frame(m_format, m_stream, ts, AVSEEK_FLAG_BACKWARD) < 0)
        return false;
    avcodec_flush_buffers(m_codec);
    swr_init(m_convert); // drops what it buffered
    m_pending.clear();
    m_inputDone = false;
    m_done = false;
    m_dropUntil = av_rescale(ms, m_rate, 1000);
    m_position = m_dropUntil;
    return true;
}

} // namespace poc
