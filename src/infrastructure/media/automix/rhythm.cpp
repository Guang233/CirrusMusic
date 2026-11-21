#include "rhythm.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>

namespace cirrus::media::automix
{
namespace
{
constexpr double kTargetRate = 11025;
constexpr float kFloorDb = -80; // quieter is silence: no onsets from its noise
constexpr double kLowestHz = 30;
constexpr double kBassHz = 150;
constexpr double kSnareFromHz = 1000, kSnareToHz = 4000;
constexpr double kChromaFromHz = 100, kChromaToHz = 2000;
constexpr int kBands = 32;
// Tempo: 50–220 BPM, a log-normal prior over it around 120 (one octave wide).
constexpr double kSlowestBpm = 50, kFastestBpm = 220, kPriorBpm = 120, kPriorOctaves = 1;
// How strictly beats keep to the period (Ellis 2007: higher is stricter).
constexpr double kTightness = 100;
constexpr qsizetype kShortestSeconds = 8; // less: no rhythm found
constexpr double kOnsetLag = 0.165;       // of the onset window
constexpr double kQuietestOnsets = 0.05;  // dB a band and frame, on average: less is none
// A beat this close to a grid is on it; a song with this share of its beats on one grid
// (and them this close on average) was made to a click.
constexpr double kOnGridMs = 20;
constexpr double kSteadyShare = 0.8;
constexpr double kSteadyRmsMs = 12;
// The tracker can slip onto the offbeats for a while (syncopation pulls it there): then the
// beats are still on the click, at a half or a quarter of the beat. Stricter, as a finer
// grid catches more beats by chance (beats strewn over it: an rms of ~11.5 ms).
constexpr int kSubdivisions[] = {2, 4};
constexpr double kFinestGridMs = 100;
constexpr double kSlippedShare = 0.85;
constexpr double kSlippedRmsMs = 8;
// Grid periods tried: ±3 % of the beats' median interval, in 0.02 % steps.
constexpr int kPeriodSteps = 150;
constexpr double kPeriodStep = 0.0002;

// Beats [first, end) fitted to a constant grid: beat i at phase + period · index[i].
struct Fit {
    double period = 0, phase = 0;
    std::vector<qint64> index;
    std::vector<double> residuals;
    double rms = 0, inliers = 0;
    int count = 0; // 0: too few to fit
};
// At a grid of their median interval over `divisions`.
Fit fitBeats(const std::vector<float>& beats, qsizetype first, qsizetype end, int divisions = 1);

float decibels(double power)
{
    return float(std::max(10 * std::log10(power + 1e-12), double(kFloorDb)));
}
double mel(double hz) { return 2595 * std::log10(1 + hz / 700); }
double hzOfMel(double m) { return 700 * (std::pow(10.0, m / 2595) - 1); }
int powerOfTwoNear(double n)
{
    int p = 16;
    while (p * 1.5 < n)
        p *= 2;
    return p;
}
double square(double x) { return x * x; }

// Where the peak between three equally spaced values is, from the middle one (−0.5–0.5).
double vertex(double before, double at, double after)
{
    const double curve = before - 2 * at + after;
    if (curve >= 0)
        return 0;
    return std::clamp(0.5 * (before - after) / curve, -0.5, 0.5);
}

// Values less their mean, divided by their spread (all 0 without one).
void standardize(std::vector<double>& values)
{
    if (values.empty())
        return;
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) / double(values.size());
    double spread = 0;
    for (const double v : values)
        spread += square(v - mean);
    spread = std::sqrt(spread / double(values.size()));
    for (double& v : values)
        v = spread > 0 ? (v - mean) / spread : 0;
}
} // namespace

float RhythmFrontEnd::Biquad::run(float x)
{
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}

