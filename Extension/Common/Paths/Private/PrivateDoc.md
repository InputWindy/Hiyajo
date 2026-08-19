# Private

## 代码文件

- [Paths.cpp](Paths.cpp)

## 实现算法字典

Paths 插件的实现集中在 `Paths.cpp`——别名解析。

| 函数 | 说明 |
|------|------|
| `FPaths::ExecuteStage(EPathsStage Stage)` | 阶段分发：`Init` / `Shutdown` 都 `Roots.clear()` |
| `FPaths::SetRoot(string_view Alias, path)` | `Roots[alias] = path` |
| `FPaths::Resolve(string_view VirtualPath)` | 找到 `/` 或 `:` 分隔符；无分隔符则整体查别名；有则拆别名 + 子路径拼接 |
| `FPaths::HasRoot(string_view Alias)` | 别名是否存在于 `Roots` |

**解析规则**——`Resolve` 用 `find_first_of("/:")` 定位分隔符；别名未命中或路径无分隔符时原样返回（包装为 `std::filesystem::path`）。

## 相关文档

- [../Paths.md](../Paths.md) — 概念 + 用法
- [../Public/PublicDoc.md](../Public/PublicDoc.md) — 接口层

