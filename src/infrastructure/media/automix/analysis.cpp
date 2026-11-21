#include "analysis.h"
#include <QDataStream>
#include <QIODevice>
#include <QList>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace cirrus::media::automix
{
namespace
{
constexpr qint64 kStepMs = Analysis::kStepMs;
constexpr float kFloor = Analysis::kFloor;
constexpr qint64 kPeakMs = 10;
constexpr float kSilentPeak = 0.001f; // −60 dBFS: quieter than this is silence
constexpr qsizetype kBlockSteps = 4;  // 400 ms: momentary loudness, BS.1770's gating blocks
constexpr qsizetype kShortSteps = 30; // 3 s: short-term loudness
constexpr double kRelativeGate = 10;  // LU under the absolutely gated loudness
// A fade-out falls at least this far, over this long.
constexpr float kFadeDepth = 10;
constexpr qint64 kFadeMinimumMs = 2000;
constexpr qint64 kFadeMaximumMs = 30000;

double lufs(double meanSquare)
{
    return meanSquare > 0 ? -0.691 + 10 * std::log10(meanSquare) : double(kFloor) - 1;
}

double meanSquare(float loudness) { return std::pow(10.0, (double(loudness) + 0.691) / 10); }

quint8 quantize(double loudness)
{
    return quint8(std::clamp<long>(std::lround((loudness - kFloor) * 2), 0, 255));
}

double meanOf(const std::vector<double>& values, qsizetype from, qsizetype to)
{
    from = std::max<qsizetype>(0, from);
    to = std::min<qsizetype>(qsizetype(values.size()), to);
    if (to <= from)
        return 0;
    double sum = 0;
    for (qsizetype i = from; i < to; ++i)
        sum += values[size_t(i)];
    return sum / double(to - from);
}

// The short-term loudness (3 s) ending at `ms`.
float shortTerm(const std::vector<double>& energy, qint64 ms)
{
    const qsizetype end = qsizetype(ms / kStepMs);
    return float(std::max(lufs(meanOf(energy, end - kShortSteps, end)), double(kFloor)));
}

// Walks back from the last sound a second at a time while the loudness keeps rising (a
// little slack for the music's own swing), until it levels off at the track's usual
// level; the fade starts where it was loudest (within about half a second: the 3 s
// windows lag it).
qint64 findFade(const std::vector<double>& energy, const Analysis& a)
{
    const qint64 end = a.soundEndMs;
    float previous = shortTerm(energy, end);
    float top = previous;
    qint64 topAt = end;
    const qint64 limit = std::max(a.soundStartMs + kShortSteps * kStepMs, end - kFadeMaximumMs);
    for (qint64 at = end - 1000; at >= limit; at -= 1000) {
        const float level = shortTerm(energy, at);
        if (level < previous - 1)
            break; // quieter again further back: not the fade any more
        if (level > top) {
            top = level;
            topAt = at;
        }
        if (level >= a.loudness - 2 && level <= previous + 0.5f)
            break; // levelled off at the track's usual level: before the fade
        previous = level;
    }
    // The last moment still about that loud.
    qint64 start = topAt;
    for (qint64 at = std::min(end, topAt + 1000); at > topAt; at -= kStepMs) {
        if (shortTerm(energy, at) >= top - 0.25f) {
            start = at;
            break;
        }
    }
    const float bottom =
        float(std::max(lufs(meanOf(energy, (end - 400) / kStepMs, end / kStepMs)), double(kFloor)));
    if (top - bottom < kFadeDepth || end - start < kFadeMinimumMs)
        return -1;
    return start;
}
} // namespace

double Analyser::Biquad::run(double x, int channel)
{
    const double y = b0 * x + z1[channel];
    z1[channel] = b1 * x - a1 * y + z2[channel];
    z2[channel] = b2 * x - a2 * y;
    return y;
}

Analyser::Analyser(int rate) : m_rate(std::max(1, rate)), m_rhythm(m_rate)
{
    // BS.1770's two stages from their analog prototypes, for any rate (the standard's
    // 48 kHz coefficients come out of these): a high shelf for the head, a high-pass.
    const double fs = m_rate;
    {
        const double f0 = 1681.974450955533, gain = 3.999843853973347, q = 0.7071752369554196;
        const double k = std::tan(std::numbers::pi * f0 / fs);
        const double vh = std::pow(10.0, gain / 20);
        const double vb = std::pow(vh, 0.4996667741545416);
        const double a0 = 1 + k / q + k * k;
        m_shelf = {(vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0,
                   (vh - vb * k / q + k * k) / a0, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0};
    }
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = std::tan(std::numbers::pi * f0 / fs);
        const double a0 = 1 + k / q + k * k;
        m_highPass = {1, -2, 1, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0};
    }
    m_stepEnd = qint64(m_rate) * kStepMs / 1000;
    m_peakEnd = qint64(m_rate) * kPeakMs / 1000;
}

void Analyser::add(const float* frames, qsizetype count)
{
    m_rhythm.add(frames, count);
    for (qsizetype i = 0; i < count; ++i) {
        const float l = frames[i * 2], r = frames[i * 2 + 1];
        const double kl = m_highPass.run(m_shelf.run(l, 0), 0);
        const double kr = m_highPass.run(m_shelf.run(r, 1), 1);
        m_sum += kl * kl + kr * kr;
        ++m_stepFrames;
        m_peak = std::max({m_peak, std::fabs(l), std::fabs(r)});
        ++m_frames;
        if (m_frames == m_stepEnd)
            closeStep();
        if (m_frames == m_peakEnd)
            closePeak();
    }
}

void Analyser::closeStep()
{
    m_energy.push_back(m_sum / double(m_stepFrames));
    m_sum = 0;
    m_stepFrames = 0;
    m_stepEnd = qint64(m_energy.size() + 1) * m_rate * kStepMs / 1000;
}

void Analyser::closePeak()
{
    m_peaks.push_back(m_peak);
    m_peak = 0;
    m_peakEnd = qint64(m_peaks.size() + 1) * m_rate * kPeakMs / 1000;
}

Analysis Analyser::finish(bool complete) const
{
    std::vector<double> energy = m_energy;
    if (m_stepFrames > 0)
        energy.push_back(m_sum / double(m_stepFrames));
    std::vector<float> peaks = m_peaks;
    if (m_frames > qint64(peaks.size()) * m_rate * kPeakMs / 1000)
        peaks.push_back(m_peak);

    Analysis a;
    a.durationMs = durationMs();
    a.complete = complete;
    const auto steps = qsizetype(energy.size());
    a.momentary.resize(size_t(steps));
    for (qsizetype k = 0; k < steps; ++k)
        a.momentary[size_t(k)] = quantize(lufs(meanOf(energy, k, k + kBlockSteps)));

    // Integrated: the 400 ms blocks over the absolute gate, then over the relative one.
    std::vector<double> blocks;
    for (qsizetype k = 0; k + kBlockSteps <= steps; ++k)
        blocks.push_back(meanOf(energy, k, k + kBlockSteps));
    if (blocks.empty() && steps > 0)
        blocks.push_back(meanOf(energy, 0, steps)); // shorter than a block
    const auto gated = [&blocks](double gate) {
        double sum = 0;
        qsizetype count = 0;
        for (const double z : blocks) {
            if (lufs(z) > kFloor && lufs(z) > gate) {
                sum += z;
                ++count;
            }
        }
        return count ? lufs(sum / double(count)) : double(kFloor);
    };
    const double absolute = gated(kFloor);
    a.loudness = float(std::max(double(kFloor), gated(absolute - kRelativeGate)));

    const auto loud = [](float peak) { return peak >= kSilentPeak; };
    const auto first = std::find_if(peaks.begin(), peaks.end(), loud);
    a.soundStartMs = first == peaks.end() ? a.durationMs : qint64(first - peaks.begin()) * kPeakMs;
    const auto last = std::find_if(peaks.rbegin(), peaks.rend(), loud);
    a.soundEndMs = last == peaks.rend()
                       ? a.soundStartMs
                       : std::min(a.durationMs, qint64(peaks.rend() - last) * kPeakMs);
    if (complete && a.soundEndMs > a.soundStartMs)
        a.fadeStartMs = findFade(energy, a);
    a.rhythm = findRhythm(m_rhythm.features());
    const auto& features = m_rhythm.features();
    a.startKey =
        findKey(features, double(a.soundStartMs), double(a.soundStartMs + Analysis::kKeyMs));
    if (complete)
        a.endKey = findKey(features, double(a.soundEndMs - Analysis::kKeyMs), double(a.soundEndMs));
    return a;
}

float Analysis::momentaryAt(qint64 ms) const
{
    const qint64 index = ms / kStepMs;
    if (ms < 0 || index >= qint64(momentary.size()))
        return kFloor;
    return kFloor + float(momentary[size_t(index)]) / 2;
}

float Analysis::loudnessOver(qint64 fromMs, qint64 toMs) const
{
    double sum = 0;
    qsizetype count = 0;
    for (qint64 at = std::max<qint64>(0, fromMs); at < toMs; at += kStepMs) {
        const float level = momentaryAt(at);
        sum += level > kFloor ? meanSquare(level) : 0;
        ++count;
    }
    return count ? float(std::max(lufs(sum / double(count)), double(kFloor))) : kFloor;
}

qint64 Analysis::fadedBy(float db) const
{
    if (fadeStartMs < 0)
        return -1;
    const float from = loudnessOver(fadeStartMs - 3000, fadeStartMs);
    for (qint64 at = fadeStartMs; at < soundEndMs; at += kStepMs)
        if (momentaryAt(at) <= from - db)
            return at;
    return soundEndMs;
}

QByteArray Analysis::serialize() const
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_5);
    out << qint32(kVersion) << durationMs << complete << loudness << soundStartMs << soundEndMs
        << fadeStartMs
        << QByteArray(reinterpret_cast<const char*>(momentary.data()), qsizetype(momentary.size()))
        << rhythm.bpm << rhythm.strength << qint32(rhythm.downbeat) << rhythm.barConfidence
        << rhythm.steady << QList<float>(rhythm.beats.begin(), rhythm.beats.end())
        << QList<float>(rhythm.bass.begin(), rhythm.bass.end())
        << QList<float>(rhythm.change.begin(), rhythm.change.end())
        << QList<float>(rhythm.accent.begin(), rhythm.accent.end());
    for (const Key& key : {startKey, endKey})
        out << qint32(key.tonic) << key.minor << key.confidence;
    return bytes;
}

