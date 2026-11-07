#include "backend_sources.h"

#include "backend_host.h"
#include "backend_images.h"
#include "fake_sources.h"

namespace cirrus::ui
{

Sources makeBackendSources(BackendHost& host)
{
    Sources sources = makeFakeSources();
    // Step 2: covers (sample covers still come from the fakes) and the cache.
    sources.images = std::make_unique<BackendImages>(host.runtime(), std::move(sources.images));
    sources.cache = std::make_unique<BackendCache>(host.runtime());
    return sources;
}

} // namespace cirrus::ui
