# material-color-utilities（C++ 子集）

- 上游：<https://github.com/material-foundation/material-color-utilities>
- 固定提交：`5b3618b16fdc3825e21d5679bafd144662088ea1`（2026-08-21）。`cpp/` 目录最后一次上游改动是 2024-10-01。
- 许可：Apache License 2.0，见 `LICENSE`。
- 范围：`cpp/` 下除 `*_test.cc` 以外的全部源码，目录结构保持不变（上游以 `cpp/...` 为包含路径根）。
- 用途：Cirrus UI 主题系统的封面取色与 MD3 配色方案（`ui/theme/`），只被 UI 链接。

## 本地修改

共三处修改，均在代码中以 `Cirrus:` 注释标出：

1. `cpp/quantize/wsmeans.cc`：为不引入 Abseil，`absl::flat_hash_map<Argb, int>` 改为 `std::unordered_map<Argb, int>`，删除对应 include。上游注释给出的耗时差异约 10.2 ms 对 8.0 ms（112×112 像素、128 色），在后台线程中可以接受。
2. `cpp/utils/utils.cc`：同上，`HexFromArgb` 由 `absl::StrCat(absl::Hex(argb))` 改为 `snprintf("%x")`，输出格式相同（小写、无前导零）。
3. `cpp/temperature/temperature_cache.h`：补上缺失的 `#include <optional>`（上游依赖其构建环境的传递包含，GCC 下无法编译）。

## 已知差距

C++ 版本只实现 2021 规范的动态配色（含 2024 年的 Expressive on-color 调整），没有 TS/Java 版本中的 `SpecVersion`（2025、2026）与 `Platform` 参数。比较结果见 `docs/ui/theme.md`。

## 更新方法

```bash
git clone --filter=blob:none --no-checkout https://github.com/material-foundation/material-color-utilities.git build/reference/mcu
git -C build/reference/mcu checkout --detach <新提交>
```

复制 `cpp/` 中非测试文件覆盖本目录，重新应用上述修改，更新本文的提交号，并重新运行 Gallery 主题页检查配色。