std::optional<Analysis> Analysis::deserialize(const QByteArray& bytes)
{
    QDataStream in(bytes);
    in.setVersion(QDataStream::Qt_6_5);
    qint32 version = 0;
    in >> version;
    if (version != kVersion)
        return std::nullopt;
    Analysis a;
    QByteArray curve;
    qint32 downbeat = 0;
    QList<float> beats, bass, change, accent;
    in >> a.durationMs >> a.complete >> a.loudness >> a.soundStartMs >> a.soundEndMs >>
        a.fadeStartMs >> curve >> a.rhythm.bpm >> a.rhythm.strength >> downbeat >>
        a.rhythm.barConfidence >> a.rhythm.steady >> beats >> bass >> change >> accent;
    bool keysValid = true;
    for (Key* key : {&a.startKey, &a.endKey}) {
        qint32 tonic = -1;
        in >> tonic >> key->minor >> key->confidence;
        key->tonic = tonic;
        keysValid &= tonic >= -1 && tonic < 12;
    }
    if (in.status() != QDataStream::Ok || !in.atEnd() || !keysValid || downbeat < 0 ||
        downbeat > 3 || !std::is_sorted(beats.begin(), beats.end()) ||
        bass.size() != beats.size() || change.size() != beats.size() ||
        accent.size() != beats.size())
        return std::nullopt;
    a.momentary.assign(curve.begin(), curve.end());
    a.rhythm.downbeat = downbeat;
    a.rhythm.beats.assign(beats.begin(), beats.end());
    a.rhythm.bass.assign(bass.begin(), bass.end());
    a.rhythm.change.assign(change.begin(), change.end());
    a.rhythm.accent.assign(accent.begin(), accent.end());
    return a;
}

} // namespace cirrus::media::automix
