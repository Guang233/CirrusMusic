#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

// Shared request state of loading ViewModels (docs/ui/presentation.md §4.1):
//   PageState { visible: model.state === LoadState.Loading }
namespace cirrus::ui::loadstate
{
Q_NAMESPACE
QML_NAMED_ELEMENT(LoadState)

// Loading: first load, nothing to show yet. Partial: content the source reports as
// incomplete. Offline and SignedOut replace Error for those causes.
enum State { Idle, Loading, Ready, Empty, Error, Offline, SignedOut, Partial };
Q_ENUM_NS(State)
} // namespace cirrus::ui::loadstate
