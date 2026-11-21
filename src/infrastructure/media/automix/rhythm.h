#pragma once
#include "spectrum.h"
#include <QtGlobal>
#include <array>
#include <vector>

namespace cirrus::media::automix
{

// What the beat analysis works from (docs/tasks/automix.md §4.2), per short frame of the
// downmixed audio. Times: frame `i` stands for media time originMs + i · frameMs.
struct RhythmFeatures {
    double frameMs = 0;
    double originMs = 0;
    std::vector<float> onset; // spectral flux over all bands (dB rises, averaged)
    std::vector<float> kick;  // the same under 150 Hz: kick drums, bass notes
    std::vector<float> snare; // and over 1–4 kHz: snares, claps
    std::vector<float> bass;  // the power under 150 Hz, dB
    // Pitch classes (C first) from a longer spectrum, for harmonic changes and keys.
    double chromaFrameMs = 0;
    double chromaOriginMs = 0;
    std::vector<std::array<float, 12>> chroma;
};

// Turns a file's audio (interleaved stereo at its rate, fed in order) into
// RhythmFeatures: downmixed, low-passed and decimated to 11–12 kHz, a 46 ms spectrum
// every 11.6 ms (a 186 ms one every 93 ms for the pitch classes).
class RhythmFrontEnd
{
  public:
    explicit RhythmFrontEnd(int rate);
    void add(const float* stereo, qsizetype frames);
    [[nodiscard]] const RhythmFeatures& features() const { return m_features; }

  private:
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;
        float run(float x);
    };
    struct Band {
        int from = 0;
        std::vector<float> weights; // over bins from `from`, summing to 1
        float hz = 0;               // its centre
    };
    struct ChromaBin {
        int bin = 0;
        int lower = 0;      // the pitch class under it
        float fraction = 0; // of the way to the next one up
    };
    void onsetFrame(const float* samples);
    void chromaFrame(const float* samples);

    int m_factor;    // decimation
    double m_rate;   // after it
    int m_size;      // the onset spectrum's
    int m_hop;       // between onset frames
    int m_chromaHop; // between chroma frames
    Biquad m_lowPass[2];
    int m_phase = 0; // input samples until the next decimated one
    Spectrum m_spectrum, m_chromaSpectrum;
    std::vector<Band> m_bands;
    std::vector<ChromaBin> m_chromaBins;
    int m_kickFrom = 1, m_kickTo = 1; // bins [from, to)
    std::vector<float> m_power, m_chromaPower, m_previous;
    float m_previousKick = 0;
    bool m_first = true;
    // The decimated signal from sample m_start on; the next frames start at these.
    std::vector<float> m_signal;
    qint64 m_start = 0, m_total = 0, m_nextOnset = 0, m_nextChroma = 0;
    RhythmFeatures m_features;
};

// The beats of a file, from its RhythmFeatures. Times are ms of media time.
struct Rhythm {
    float bpm = 0;            // the usual tempo (0: none found)
    float strength = 0;       // how regular the onsets are, 0–1: rhythmic music is high
    std::vector<float> beats; // ms
    bool steady = false;      // made to a click: the beats are one constant grid
    int downbeat = 0;         // the first beat that starts a bar (4/4): 0–3, over all of it
    float barConfidence = 0;  // how much that phase stands out (0: not at all)
    // Per beat: the power under 150 Hz (dB); the change of harmony into it and the kick
    // less the snare on it (both in spreads over the file), which say where bars start.
    std::vector<float> bass, change, accent;
};

// Where bars start among beats [from, to): beat `downbeat` + 4k (0–3 from beat 0).
struct BarPhase {
    int downbeat = 0;
    float confidence = 0; // by how much it beats the next likeliest phase (0: a guess)
};
[[nodiscard]] BarPhase barPhase(const Rhythm& rhythm, qsizetype from, qsizetype to);

// Tempo by autocorrelation under a prior around 120 BPM, beats by dynamic programming
// (Ellis 2007), bars from harmonic changes and the kick and snare pattern. `toGrid`: a
// song made to a click gets that grid as its beats (off: the beats as found, to measure).
[[nodiscard]] Rhythm findRhythm(const RhythmFeatures& features, bool toGrid = true);

// A constant beat grid fitted to the beats in [fromMs, toMs): beat i of it at
// phaseMs + i · periodMs (i from 0 for the first beat inside, at index `first`).
struct Grid {
    double periodMs = 0;
    double phaseMs = 0;
    int first = 0;      // the beats' index of grid beat 0
    int count = 0;      // beats it was fitted to
    double rmsMs = 0;   // how far the beats are from it (the inliers')
    double inliers = 0; // the share of beats close to it
    [[nodiscard]] bool valid() const { return count > 0 && periodMs > 0; }
    [[nodiscard]] double at(double index) const { return phaseMs + index * periodMs; }
};
[[nodiscard]] Grid fitGrid(const std::vector<float>& beats, double fromMs, double toMs);

} // namespace cirrus::media::automix
