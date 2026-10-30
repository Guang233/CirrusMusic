#pragma once

#include "playback.h"
#include "request.h"
#include "types.h"

#include <QByteArray>

#include <functional>
#include <memory>

// Data source interfaces: the only way ViewModels reach data. Fakes (fake_*.cpp) serve
// the sample catalog; the backend implementations arrive in UI phase 5
// (docs/ui/presentation.md §6, §8). Methods are added as pages need them.
namespace cirrus::ui
{

class FakeControls;

// Read-only catalog: home, search, playlist/album/artist details, daily songs.
class CatalogSource
{
  public:
    virtual ~CatalogSource() = default;
    virtual Request playlist(const QString& key, Reply<PlaylistPage> reply) = 0;
};

// Encoded images (JPEG, PNG) by cover key; the cover image provider decodes them.
class ImageSource
{
  public:
    virtual ~ImageSource() = default;
    virtual Request image(const QString& key, Reply<QByteArray> reply) = 0;
};

// Playback and queue commands, and the playback state. Commands are intents: their
// effect shows up in a later snapshot. Personal FM, playNext and append arrive with
// the queue panel (UI phase 4).
class PlaybackSource
{
  public:
    virtual ~PlaybackSource() = default;

    [[nodiscard]] virtual const PlaybackSnapshot& snapshot() const = 0;
    // Called after the snapshot changed, never synchronously from a command.
    virtual void setChangedHandler(std::function<void()> changed) = 0;

    // Replaces the queue with `keys` and plays the one at `index`.
    virtual void playSongs(const QStringList& keys, int index) = 0;
    virtual void playPause() = 0;
    virtual void next() = 0;
    // Back to the start of the song when past its first seconds, else the previous one.
    virtual void previous() = 0;
    virtual void seek(qint64 positionMs) = 0;
    virtual void setVolume(double volume) = 0;
    virtual void setOrder(playback::Order order) = 0;
    virtual void setRepeat(playback::Repeat repeat) = 0;
};

struct Sources {
    // Defined where FakeControls is complete (app.cpp).
    Sources();
    Sources(Sources&&) noexcept;
    Sources& operator=(Sources&&) noexcept;
    ~Sources();

    // Delay and failure knobs of the fakes (App.debug); null for real sources.
    // First, so it outlives the fakes that use it.
    std::unique_ptr<FakeControls> fakeControls;
    std::unique_ptr<CatalogSource> catalog;
    std::unique_ptr<ImageSource> images;
    std::unique_ptr<PlaybackSource> playback;
};

} // namespace cirrus::ui
