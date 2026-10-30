#pragma once

#include "request.h"
#include "types.h"

#include <QByteArray>

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
};

} // namespace cirrus::ui