RhythmFrontEnd::RhythmFrontEnd(int rate)
    : m_factor(std::max(1, int(rate / kTargetRate))), m_rate(double(std::max(1, rate)) / m_factor),
      m_size(powerOfTwoNear(0.046 * m_rate)), m_hop(m_size / 4), m_chromaHop(m_size * 2),
      m_spectrum(m_size), m_chromaSpectrum(m_size * 4)
{
    // Against aliasing: a 4th-order Butterworth low-pass at 0.4 of the new rate.
    if (m_factor > 1) {
        const double q[2] = {0.54119610, 1.30656296};
        const double w0 = 2 * std::numbers::pi * 0.4 * m_rate / std::max(1, rate);
        for (int i = 0; i < 2; ++i) {
            const double alpha = std::sin(w0) / (2 * q[i]);
            const double cosine = std::cos(w0);
            const double a0 = 1 + alpha;
            m_lowPass[i] = {float((1 - cosine) / 2 / a0), float((1 - cosine) / a0),
                            float((1 - cosine) / 2 / a0), float(-2 * cosine / a0),
                            float((1 - alpha) / a0)};
        }
    }
    const double binHz = m_rate / m_size;
    const int bins = m_spectrum.bins();
    // Mel bands from 30 Hz to near the new Nyquist frequency, triangles over the bins.
    const double low = mel(kLowestHz), high = mel(0.45 * m_rate);
    for (int b = 0; b < kBands; ++b) {
        const double lo = hzOfMel(low + (high - low) * b / (kBands + 1));
        const double mid = hzOfMel(low + (high - low) * (b + 1) / (kBands + 1));
        const double hi = hzOfMel(low + (high - low) * (b + 2) / (kBands + 1));
        Band band;
        band.hz = float(mid);
        std::vector<std::pair<int, float>> taken;
        for (int k = 1; k < bins; ++k) {
            const double hz = k * binHz;
            if (hz <= lo || hz >= hi)
                continue;
            taken.push_back({k, float(hz < mid ? (hz - lo) / (mid - lo) : (hi - hz) / (hi - mid))});
        }
        if (taken.empty()) // narrower than a bin: the nearest one
            taken.push_back({std::clamp(int(std::lround(mid / binHz)), 1, bins - 1), 1.0f});
        float sum = 0;
        for (const auto& t : taken)
            sum += t.second;
        band.from = taken.front().first;
        band.weights.assign(size_t(taken.back().first - band.from + 1), 0.0f);
        for (const auto& t : taken)
            band.weights[size_t(t.first - band.from)] = t.second / sum;
        m_bands.push_back(std::move(band));
    }
    m_kickFrom = std::max(1, int(std::ceil(kLowestHz / binHz)));
    m_kickTo = std::max(m_kickFrom + 1, int(kBassHz / binHz) + 1);
    // Each bin's share of the two pitch classes around it (MIDI pitch: C is 0, mod 12).
    const double chromaHz = m_rate / m_chromaSpectrum.size();
    const int chromaFrom = std::max(1, int(std::ceil(kChromaFromHz / chromaHz)));
    const int chromaTo = std::min(m_chromaSpectrum.bins(), int(kChromaToHz / chromaHz) + 1);
    for (int k = chromaFrom; k < chromaTo; ++k) {
        const double pitch = 12 * std::log2(k * chromaHz / 440) + 69;
        const double lower = std::floor(pitch);
        m_chromaBins.push_back({k, int(lower) % 12, float(pitch - lower)});
    }
    m_power.resize(size_t(bins));
    m_chromaPower.resize(size_t(m_chromaSpectrum.bins()));
    m_previous.assign(m_bands.size(), kFloorDb);
    m_features.frameMs = 1000.0 * m_hop / m_rate;
    // An onset frame peaks a little after the attack reaches its window's centre (measured
    // on synthetic drums: about a sixth of the window).
    m_features.originMs = 1000.0 * (0.5 + kOnsetLag) * m_size / m_rate;
    m_features.chromaFrameMs = 1000.0 * m_chromaHop / m_rate;
    m_features.chromaOriginMs = 1000.0 * (m_chromaSpectrum.size() / 2) / m_rate;
}

