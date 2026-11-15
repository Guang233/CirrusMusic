#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

// Shared MD3E control enums for QML: `Button { variant: Md.Tonal; size: Md.Medium }`.
namespace cirrus::ui::md
{
Q_NAMESPACE
QML_NAMED_ELEMENT(Md)

// Container style. Text is Button-only; Standard is IconButton-only.
enum Variant { Filled, Tonal, Outlined, Text, Elevated, Standard };
Q_ENUM_NS(Variant)

// MD3E button and icon-button sizes (32, 40, 56, 96, 136 dp tall).
enum Size { XSmall, Small, Medium, Large, XLarge };
Q_ENUM_NS(Size)

// Resting container shape; checkable buttons switch to the other one when checked.
enum ShapeKind { Round, Square };
Q_ENUM_NS(ShapeKind)

// Icon-button horizontal padding.
enum WidthMode { Narrow, Default, Wide };
Q_ENUM_NS(WidthMode)

// Position inside a ConnectedButtonGroup; decides which corners stay fully round.
enum GroupPosition { Alone, First, Middle, Last };
Q_ENUM_NS(GroupPosition)

// Container color role: the container is the role's `*Container` color and the content its
// `on*Container` (Neutral: surfaceContainerHighest and onSurfaceVariant).
enum Tone { Primary, Secondary, Tertiary, Error, Neutral };
Q_ENUM_NS(Tone)
} // namespace cirrus::ui::md
