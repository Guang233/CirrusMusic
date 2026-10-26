# Agent notes

Project docs are in Chinese; start with `docs/HANDOFF.md` (backend) and `docs/ui/README.md` (UI).

## Working with the maintainer

- Reply in Chinese.
- The agent owns the UI only. Do not modify backend code; report backend problems (e.g. the `tst_entity_search` failure below) to the maintainer instead.
- Keep verification proportional to the change; skip tests that are not needed.
- Open design questions (UQ-xx in `docs/ui/README.md` §14) are decided by the maintainer: ask, then record the decision there.
- Current progress and the next step are at the top of `docs/ui/README.md` §12.

## UI work

- UI code lives in `ui/` (QML modules + gallery); the plan, phases and open questions are in `docs/ui/README.md`. Shell/pages use Material 3 Expressive; the player page uses an Apple Music-like style.
- Cross-platform project, Linux first. Windows/macOS are not verified locally.
- Commit often (small conventional commits: `feat(ui): ...`, `docs(ui): ...`). Do not push.
- Do not hand-edit generated files: `ui/theme/Symbols.qml`, `ui/theme/fonts/*.ttf`, `ui/samples/**`. Regenerate with:
  - `python3 tools/ui/prepare_fonts.py` (after editing `ui/theme/fonts/symbols.txt`; needs fontTools)
  - `python3 tools/ui/generate_samples.py` (needs Pillow)
- QML singletons need `QT_QML_SINGLETON_TYPE` in CMake; executables link `<module>plugin` and use `Q_IMPORT_QML_PLUGIN`.
- Avoid QML property names of the form `onXxx` (parsed as signal handlers) and ECMAScript reserved words.
- Do not copy code from GPL/AGPL reference projects; see `docs/ui/references.md`.
- Theme API and design decisions: `docs/ui/theme.md`. Use `Theme.color.*`, `Theme.type.*`, `Theme.shape.*`, `Theme.motion.*`; never hard-code colors in product QML. Use the `Spring` element (not `SpringAnimation`) for spring motion.
- QML-registered C++ classes must not be `final` (Qt derives from them).
- Controls (`Cirrus.Controls`, spec in `docs/ui/controls.md`) have no prefix: product QML imports `Cirrus.Controls` and never `QtQuick.Controls` (use `import QtQuick.Templates as T` for non-visual helpers such as `T.ButtonGroup`). Gallery pages may use `import QtQuick.Controls.Basic as B` for controls that do not exist yet.
- Animate control state colors with a `Spring` 0–1 progress plus `Theme.mix(a, b, t)`, not `Behavior on color` (it lags while the theme blends).
- Vendored m3shapes lives in `third_party/m3shapes` (unmodified upstream sources, own CMake); executables link `m3shapesplugin` and `Q_IMPORT_QML_PLUGIN(M3ShapesPlugin)`.
- No Qt Quick Test for UI (UQ-07): verify in the gallery. `--grab` sets `GalleryState.capturing`, which opens menus/tooltips for screenshots.

## Build and verify

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --target appCirrusMusic appCirrusGallery
cmake --build --preset linux-dev-local --target all_qmllint
QT_QPA_PLATFORM=offscreen ./build/linux-dev-local/appCirrusGallery --grab /tmp/gallery.png --size 1280x2400 [--page theme|buttons|inputs|progress|lists|resources] [--cover sunset] [--mode light|dark]
clang-format --dry-run --Werror <changed C++ files>
```

- Live preview: `tools/ui/gallery-watch.sh [--page buttons]` rebuilds and restarts the gallery on every UI source change (VS Code task `Gallery: watch`; also `Gallery: run` / `Gallery: screenshot`). Interactive gallery runs reopen the last page, cover and mode.
- Release: same with `linux-release-local`.
- Offscreen uses the software renderer; custom shaders need `QT_QUICK_BACKEND=rhi QSG_RHI_BACKEND=opengl`.
- Full `cmake --build --preset linux-dev-local` currently fails in the backend test `tst_entity_search` (pre-existing, from commit 2c164a7); UI targets and `cirrus_backend` build fine.
- Backend tests: `ctest --test-dir build/linux-dev-local --output-on-failure --no-tests=error`.
