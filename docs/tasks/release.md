# 发布与打包

> 状态：2026-11-10 起步。安装规则、依赖构建脚本和 Linux 打好的目录树在本机验证过；deb、AppImage、Windows 和两个工作流要推到 GitHub 后才第一次运行（§8）。

维护者 2026-11-10 定：按正常软件的标准发布；Linux 出 deb 和 AppImage，以后上传 AUR；Windows 出安装包；这一轮不做 macOS。测试只在 CI 里跑，不进安装包；gallery 也不进安装包。已知失败的测试先排除（§7.1）。

## 1. 产物

| 产物 | 文件名 | 内容 |
| --- | --- | --- |
| deb | `cirrus_<版本>_amd64.deb` | 装到 `/opt/cirrus`，自带 Qt；`/usr/bin/cirrus` 是链接；桌面文件、图标、AppStream 装在 `/usr/share` |
| AppImage | `Cirrus-<版本>-x86_64.AppImage` | 和 deb 同一套目录，另外带上基础系统以外的库 |
| AUR | `packaging/aur/PKGBUILD`（`cirrus`） | 用 Arch 的 Qt 和库从源码编译，仓库公开后上传 |
| Windows 安装包 | `Cirrus-<版本>-windows-x64-setup.exe` | Inno Setup；默认只装给当前用户（不用管理员），第一页可改为所有用户 |
| Windows 免安装版 | `Cirrus-<版本>-windows-x64.zip` | 解压即用；设置和数据仍在用户目录 |
| 库的源码 | `Cirrus-<版本>-library-sources.tar` | 包里带的 FFmpeg、QtKeychain 的原始源码（LGPL 的源码提供，§6） |
| 校验 | `SHA256SUMS.txt` | |

版本号用语义化版本，唯一来源是 `CMakeLists.txt` 的 `project(CirrusMusic VERSION x.y.z)`；标签 `vx.y.z`；`CHANGELOG.md` 每个版本一节，就是 Release 的说明。

## 2. 安装规则

`cmake/CirrusInstall.cmake`（由 `ui/app/CMakeLists.txt` 引入）。只装应用本身：打包时 `-DBUILD_TESTING=OFF -DCIRRUS_BUILD_GALLERY=OFF`，测试、gallery、`tools/` 都不编译。

| 选项 | 默认 | 作用 |
| --- | --- | --- |
| `CIRRUS_EXECUTABLE` | `appCirrusMusic` | 可执行文件名。开发构建保持原名（文档、启动配置都用它）；包里 Linux 是 `cirrus`，Windows 是 `Cirrus` |
| `CIRRUS_DEPLOY_QT` | OFF | 安装时把 Qt 运行库（Qt 的部署脚本，QML 模块按导入扫描）和不在系统目录的库一起装进去。deb、AppImage、Windows 用；AUR 不用 |
| `CIRRUS_RUNTIME_DIRS` | 空 | 仅 Windows：去哪里找 FFmpeg、QtKeychain、OpenSSL、zlib 的 DLL |

Linux 装的东西：`bin/cirrus`；`share/applications/com.guang233.Cirrus.desktop`（`Exec=cirrus`，`StartupWMClass=Cirrus`）；`share/metainfo/com.guang233.Cirrus.metainfo.xml`；`share/icons/hicolor/<N>x<N>/apps/com.guang233.Cirrus.png`（16–512）和 `scalable/…svg`；`share/doc/cirrus/THIRD-PARTY-NOTICES.md` 和 `licenses/`。桌面文件和 AppStream 的模板在 `packaging/linux/`。

Dock 图标：Wayland 下合成器按 app ID 找同名桌面文件里的图标，程序自己设的窗口图标不算。开发构建没有桌面文件，所以是默认图标；`tools/ui/install-dev-desktop-entry.sh` 把指向开发构建的桌面文件和图标装到 `~/.local/share`（装正式包前用 `--remove` 去掉，否则它会盖住包里的）。

## 3. 随包的库

版本和 SHA-256 固定在 `packaging/deps/versions.sh`。

| 库 | 来源 | 说明 |
| --- | --- | --- |
| Qt 6.11.2 | Qt 官方二进制（CI 用 install-qt-action/aqtinstall），模块 qtmultimedia、qtshadertools、qtimageformats | Linux 版在 RHEL 9.6 上编译，要 glibc 2.34 |
| FFmpeg 9.0.2（Linux） | `packaging/deps/build-ffmpeg.sh` 从源码编 | 共享库、LGPL、只有 MP3 和 FLAC 的解封装与解码（解码器只开这两种），约 2 MB |
| FFmpeg 9.0.2（Windows） | vcpkg `ffmpeg`，特性 avcodec、avformat、swresample | 不开 gpl、nonfree |
| QtKeychain 0.17.0 | `packaging/deps/build-qtkeychain.sh`，针对随包的 Qt 编 | 发行版的 QtKeychain 是针对它们自己的旧 Qt 编的，不能用。Windows 上静态链接：windeployqt 把所有 `qt6*.dll` 当成 Qt 模块，遇到 `qt6keychain.dll` 会失败 |
| OpenSSL 3、zlib | Linux：系统的（deb 依赖 `libssl3`）；Windows：vcpkg | |

