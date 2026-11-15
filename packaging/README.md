# 打包与发布

Cirrus 在 Linux 上出 deb 和 AppImage，Windows 上出安装包和免安装的 zip，Arch Linux 另有 AUR 的 PKGBUILD。安装包里只有应用本身，没有测试和 gallery。

## 产物

| 产物 | 文件名 | 内容 |
| --- | --- | --- |
| deb | `cirrus_<版本>_amd64.deb` | 装到 `/opt/cirrus`，自带 Qt；`/usr/bin/cirrus` 是链接；桌面文件、图标、AppStream 装在 `/usr/share` |
| AppImage | `Cirrus-<版本>-x86_64.AppImage` | 和 deb 同一套目录，另外带上基础系统以外的库 |
| AUR | `packaging/aur/PKGBUILD`（`cirrus`） | 用 Arch 的 Qt 和库从源码编译 |
| Windows 安装包 | `Cirrus-<版本>-windows-x64-setup.exe` | Inno Setup；默认只装给当前用户（不需要管理员），也可以选所有用户 |
| Windows 免安装版 | `Cirrus-<版本>-windows-x64.zip` | 解压即用；设置和数据仍在用户目录 |
| 库的源码 | `Cirrus-<版本>-library-sources.tar` | 包里带的 FFmpeg、QtKeychain 的原始源码（LGPL 的源码提供） |
| 校验 | `SHA256SUMS.txt` | |

版本号遵循语义化版本，唯一来源是 `CMakeLists.txt` 里的 `project(CirrusMusic VERSION x.y.z)`；标签是 `vx.y.z`；`CHANGELOG.md` 里每个版本一节，用作 Release 的说明。

## 安装规则

安装规则在 `cmake/CirrusInstall.cmake`。打包时用 `-DBUILD_TESTING=OFF -DCIRRUS_BUILD_GALLERY=OFF`，测试、gallery 和 `tools/` 都不编译。

| 选项 | 默认 | 作用 |
| --- | --- | --- |
| `CIRRUS_EXECUTABLE` | `appCirrusMusic` | 可执行文件名；包里 Linux 是 `cirrus`，Windows 是 `Cirrus` |
| `CIRRUS_DEPLOY_QT` | OFF | 把 Qt 运行库和不在系统目录的库一起装进去（deb、AppImage、Windows 用，AUR 不用） |
| `CIRRUS_RUNTIME_DIRS` | 空 | 仅 Windows：去哪里找 FFmpeg、QtKeychain、OpenSSL、zlib、zstd 的 DLL |

Linux 上装的东西：`bin/cirrus`；`share/applications/com.guang233.Cirrus.desktop`；`share/metainfo/com.guang233.Cirrus.metainfo.xml`；`share/icons/hicolor/…/apps/com.guang233.Cirrus.{png,svg}`；`share/doc/cirrus/` 下的第三方声明和许可证。桌面文件和 AppStream 的模板在 `packaging/linux/`。

Wayland 下 Dock 图标由合成器按 app ID 从桌面文件里取。开发构建没有桌面文件，用 `tools/ui/install-dev-desktop-entry.sh` 装一个指向开发构建的（装正式包前用 `--remove` 去掉）。

## 随包的库

版本和 SHA-256 固定在 `packaging/deps/versions.sh`。

| 库 | 来源 | 说明 |
| --- | --- | --- |
| Qt 6.11.2 | Qt 官方二进制，模块 qtmultimedia、qtshadertools、qtimageformats | Linux 版要 glibc 2.34 |
| FFmpeg 9.0.2（Linux） | `packaging/deps/build-ffmpeg.sh` 从源码编译 | 共享库、LGPL，只开 MP3 和 FLAC |
| FFmpeg 9.0.2（Windows） | vcpkg 的 `ffmpeg`（avcodec、avformat、swresample） | 不开 gpl、nonfree |
| QtKeychain 0.17.0 | `packaging/deps/build-qtkeychain.sh`，针对随包的 Qt 编译 | 发行版的 QtKeychain 是针对它们自己的 Qt 编的，不能混用。Windows 上静态链接 |
| OpenSSL 3、zlib、zstd | Linux 用系统的；Windows 用 vcpkg | |

包里有两份 FFmpeg：Qt Multimedia 自带的 FFmpeg 7.1（`libavcodec.so.61`）用于音频输出，Cirrus 的解码器用自己编的 9.0（`libavcodec.so.63`）。大版本不同，库名不同，同时加载不冲突。依赖库的 RUNPATH 是 `$ORIGIN`，在包的 `lib/` 里互相能找到。

## Linux

