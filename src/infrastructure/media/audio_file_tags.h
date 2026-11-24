#pragma once
#include "domain/music/cloud_upload_types.h"

namespace cirrus::media
{
// Title, artist, album and bitrate of a local audio file, from its container's metadata
// (FFmpeg demuxers for audio formats only, local files only, nothing decoded). Leaves what
// it can't read as it was. Blocking: call off the event loop (the cloud upload's read step).
void readAudioFileTags(const QString& path, domain::CloudFileFacts& facts);
} // namespace cirrus::media
