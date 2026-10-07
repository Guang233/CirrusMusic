#pragma once
#include "domain/music/podcast_types.h"
#include <type_traits>
#include <variant>

namespace cirrus::domain
{
// Transport namespace, NOT SongId or evidence of a playable association.
struct AudioTrackId {
    QString value;
    [[nodiscard]] bool isValid() const { return validMusicId(value); }
};
// Tagged logical identity. Equal decimal text across namespaces does not merge entities.
// This value neither creates a binding nor enables program/voice playback.
using PlaybackEntity = std::variant<SongId, ProgramId, VoiceId>;
inline bool validPlaybackEntity(const PlaybackEntity& entity)
{
    return std::visit([](const auto& id) { return validMusicId(id.value); }, entity);
}
inline bool samePlaybackEntity(const PlaybackEntity& left, const PlaybackEntity& right)
{
    return std::visit(
        [&right](const auto& id) {
            using Id = std::decay_t<decltype(id)>;
            const auto* other = std::get_if<Id>(&right);
            return other && other->value == id.value;
        },
        left);
}
} // namespace cirrus::domain
