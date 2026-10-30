#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

// What a collection card shows (docs/ui/presentation.md §5.3).
namespace cirrus::ui::collection
{
Q_NAMESPACE
QML_NAMED_ELEMENT(Collection)

enum Kind { Playlist, Album, Artist };
Q_ENUM_NS(Kind)
} // namespace cirrus::ui::collection
