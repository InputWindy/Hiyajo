# Paths（Private）— 实现算法字典

cpp 侧每个函数的算法伪代码解释。Public 侧 API 文档通过 `#fn-...` 锚点跳转落到这里。

## Paths.cpp

<a id="fn-paths-get"></a>
### GetPaths()

← [公开 API](API.md) · `FPaths*`

返回全局指针 `GPaths`——`Initialize` 置 `this`，`Shutdown` 置空。

```text
GetPaths():
1. return GPaths
```

<a id="fn-paths-init"></a>
### FPaths::Initialize(FEngineBase&)

← [公开 API](API.md) · `void`

发布 `this` 并清空根表。

```text
Initialize(Engine):
1. GPaths = this
2. Roots.clear()
```

<a id="fn-paths-shutdown"></a>
### FPaths::Shutdown(FEngineBase&)

← [公开 API](API.md) · `void`

回收：撤回 `this`，清空根表。

```text
Shutdown(Engine):
1. GPaths = nullptr
2. Roots.clear()
```

<a id="fn-paths-setroot"></a>
### FPaths::SetRoot(std::string_view Alias, std::filesystem::path Path)

← [公开 API](API.md) · `void`

注册 / 覆盖根别名。

```text
SetRoot(Alias, Path):
1. Roots[string(Alias)] = move(Path)
```

<a id="fn-paths-resolve"></a>
### FPaths::Resolve(std::string_view VirtualPath) const

← [公开 API](API.md) · `std::filesystem::path`

虚拟路径 → 物理路径。首个 `/` 或 `:` 前为别名；无分隔符时整串作别名（未注册则原样返回）。

```text
Resolve(VirtualPath):
1. Sep = VirtualPath.find_first_of("/:")
2. if Sep == npos:                       // 无分隔符
3.   It = Roots.find(string(VirtualPath))
4.   if It != Roots.end(): return It->second       // 整串是别名 → 根
5.   return path(string(VirtualPath))              // 否则按物理路径原样
6. Alias = VirtualPath[0 .. Sep)
7. It = Roots.find(string(Alias))
8. if It == Roots.end(): return path(string(VirtualPath))   // 未知别名 → 原样
9. Result = It->second
10. Result /= VirtualPath[Sep+1 ..]
11. return Result
```

<a id="fn-paths-hasroot"></a>
### FPaths::HasRoot(std::string_view Alias) const

← [公开 API](API.md) · `bool`

别名是否已注册。

```text
HasRoot(Alias):
1. return Roots.find(string(Alias)) != Roots.end()
```

<a id="fn-paths-createlayer"></a>
### CreateLayer()（extern "C"）

DLL 动态安装入口——宿主按符号名查找。委托静态 `FPaths::CreateLayer()`（`MAHO_DECLARE_LAYER` 生成）。

```text
CreateLayer():
1. return Maho::Paths::FPaths::CreateLayer()
```

- [Paths.md](Paths.md) — 概念 · [公开 API](API.md) — 签名入口
