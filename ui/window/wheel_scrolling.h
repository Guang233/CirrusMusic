#pragma once

#include <QtGlobal>

namespace cirrus::ui
{
// Mouse wheel scrolling of every Flickable (ListView, pages): Qt's wheel model unchanged
// (a notch moves wheelScrollLines × 24 px, faster turning flicks further), with a lower
// wheel deceleration than Qt's 15000 px/s². At 15000 it stops within about 0.1 s and
// fast turning moves only ~33 px a notch; at 5000 a fast turn moves 86–127 px a notch
// and glides 0.4–0.5 s (measured 2026-11-14, docs/ui/pages/playlist.md §12). Qt reads
// the variable when each Flickable is created, so call this before loading QML. A value
// set in the environment wins (to try others).
inline void applyWheelScrolling()
{
    if (!qEnvironmentVariableIsSet("QT_QUICK_FLICKABLE_WHEEL_DECELERATION"))
        qputenv("QT_QUICK_FLICKABLE_WHEEL_DECELERATION", "5000");
}
} // namespace cirrus::ui