void RhythmFrontEnd::add(const float* stereo, qsizetype frames)
{
    for (qsizetype i = 0; i < frames; ++i) {
        float x = 0.5f * (stereo[i * 2] + stereo[i * 2 + 1]);
        if (m_factor > 1)
            x = m_lowPass[1].run(m_lowPass[0].run(x));
        if (m_phase == 0) {
            m_signal.push_back(x);
            ++m_total;
        }
        m_phase = (m_phase + 1) % m_factor;
    }
    while (m_nextOnset + m_size <= m_total) {
        onsetFrame(m_signal.data() + (m_nextOnset - m_start));
        m_nextOnset += m_hop;
    }
    const int chromaSize = m_chromaSpectrum.size();
    while (m_nextChroma + chromaSize <= m_total) {
        chromaFrame(m_signal.data() + (m_nextChroma - m_start));
        m_nextChroma += m_chromaHop;
    }
    // What no frame needs any more goes, now and then.
    const qint64 needed = std::min(m_nextOnset, m_nextChroma);
    if (needed - m_start > (1 << 16)) {
        m_signal.erase(m_signal.begin(), m_signal.begin() + (needed - m_start));
        m_start = needed;
    }
}

void RhythmFrontEnd::onsetFrame(const float* samples)
{
    m_spectrum.power(samples, m_power.data());
    float flux = 0, snare = 0;
    int snareBands = 0;
    for (size_t b = 0; b < m_bands.size(); ++b) {
        const Band& band = m_bands[b];
        double power = 0;
        for (size_t k = 0; k < band.weights.size(); ++k)
            power += double(band.weights[k]) * m_power[size_t(band.from) + k];
        const float level = decibels(power);
        const float rise = m_first ? 0.0f : std::max(0.0f, level - m_previous[b]);
        flux += rise;
        if (band.hz >= kSnareFromHz && band.hz <= kSnareToHz) {
            snare += rise;
            ++snareBands;
        }
        m_previous[b] = level;
    }
    double bassPower = 0;
    for (int k = m_kickFrom; k < m_kickTo; ++k)
        bassPower += m_power[size_t(k)];
    const float bass = decibels(bassPower);
    m_features.onset.push_back(flux / float(m_bands.size()));
    m_features.snare.push_back(snareBands ? snare / float(snareBands) : 0.0f);
    m_features.kick.push_back(m_first ? 0.0f : std::max(0.0f, bass - m_previousKick));
    m_features.bass.push_back(bass);
    m_previousKick = bass;
    m_first = false;
}

void RhythmFrontEnd::chromaFrame(const float* samples)
{
    m_chromaSpectrum.power(samples, m_chromaPower.data());
    std::array<float, 12> classes{};
    for (const ChromaBin& bin : m_chromaBins) {
        const float magnitude = std::sqrt(m_chromaPower[size_t(bin.bin)]);
        classes[size_t(bin.lower)] += magnitude * (1 - bin.fraction);
        classes[size_t((bin.lower + 1) % 12)] += magnitude * bin.fraction;
    }
    m_features.chroma.push_back(classes);
}

