# 随包字体

本目录的字体文件由 `tools/ui/prepare_fonts.py` 从固定上游版本获取并校验 SHA-256 后生成，不要手工替换。修改图标集合时编辑 `symbols.txt` 后重新运行该脚本，它会同时重新生成 `../Symbols.qml`。

| 文件 | 上游 | 版本 | 许可 | 是否修改 |
| --- | --- | --- | --- | --- |
| `GoogleSansFlex.ttf` | [google/fonts](https://github.com/google/fonts/tree/a0e3dbcdc3a3ecfafff3f071159ae0221628922d/ofl/googlesansflex) `ofl/googlesansflex/GoogleSansFlex[GRAD,ROND,opsz,slnt,wdth,wght].ttf`，提交 `a0e3dbcd` | 4.005 | SIL OFL 1.1，见 `GoogleSansFlex-OFL.txt`；商标说明见 `GoogleSansFlex-TRADEMARKS.md` | 未修改，仅重命名文件 |
| `MaterialSymbolsRounded.ttf` | [google/material-design-icons](https://github.com/google/material-design-icons/tree/737e3324305806514d7909874fa1818ae1808232/variablefont) `variablefont/MaterialSymbolsRounded[FILL,GRAD,opsz,wght].ttf`，提交 `737e3324` | 2.973 | Apache License 2.0，见 `MaterialSymbols-LICENSE` | **已修改**：用 fontTools 裁剪为 `symbols.txt` 列出的字形，并移除 OpenType 布局特性（不再支持连字输入）；保留全部可变轴 |

上游原始文件 SHA-256：

- `GoogleSansFlex[GRAD,ROND,opsz,slnt,wdth,wght].ttf`：`c31a482fbecbf2e07e6890134d20078723aadf732c9b9c6c9a44f86f8265b6fe`
- `MaterialSymbolsRounded[FILL,GRAD,opsz,wght].ttf`：`95b24392bb49efd1bc3e92cff4e2452ad094461bab7c97e7d8723fab97e330ca`

注意事项：

- Google Sans Flex 4.005 只覆盖 535 个拉丁字符，不含中文、西里尔、希腊字母，这些字符依赖系统字体回退（见 `docs/ui/README.md` 的 UQ-03）。
- “Google”“Google Sans”“Google Sans Flex” 是 Google LLC 的商标；不得用于产品名称或暗示与 Google 有关联。为避免触及修改版的商标限制，该字体保持原样分发。
- Material Symbols 的子集属于修改后的文件，再分发时须保留本说明与 Apache 2.0 许可文本。
