# Compress

## Code files

- [Compress.h](Public/Compress.h) — zstd 压缩/解压纯库自由函数（`Compress` / `Decompress` / `GetDecompressedSize`）
- [Compress.cpp](Private/Compress.cpp) — zstd C API 薄封装实现

## Concept - zstd 压缩

zstd 压缩/解压**纯库**——无状态、无单例，三个自由函数，失败返回 `nullopt`（不抛异常）。**zstd 是引擎级第三方**：编译型 C 静态库，由构建侧 `Build/CMake/MahoDependencies.cmake` 拉取并经 Maho PUBLIC 传递链接，本插件直接 `#include <zstd.h>` 即可。

### Compress — 压缩

`Compress(Data, Level = 0)`：zstd 压缩整段字节。`Level` 是 zstd 的 1..22（0 = 默认；负/快速模式不支持）。空输入返回空 vector（合法结果而非错误）；编码错误返回 `nullopt`。

### Decompress — 解压

`Decompress(Data)`：先经 `GetDecompressedSize` 拿到目标尺寸，再 `ZSTD_decompress`。输入非合法 zstd 或尺寸无法计算 → `nullopt`。

### GetDecompressedSize — 解压尺寸

`GetDecompressedSize(Data)`：`ZSTD_getFrameContentSize` 的封装——错误 / `ZSTD_CONTENTSIZE_ERROR` / `ZSTD_CONTENTSIZE_UNKNOWN` → `nullopt`。

```cpp
std::vector<std::uint8_t> Raw = /* ... */;
auto Compressed = Compress::Compress(Raw, 3);        // optional<vector<uint8_t>>
auto Recovered  = Compress::Decompress(*Compressed); // optional<vector<uint8_t>>
const auto Size = Compress::GetDecompressedSize(*Compressed);
```

## Third-party dependencies

- **zstd**（`zstd.h`，引擎级编译型 C 静态库，MahoDependencies.cmake 拉取 + Maho PUBLIC 传递链接）— 压缩库。

## Related docs

- [API.md](API.md) - API documentation
