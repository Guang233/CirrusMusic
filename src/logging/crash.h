#pragma once
#include <QString>

#include <atomic>

// Crash stacks (docs/tasks/logging-v2.md §5.3). A fatal signal writes the stack's addresses
// and the loaded modules to a raw file with async-signal-safe calls only; the next run
// turns that file into app.crash and app.crash_frame records (module file names and
// offsets, no paths) and removes it. Nothing of the process's memory is written.
namespace cirrus::log::crash
{
// The open log file, for a "fatal signal N" line; -1 when there is none.
extern std::atomic<int> logFd;

// Installs the handlers once (fatal signals, std::terminate); a crash's stack goes to
// `path`, which must not look like a log file (crash-<session>.txt).
void install(const QString& path);

// Logs and removes the crash files that earlier runs left in `directory`, and keeps each
// as crashes/<session>.txt there (module and offset per frame; the newest ten).
void report(const QString& directory);

// This build's identity for the symbols that turn offsets back into names: the GNU
// build-id of the executable on Linux; empty elsewhere.
QString buildId();
} // namespace cirrus::log::crash
