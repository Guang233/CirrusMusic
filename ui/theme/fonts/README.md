# 随包字体

文字使用系统 UI 字体，**不随包附带文字字体**（见 `docs/ui/README.md` 的 UQ-03）。本目录只有图标字体，由 `tools/ui/prepare_fonts.py` 从固定上游版本获取并校验 SHA-256 后生成，不要手工替换。修改图标集合时编辑 `symbols.txt` 后重新运行该脚本，它会同时重新生成 `../Symbols.qml`。

| 文件 | 上游 | 版本 | 许可 | 是否修改 |
| --- | --- | --- | --- | --- |
| `MaterialSymbolsRounded.ttf` | [google/material-design-icons](https://github.com/google/material-design-icons/tree/737e3324305806514d7909874fa1818ae1808232/variablefont) `variablefont/MaterialSymbolsRounded[FILL,GRAD,opsz,wght].ttf`，提交 `737e3324` | 2.973 | Apache License 2.0，见 `MaterialSymbols-LICENSE` | **已修改**：用 fontTools 裁剪为 `symbols.txt` 列出的字形，并移除 OpenType 布局特性（不再支持连字输入）；保留全部可变轴 |

上游原始文件 SHA-256：`95b24392bb49efd1bc3e92cff4e2452ad094461bab7c97e7d8723fab97e330ca`

Material Symbols 的子集属于修改后的文件，再分发时须保留本说明与 Apache 2.0 许可文本。

## 曾评估、未打包的文字字体

如果以后决定随包附带文字字体，在 `ui/theme/fonts.cpp` 的 `textFamily()` 处注册即可，QML 侧统一通过 `Fonts.sans` 取字体名，不需要改动。

| 字体 | 来源 | 许可 | 备注 |
| --- | --- | --- | --- |
| Google Sans Flex 4.005 | google/fonts 提交 `a0e3dbcd`，`ofl/googlesansflex/GoogleSansFlex[GRAD,ROND,opsz,slnt,wdth,wght].ttf`，SHA-256 `c31a482fbecbf2e07e6890134d20078723aadf732c9b9c6c9a44f86f8265b6fe`，约 4.1 MB | OFL-1.1，另有商标说明 | 只覆盖 535 个拉丁字符；商标说明限制修改版使用其名称，若采用应原样分发。阶段 0 曾短暂引入（提交 `8240729`），已撤出 |
| Noto Sans SC（可变） | google/fonts `ofl/notosanssc/NotoSansSC[wght].ttf`，约 17.8 MB | OFL-1.1 | 中文显示统一的方案，未下载 |
