#include "deck.h"
#include "infrastructure/media/audio_decoder.h"
#include <algorithm>

namespace cirrus::media::automix
{
namespace
{
constexpr qsizetype kDecodeFrames = 2048;
// A normalization gain changing (another track, normalization switched) slides over this.
constexpr int kGainRampMs = 50;
// Frames given out before the buffer is compacted.
constexpr size_t kCompactFrames = 1 << 16;
} // namespace

Deck::Deck(std::unique_ptr<AudioDecoder> decoder, int outRate, float gain, bool normalize)
    : m_decoder(std::move(decoder)),
      m_resampler(std::make_unique<AudioResampler>(m_decoder->sampleRate(), outRate)),
      m_outRate(outRate), m_trackGain(gain), m_applied(normalize ? gain : 1.0f),
      m_rampTarget(m_applied)
{
}

Deck::~Deck() = default;

qsizetype Deck::pull(float* out, qsizetype frames, bool normalize)
{
    while (buffered() < frames && !m_decoderEnded && !m_finished) {
        m_chunk.clear();
        if (m_decoder->read(m_chunk, kDecodeFrames)) {
            m_resampler->process(m_chunk, m_buffer);
            continue;
        }
        if (m_decoder->error() || m_decoder->interrupted())
            break;
        m_decoderEnded = true;
    }
    const qsizetype take = std::min(frames, buffered());
    std::copy_n(m_buffer.data() + m_read * 2, size_t(take) * 2, out);
    m_read += size_t(take);
    if (m_read * 2 >= m_buffer.size()) {
        m_buffer.clear();
        m_read = 0;
    } else if (m_read >= kCompactFrames) {
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + qsizetype(m_read * 2));
        m_read = 0;
    }
    applyGain(out, take, normalize);
    m_framesOut += take;
    return take;
}

void Deck::applyGain(float* out, qsizetype frames, bool normalize)
{
    const float target = normalize ? m_trackGain : 1.0f;
    if (target != m_rampTarget) {
        m_rampTarget = target;
        m_rampStep = (target - m_applied) / float(std::max(1, m_outRate * kGainRampMs / 1000));
    }
    if (m_applied == 1.0f && m_rampTarget == 1.0f)
        return;
    for (qsizetype i = 0; i < frames; ++i) {
        if (m_applied != m_rampTarget) {
            m_applied += m_rampStep;
            if ((m_rampStep > 0) == (m_applied > m_rampTarget))
                m_applied = m_rampTarget;
        }
        out[i * 2] *= m_applied;
        out[i * 2 + 1] *= m_applied;
    }
}

qint64 Deck::follow(std::unique_ptr<AudioDecoder> next, float gain)
{
    // The same rate keeps the resampler running, so the joint stays continuous.
    if (next->sampleRate() != m_resampler->inRate()) {
        m_resampler->flush(m_buffer);
        m_resampler = std::make_unique<AudioResampler>(next->sampleRate(), m_outRate);
    }
    const qint64 joint = m_framesOut + buffered();
    m_decoder = std::move(next);
    m_decoderEnded = false;
    m_trackStartFrame = joint;
    m_trackStartMs = 0;
    m_trackGain = gain;
    return joint;
}

void Deck::finish()
{
    if (m_finished)
        return;
    m_resampler->flush(m_buffer);
    m_finished = true;
}

bool Deck::seek(qint64 ms)
{
    if (!m_decoder->seek(ms))
        return false;
    m_resampler = std::make_unique<AudioResampler>(m_decoder->sampleRate(), m_outRate);
    m_buffer.clear();
    m_read = 0;
    m_framesOut = 0;
    m_trackStartFrame = 0;
    m_trackStartMs = ms;
    m_decoderEnded = false;
    m_finished = false;
    return true;
}

void Deck::resume()
{
    if (m_decoder->interrupted())
        m_decoder->resume();
}

qint64 Deck::positionMs() const
{
    const qint64 frames = std::max<qint64>(0, m_framesOut - m_trackStartFrame);
    return m_trackStartMs + frames * 1000 / m_outRate;
}

} // namespace cirrus::media::automix
