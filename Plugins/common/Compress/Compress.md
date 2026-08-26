# Compress

zstd 压缩/解压纯库——无状态、无单例，静态自由函数，失败返回 `nullopt`。

## 提供

- `Compress(Data, Level = 0)`：zstd 压缩。Level 为 zstd 的 1..22（0 = 默认；负数/快速模式不支持）。
- `Decompress(Data)`：解压；非法 zstd 输入或解压大小不合理时返回 `nullopt`。
- `GetDecompressedSize(Data)`：已知时返回解压后大小（损坏输入失败）。

## 示例

```cpp
std::vector<std::uint8_t> Compressed = Compress::Compress(Raw, 3);
auto Raw = Compress::Decompress(Compressed);
```

## 依赖

- 三方：zstd（引擎三方，编译 C 静态库，include 与链接由构建侧 `MahoDependencies.cmake` 补全）。
- 其他插件：无。