构建机是 Ubuntu 22.04（glibc 2.35，GCC 12），包能在 Ubuntu 22.04、Debian 12 及更新的系统上运行。

```bash
packaging/linux/install-build-deps.sh                  # 系统依赖（Ubuntu 22.04）
packaging/deps/build-ffmpeg.sh <deps>
packaging/deps/build-qtkeychain.sh <deps> <Qt 前缀>
JOBS=$(nproc) packaging/linux/build-packages.sh <Qt 前缀> <deps> <输出目录> [deb] [appimage] [stage]
```

`stage` 只编译并装出 `/opt/cirrus` 那套目录（`build/package-linux/stage`），可以在任何发行版上检查。`JOBS` 是并行编译的任务数（默认 4）。

- **deb**：`/opt/cirrus` 自成一体，因为发行版的 Qt 比 6.11 旧。`Depends` 由 `dpkg-shlibdeps` 按包里的程序和库算出，另加 `ca-certificates`（Qt 的 TLS 用系统的根证书）；`Recommends` 一个钥匙串服务。只 strip 还带符号的文件：再 strip 一遍 Qt 已经处理过的库，会弄坏它自带的 `libavutil`。
- **AppImage**：同一套目录装到 `AppDir/usr`，再用 linuxdeploy（固定版本，构建时下载）补上基础系统以外的库并打包。没有自动更新信息。
- **AUR**：从 GitHub 的标签源码包编译，依赖 Arch 的 `qt6-*`、`qtkeychain-qt6`、`ffmpeg`，`/usr` 布局。Cirrus 用了 Qt 的私有 Wayland 客户端接口，Arch 的 Qt 每升一个小版本都要重新编译（`pkgrel` 加一）。

在本机干净的环境里检查：用 `ubuntu:22.04` 容器按上面的命令打包，再在另一个全新的 `ubuntu:22.04` 容器里 `apt install` 生成的 deb，在 Xvfb 上启动。

## Windows

MSVC（VS 2022）、Qt `win64_msvc2022_64`、vcpkg（`packaging/windows/vcpkg.json`，固定 baseline），QtKeychain 用同一个脚本在 Git Bash 里编译。安装时 windeployqt 部署 Qt，`CIRRUS_RUNTIME_DIRS` 里的 DLL 和 MSVC 运行库一起放到 `Cirrus.exe` 旁边，不需要另装 vc_redist。安装程序脚本是 `packaging/windows/cirrus.iss`（`AppId` 永远不改）。

安装包没有签名：第一次运行时 SmartScreen 会拦，点“更多信息 → 仍要运行”。

## 许可与合规

- Cirrus 本身是 MIT 许可证（根目录 `LICENSE`）。第三方声明在 `packaging/THIRD-PARTY-NOTICES.md`，许可原文在 `packaging/licenses/`，都随包安装（Linux `share/doc/cirrus/`，Windows `licenses\`）。
- LGPL 的库全部动态链接、不加修改，用户可以替换包里的库；Release 附上 FFmpeg、QtKeychain 的原始源码。
- MiSans 字体原样随应用分发，不单独分发（见 `ui/theme/fonts/README.md`）。

## 工作流

| 文件 | 何时 | 做什么 |
| --- | --- | --- |
| `.github/workflows/ci.yml` | 推到 master、PR、手动 | Linux：编译应用、gallery 和测试，qmllint，ctest。手动运行时可加 Windows（只编译）、macOS（编译和测试）或打包（Linux 和 Windows 都打包，不建 Release） |
| `.github/workflows/windows.yml` | 被 CI 和发布调用 | Windows 编译；打包时出 zip 和安装包 |
| `.github/workflows/macos.yml` | 被 CI 调用 | macOS 编译和测试，不打包 |
| `.github/workflows/linux-packages.yml` | 被 CI 和发布调用 | 打 deb 和 AppImage，校验桌面文件和 deb，附库的源码 |
| `.github/workflows/release.yml` | 推 `v*` 标签 | 检查标签和版本号、CHANGELOG 一致 → 跑一遍 CI → 打 Linux 和 Windows 的包 → 建一个**草稿** Release，附上全部产物、库的源码和 SHA256SUMS |

FFmpeg、QtKeychain、vcpkg 的编译结果按脚本和清单的哈希缓存，Qt 由 install-qt-action 缓存。

## 发一个版本

1. 改 `CMakeLists.txt` 的版本号；把 `CHANGELOG.md` 的 Unreleased 改成 `## [x.y.z]`。
2. 提交，打标签 `vx.y.z`，推送提交和标签。
3. 等发布工作流跑完，在 GitHub 上检查草稿（试装 deb、AppImage、安装包），再发布。
