#pragma once

#include "decoder.h"

#include <QStringList>

#include <memory>
#include <vector>

struct SwrContext;

namespace poc
{

// Tracks played back to back into one stream of interleaved stereo float at `outRate`.
// While the source rate stays the same the resampler is not flushed between tracks, so
// the joint is continuous (gapless); a rate change flushes and starts a new resampler.
class Chain
{
  public:
    struct Boundary {
        qint64 outFrame = 0; // first output frame of `track`
        int track = 0;
    };

    explicit Chain(int outRate);
    ~Chain();

    bool setTracks(const QStringList& paths, int first = 0);
    // Restarts output (frame 0) at `ms` into `track`.
    bool seek(int track, qint64 ms);
    // Appends up to `frames` frames; returns how many (0 at the end of the last track).
    qsizetype pull(std::vector<float>& out, qsizetype frames);

    [[nodiscard]] int outRate() const { return m_outRate; }
    [[nodiscard]] qint64 produced() const { return m_produced; }
    [[nodiscard]] const std::vector<Boundary>& boundaries() const { return m_boundaries; }
    [[nodiscard]] int trackCount() const { return int(m_paths.size()); }

  private:
    bool openTrack(int track);
    void resetResampler(int inRate);
    void resample(const std::vector<float>& in, bool flush);

    int m_outRate;
    QStringList m_paths;
    int m_track = -1;
    std::unique_ptr<Decoder> m_decoder;
    SwrContext* m_resampler = nullptr; // null when the rates match
    int m_inRate = 0;
    std::vector<float> m_out; // resampled, not yet pulled
    std::vector<float> m_chunk;
    qint64 m_produced = 0; // output frames handed out since the last seek
    qint64 m_resampledIn = 0;
    std::vector<Boundary> m_boundaries;
    bool m_finished = false;
};

} // namespace poc
