#include "mix.h"
#include <algorithm>
#include <numbers>

namespace cirrus::media::automix
{
namespace
{
constexpr double kBassHz = 200;    // what a bass cut takes away (24 dB an octave under it)
constexpr qint64 kSweepEvery = 32; // frames between the sweep's new cutoffs
} // namespace

void Mixer::HighPass::tune(double hz, int rate)
{
    hz = std::clamp(hz, 10.0, 0.45 * rate);
    const double q[2] = {0.54119610, 1.30656296};
    const double w0 = 2 * std::numbers::pi * hz / rate;
    const double cosine = std::cos(w0);
    for (int i = 0; i < 2; ++i) {
        const double alpha = std::sin(w0) / (2 * q[i]);
        const double a0 = 1 + alpha;
        for (int channel = 0; channel < 2; ++channel) {
            Biquad& b = sections[size_t(channel * 2 + i)];
            b.b0 = float((1 + cosine) / 2 / a0);
            b.b1 = float(-(1 + cosine) / a0);
            b.b2 = b.b0;
            b.a1 = float(-2 * cosine / a0);
            b.a2 = float((1 - alpha) / a0);
        }
    }
}

Mixer::Mixer(const Plan& plan, int rate) : m_plan(plan), m_rate(std::max(1, rate))
{
    m_delayFrames = std::max<qint64>(0, plan.inDelayMs) * m_rate / 1000;
    m_bassOut.tune(kBassHz, m_rate);
    m_bassIn.tune(kBassHz, m_rate);
    if (!plan.sweepOut.empty()) {
        m_sweepHz = plan.sweepOut.at(0);
        m_sweep.tune(m_sweepHz, m_rate);
    }
    if (plan.echoMs > 0) {
        const auto length = size_t(std::max<qint64>(1, qint64(plan.echoMs * m_rate / 1000)));
        m_echo[0].assign(length, 0.0f);
        m_echo[1].assign(length, 0.0f);
    }
}

qsizetype Mixer::silentIn(qsizetype frames) const
{
    return qsizetype(std::clamp<qint64>(m_delayFrames - m_offset, 0, frames));
}

void Mixer::mix(const float* out, const float* in, float* dst, qsizetype frames)
{
    const Plan& p = m_plan;
    const bool bassOut = !p.bassCutOut.empty(), bassIn = !p.bassCutIn.empty();
    const bool sweep = !p.sweepOut.empty(), echo = !m_echo[0].empty();
    for (qsizetype i = 0; i < frames; ++i) {
        const qint64 frame = m_offset + i;
        const double seconds = double(frame) / m_rate;
        if (sweep && frame % kSweepEvery == 0) {
            const double hz = p.sweepOut.at(seconds);
            if (hz != m_sweepHz) {
                m_sweepHz = hz;
                m_sweep.tune(hz, m_rate);
            }
        }
        const float a = p.gainOut.at(seconds), b = p.gainIn.at(seconds);
        const float cutOut = bassOut ? p.bassCutOut.at(seconds) : 0;
        const float cutIn = bassIn ? p.bassCutIn.at(seconds) : 0;
        const float send = echo ? p.echoSend.at(seconds) : 0;
        for (int c = 0; c < 2; ++c) {
            float x = out[i * 2 + c];
            if (bassOut) // the filter runs all along: no start-up transient when it comes in
                x += cutOut * (m_bassOut.run(x, c) - x);
            if (sweep)
                x = m_sweep.run(x, c);
            float wet = 0;
            if (echo) {
                float& slot = m_echo[size_t(c)][m_echoAt];
                wet = slot;
                slot = send * x + p.echoFeedback * wet;
            }
            float y = in[i * 2 + c];
            if (bassIn)
                y += cutIn * (m_bassIn.run(y, c) - y);
            dst[i * 2 + c] = softLimit(x * a + wet + y * b);
        }
        if (echo)
            m_echoAt = (m_echoAt + 1) % m_echo[0].size();
    }
    m_offset += frames;
}

} // namespace cirrus::media::automix
