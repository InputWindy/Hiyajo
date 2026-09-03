# Name — API 文档

服务层：`FNamePool` 是 `FLayer<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>`（`Name.dll`）。**字符串驻留池**——`FName` 是不可变驻留字符串标识符，构造时把字符串 intern 进全局池，相同字符串共享同一条目，O(1) 比较。默认构造的 `FName` 是 None（空）。

## Name.h

### FName <class>

驻留不可变字符串标识符——池化存储，O(1) 比较。构造 `FName("head")` 会 intern 进全局池（须在 `FNamePool` 已初始化后使用）。

#### 接口

| 签名 | 说明 |
|------|------|
| `FName() = default` | 空标识符（None，Id == 0） |
| `explicit FName(std::string_view Str)` | 构造即 intern（经 `FNamePool::Intern`） |
| `std::string_view ToString() const` | 取回池中字符串（None → 空串） |
| `bool IsNone() const` | 是否 None（`Id == 0`） |
| `std::uint32_t GetId() const` | 池内数值 id |
| `bool operator==(const FName&) const` | 按 id 相等（O(1)） |
| `bool operator!=(const FName&) const` | 按 id 不等 |
| `bool operator<(const FName&) const` | 按 id 序 |

#### 约束

| 签名 | 说明 |
|------|------|
| `explicit FName(std::uint32_t InId) private` | 私有 id 构造——仅 `FNamePool`（friend）可用 |
| `std::uint32_t Id = 0` | 私有存储；0 = None |

### std::hash<FName> <特化>

`std::hash<Maho::Name::FName>` 特化——散列 `GetId()`，可作 `std::unordered_*` 键。

### FNamePool <class>

全局驻留字符串池（服务层）。`Intern` 线程安全（`Mutex` 保护 `Pool` + `Lookup`）。`Initialize` 清池并发布 `this`；`Shutdown` 清池并撤回。

#### 接口

| 签名 | 说明 |
|------|------|
| `MAHO_DECLARE_LAYER(FNamePool, "Name.dll")` | 层声明宏（DLL 导出入口） |
| `FName Intern(std::string_view Str)` | 驻留一个字符串，返回规范 `FName`（线程安全；空串返回 None，不驻留） |
| `std::string_view StringForId(std::uint32_t Id) const` | `Id` 处存储的字符串（`Intern` 的逆操作；越界返回空串） |

### GetNamePool <自由函数>

全局名称池访问器——跨 DLL 经函数访问。

#### 接口

| 签名 | 说明 |
|------|------|
| `MAHO_NAME_API FNamePool* GetNamePool()` | 返回已初始化的 `FNamePool*`；`Initialize` 前 / `Shutdown` 后为 `nullptr` |

## NameApi.h

### MAHO_NAME_API <宏>

DLL 导出/导入宏——`MAHO_NAME_MODULE_EXPORTS` 定义时展开为 `MAHO_EXPORT`，否则为 `MAHO_IMPORT`（详见 `Core/Export.h`）。

#### 宏

| 签名 | 说明 |
|------|------|
| `MAHO_NAME_API` | 修饰本 DLL 导出的符号（`GetNamePool`、`CreateLayer`） |

- [Name.md](Name.md) — 概念 · [实现字典](ImplAPI.md) — 算法
