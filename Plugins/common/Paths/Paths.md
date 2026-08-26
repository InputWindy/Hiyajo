# Paths

## 代码文件

- [Paths.h](Paths.h) — 路径解析单例 `FPaths`

## 概念——路径解析

路径解析单例服务——把**虚拟路径**解析成**物理路径**。注册根别名（如 `Engine` → 引擎目录）后，`Resolve("Alias/Sub/Path")`（或 `"Alias:Sub/Path"` 冒号分隔）拼接别名根与子路径。引擎内一切跨平台路径抽象（Asset 的 MountAlias、Resource 的虚拟源路径）都经它落地。

### FPaths —— 根别名 → 物理路径

`TSingleton<FPaths>` + `IPlugin<IInit, IShutdown>`（Initialize/Shutdown 清 `Roots`）。内部 `std::map<std::string, std::filesystem::path>`：

- `SetRoot(Alias, Path)`：注册/覆盖根别名。
- `Resolve(VirtualPath)`：取首个 `/` 或 `:` 前的别名段查表，其余部分拼到根后；未注册别名原样返回（容错）。
- `HasRoot(Alias)`：别名是否已注册。

```cpp
FPaths::Get().SetRoot("Engine", engineDir);
const auto Full = FPaths::Get().Resolve("Engine/Config/Base.ini");
const bool bOk   = FPaths::Get().HasRoot("Engine");
```

## 三方依赖

- 无（纯 std，`std::filesystem`）。

## 相关文档

- [API.html](API.html) — API 文档
