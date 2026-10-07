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
};
} // namespace cirrus::domain
