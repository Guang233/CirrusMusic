#pragma once

// How the process uses memory under glibc on Linux; nothing elsewhere. A music player is
// open for days, and with glibc's defaults its footprint only grew: a decoded cover is a
// buffer of hundreds of kB made on a worker thread, and once such buffers had been freed a
// few times glibc kept them all inside its per-thread arenas, where what is freed between
// live blocks never goes back to the system (hundreds of MB after a few minutes of use).
namespace cirrus::ui::memory
{

// Call first in main(), before any thread exists:
// - buffers of 256 kB and more are always mapped on their own, so freeing one returns it
//   to the system (by default glibc raises that limit, to 32 MB, as large blocks are freed);
// - at most four arenas for all threads, instead of eight per core;
// - no transparent huge pages: with `always`, a sparsely used heap is resident 2 MB at
//   a time.
void configure();

// Returns the heap's free pages to the system. For when the window was hidden: the scene
// graph and its textures have just been freed.
void trim();

// The resident memory in MB, or -1 when unknown (for the log).
[[nodiscard]] int residentMb();

} // namespace cirrus::ui::memory
