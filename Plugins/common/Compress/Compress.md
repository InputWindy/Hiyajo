# Compress

## 代码文件

- [Compress.h](Compress.h) — zstd 压缩纯库（`Compress` / `Decompress` / `GetDecompressedSize`）

## 概念——zstd 压缩

zstd 压缩/解压**纯库**——无状态、无单例，三个静态自由函数，失败返回 `nullopt`（不抛异常）。**zstd 是引擎三方**（编译 C 静态库，include 与链接由构建侧 `Build/CMake/MahoDependencies.cmake` 经 Maho PUBLIC 链接补全）。

### 自由函数

- `Compress(Data, Level = 0)`：zstd 压缩字节流。Level 为 zstd 的 1..22（0 = 默认；负数/快速模式不支持）。
- `Decompress(Data)`：解压；输入非合法 zstd 或解压大小不可算/不合理时返回 `nullopt`。
- `GetDecompressedSize(Data)`：已知时返回解压后大小（损坏输入失败）。

```cpp
std::vector<std::uint8_t> Compressed = Compress::Compress(Raw, 3);
auto Raw = Compress::Decompress(Compressed);          // optional<vector<uint8_t>>
const auto Size = Compress::GetDecompressedSize(Compressed);
```

## 三方依赖

- **zstd**（`zstd.h`，引擎三方编译静态库）——C 压缩库。

## 相关文档

- [API.html](API.html) — API 文档