Rhythm findRhythm(const RhythmFeatures& f, bool toGrid)
{
    Rhythm rhythm;
    const auto n = qsizetype(f.onset.size());
    const double frameSeconds = f.frameMs / 1000;
    if (frameSeconds <= 0 || double(n) * frameSeconds < kShortestSeconds)
        return rhythm;
    // Next to nothing ever starts (a held tone, a drone): no beat to find.
    if (std::accumulate(f.onset.begin(), f.onset.end(), 0.0) / double(n) < kQuietestOnsets)
        return rhythm;

    // The envelope less its local mean (over a second), never below 0.
    std::vector<double> envelope(static_cast<size_t>(n));
    {
        std::vector<double> sums(size_t(n) + 1, 0.0);
        for (qsizetype t = 0; t < n; ++t)
            sums[size_t(t) + 1] = sums[size_t(t)] + f.onset[size_t(t)];
        const auto half = qsizetype(std::lround(0.5 / frameSeconds));
        for (qsizetype t = 0; t < n; ++t) {
            const qsizetype from = std::max<qsizetype>(0, t - half);
            const qsizetype to = std::min(n, t + half + 1);
            const double mean = (sums[size_t(to)] - sums[size_t(from)]) / double(to - from);
            envelope[size_t(t)] = std::max(0.0, f.onset[size_t(t)] - mean);
        }
    }

    // Tempo: the autocorrelation of the (zero-mean) envelope, weighted by the prior.
    const int shortest = std::max(1, int(std::floor(60 / kFastestBpm / frameSeconds)));
    const int longest = int(std::ceil(60 / kSlowestBpm / frameSeconds));
    if (longest + 2 >= n)
        return rhythm;
    const double mean =
        std::accumulate(envelope.begin(), envelope.end(), 0.0) / double(envelope.size());
    std::vector<double> acf(size_t(longest) + 2, 0.0);
    for (int lag = 0; lag <= longest + 1; ++lag) {
        double sum = 0;
        for (qsizetype t = 0; t + lag < n; ++t)
            sum += (envelope[size_t(t)] - mean) * (envelope[size_t(t + lag)] - mean);
        acf[size_t(lag)] = sum / double(n - lag);
    }
    const auto prior = [&](double lag) {
        return std::exp(-0.5 *
                        square(std::log2(lag * frameSeconds * kPriorBpm / 60) / kPriorOctaves));
    };
    int best = shortest;
    for (int lag = shortest; lag <= longest; ++lag)
        if (acf[size_t(lag)] * prior(lag) > acf[size_t(best)] * prior(best))
            best = lag;
    if (acf[0] <= 0 || acf[size_t(best)] <= 0)
        return rhythm;
    const double period =
        best + vertex(acf[size_t(best) - 1] * prior(best - 1), acf[size_t(best)] * prior(best),
                      acf[size_t(best) + 1] * prior(best + 1));
    rhythm.bpm = float(60 / (period * frameSeconds));
    rhythm.strength = float(std::clamp(acf[size_t(best)] / acf[0], 0.0, 1.0));

    // The local score: the envelope smoothed over a 32nd of a beat, in its spreads.
    std::vector<double> local(size_t(n), 0.0);
    {
        const double sigma = std::max(0.5, period / 32);
        const int radius = int(std::ceil(3 * sigma));
        std::vector<double> kernel;
        for (int i = -radius; i <= radius; ++i)
            kernel.push_back(std::exp(-0.5 * square(i / sigma)));
        for (qsizetype t = 0; t < n; ++t) {
            double sum = 0;
            for (int i = -radius; i <= radius; ++i)
                if (t + i >= 0 && t + i < n)
                    sum += kernel[size_t(i + radius)] * envelope[size_t(t + i)];
            local[size_t(t)] = sum;
        }
        double energy = 0;
        for (const double v : local)
            energy += v * v;
        const double spread = std::sqrt(energy / double(n));
        if (spread <= 0)
            return rhythm;
        for (double& v : local)
            v /= spread;
    }

    // Beats: the best path of beats about a period apart through the local score.
    std::vector<double> score(size_t(n), 0.0);
    std::vector<qsizetype> back(size_t(n), -1);
    const double top = *std::max_element(local.begin(), local.end());
    const auto far = qsizetype(std::lround(2 * period));
    const auto near = std::max<qsizetype>(1, qsizetype(std::lround(period / 2)));
    bool started = false;
    for (qsizetype t = 0; t < n; ++t) {
        double bestScore = -std::numeric_limits<double>::infinity();
        qsizetype from = -1;
        for (qsizetype prev = std::max<qsizetype>(0, t - far); prev <= t - near; ++prev) {
            const double s =
                score[size_t(prev)] - kTightness * square(std::log(double(t - prev) / period));
            if (s > bestScore) {
                bestScore = s;
                from = prev;
            }
        }
        score[size_t(t)] = local[size_t(t)] + (from >= 0 ? bestScore : 0.0);
        if (!started && local[size_t(t)] < 0.01 * top) {
            back[size_t(t)] = -1; // nothing yet to start from
        } else {
            back[size_t(t)] = from;
            started = true;
        }
    }
    // The last beat: the last local peak of the score over half their median.
    std::vector<qsizetype> peaks;
    for (qsizetype t = 1; t + 1 < n; ++t)
        if (score[size_t(t)] > score[size_t(t - 1)] && score[size_t(t)] >= score[size_t(t + 1)])
            peaks.push_back(t);
    if (peaks.empty())
        return rhythm;
    std::vector<double> peakScores;
    for (const auto t : peaks)
        peakScores.push_back(score[size_t(t)]);
    std::nth_element(peakScores.begin(), peakScores.begin() + qsizetype(peakScores.size() / 2),
                     peakScores.end());
    const double median = peakScores[peakScores.size() / 2];
    qsizetype last = peaks.back();
    for (auto it = peaks.rbegin(); it != peaks.rend(); ++it)
        if (score[size_t(*it)] > 0.5 * median) {
            last = *it;
            break;
        }
    std::vector<qsizetype> frames;
    for (qsizetype t = last; t >= 0; t = back[size_t(t)])
        frames.push_back(t);
    std::reverse(frames.begin(), frames.end());
    // Weak beats at either end go (silence, a quiet intro without a beat).
    {
        const double hann[5] = {0, 0.5, 1, 0.5, 0};
        const auto count = qsizetype(frames.size());
        std::vector<double> smooth(frames.size(), 0.0);
        for (qsizetype i = 0; i < count; ++i)
            for (int j = -2; j <= 2; ++j)
                if (const qsizetype k = i + j; k >= 0 && k < count)
                    smooth[size_t(i)] += hann[j + 2] * local[size_t(frames[size_t(k)])];
        double energy = 0;
        for (const double v : smooth)
            energy += v * v;
        const double threshold = 0.5 * std::sqrt(energy / double(std::max<qsizetype>(1, count)));
        size_t from = 0, to = frames.size();
        while (from < to && smooth[from] <= threshold)
            ++from;
        while (to > from && smooth[to - 1] <= threshold)
            --to;
        frames = std::vector<qsizetype>(frames.begin() + qsizetype(from),
                                        frames.begin() + qsizetype(to));
    }
    if (frames.size() < 8)
        return rhythm;

    // To the peak of the local score near each (a frame is 11.6 ms), in ms.
    for (const qsizetype t : frames) {
        qsizetype at = t;
        if (t > 0 && local[size_t(t - 1)] > local[size_t(at)])
            at = t - 1;
        if (t + 1 < n && local[size_t(t + 1)] > local[size_t(at)])
            at = t + 1;
        double offset = 0;
        if (at > 0 && at + 1 < n)
            offset = vertex(local[size_t(at - 1)], local[size_t(at)], local[size_t(at + 1)]);
        rhythm.beats.push_back(float(f.originMs + (double(at) + offset) * f.frameMs));
    }
    const auto frameOf = [&](double ms) {
        return std::clamp<qsizetype>(qsizetype(std::lround((ms - f.originMs) / f.frameMs)), 0,
                                     n - 1);
    };
    // A song made to a click (most produced music): its beats are that grid, also through
    // a breakdown where the onsets misled the tracker. Only between the first and last beat
    // on it: a free intro or ending keeps its own.
    if (toGrid) {
        const auto count = qsizetype(rhythm.beats.size());
        Fit whole = fitBeats(rhythm.beats, 0, count);
        int divisions = 1;
        if (whole.count == 0 || whole.inliers < kSteadyShare || whole.rms > kSteadyRmsMs) {
            whole = {};
            for (const int d : kSubdivisions) {
                Fit fine = fitBeats(rhythm.beats, 0, count, d);
                if (fine.count > 0 && fine.period >= kFinestGridMs &&
                    fine.inliers >= kSlippedShare && fine.rms <= kSlippedRmsMs) {
                    whole = std::move(fine);
                    divisions = d;
                    break;
                }
            }
        }
        if (whole.count > 0) {
            qint64 firstOn = -1, lastOn = -1;
            for (qsizetype i = 0; i < count; ++i) {
                if (std::fabs(whole.residuals[size_t(i)]) <= kOnGridMs) {
                    if (firstOn < 0)
                        firstOn = whole.index[size_t(i)];
                    lastOn = whole.index[size_t(i)];
                }
            }
            // On a finer grid, the beat is its phase with the most kicks and onsets on it
            // (each a share of all): kicks are on the beat in nearly all styles, onsets
            // decide where kicks don't (a fast punk beat has as many onsets off the beat).
            const auto offsetOf = [divisions](qint64 k) {
                return int(((k % divisions) + divisions) % divisions);
            };
            std::vector<double> kicks(size_t(divisions), 0.0), onsets(size_t(divisions), 0.0);
            for (qint64 k = firstOn; k <= lastOn; ++k) {
                const qsizetype at = frameOf(whole.phase + whole.period * double(k));
                float kick = 0;
                for (qsizetype t = std::max<qsizetype>(0, at - 1); t <= std::min(n - 1, at + 1);
                     ++t)
                    kick = std::max(kick, f.kick[size_t(t)]);
                kicks[size_t(offsetOf(k))] += kick;
                onsets[size_t(offsetOf(k))] += local[size_t(at)];
            }
            const double allKicks = std::accumulate(kicks.begin(), kicks.end(), 0.0);
            const double allOnsets = std::accumulate(onsets.begin(), onsets.end(), 0.0);
            std::vector<double> votes(size_t(divisions), 0.0);
            for (size_t o = 0; o < votes.size(); ++o)
                votes[o] = (allKicks > 0 ? kicks[o] / allKicks : 0) +
                           (allOnsets > 0 ? onsets[o] / allOnsets : 0);
            const int beat = int(std::max_element(votes.begin(), votes.end()) - votes.begin());
            while (offsetOf(firstOn) != beat)
                ++firstOn;
            std::vector<float> grid;
            for (qint64 k = firstOn; k <= lastOn; k += divisions)
                grid.push_back(float(whole.phase + whole.period * double(k)));
            if (grid.size() >= 8) {
                rhythm.beats = std::move(grid);
                rhythm.steady = true;
            }
        }
    }
    const auto beats = qsizetype(rhythm.beats.size());

    // The bass in each beat.
    for (qsizetype i = 0; i < beats; ++i) {
        const qsizetype from = frameOf(rhythm.beats[size_t(i)]);
        const qsizetype to = i + 1 < beats ? frameOf(rhythm.beats[size_t(i) + 1])
                                           : std::min(n, from + qsizetype(period));
        double sum = 0;
        for (qsizetype t = from; t < std::max(from + 1, to); ++t)
            sum += f.bass[size_t(std::min(t, n - 1))];
        rhythm.bass.push_back(float(sum / double(std::max<qsizetype>(1, to - from))));
    }

    // What says where bars start (4/4): chords change there (the pitch classes of the two
    // beats after against the two before), kicks fall on 1 and 3, snares and claps on 2
    // and 4 (the kick less the snare).
    std::vector<std::array<double, 12>> chroma(static_cast<size_t>(beats));
    if (!f.chroma.empty() && f.chromaFrameMs > 0) {
        const auto chromaFrames = qsizetype(f.chroma.size());
        for (qsizetype i = 0; i < beats; ++i) {
            const double start = rhythm.beats[size_t(i)];
            const double end =
                i + 1 < beats ? rhythm.beats[size_t(i) + 1] : start + period * f.frameMs;
            auto from = qsizetype(std::ceil((start - f.chromaOriginMs) / f.chromaFrameMs));
            auto to = qsizetype(std::ceil((end - f.chromaOriginMs) / f.chromaFrameMs));
            from = std::clamp<qsizetype>(from, 0, chromaFrames - 1);
            to = std::clamp<qsizetype>(to, from + 1, chromaFrames);
            std::array<double, 12> sum{};
            for (qsizetype u = from; u < to; ++u)
                for (size_t c = 0; c < 12; ++c)
                    sum[c] += f.chroma[size_t(u)][c];
            chroma[size_t(i)] = sum;
        }
    }
    const auto normalized = [](std::array<double, 12> v) {
        double length = 0;
        for (const double x : v)
            length += x * x;
        length = std::sqrt(length);
        for (double& x : v)
            x = length > 0 ? x / length : 0;
        return v;
    };
    std::vector<double> change, kick, snare;
    const auto window = std::max<qsizetype>(1, qsizetype(std::lround(period / 8)));
    for (qsizetype i = 0; i < beats; ++i) {
        if (i >= 2 && i + 2 <= beats) {
            std::array<double, 12> before{}, after{};
            for (size_t c = 0; c < 12; ++c) {
                before[c] = chroma[size_t(i) - 2][c] + chroma[size_t(i) - 1][c];
                after[c] = chroma[size_t(i)][c] + chroma[size_t(i) + 1][c];
            }
            before = normalized(before);
            after = normalized(after);
            double dot = 0;
            for (size_t c = 0; c < 12; ++c)
                dot += before[c] * after[c];
            change.push_back(1 - dot);
        } else {
            change.push_back(0);
        }
        const qsizetype at = frameOf(rhythm.beats[size_t(i)]);
        float k = 0, sn = 0;
        for (qsizetype t = std::max<qsizetype>(0, at - window); t <= std::min(n - 1, at + window);
             ++t) {
            k = std::max(k, f.kick[size_t(t)]);
            sn = std::max(sn, f.snare[size_t(t)]);
        }
        kick.push_back(k);
        snare.push_back(sn);
    }
    standardize(change);
    standardize(kick);
    standardize(snare);
    for (qsizetype i = 0; i < beats; ++i) {
        rhythm.change.push_back(float(change[size_t(i)]));
        rhythm.accent.push_back(float(kick[size_t(i)] - snare[size_t(i)]));
    }
    const BarPhase bars = barPhase(rhythm, 0, beats);
    rhythm.downbeat = bars.downbeat;
    rhythm.barConfidence = bars.confidence;
    return rhythm;
}

