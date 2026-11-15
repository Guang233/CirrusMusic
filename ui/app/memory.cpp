#include "memory.h"

#include <cstdlib> // defines __GLIBC__ under glibc
#include <cstring>

#if defined(__GLIBC__)
#include <malloc.h>
#endif
#if defined(__linux__)
#include <cstdio>
#include <sys/prctl.h>
#include <unistd.h>
#endif

namespace cirrus::ui::memory
{

void configure()
{
    // CIRRUS_MEMORY=default leaves the allocator alone, to compare.
    if (const char* choice = std::getenv("CIRRUS_MEMORY");
        choice && std::strcmp(choice, "default") == 0)
        return;
#if defined(__linux__) && defined(PR_SET_THP_DISABLE)
    prctl(PR_SET_THP_DISABLE, 1, 0, 0, 0);
#endif
#if defined(__GLIBC__)
    mallopt(M_MMAP_THRESHOLD, 256 * 1024);
    mallopt(M_ARENA_MAX, 4);
#endif
}

void trim()
{
#if defined(__GLIBC__)
    malloc_trim(0);
#endif
}

int residentMb()
{
#if defined(__linux__)
    long pages = 0;
    long resident = 0;
    if (std::FILE* file = std::fopen("/proc/self/statm", "r")) {
        const int read = std::fscanf(file, "%ld %ld", &pages, &resident);
        std::fclose(file);
        if (read == 2)
            return int(resident * sysconf(_SC_PAGESIZE) / (1024 * 1024));
    }
#endif
    return -1;
}

} // namespace cirrus::ui::memory
