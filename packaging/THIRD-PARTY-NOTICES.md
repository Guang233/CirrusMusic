# Cirrus: third-party software

Cirrus is a third-party NetEase Cloud Music client. It is not affiliated with, endorsed
by or sponsored by NetEase.

Cirrus uses the following software. The license texts are in `licenses/` next to this
file. Libraries under the LGPL are linked dynamically and are not modified; you may
replace them with compatible builds of your own (in a package, the files in `lib/` on
Linux or next to `Cirrus.exe` on Windows).

| Component | Version | License | Use |
| --- | --- | --- | --- |
| Qt | 6.11 | LGPL-3.0-only (`licenses/LGPL-3.0-only.txt`, `licenses/GPL-3.0-only.txt`) | Application framework |
| FFmpeg (bundled by Qt Multimedia) | 7.1 | LGPL-2.1-or-later (`licenses/LGPL-2.1-or-later.txt`) | Audio output backend of Qt Multimedia |
| FFmpeg | 9.0 | LGPL-2.1-or-later (`licenses/LGPL-2.1-or-later.txt`) | Audio decoding (MP3, FLAC) |
| QtKeychain | 0.17 | BSD-3-Clause (below) | Storing the sign-in in the system keyring |
| OpenSSL | 3 | Apache-2.0 (`licenses/Apache-2.0.txt`) | Cryptography for the service protocol |
| zlib | 1.x | Zlib (below) | Decompression |
| MiSans (Xiaomi) | 4.009 | MiSans font license (`licenses/MiSans-LICENSE.txt`) | Interface font, unmodified |
| Material Symbols (Google) | 2.973 | Apache-2.0 | Icons; subset to the glyphs Cirrus uses |
| material-color-utilities (Google) | `5b3618b` | Apache-2.0 | Colour schemes |
| m3shapes | `8a6fe89` | Apache-2.0 | Shapes and morphing |
| QR Code generator library (Project Nayuki) | 1.8.0 | MIT (`licenses/qrcodegen-MIT.txt`) | Sign-in QR code |
| NeteaseCloudMusicApi Enhanced | `a8c781f` | MIT (`licenses/api-enhanced-MIT.txt`) | Reference for the service's interface |

Which libraries a package carries:

- **deb, AppImage, Windows installer and zip** carry Qt, both FFmpeg builds and
  QtKeychain. The Windows packages also carry OpenSSL, zlib and the Microsoft Visual C++
  runtime. On Linux, OpenSSL, zlib and libsecret come from the system (the AppImage may
  carry them).
- **AUR and other source builds** use the system's libraries.

## Sources

The bundled libraries are unmodified releases:

- Qt 6.11: <https://download.qt.io/archive/qt/6.11/> (the official binaries; Qt
  Multimedia's FFmpeg 7.1 is part of them).
- FFmpeg 9.0.2: <https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz>. Linux packages use
  the configuration in `packaging/deps/build-ffmpeg.sh` of the Cirrus source (shared,
  LGPL, MP3 and FLAC only); Windows packages use vcpkg's `ffmpeg` port with the features
  `avcodec`, `avformat` and `swresample` (no `gpl`).
- QtKeychain 0.17.0: <https://github.com/frankosterfeld/qtkeychain/releases/tag/0.17.0>
- OpenSSL 3 and zlib on Windows: vcpkg's `openssl` and `zlib` ports.

On request, the maintainer provides the exact source of every LGPL library in a
release for at least three years after it.

## QtKeychain

```
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the author may not be used to
   endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## zlib

```
(C) 1995-2024 Jean-loup Gailly and Mark Adler

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```
