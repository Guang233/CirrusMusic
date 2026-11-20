#pragma once
#include <QtGlobal>
#include <memory>
#include <vector>

namespace cirrus::media
{
class AudioDecoder;
class AudioResampler;
} // namespace cirrus::media

namespace cirrus::media::automix
{

// One track's audio at the output rate (decoder → resampler → normalization gain), pulled
// in exact amounts: what the engine's producer plays and mixes, and the automix tool
// renders. Blocking like the decoder: run it on a decoding thread.
class Deck
{
  public:
    // `gain`: the track's normalization gain (loudness.h), used while normalization is on.
    Deck(std::unique_ptr<AudioDecoder> decoder, int outRate, float gain, bool normalize);
    ~Deck();
    Deck(const Deck&) = delete;
    Deck& operator=(const Deck&) = delete;

    // Up to `frames` frames into `out` (interleaved stereo). Fewer only when the decoder
    // reached its end (decoderEnded(): follow() or finish() next), failed or was
    // interrupted (resume() or seek() next).
    qsizetype pull(float* out, qsizetype frames, bool normalize);
    [[nodiscard]] bool decoderEnded() const { return m_decoderEnded; }
    // Gapless: the next track goes on through the same resampler when its rate is the
    // same, so the joint stays continuous. Returns the output frame it starts at.
    qint64 follow(std::unique_ptr<AudioDecoder> next, float gain);
    // No next track: the resampler's last frames, then the end.
    void finish();
    [[nodiscard]] bool ended() const { return m_finished && buffered() == 0; }
    // Sample-exact (the decoder's seek); drops what it held, framesOut() starts again.
    bool seek(qint64 ms);
    void resume(); // after an interrupted read that is no longer wanted
    [[nodiscard]] AudioDecoder& decoder() { return *m_decoder; }

    // Output frames given since the start (or the last seek).
    [[nodiscard]] qint64 framesOut() const { return m_framesOut; }
    // The media time of the next frame pull() gives (of the track after a follow()).
    [[nodiscard]] qint64 positionMs() const;

  private:
    [[nodiscard]] qsizetype buffered() const
    {
        return qsizetype(m_buffer.size() / 2) - qsizetype(m_read);
    }
    void applyGain(float* out, qsizetype frames, bool normalize);

    std::unique_ptr<AudioDecoder> m_decoder;
    std::unique_ptr<AudioResampler> m_resampler;
    int m_outRate;
    std::vector<float> m_chunk, m_buffer;
    size_t m_read = 0; // frames of m_buffer already given out
    qint64 m_framesOut = 0;
    qint64 m_trackStartFrame = 0; // output frame the current track starts at
    qint64 m_trackStartMs = 0;    // and its media time there
    bool m_decoderEnded = false;
    bool m_finished = false;
    // Normalization: the track's gain, what is applied now, sliding to its target.
    float m_trackGain;
    float m_applied;
    float m_rampTarget;
    float m_rampStep = 0;
};

} // namespace cirrus::media::automix
