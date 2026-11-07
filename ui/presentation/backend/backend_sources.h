#pragma once

#include "sources.h"

namespace cirrus::ui
{

class BackendHost;

// The app's data sources (docs/ui/integration.md §5): the backend's where they are
// done, the fakes elsewhere until their step. The sources use `host`'s backend and
// cope with it being gone first at exit.
Sources makeBackendSources(BackendHost& host);

} // namespace cirrus::ui
