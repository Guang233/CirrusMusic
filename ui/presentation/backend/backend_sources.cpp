#include "backend_sources.h"

#include "backend_catalog.h"
#include "backend_host.h"
#include "backend_images.h"
#include "backend_playback.h"
#include "backend_session.h"
#include "fake_sources.h"

namespace cirrus::ui
{

Sources makeBackendSources(BackendHost& host)
{
    Sources sources = makeFakeSources();
    // Step 2: covers (sample covers still come from the fakes) and the cache.
    sources.images = std::make_unique<BackendImages>(host.runtime(), std::move(sources.images));
    sources.cache = std::make_unique<BackendCache>(host.runtime());
    // Step 3: the account, sign-in and restoring a remembered account.
    sources.session = std::make_unique<BackendSession>(host.runtime());
    // Step 4: search (the rest of the catalog still comes from the fakes).
    sources.catalog = std::make_unique<BackendCatalog>(host.runtime(), std::move(sources.catalog));
    // Step 6: playback and lyrics.
    sources.playback = std::make_unique<BackendPlayback>(host.runtime(), *sources.catalog);
    sources.lyrics = std::make_unique<BackendLyrics>(host.runtime());
    return sources;
}

} // namespace cirrus::ui