**两份 FFmpeg**：Qt Multimedia 自带 FFmpeg 7.1（`libavcodec.so.61`），它的音频输出后端要用；我们的解码器用自己编的 9.0（`libavcodec.so.63`）。大版本不同，库名不同，两份同时在 `lib/` 里、同时加载也不冲突。不改用 Qt 的那份：Qt 不带它的头文件，Windows 上也没有导入库。

依赖库装好后用 patchelf 把 RUNPATH 设成 `$ORIGIN`，它们在包的 `lib/` 里能互相找到。

## 4. Linux

构建机是 Ubuntu 22.04（glibc 2.35，GCC 12；22.04 自带的 libstdc++ 就是 GCC 12 的），包能在 Ubuntu 22.04、Debian 12 及更新的系统上运行。系统依赖：`packaging/linux/install-build-deps.sh`。

```bash
packaging/deps/build-ffmpeg.sh <deps>
packaging/deps/build-qtkeychain.sh <deps> <Qt 前缀>
packaging/linux/build-packages.sh <Qt 前缀> <deps> <输出目录> [deb] [appimage] [stage]
```

`stage` 只编译、装出 `/opt/cirrus` 那套目录（`build/package-linux/stage`），在任何发行版上都能用来检查。

### 4.1 deb

`/opt/cirrus` 自成一体（Chrome、VS Code 的 deb 也这么做）：发行版的 Qt 比 6.11 旧，不能依赖系统 Qt。`Depends` 由 `dpkg-shlibdeps` 按包里的 ELF 算出，只列系统库（OpenSSL、libsecret、glib、X11/Wayland、OpenGL 等）。`Recommends` 一个密钥环（gnome-keyring、KWallet、KeePassXC 任一）。不写维护者脚本：桌面数据库和图标缓存由 dpkg 触发器更新。

### 4.2 AppImage

同一套目录装到 `AppDir/usr`，再用 linuxdeploy（固定版本，构建时下载）补上基础系统以外的库（xcb-cursor 等）并打包。Qt 已经部署好，不用 linuxdeploy 的 Qt 插件。没有自动更新信息。

### 4.3 AUR

`packaging/aur/PKGBUILD`：从 GitHub 的标签源码包编译，依赖 Arch 的 `qt6-*`、`qtkeychain-qt6`、`ffmpeg`，`/usr` 布局。Cirrus 用了 Qt 的私有 Wayland 客户端接口，Arch 的 Qt 每升一个小版本都要重新编译（`pkgrel` 加一）。上传前要做：仓库公开、填许可证和源码包的 sha256。

## 5. Windows

MSVC（VS 2022）、Qt `win64_msvc2022_64`、vcpkg（`packaging/windows/vcpkg.json`，固定 baseline）、QtKeychain 用同一个脚本在 Git Bash 里编。安装时 Qt 的部署脚本调 windeployqt，`CIRRUS_RUNTIME_DIRS` 里的 DLL 和 MSVC 运行库（app-local，不用另装 vc_redist）一起放到 `Cirrus.exe` 旁边。Inno Setup 脚本：`packaging/windows/cirrus.iss`（`AppId` 永不改；有简体中文语言文件时用中文界面）。

- **能编译，还没运行过**（2026-11-11 CI 第一次通过，MSVC 2022，约 80 分钟）。为此改过：material-color-utilities 的 C 复合字面量、`M_PI`、ARGB 字面量（该库 README 的本地修改 4、5）；Qt 6.11 的 Windows 包换了仓库目录结构，aqtinstall 3.3.0 找不到，暂用它 master 上的固定提交（`windows.yml`，等 3.3.0 之后的正式版发布再改回）。SMTC、单实例、IPC、日志的 Windows 代码只是编过，行为没验证；安装包和 zip 要到发布工作流才会打。
- 安装包不签名：第一次运行 SmartScreen 会拦，要点"更多信息 → 仍要运行"。代码签名证书以后再说。

## 6. 合规

