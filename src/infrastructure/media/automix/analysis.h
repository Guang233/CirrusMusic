#pragma once
#include "rhythm.h"
#include <QByteArray>
#include <QtGlobal>
#include <optional>
#include <vector>

namespace cirrus::media::automix
{

// What transitions know of one file from its audio (docs/tasks/automix.md §4.2): its
// loudness (ITU-R BS.1770-4) over time, the silence at its start and end, a fade-out, its
// beats and bars. Times are the file's media time in ms. Cached by md5, a few KB each.
struct Analysis {
    static constexpr int kVersion = 3;      // bumped when the results change: analysed again
    static constexpr int kStepMs = 100;     // between momentary loudness values
    static constexpr float kFloor = -70.0f; // LUFS, BS.1770's absolute gate: silence

    qint64 durationMs = 0;   // of what was analysed: the file, or its start (the next track's)
    bool complete = false;   // the whole file: the end's values mean something
    float loudness = kFloor; // integrated (gated), LUFS, over what was analysed
    qint64 soundStartMs = 0; // the first sound; silence before it
    qint64 soundEndMs = 0;   // after the last sound only silence (complete)
    qint64 fadeStartMs = -1; // the end fades out from here to soundEndMs (-1: it doesn't)
    // Momentary loudness of the 400 ms from every kStepMs, from kFloor in half-dB steps.
    std::vector<quint8> momentary;
    Rhythm rhythm; // beats and bars (none: not rhythmic, or too short)

    // The momentary loudness at `ms` (LUFS; kFloor past what was analysed).
    [[nodiscard]] float momentaryAt(qint64 ms) const;
    // The loudness (by energy) over [fromMs, toMs), LUFS.
    [[nodiscard]] float loudnessOver(qint64 fromMs, qint64 toMs) const;
    // When the fade-out is `db` below where it started (soundEndMs if it never gets there;
    // -1 without a fade-out).
    [[nodiscard]] qint64 fadedBy(float db) const;

    [[nodiscard]] QByteArray serialize() const;
    // Nothing for another version or damaged bytes.
    [[nodiscard]] static std::optional<Analysis> deserialize(const QByteArray& bytes);
};

// Builds an Analysis from a file's decoded audio, fed in order (interleaved stereo at
// the file's rate). Keeps a value per step, not the audio.
class Analyser
{
  public:
    explicit Analyser(int rate);
    void add(const float* frames, qsizetype count);
    [[nodiscard]] qint64 durationMs() const { return m_frames * 1000 / m_rate; }
    [[nodiscard]] Analysis finish(bool complete) const;

  private:
    struct Biquad {
        double b0, b1, b2, a1, a2;
        double z1[2] = {0, 0}, z2[2] = {0, 0}; // per channel
        double run(double x, int channel);
    };
    void closeStep();
    void closePeak();

    int m_rate;
    Biquad m_shelf, m_highPass; // the K-weighting
    RhythmFrontEnd m_rhythm;
    qint64 m_frames = 0;
    // Per kStepMs: the K-weighted mean square, both channels summed.
    std::vector<double> m_energy;
    double m_sum = 0;
    qint64 m_stepFrames = 0, m_stepEnd = 0;
    // Per 10 ms: the sample peak, for silence.
    std::vector<float> m_peaks;
    float m_peak = 0;
    qint64 m_peakEnd = 0;
};

} // namespace cirrus::media::automix
