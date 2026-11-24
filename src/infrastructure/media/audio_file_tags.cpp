#include "audio_file_tags.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/mem.h>
}

namespace cirrus::media
{
namespace
{
QString tag(const AVFormatContext* format, const char* key)
{
    if (const auto* e = av_dict_get(format->metadata, key, nullptr, 0))
        return QString::fromUtf8(e->value).trimmed();
    // Ogg and some others keep their comments on the stream.
    for (unsigned i = 0; i < format->nb_streams; ++i)
        if (const auto* e = av_dict_get(format->streams[i]->metadata, key, nullptr, 0))
            return QString::fromUtf8(e->value).trimmed();
    return {};
}
} // namespace

void readAudioFileTags(const QString& path, domain::CloudFileFacts& facts)
{
    AVFormatContext* format = avformat_alloc_context();
    if (!format)
        return;
    format->format_whitelist = av_strdup("mp3,flac,ogg,mov,mp4,m4a,wav,ape,asf,aac,matroska,webm");
    AVDictionary* options = nullptr;
    av_dict_set(&options, "protocol_whitelist", "file", 0);
    const QByteArray file = path.toUtf8();
    const int opened = avformat_open_input(&format, file.constData(), nullptr, &options);
    av_dict_free(&options);
    if (opened < 0) // frees the context
        return;
    if (const auto title = tag(format, "title"); !title.isEmpty())
        facts.title = title;
    auto artist = tag(format, "artist");
    if (artist.isEmpty())
        artist = tag(format, "album_artist");
    if (!artist.isEmpty())
        facts.artist = artist;
    if (const auto album = tag(format, "album"); !album.isEmpty())
        facts.album = album;
    if (format->bit_rate > 0)
        facts.bitrate = format->bit_rate;
    else if (format->duration > 0 && facts.size > 0)
        facts.bitrate = facts.size * 8 * AV_TIME_BASE / format->duration;
    avformat_close_input(&format);
}
} // namespace cirrus::media