- 第三方声明：`packaging/THIRD-PARTY-NOTICES.md`，许可原文在 `packaging/licenses/`（LGPL-2.1、LGPL-3.0、GPL-3.0、Apache-2.0）和各依赖目录；装到包的 `share/doc/cirrus/`（Windows：`licenses\`）。设置 › 关于的"第三方许可"同步列了 FFmpeg、QtKeychain、OpenSSL、zlib、QR Code generator。
- LGPL：全部动态链接、不修改，用户可以替换 `lib/` 里的库。Release 附上 FFmpeg、QtKeychain 的原始源码；声明里写了"三年内应要求提供源码"，Qt 指向官方源码下载（Qt 源码约 1 GB，不附）。
- MiSans：原样随应用分发，不单独分发（字体许可，`ui/theme/fonts/README.md`）。
- **待维护者决定**：
  - 项目自己的许可证（发布阻塞项，`docs/11-security-privacy-and-compliance.md` §7）。在那之前 AppStream 不写 `project_license`，PKGBUILD 是 `LicenseRef-unknown`，安装包没有许可页。
  - 源码提供的承诺（上面"三年内应要求提供"）是否保留，还是只靠 Release 附的源码。
  - deb 的 `Maintainer` 现在只有 `guang233`，没有邮箱。
  - 安全问题的报告渠道（同上文档 §8）。

## 7. 工作流

| 文件 | 何时 | 做什么 |
| --- | --- | --- |
| `.github/workflows/ci.yml` | 推到 master、PR、手动 | Linux：编译应用、gallery、测试（`BUILD_TESTING` 打开），qmllint，ctest（offscreen，`dbus-run-session`）。手动运行时可勾选 Windows（只编译）、macOS（试验：编译和测试，不打包）或"Build packages"（Linux 和 Windows 都打包，放在这次运行的产物里，不建 Release，用来试包） |
| `.github/workflows/windows.yml` | 被另外两个调用 | Windows 编译；打包时再装出目录、打 zip 和安装包 |
| `.github/workflows/macos.yml` | 被 CI 调用（勾选 macOS） | macOS 试验：Apple Silicon 运行器，Qt 来自安装器，FFmpeg、OpenSSL 来自 Homebrew，QtKeychain 用 `packaging/deps/`；编译应用、gallery、测试，qmllint，ctest（排除同 §7.1）。不打包 |
| `.github/workflows/linux-packages.yml` | 被另外两个调用 | 打 deb 和 AppImage，校验桌面文件和 deb，附库的源码 |
| `.github/workflows/release.yml` | 推 `v*` 标签 | 检查标签和版本号、CHANGELOG 一致 → 跑一遍 CI → Linux 打 deb 和 AppImage、校验桌面文件 → Windows 打包 → 创建**草稿** Release，附上全部产物、库源码和 SHA256SUMS |

FFmpeg、QtKeychain、vcpkg 的编译结果都缓存（按脚本和清单的哈希），Qt 由 install-qt-action 缓存。仓库私有时 Windows 运行器按 2 倍、macOS 按 10 倍计分钟，所以日常 CI 不跑这两个。

### 7.1 排除的测试

- `tst_entity_search` 不能编译（2c164a7 起，`entity_search_test.cpp:42` 的 `detail` 有歧义）：放在 `CIRRUS_TEST_ENTITY_SEARCH`（默认 OFF）后面，本地全量构建也因此能通过了。
- `backend_runtime`、`playback_observation`、`playback_observation_feed`、`linux_observation_service` 已知失败：CI 的 ctest 用 `-E` 排除。本地照常跑、照常失败。
- `audio_store` 的 `interruptWakesOneRead` 不稳定（2026-11-10 第一次 CI 发现；本机单独跑 6 次失败 3 次）：测试先读第 4 块再 `interrupt()`，期望读还在等数据；但读请求有时比开头的下载先到，缓存直接从那里下载，第一个数据块就满足了这次读，返回数据而不是 FAILED。也先在 CI 里排除整个 `audio_store`。
- 修好以后从这里和 `ci.yml` 里去掉。

### 7.2 发一个版本

1. 改 `CMakeLists.txt` 的版本号；把 `CHANGELOG.md` 的 Unreleased 改成 `## [x.y.z]`。
2. 提交，打标签 `vx.y.z`，推送提交和标签。
3. 等 Release 工作流跑完，在 GitHub 上检查草稿（试装 deb、AppImage、安装包），再发布。

## 8. 验证情况

- 本机（CachyOS，2026-11-10）：
  - 两个依赖脚本编译通过：FFmpeg 的 `lib/` 约 2.2 MB；QtKeychain 链接随包的 Qt，RUNPATH 是 `$ORIGIN`。
  - `build-packages.sh … stage` 装出的 `/opt/cirrus` 约 174 MB（ICU 约 35 MB、Qt Quick Controls 的全部样式也被部署，以后可以裁）。
  - 包里每个 ELF 的依赖都在包内或系统里找到，没有指向构建机 Qt 或依赖目录的。
  - 不设 `LD_LIBRARY_PATH`、`QT_PLUGIN_PATH` 启动（临时放入 offscreen 插件，测试 XDG 目录）：加载的是包里的 Qt、两份 FFmpeg（61 和 63）、QtKeychain、SQLite 驱动、Wayland 和 xcb 平台插件，系统的只有 OpenSSL、libsecret；日志没有错误。
  - Qt 的部署脚本默认只带 xcb 平台插件：要显式包含 `qwayland`。插件按文件名（去掉 lib 和后缀）指定，不是 CMake 目标名。
  - 桌面文件过了 `desktop-file-validate`，AppStream 过了 `appstreamcli validate`（只剩 ID 含大写的 pedantic 提示）。
- CI（2026-11-11）：Linux 编译、qmllint、测试通过（排除 §7.1 的测试）。GCC 12 比本机的 GCC 16 严格，`std::optional<T>({x})` 要写成 `std::optional<T>(T{x})`。Windows 只编译，通过。
- 未运行：deb、AppImage、Windows 安装包和 zip（都在发布工作流里，还没推过标签）。
