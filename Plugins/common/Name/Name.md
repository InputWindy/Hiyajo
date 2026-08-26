# Name

字符串驻留 + 不可变字符串标识符——构造时把字符串 intern 进全局池，相同字符串共享同一条目，O(1) 比较。

## 提供

- `FName`：interned 不可变标识符（默认构造 = None）——`ToString()` / `IsNone()` / `GetId()` / 比较运算；配套 `std::hash` 特化。
- `FNamePool`：`TSingleton<FNamePool>` + `IPlugin<IInit, IShutdown>`，线程安全驻留池。
  - `Intern(std::string_view)`：返回规范 FName。
  - `StringForId(std::uint32_t)`：Id 反查字符串。

## 示例

```cpp
const FName Bone = "head";
const FName Also = "head";          // 同一池条目
Bone == Also;                       // true，O(1)
const FName N = FNamePool::Get().Intern("bip01");
```

## 依赖

- 三方：无。
- 其他插件：无。
