# m3shapes

- 上游：<https://github.com/soramanew/m3shapes>
- 固定提交：`8a6fe8961749887d677700b6508e0c9249968b7e`（2026-09-18）。
- 许可：Apache License 2.0，见 `LICENSE`。移植自 androidx `graphics-shapes`（同为 Apache-2.0）。
- 范围：上游 `include/`、`src/`（含两个着色器）原样复制，未做修改。未包含上游的 `CMakeLists.txt`、示例、Nix 文件。
- 构建：`CMakeLists.txt` 由 Cirrus 自写，把同一组源码构建为静态 QML 模块（URI `M3Shapes`，类型 `MaterialShape`）。上游工程构建共享库、带安装规则，并用仅 GCC/Clang 可用的全局警告选项，不能直接 `add_subdirectory`。
- 用途：Cirrus UI 的 MD3E 形状与变形（`Cirrus.Controls` 的 `MorphShape`、`LoadingIndicator`），只被 UI 链接。评估记录见 `docs/ui/theme.md` §7。
- 渲染限制：使用自定义着色器，Qt Quick 软件渲染后端下不绘制。`Cirrus.Controls` 在软件渲染时改用普通圆角矩形（见 `docs/ui/controls.md`）。

## 更新方法

```bash
git clone https://github.com/soramanew/m3shapes.git build/reference/m3shapes
git -C build/reference/m3shapes checkout --detach <新提交>
rm -rf third_party/m3shapes/include third_party/m3shapes/src
cp -r build/reference/m3shapes/{include,src,LICENSE} third_party/m3shapes/
```

更新后核对源文件清单是否变化（同步 `CMakeLists.txt`），并重新做 Gallery 截图检查。
