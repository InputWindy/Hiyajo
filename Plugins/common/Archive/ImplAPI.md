# Archive（Private）— 实现算法字典

内建 typed 序列化算子与内存字节流实现。公开 API 签名入口见 API.md。

## Archive.cpp

<a id="fn-archive-op-pod"></a>
### FArchive::operator<<(std::int32_t& / std::uint32_t& / std::int64_t& / std::uint64_t& / float& / double& / bool&)

← [公开 API](API.md) · `FArchive&`

7 个数值/布尔内建重载：整块 memcpy 原样读写。直接 `Serialize(&V, sizeof(V))`，返回 `*this` 以支持链式 `<<`。

```text
operator<<(V):
1. Serialize(&V, sizeof(V))   // 原样读写
2. return *this
```

<a id="fn-archive-op-string"></a>
### FArchive::operator<<(std::string& V)

← [公开 API](API.md) · `FArchive&`

字符串带长度前缀。写入方向：先写 `uint32` 长度再写字节；读取方向：先读长度、resize、再读字节。

```text
operator<<(V):
1. Len = uint32(V.size())
2. *this << Len                              // 长度前缀
3. if IsReading(): V.resize(Len)
4. if Len > 0: Serialize(V.data(), Len)
5. return *this
```

<a id="fn-archive-reader-serialize"></a>
### FMemoryReader::Serialize(void* Out, size_t Size)

← [公开 API](API.md) · `void`

从外部缓冲拷出。越界读静默返回（TODO: 报告越界），不抛异常。

```text
Serialize(Out, Size):
1. if Pos + Size > Data.size(): return        // 越界，静默跳过
2. memcpy(Out, Data.data() + Pos, Size)
3. Pos += Size
```

<a id="fn-archive-reader-seek"></a>
### FMemoryReader::Seek(size_t InPos)

← [公开 API](API.md) · `void`

设置内部位置；不做边界校验（越界由后续 Serialize 兜底）。

```text
Seek(InPos):
1. Pos = InPos
```

<a id="fn-archive-reader-tell"></a>
### FMemoryReader::Tell() const

← [公开 API](API.md) · `size_t`

```text
Tell():
1. return Pos
```

<a id="fn-archive-writer-serialize"></a>
### FMemoryWriter::Serialize(void* In, size_t Size)

← [公开 API](API.md) · `void`

追加写入自有缓冲：扩容后 memcpy 到尾部。

```text
Serialize(In, Size):
1. Old = Buffer.size()
2. Buffer.resize(Old + Size)
3. memcpy(Buffer.data() + Old, In, Size)
```

<a id="fn-archive-writer-seek"></a>
### FMemoryWriter::Seek(size_t InPos)

← [公开 API](API.md) · `void`

定位：只支持**截断**（`InPos < 当前大小` 时 resize 到 `InPos`），不能回写中间位置。

```text
Seek(InPos):
1. if InPos < Buffer.size(): Buffer.resize(InPos)
```

<a id="fn-archive-writer-tell"></a>
### FMemoryWriter::Tell() const

← [公开 API](API.md) · `size_t`

```text
Tell():
1. return Buffer.size()
```

<a id="fn-archive-writer-getbytes"></a>
### FMemoryWriter::GetBytes() const

← [公开 API](API.md) · `const vector<uint8_t>&`

只读访问自有缓冲（不转移所有权）。

```text
GetBytes():
1. return Buffer
```

<a id="fn-archive-writer-takebytes"></a>
### FMemoryWriter::TakeBytes()

← [公开 API](API.md) · `vector<uint8_t>`

移动取走缓冲——调用后 Writer 内部缓冲为空，后续写入从头开始。

```text
TakeBytes():
1. return std::move(Buffer)
```

- [Archive.md](Archive.md) — 概念 · [公开 API](API.md) — 签名入口
