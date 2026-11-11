#pragma once
#include <QString>

namespace cirrus::ui
{
// A notice's text, with its stable id logged first (docs/tasks/logging-v2.md §4.2): the
// log says which notice the user saw, never its text, which may name a playlist.
//   emit notice(noticed("playlist.deleted", tr("已删除歌单「%1」").arg(title)));
// One id for each kind of text; texts that only differ in a count share one.
QString noticed(const char* id, const QString& text);
} // namespace cirrus::ui
