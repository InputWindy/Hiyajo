# Compress（Private）— 实现算法字典

三个自由函数对 zstd C API 的薄封装。公开 API 签名入口见 API.md。

## Compress.cpp

<a id="fn-compress"></a>
### Compress(const vector<uint8_t>& Data, int Level)

← [公开 API](API.md) · `std::optional<std::vector<std::uint8_t>>`

zstd 压缩。空输入直接返回空 vector；否则算压缩上界、压缩、失败 → `nullopt`，成功按实际写入尺寸 resize。

```text
Compress(Data, Level):
1. if Data.empty(): return {}                          // 空输入 → 空结果（合法）
2. Bound = ZSTD_compressBound(Data.size())
3. if ZSTD_isError(Bound): return nullopt
4. Out = vector<uint8_t>(Bound)
5. Written = ZSTD_compress(Out.data(), Out.size(), Data.data(), Data.size(), Level)
6. if ZSTD_isError(Written): return nullopt
7. Out.resize(Written)
8. return Out
```

<a id="fn-decompress"></a>
### Decompress(const vector<uint8_t>& Data)

← [公开 API](API.md) · `std::optional<std::vector<std::uint8_t>>`

先取解压尺寸再解压。尺寸不可知 → `nullopt`；解压出错 → `nullopt`。

```text
Decompress(Data):
1. Size = GetDecompressedSize(Data)
2. if !Size: return nullopt
3. Out = vector<uint8_t>(*Size)
4. Written = ZSTD_decompress(Out.data(), Out.size(), Data.data(), Data.size())
5. if ZSTD_isError(Written): return nullopt
6. Out.resize(Written)
7. return Out
```

<a id="fn-getdecompressedsize"></a>
### GetDecompressedSize(const vector<uint8_t>& Data)

← [公开 API](API.md) · `std::optional<std::size_t>`

`ZSTD_getFrameContentSize` 封装：错误码 / 帧头损坏 / 尺寸未知 → `nullopt`。

```text
GetDecompressedSize(Data):
1. Size = ZSTD_getFrameContentSize(Data.data(), Data.size())
2. if ZSTD_isError(Size) || Size == ZSTD_CONTENTSIZE_ERROR || Size == ZSTD_CONTENTSIZE_UNKNOWN:
3.     return nullopt
4. return size_t(Size)
```

- [Compress.md](Compress.md) — 概念 · [公开 API](API.md) — 签名入口