BarPhase barPhase(const Rhythm& rhythm, qsizetype from, qsizetype to)
{
    BarPhase bars;
    const auto beats = qsizetype(std::min(rhythm.change.size(), rhythm.accent.size()));
    from = std::clamp<qsizetype>(from, 0, beats);
    to = std::clamp<qsizetype>(to, from, beats);
    if (to - from < 16)
        return bars;
    double scores[4];
    for (int phase = 0; phase < 4; ++phase) {
        double harmonic = 0, on = 0, off = 0;
        int counted = 0, ons = 0, offs = 0;
        for (qsizetype i = from; i < to; ++i) {
            const auto position = int(((i - phase) % 4 + 4) % 4);
            if (position == 0 && i >= 2 && i + 2 <= beats) {
                harmonic += rhythm.change[size_t(i)];
                ++counted;
            }
            if (position % 2 == 0) {
                on += rhythm.accent[size_t(i)];
                ++ons;
            } else {
                off += rhythm.accent[size_t(i)];
                ++offs;
            }
        }
        scores[phase] =
            (counted ? harmonic / counted : 0) + 0.5 * (ons && offs ? on / ons - off / offs : 0);
    }
    const int best = int(std::max_element(scores, scores + 4) - scores);
    double second = -std::numeric_limits<double>::infinity();
    for (int phase = 0; phase < 4; ++phase)
        if (phase != best)
            second = std::max(second, scores[phase]);
    bars.downbeat = best;
    bars.confidence = float(std::max(0.0, scores[best] - second));
    return bars;
}

