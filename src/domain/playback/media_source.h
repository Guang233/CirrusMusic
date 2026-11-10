#pragma once
#include <QDeadlineTimer>
#include <QString>
#include <QUrl>
#include <optional>

namespace cirrus::domain
{
struct PreviewRange {
    qint64 startMs = 0;
    qint64 endMs = 0;
};
// Physical, transient payload ONLY. No song/program/voice/track/account identity.
// Signed URLs never enter UI snapshots/logs; metadata is not a permission grant.
struct MediaSource {
    QUrl url;
    QString actualQuality;
    QString format;
    qint64 size = 0;
    QDeadlineTimer validUntil;
    std::optional<PreviewRange> preview;
    // The provider's content digest (32 lowercase hex), empty when it gave none. Physical
    // like the rest: it keys the audio cache and checks a finished file, nothing more.
    QByteArray md5;
    // Loudness from the provider: the ReplayGain 2 track gain (dB to −18 LUFS) and the
    // sample peak (0–1, capped at 1). Absent when it gave none or only a placeholder
    // (preview clips say gain 1.5, peak 0). Physical too: for volume normalization only.
    std::optional<float> gainDb;
    float peak = 0;
};
} // namespace cirrus::domain
