# Compress

## Code files

- [Compress.h](Compress.h) - zstd compression pure library (`Compress` / `Decompress` / `GetDecompressedSize`)

## Concept - zstd compression

zstd compress/decompress **pure library** - no state, no singleton, three static free functions, failure returns `nullopt` (no exceptions). **zstd is engine third-party** (compiled C static library; include and linking are provided by the build side `Build/CMake/MahoDependencies.cmake` through Maho PUBLIC linking).

### Free functions

- `Compress(Data, Level = 0)`: compress a byte stream with zstd. Level is zstd's 1..22 (0 = default; negative/fast modes unsupported).
- `Decompress(Data)`: decompress; returns `nullopt` when the input is not valid zstd or the decompressed size cannot be computed / is implausible.
- `GetDecompressedSize(Data)`: returns the decompressed size when known (fails on corrupt input).

```cpp
std::vector<std::uint8_t> Compressed = Compress::Compress(Raw, 3);
auto Raw = Compress::Decompress(Compressed);          // optional<vector<uint8_t>>
const auto Size = Compress::GetDecompressedSize(Compressed);
```

## Third-party dependencies

- **zstd** (`zstd.h`, engine third-party compiled static library) - C compression library.

## Related docs

- [API.html](API.html) - API documentation
