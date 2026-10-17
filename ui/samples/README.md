# 示例数据

供 Gallery 和原型阶段的假数据实现使用，不链接进正式应用。所有内容由 `tools/ui/generate_samples.py` 生成：

- `covers/*.jpg`：程序绘制的抽象封面，覆盖高饱和、低饱和、单色、近白、高对比、多色等情况，用于测试取色。
- `catalog.json`：虚构的歌手、专辑、歌曲、歌单。刻意包含边界情况：缺失封面（`al-13`、`pl-05`）、超长标题、中英日混排、未知时长（`so-008`）、不可播放（`so-012`）、空歌单（`pl-05`）。
- `lyrics/*.json`：虚构歌词，含翻译、拼音、空白间奏行和超长行；逐字时间为均匀合成。
- `Samples.qml`：QML 访问入口（`Samples.cover(name)`、`Samples.lyrics(songId)`、`Samples.catalog`）。

全部名称、封面与歌词均为本项目原创，不对应任何真实作品。修改时编辑生成脚本后重新运行，不要手工改生成文件。