namespace
{
Fit fitBeats(const std::vector<float>& beats, qsizetype first, qsizetype end, int divisions)
{
    Fit fit;
    const qsizetype count = end - first;
    if (count < 8)
        return fit;
    std::vector<double> intervals;
    for (qsizetype i = first + 1; i < end; ++i)
        intervals.push_back(double(beats[size_t(i)]) - beats[size_t(i) - 1]);
    std::nth_element(intervals.begin(), intervals.begin() + qsizetype(intervals.size() / 2),
                     intervals.end());
    const double guess = intervals[intervals.size() / 2] / std::max(1, divisions);
    if (guess <= 0)
        return fit;
    // The period the beats line up best at: the longest mean of them as angles around a
    // circle of it (beats too many, too few or astray barely move it).
    double period = guess, angle = 0, strongest = -1;
    for (int step = -kPeriodSteps; step <= kPeriodSteps; ++step) {
        const double candidate = guess * (1 + step * kPeriodStep);
        double c = 0, s = 0;
        for (qsizetype i = first; i < end; ++i) {
            const double a = 2 * std::numbers::pi * beats[size_t(i)] / candidate;
            c += std::cos(a);
            s += std::sin(a);
        }
        if (c * c + s * s > strongest) {
            strongest = c * c + s * s;
            period = candidate;
            angle = std::atan2(s, c);
        }
    }
    double phase = angle / (2 * std::numbers::pi) * period;
    // Then least squares through the beats on it, each at its nearest grid beat.
    fit.index.assign(size_t(count), 0);
    fit.residuals.assign(size_t(count), 0);
    std::vector<bool> used(static_cast<size_t>(count), true);
    for (int pass = 0; pass < 4; ++pass) {
        for (qsizetype i = 0; i < count; ++i) {
            const double t = beats[size_t(first + i)];
            fit.index[size_t(i)] = qint64(std::llround((t - phase) / period));
            fit.residuals[size_t(i)] = t - (phase + period * double(fit.index[size_t(i)]));
        }
        std::vector<double> magnitudes;
        for (const double r : fit.residuals)
            magnitudes.push_back(std::fabs(r));
        std::nth_element(magnitudes.begin(), magnitudes.begin() + qsizetype(magnitudes.size() / 2),
                         magnitudes.end());
        const double limit =
            pass == 0 ? period / 4
                      : std::max(kOnGridMs, 4 * 1.4826 * magnitudes[magnitudes.size() / 2]);
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int m = 0;
        for (qsizetype i = 0; i < count; ++i) {
            used[size_t(i)] = std::fabs(fit.residuals[size_t(i)]) <= limit;
            if (!used[size_t(i)])
                continue;
            const auto x = double(fit.index[size_t(i)]);
            const double y = beats[size_t(first + i)];
            sx += x;
            sy += y;
            sxx += x * x;
            sxy += x * y;
            ++m;
        }
        const double d = m * sxx - sx * sx;
        if (m < 4 || d <= 0)
            return {};
        period = (m * sxy - sx * sy) / d;
        phase = (sy - period * sx) / m;
    }
    double sum = 0;
    int m = 0, close = 0;
    for (qsizetype i = 0; i < count; ++i) {
        const double t = beats[size_t(first + i)];
        fit.index[size_t(i)] = qint64(std::llround((t - phase) / period));
        fit.residuals[size_t(i)] = t - (phase + period * double(fit.index[size_t(i)]));
        if (std::fabs(fit.residuals[size_t(i)]) <= kOnGridMs)
            ++close;
        if (used[size_t(i)]) {
            sum += square(fit.residuals[size_t(i)]);
            ++m;
        }
    }
    fit.period = period;
    fit.phase = phase;
    fit.count = int(count);
    fit.rms = m ? std::sqrt(sum / m) : 0;
    fit.inliers = double(close) / double(count);
    return fit;
}
} // namespace

Grid fitGrid(const std::vector<float>& beats, double fromMs, double toMs)
{
    Grid grid;
    const auto first =
        qsizetype(std::lower_bound(beats.begin(), beats.end(), float(fromMs)) - beats.begin());
    const auto end =
        qsizetype(std::lower_bound(beats.begin(), beats.end(), float(toMs)) - beats.begin());
    const Fit fit = fitBeats(beats, first, end);
    if (fit.count == 0)
        return grid;
    grid.periodMs = fit.period;
    grid.phaseMs = fit.phase + fit.period * double(fit.index.front()); // at the first beat
    grid.first = int(first);
    grid.count = fit.count;
    grid.rmsMs = fit.rms;
    grid.inliers = fit.inliers;
    return grid;
}

} // namespace cirrus::media::automix
