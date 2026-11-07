# Agent notes

Project docs are in Chinese; start with `docs/HANDOFF.md` (backend) and `docs/ui/README.md` (UI).

## Working with the maintainer

- Reply in Chinese.
- The agent's focus is the UI. Backend code may be changed when a task needs it (the maintainer lifted the UI-only rule on 2026-11-01); follow the backend's own docs and conventions (`docs/HANDOFF.md`) and report unrelated backend problems (e.g. the `tst_entity_search` failure below) to the maintainer.
- Keep verification proportional to the change; skip tests that are not needed.
- Open design questions (UQ-xx in `docs/ui/README.md` §14) are decided by the maintainer: ask, then record the decision there.
- Current progress and the next step are at the top of `docs/ui/README.md` §12.

## UI work

- UI code lives in `ui/` (QML modules + gallery); the plan, phases and open questions are in `docs/ui/README.md`. Shell/pages use Material 3 Expressive; the player page uses an Apple Music-like style.
- Cross-platform project, Linux first. Windows/macOS are not verified locally.
- Commit often (small conventional commits: `feat(ui): ...`, `docs(ui): ...`), without a Claude co-author trailer. The remote `origin` is a private GitHub repo; push only when the maintainer asks, never automatically after commits.
- Do not hand-edit generated files: `ui/theme/Symbols.qml`, `ui/theme/fonts/*.ttf`, `ui/samples/**`. Regenerate with:
  - `python3 tools/ui/prepare_fonts.py` (after editing `ui/theme/fonts/symbols.txt`; needs fontTools). It also fetches the default text font MiSans (`ui/theme/fonts/MiSansVF.ttf`, committed unmodified; its license forbids modifying it, so never subset it). Builds without the file fall back to the system font with a CMake warning.
  - `python3 tools/ui/generate_samples.py` (needs Pillow)
- QML singletons need `QT_QML_SINGLETON_TYPE` in CMake; executables link `<module>plugin` and use `Q_IMPORT_QML_PLUGIN`.
- Avoid QML property names of the form `onXxx` (parsed as signal handlers) and ECMAScript reserved words.
- Do not copy code from GPL/AGPL reference projects; see `docs/ui/references.md`.
- Text fonts come from `Theme.type.*` or `Fonts.font(weight, size)`; never set `font.weight` in QML (MiSans's weight axis is non-standard, see `docs/ui/README.md` §4.2).
- Theme API and design decisions: `docs/ui/theme.md`. Use `Theme.color.*` (in `Cirrus.Controls`, `Cirrus.Components` and `ui/app/settings/`: `ColorScope.scheme.*`, so a scope such as the player can hand them another scheme), `Theme.type.*`, `Theme.shape.*`, `Theme.motion.*`; never hard-code colors in product QML. Use the `Spring` element (not `SpringAnimation`) for spring motion.
- QML-registered C++ classes must not be `final` (Qt derives from them).
- Controls (`Cirrus.Controls`, spec in `docs/ui/controls.md`) have no prefix: product QML imports `Cirrus.Controls` and never `QtQuick.Controls` (use `import QtQuick.Templates as T` for non-visual helpers such as `T.ButtonGroup`). Gallery pages may use `import QtQuick.Controls.Basic as B` for controls that do not exist yet.
- Animate control state colors with a `Spring` 0–1 progress plus `Theme.mix(a, b, t)`, not `Behavior on color` (it lags while the theme blends).
- `Icon` must keep `Text.NativeRendering` + `Font.PreferNoHinting`: Material Symbols' FILL axis uses overlapping contours, which distance-field/curve rendering and hinting break (holes, hairline seams). See `docs/ui/controls.md` §2.
- Vendored m3shapes lives in `third_party/m3shapes` (unmodified upstream sources, own CMake); executables link `m3shapesplugin` and `Q_IMPORT_QML_PLUGIN(M3ShapesPlugin)`.
- No Qt Quick Test for UI (UQ-07): verify in the gallery. `--grab` sets `GalleryState.capturing`, which opens menus/tooltips for screenshots.

## Build and verify

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --target appCirrusMusic appCirrusGallery
cmake --build --preset linux-dev-local --target all_qmllint
QT_QPA_PLATFORM=offscreen ./build/linux-dev-local/appCirrusGallery --grab /tmp/gallery.png --size 1280x2400 [--page theme|buttons|inputs|progress|lists|navigation|data|resources] [--cover sunset] [--mode light|dark] [--titlebar custom|system|hidden]
clang-format --dry-run --Werror <changed C++ files>
```

- Live preview: `tools/ui/gallery-watch.sh [--page buttons]` rebuilds and restarts the gallery on every UI source change (VS Code task `Gallery: watch`; also `Gallery: run` / `Gallery: screenshot`). Interactive gallery runs reopen the last page, cover and mode.
- Release: same with `linux-release-local`.
- `appCirrusMusic` is single-instance: while one runs (including the maintainer's), a second launch only activates it and exits 0. Linux hybrid-graphics machines render on the discrete GPU unless `CIRRUS_GPU=default` (docs/ui/pages/shell.md §7).
- `appCirrusMusic` starts the real backend (UI phase 5, `docs/ui/integration.md`): it writes `~/.local/share/Cirrus/state.sqlite`, `~/.cache/Cirrus/resources` and the keyring (`com.guang233.Cirrus`), and talks to NetEase on start (anonymous registration). For test runs set `XDG_DATA_HOME`, `XDG_CACHE_HOME`, `XDG_CONFIG_HOME` to a fixed temporary directory; do not start it with fresh data directories in a loop (the service throttles new anonymous registrations).
- Offscreen uses the software renderer; custom shaders need `QT_QUICK_BACKEND=rhi QSG_RHI_BACKEND=opengl`.
- Full `cmake --build --preset linux-dev-local` currently fails in the backend test `tst_entity_search` (pre-existing, from commit 2c164a7); UI targets and `cirrus_backend` build fine.
- Backend tests: `ctest --test-dir build/linux-dev-local --output-on-failure --no-tests=error`.
