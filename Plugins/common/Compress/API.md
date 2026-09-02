# Compress — API 文档

Compress 插件 = zstd 压缩/解压纯库（namespace `Maho::Compress`）。三个自由函数，无状态、无单例、无生命周期。失败一律返回 `std::nullopt`，不抛异常。zstd 是引擎级第三方——编译型 C 静态库，由 `Build/CMake/MahoDependencies.cmake` 拉取并经 Maho PUBLIC 传递链接，本插件不 FetchContent。

## Compress / Decompress / GetDecompressedSize <自由函数>

zstd 的三个薄封装：压缩、解压、查解压尺寸。全部 `[[nodiscard]]`。

#### 接口

| 签名 | 说明 |
|------|------|
| `[[nodiscard]] std::optional<std::vector<std::uint8_t>> Compress(const std::vector<std::uint8_t>& Data, int Level = 0)` | zstd 压缩。`Level` 为 zstd 的 1..22（0 = 默认；负/快速模式不支持）；目标尺寸过小或编码错误 → `nullopt`；空输入返回空 vector |
| `[[nodiscard]] std::optional<std::vector<std::uint8_t>> Decompress(const std::vector<std::uint8_t>& Data)` | zstd 解压。输入非合法 zstd / 解压尺寸无法计算或离谱 → `nullopt` |
| `[[nodiscard]] std::optional<std::size_t> GetDecompressedSize(const std::vector<std::uint8_t>& Data)` | 解压后尺寸（已知时）；损坏输入 → `nullopt` |

- [Compress.md](Compress.md) — 概念 · [实现字典](ImplAPI.md) — 算法
