# -*- coding: utf-8 -*-
# Archive 插件文档内容（由 Tools/plugin_docs.py 执行，docs_builder 以变量 D 注入）。

# ══════════════════════════════════════════════════════════════════════════════
# Public/Archive.h —— 二进制序列化原语
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Archive.h", Title="Archive.h —— 二进制序列化原语（纯库 / header-only）",
         Desc="二进制序列化的**积木**，不是某个具体文件格式：一个抽象字节流 `FArchive`，两个实现"
              "（`FMemoryReader` 读外部缓冲、`FMemoryWriter` 写自有缓冲），外加自序列化接口 "
              "`ISerialize`。\n"
              "**为什么做成纯库（没有生命周期、没有单例）**：序列化是「对一段字节做变换」的纯计算，"
              "不持有跨帧状态；把它做成服务层只会逼每个使用者去等一个 stage，而它其实什么都不等。\n"
              "**为什么 header-only**：`operator<<` 的 POD 重载是模板（要看到类型的完整性才能 "
              "`sizeof`），模板无法 out-of-line 到别的模块 —— 全放头里就没有「模板实现藏在 DLL 里、"
              "消费方实例化不出来」的坑。`Private/Archive.cpp` 只放非模板成员。\n"
              "**为什么零第三方**：读写端可能同时存在于引擎与项目插件里，格式一旦绑死某个库，"
              "版本差异就会变成字节级不兼容；这里只认 `memcpy` 语义与显式字节序约定。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("<cstddef>", "`std::size_t` —— `Serialize` / `Seek` / `Tell` 的长度与偏移都用它")
D.Row("<cstdint>", "`std::int32_t` / `std::uint64_t` 等定宽整型 + `std::uint8_t` 字节："
                    "**定宽是跨平台二进制兼容的前提**（`int` 在 32/64 位下不保证一样）")
D.Row("<span>", "`std::span<const std::uint8_t>`：`FMemoryReader` **引用**外部缓冲而不拷贝，"
                 "span 正好表达「视图而非所有者」")
D.Row("<string>", "`std::string` 的按值序列化重载与序列化缓冲")
D.Row("<type_traits>", "`std::is_trivially_copyable_v<T>` —— 把「拿非 POD 走 memcpy」变成**编译错误**")
D.Row("<vector>", "`FMemoryWriter` 的自有字节缓冲与 `TakeBytes()` 的返回类型")

D.Card("序列化模式（EArchiveMode）")
D.Table("取值", "含义")
D.Row("Read = 0", "读模式：`IsReading()` 为真，`operator<<` 从流里**取**数据填进变量")
D.Row("Write = 1", "写模式：`IsWriting()` 为真，`operator<<` 把变量**推**进流")

D.Enum("EArchiveMode", Base="std::uint8_t",
       Desc="把「方向」编码进**同一个** `operator<<` 里 —— 读写共用一套调用点，"
            "业务类型的 `Serialize(FArchive&)` 只需要写一遍。"
            "基点显式写成 `std::uint8_t` 是为了让它只占一个字节（序列化结构体里常见）。")

D.Class("FArchive", Desc="抽象二进制流 —— 原始字节与有类型数据之间的桥。\n"
                        "设计要点：**原始字节是唯一的虚接口**（`Serialize`），所有类型化读写都建在它上面 ⇒ "
                        "新增一个后端（文件 / 网络 / 内存）只需实现三个虚函数。\n"
                        "用 `operator<<` 而不是 `Read/Write` 分名：读写两端调用点长得一样，"
                        "`Ar << A << B;` 在两种模式下都成立。")
D.SetAccess("public")
D.Interface("virtual ~FArchive() = default",
            "虚析构 —— 消费方常按 `FArchive&`（基类引用）持有，多态删除必须安全")
D.Interface("[[nodiscard]] bool IsReading() const",
            "当前是否读模式（`Mode == Read`）")
D.Interface("[[nodiscard]] bool IsWriting() const",
            "当前是否写模式（`Mode == Write`）")
D.Interface("virtual void Serialize(void* Data, std::size_t Size) = 0",
            "**唯一的原始字节入口**（memcpy 语义）：`Data` 在写模式是源、读模式是目标")
D.Interface("virtual void Seek(std::size_t Pos) = 0",
            "把流位置挪到绝对偏移（随机访问；顺序读写的实现各自解释）")
D.Interface("[[nodiscard]] virtual std::size_t Tell() const = 0",
            "当前流位置（与 `Seek` 配对，用于回填长度之类的两遍写法）")
D.Interface("FArchive& operator<<(std::int32_t& V)",
            "定宽整型重载；**取引用**：读模式要写回变量，写模式只读它")
D.Interface("FArchive& operator<<(std::uint32_t& V)", "同上（无符号 32 位）")
D.Interface("FArchive& operator<<(std::int64_t& V)", "同上（有符号 64 位）")
D.Interface("FArchive& operator<<(std::uint64_t& V)", "同上（无符号 64 位）")
D.Interface("FArchive& operator<<(float& V)", "同上（IEEE-754 单精度）")
D.Interface("FArchive& operator<<(double& V)", "同上（IEEE-754 双精度）")
D.Interface("FArchive& operator<<(bool& V)",
            "单独重载：`bool` 的 `sizeof` 与表示是实现定义的，必须走**规范化**的 0/1 字节，"
            "不能靠 memcpy 直接倾倒")
