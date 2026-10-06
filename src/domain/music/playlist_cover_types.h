#pragma once
#include "catalog_types.h"
#include <QByteArray>
namespace cirrus::domain
{
enum class CoverStep { Unobserved, Preparing, Allocating, Uploading, Committing };
struct PlaylistCoverReplace {
    static constexpr qsizetype MaximumBytes = 1024 * 1024; // local input/output budget
    PlaylistId playlist;
    QByteArray jpeg; // explicit in-memory selection; never a path, URL or upload credential
    [[nodiscard]] bool isValid() const
    {
        // Bounded envelope only. Full decoding/size/metadata stripping is infrastructure work.
        return validMusicId(playlist.value) && jpeg.size() >= 4 && jpeg.size() <= MaximumBytes &&
               jpeg.startsWith(QByteArray::fromHex("ffd8ff")) &&
               jpeg.endsWith(QByteArray::fromHex("ffd9"));
    }
};
} // namespace cirrus::domain
