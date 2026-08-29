# Archive

## Code files

- [Archive.h](Archive.h) - binary serialization pure library (`EArchiveMode` / `FArchive` / `ISerialize` / `FMemoryReader` / `FMemoryWriter`)

## Concept - binary serialization

Binary serialization **pure library** - abstract read/write stream + memory byte streams + self-serialization interface. **No singleton, no state, zero third-party**, header-only implementation, compiled into users. `FArchive` is the bridge between raw bytes and typed data: write direction streams data with `<<`, read direction recovers from the stream.

### FArchive - abstract read/write stream

- Virtual interface: `Serialize(void*, size_t)` (memcpy semantics) / `Seek` / `Tell`; `IsReading / IsWriting` distinguish direction.
- Built-in `operator<<`: int32/uint32/int64/uint64/float/double/bool/`std::string` (string prefixed with a uint32 length).
- Generic POD template `operator<<(T&)`: `static_assert` trivially copyable (e.g. `glm::vec3`), serialized as a whole block.

### ISerialize - self-serialization interface

Types implement `void Serialize(FArchive& Ar)` and push all fields out with `Ar <<` - the same function handles both read and write.

### FMemoryReader / FMemoryWriter - memory byte streams

- `FMemoryReader`: holds an **external** buffer reference (the buffer must outlive the reader); out-of-bounds reads are silently skipped.
- `FMemoryWriter`: writes into an **owned** buffer; `GetBytes()` views / `TakeBytes()` moves it away.

```cpp
// Writing
FMemoryWriter Writer;
int X = 42; Writer << X;
auto Bytes = Writer.TakeBytes();

// Reading
FMemoryReader Reader(Bytes);
int Y = 0; Reader << Y;   // Y == 42
```

## Third-party dependencies

- None (pure std).

## Related docs

- [API.html](API.html) - API documentation