D.Interface("FArchive& operator<<(std::string& V)",
            "字符串：长度 + 字节，不依赖 `\\0` 结尾（可承载内嵌空字节）")
D.Interface("template <typename T> FArchive& operator<<(T& Value)",
            "泛型 POD 重载：`static_assert(std::is_trivially_copyable_v<T>)` 后按 `sizeof(T)` "
            "整块 memcpy —— 例如 `glm::vec3`。**断言是刻意的门禁**：非 POD（有指针 / 虚表 / "
            "需要逐字段控制）必须走 `ISerialize`，否则二进制里会出现悬空指针。")
D.SetAccess("protected")
D.Interface("explicit FArchive(EArchiveMode InMode)",
            "protected 构造：流是抽象层，只能由具体后端构造并声明自己的方向")
D.SetAccess("private")
D.Field("EArchiveMode Mode",
        "方向标志（唯一状态）。**为什么放基类**：方向是「我是什么」而不是「我记得什么」，"
        "两个实现都不需要各自维护一份，`IsReading/IsWriting` 也就无需虚调用")

D.Class("ISerialize",
        Desc="自序列化接口 —— 让**类型自己**知道怎么把自己摊成字节。\n"
             "**为什么要它**：`FArchive` 只认识 POD 与字符串，组合类型（材质、存档结构体）"
             "的字段布局只有它自己知道；把它写成成员函数，读写两端共用同一份布局描述，"
             "不会出现「写端改了字段、读端忘了同步」的静默错位。\n"
             "约定：读端与写端**同一个函数**（靠 `FArchive` 的模式区分方向），因此它必须对两种模式都正确。")
D.SetAccess("public")
D.Interface("virtual ~ISerialize() = default", "虚析构 —— 常按接口指针删除")
D.Interface("virtual void Serialize(FArchive& Ar) = 0",
            "把本类型的全部字段过一遍归档（读或写由 `Ar` 的模式决定）")

D.Class("FMemoryReader", Base="FArchive",
        Desc="外部字节缓冲上的读取器 —— **不拷贝**，只是视图 + 一个游标。\n"
             "`FMemoryReader(Bytes)` 之后 `Reader << X` 就是「从缓冲区取一个 X」。\n"
             "**生命周期须注意**：缓冲必须活得比读取器长（`std::span` 明确表达这层借用关系，"
             "而不是让人以为进来了就安全）。")
D.SetAccess("public")
D.Interface("explicit FMemoryReader(std::span<const std::uint8_t> InData)",
            "绑定一段只读字节（借用，无拷贝）；方向固定为读")
D.Interface("void Serialize(void* Data, std::size_t Size) override",
            "从缓冲拷出 `Size` 字节到 `Data`，并推进游标")
D.Interface("void Seek(std::size_t Pos) override",
            "把绝对游标设到 `Pos`（读模式下唯一的随机访问入口）")
D.Interface("[[nodiscard]] std::size_t Tell() const override",
            "当前游标（相对缓冲起点）")
D.SetAccess("private")
D.Field("std::span<const std::uint8_t> Data",
        "被借用的缓冲视图（`const`：读取器永不写它）")
D.Field("std::size_t Pos = 0",
        "读游标；初始 0 ⇒ 从缓冲头开始")

D.Class("FMemoryWriter", Base="FArchive",
        Desc="写进自有缓冲的写入器 —— 与读取器对称，但**拥有**数据。\n"
             "`Writer << A << B;` 之后用 `GetBytes()`（看一眼）或 `TakeBytes()`（搬走）取结果。\n"
             "**为什么同时给两个取法**：只 `GetBytes` 会逼调用方再拷一次（往上层传成本高）；"
             "只 `TakeBytes` 会拿走内部状态、之后这写端就废了。两个都给，由调用方声明意图。")
D.SetAccess("public")
D.Interface("FMemoryWriter()",
            "构造空缓冲（方向固定为写）")
D.Interface("void Serialize(void* Data, std::size_t Size) override",
            "把 `Size` 字节追加到缓冲并推进游标")
D.Interface("void Seek(std::size_t Pos) override",
            "把写游标设到绝对偏移 —— 用于回填先前不知道的长度字段（两遍写法）")
D.Interface("[[nodiscard]] std::size_t Tell() const override",
            "当前写游标 = 已产出的字节数")
D.Interface("[[nodiscard]] const std::vector<std::uint8_t>& GetBytes() const",
            "零拷贝查看已写字节（写端保持可用）")
D.Interface("[[nodiscard]] std::vector<std::uint8_t> TakeBytes()",
            "搬走字节（移动返回，写端内容随之清空）")
D.SetAccess("private")
D.Field("std::vector<std::uint8_t> Buffer",
        "自有的字节缓冲（连续存储 ⇒ 可直接当 `span` / 网络包发送）")
