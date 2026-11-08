#include "chain.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <QDebug>

#include <algorithm>

namespace poc
{
namespace
{
constexpr qsizetype kChunkFrames = 4096;
}

Chain::Chain(int outRate) : m_outRate(outRate) {}

Chain::~Chain() { swr_free(&m_resampler); }

bool Chain::setTracks(const QStringList& paths, int first)
{
    m_paths = paths;
    return seek(first, 0);
}

bool Chain::openTrack(int track)
{
    auto decoder = std::make_unique<Decoder>();
    if (!decoder->open(m_paths.value(track))) {
        qWarning() << "cannot open" << m_paths.value(track);
        return false;
    }
    m_decoder = std::move(decoder);
    m_track = track;
    return true;
}

void Chain::resetResampler(int inRate)
{
    swr_free(&m_resampler);
    m_inRate = inRate;
    m_resampledIn = 0;
    if (inRate == m_outRate)
        return;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    swr_alloc_set_opts2(&m_resampler, &stereo, AV_SAMPLE_FMT_FLT, m_outRate, &stereo,
                        AV_SAMPLE_FMT_FLT, inRate, 0, nullptr);
    swr_init(m_resampler);
}

void Chain::resample(const std::vector<float>& in, bool flush)
{
    if (!m_resampler) {
        m_out.insert(m_out.end(), in.begin(), in.end());
        return;
    }
    const int inFrames = int(in.size() / 2);
    const int room = swr_get_out_samples(m_resampler, inFrames) + 64;
    const size_t at = m_out.size();
    m_out.resize(at + size_t(room) * 2);
    auto* out = reinterpret_cast<uint8_t*>(m_out.data() + at);
    const auto* src = reinterpret_cast<const uint8_t*>(in.data());
    const int made =
        swr_convert(m_resampler, &out, room, flush ? nullptr : &src, flush ? 0 : inFrames);
    m_out.resize(at + size_t(std::max(made, 0)) * 2);
}

bool Chain::seek(int track, qint64 ms)
{
    m_out.clear();
    m_produced = 0;
    m_boundaries.clear();
    m_finished = false;
    if (!openTrack(track))
        return false;
    if (ms > 0 && !m_decoder->seek(ms))
        return false;
    resetResampler(m_decoder->sampleRate());
    m_boundaries.push_back({0, track});
    return true;
}

qsizetype Chain::pull(std::vector<float>& out, qsizetype frames)
{
    while (qsizetype(m_out.size() / 2) < frames && !m_finished) {
        m_chunk.clear();
        if (m_decoder->read(m_chunk, kChunkFrames) && !m_chunk.empty()) {
            resample(m_chunk, false);
            continue;
        }
        if (!m_chunk.empty())
            resample(m_chunk, false);
        // The track ended: the next one follows in the same stream.
        const int next = m_track + 1;
        const bool more = next < m_paths.size() && openTrack(next);
        if (more && m_decoder->sampleRate() == m_inRate) {
            // Same rate: keep the resampler (its buffered input belongs before the joint).
            const qint64 delay = m_resampler ? swr_get_delay(m_resampler, m_outRate) : 0;
            m_boundaries.push_back({m_produced + qint64(m_out.size() / 2) + delay, next});
            continue;
        }
        resample({}, true); // flush the old rate's tail
        if (!more) {
            m_finished = true;
            break;
        }
        m_boundaries.push_back({m_produced + qint64(m_out.size() / 2), next});
        resetResampler(m_decoder->sampleRate());
    }
    const qsizetype take = std::min<qsizetype>(frames, qsizetype(m_out.size() / 2));
    out.insert(out.end(), m_out.begin(), m_out.begin() + take * 2);
    m_out.erase(m_out.begin(), m_out.begin() + take * 2);
    m_produced += take;
    return take;
}

} // namespace poc
