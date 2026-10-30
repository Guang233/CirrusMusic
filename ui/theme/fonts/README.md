# 随包字体

文字默认使用 MiSans，图标使用 Material Symbols（见 `docs/ui/README.md` §4.2、UQ-03）。两者都由 `tools/ui/prepare_fonts.py` 从固定上游版本获取并校验 SHA-256，不要手工替换。修改图标集合时编辑 `symbols.txt` 后重新运行该脚本，它会同时重新生成 `../Symbols.qml`。

| 文件 | 上游 | 版本 | 许可 | 是否修改 |
| --- | --- | --- | --- | --- |
| `MaterialSymbolsRounded.ttf` | [google/material-design-icons](https://github.com/google/material-design-icons/tree/737e3324305806514d7909874fa1818ae1808232/variablefont) `variablefont/MaterialSymbolsRounded[FILL,GRAD,opsz,wght].ttf`，提交 `737e3324` | 2.973 | Apache License 2.0，见 `MaterialSymbols-LICENSE` | **已修改**：用 fontTools 裁剪为 `symbols.txt` 列出的字形，并移除 OpenType 布局特性（不再支持连字输入）；保留全部可变轴 |

| `MiSansVF.ttf` | 小米 [MiSans](https://hyperos.mi.com/font/download) 字体包 `https://hyperos.mi.com/font-download/MiSans.zip` 中的 `MiSans/可变字体/MiSansVF.ttf` | 4.009 | MiSans 字体知识产权许可协议，见 `MiSans-LICENSE.txt` | 未修改 |

MiSansVF.ttf 的 SHA-256：`0ddef90648998900175cfdca9a6f087a2544c182f130b0ad4f7e94a03a115e79`（2026-10-30 下载的字体包为 `b6aa1fc8…`，字体包本身不固定，网站可能重新打包）。

MiSans 许可的三条要求与做法：在软件中注明使用了 MiSans（设置页字体选项的说明）；不得修改（原样入库、原样打包，不裁剪子集）；不得单独分发字体（只随应用分发，构建时嵌入可执行文件）。以后发行时另出一个不含 MiSans 的小包，构建在缺少这个文件时自动改用系统字体。可变字体的 `wght` 轴为 150–700 无级调节（命名实例 Regular 330、Medium 380、Semibold 520、Bold 630），CSS 字重到轴值的映射见 `../fonts.cpp`。

Material Symbols 上游原始文件 SHA-256：`95b24392bb49efd1bc3e92cff4e2452ad094461bab7c97e7d8723fab97e330ca`

Material Symbols 的子集属于修改后的文件，再分发时须保留本说明与 Apache 2.0 许可文本。

## 曾评估、未打包的文字字体

| 字体 | 来源 | 许可 | 备注 |
| --- | --- | --- | --- |
| Google Sans Flex 4.005 | google/fonts 提交 `a0e3dbcd`，`ofl/googlesansflex/GoogleSansFlex[GRAD,ROND,opsz,slnt,wdth,wght].ttf`，SHA-256 `c31a482fbecbf2e07e6890134d20078723aadf732c9b9c6c9a44f86f8265b6fe`，约 4.1 MB | OFL-1.1，另有商标说明 | 只覆盖 535 个拉丁字符；商标说明限制修改版使用其名称，若采用应原样分发。阶段 0 曾短暂引入（提交 `8240729`），已撤出 |
| Noto Sans SC（可变） | google/fonts `ofl/notosanssc/NotoSansSC[wght].ttf`，约 17.8 MB | OFL-1.1 | 中文显示统一的方案，未下载 |
