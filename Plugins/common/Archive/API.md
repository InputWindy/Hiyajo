# Archive — API 文档

Archive 插件 = 二进制序列化纯库（namespace `Maho::Archive`）。抽象读写流 `FArchive` + 内存字节流 `FMemoryReader`/`FMemoryWriter` + 自序列化接口 `ISerialize`。零第三方依赖（pure std）、无生命周期、无单例。接口声明集中在 `Archive.h`，内建 typed 序列化算子在 `Archive.cpp` 编译进插件目标。

## EArchiveMode <enum class>

串行化方向。`FArchive` 构造时固定，`IsReading` / `IsWriting` 查询。

| 值 | 说明 |
|------|------|
| `Read = 0` | 读取方向（从流中恢复数据） |
| `Write = 1` | 写入方向（把数据写进流） |

## FArchive <class>

二进制序列化流——原始字节与类型化数据之间的桥。抽象基类：原始字节协议（`Serialize`/`Seek`/`Tell`）由子类实现，typed `operator<<` 建立在原始协议之上。`<<` 同一套写法即写即读，方向由构造时的 `EArchiveMode` 决定。

```cpp
// Writing
FMemoryWriter Writer;
int X = 42; Writer << X;
auto Bytes = Writer.TakeBytes();

// Reading
FMemoryReader Reader(Bytes);
int Y = 0; Reader << Y;   // Y == 42
```

#### 接口

| 签名 | 说明 |
|------|------|
| `virtual ~FArchive()` | 虚析构（默认） |
| `[[nodiscard]] bool IsReading() const` | 是否读取方向（`Mode == EArchiveMode::Read`） |
| `[[nodiscard]] bool IsWriting() const` | 是否写入方向 |
| `virtual void Serialize(void* Data, std::size_t Size) = 0` | 原始字节（memcpy 语义），子类实现 |
| `virtual void Seek(std::size_t Pos) = 0` | 定位流位置 |
| `[[nodiscard]] virtual std::size_t Tell() const = 0` | 当前流位置 |
| `FArchive& operator<<(std::int32_t& V)` 等 7 个数值/布尔重载 | 内建 POD：`int32/uint32/int64/uint64/float/double/bool`，整块 `Serialize(&V, sizeof(V))` 原样读写 |
| `FArchive& operator<<(std::string& V)` | 字符串：先写 `uint32` 长度前缀再写字节；读方向先读长度、resize、再读入 |
| `template<typename T> FArchive& operator<<(T& Value)` | 通用 POD：`static_assert(std::is_trivially_copyable_v<T>)`（如 `glm::vec3`），整块序列化；header 内联 |

#### 约束

| 签名 | 说明 |
|------|------|
| `explicit FArchive(EArchiveMode InMode) protected` | 受保护构造——仅派生类可实例化，构造即定方向 |

## ISerialize <class>

自序列化接口——类型实现 `Serialize(FArchive&)`，把所有字段经 `Ar <<` 推出；同一个函数同时处理读和写。

```cpp
struct FMaterial : public Maho::Archive::ISerialize
{
    void Serialize(Maho::Archive::FArchive& Ar) override
    {
        Ar << BaseColor << Roughness;
    }
};
```

#### 接口

| 签名 | 说明 |
|------|------|
| `virtual void Serialize(FArchive& Ar) = 0` | 经流序列化全部字段（读或写） |

## FMemoryReader <class>

读取方向的内存字节流——持有**外部**字节缓冲的引用（缓冲须比 reader 存活更久）；越界读被静默跳过（实现留 TODO 报告越界）。

#### 接口

| 签名 | 说明 |
|------|------|
| `explicit FMemoryReader(const std::vector<std::uint8_t>& InData)` | 绑定外部缓冲，方向固定为 `Read` |
| `void Serialize(void* Data, std::size_t Size) override` | 拷贝 `Size` 字节到目标；越界则直接返回不拷贝 |
| `void Seek(std::size_t Pos) override` | 设置内部位置 |
| `[[nodiscard]] std::size_t Tell() const override` | 当前内部位置 |

## FMemoryWriter <class>

写入方向的内存字节流——写进**自有**缓冲。`GetBytes()` 只读视图，`TakeBytes()` 移动取走。

#### 接口

| 签名 | 说明 |
|------|------|
| `FMemoryWriter()` | 空构造，方向固定为 `Write` |
| `void Serialize(void* Data, std::size_t Size) override` | 追加 `Size` 字节到缓冲尾部 |
| `void Seek(std::size_t Pos) override` | 定位：`Pos < 当前大小` 时把缓冲截断到 `Pos` |
| `[[nodiscard]] std::size_t Tell() const override` | 当前缓冲大小 |
| `[[nodiscard]] const std::vector<std::uint8_t>& GetBytes() const` | 只读访问缓冲 |
| `std::vector<std::uint8_t> TakeBytes()` | 移动取走缓冲（后续写入从头开始） |

- [Archive.md](Archive.md) — 概念 · [实现字典](ImplAPI.md) — 算法
