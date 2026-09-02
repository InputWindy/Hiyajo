# Asset（Private）— 实现算法字典

cpp 侧每个函数的算法伪代码解释。Public 侧 API 文档通过 `#fn-...` 锚点跳转落到这里。

## Asset.cpp

<a id="fn-asset-get"></a>
### GetAssetRegistry()

← [公开 API](API.md) · `FAssetRegistry*`

返回全局指针 `GAssetRegistry`——`Initialize` 置 `this`，`Shutdown` 置空。跨 DLL 经函数访问。

```text
GetAssetRegistry():
1. return GAssetRegistry
```

<a id="fn-asset-typefromext"></a>
### TypeFromExtension(std::string_view Extension)（内部）

匿名命名空间，仅被 `Scan` 引用。扩展名 → 资源类型。

```text
TypeFromExtension(Extension):
1. if Extension == ".material": return EAssetType::Material
2. if Extension == ".texture":  return EAssetType::Texture
3. return EAssetType::Unknown
```

<a id="fn-asset-init"></a>
### FAssetRegistry::Initialize(FEngineBase& Engine)

← [公开 API](API.md) · `void`

清空旧索引并发布 `this`——`GetAssetRegistry()` 从此刻起可用。

```text
Initialize(Engine):
1. lock(Mutex):
2.   Assets.clear()
3.   GAssetRegistry = this
```

<a id="fn-asset-shutdown"></a>
### FAssetRegistry::Shutdown(FEngineBase&)

← [公开 API](API.md) · `void`

回收：清空索引、撤回 `GAssetRegistry`。

```text
Shutdown(Engine):
1. lock(Mutex):
2.   Assets.clear()
3.   GAssetRegistry = nullptr
```

<a id="fn-asset-scan"></a>
### FAssetRegistry::Scan(const std::filesystem::path& ContentDir, std::string_view MountAlias)

← [公开 API](API.md) · `void`

递归索引内容目录。先把 mount 别名注册进 `FPaths`（使 `Resolve` 能映射），再逐文件登记元数据。

```text
Scan(ContentDir, MountAlias):
1. if !is_directory(ContentDir): return
2. Paths::GetPaths()->SetRoot(MountAlias, ContentDir)   // mount 别名 = FPaths 根
3. lock(Mutex):
4.   for Entry in recursive_directory_iterator(ContentDir):
5.     if !Entry.is_regular_file(): continue
6.     Relative = relative(Entry.path(), ContentDir).generic_string()
7.     Dot = Relative.find_last_of('.')
8.     if Dot == npos: continue                    // 无扩展名跳过
9.     Data.Path    = "/" + MountAlias + "/" + Relative[0..Dot)
10.    Data.Type    = TypeFromExtension(Relative[Dot..])
11.    Data.File    = Entry.path()
12.    Assets[string(Data.Path.GetPath())] = move(Data)
```

<a id="fn-asset-find"></a>
### FAssetRegistry::Find(const FAssetPath& Path) const

← [公开 API](API.md) · `const FAssetData*`

按逻辑路径查元数据；缺失返回 `nullptr`。

```text
Find(Path):
1. lock(Mutex):
2.   It = Assets.find(string(Path.GetPath()))
3.   return It != Assets.end() ? &It->second : nullptr
```

<a id="fn-asset-resolve"></a>
### FAssetRegistry::Resolve(const FAssetPath& Path) const

← [公开 API](API.md) · `std::filesystem::path`

逻辑路径 → 物理文件。剥掉前导 `/` 后委托 `FPaths::Resolve`（mount 别名是根）。

```text
Resolve(Path):
1. P = Path.GetPath()
2. if P 以 '/' 开头: P 去掉首字符
3. return Paths::GetPaths()->Resolve(P)
```

<a id="fn-asset-load"></a>
### FAssetRegistry::Load(const FAssetPath& Path) const

← [公开 API](API.md) · `std::optional<std::vector<std::uint8_t>>`

先 `Find` 拿物理路径，再以二进制流读全部字节；缺失 / 打不开返回 `nullopt`。

```text
Load(Path):
1. Data = Find(Path)
2. if Data == nullptr: return nullopt
3. Stream = ifstream(Data->File, binary)
4. if !Stream: return nullopt
5. Bytes = 从 Stream 读到 EOF
6. return Bytes
```

<a id="fn-asset-count"></a>
### FAssetRegistry::GetAssetCount() const

← [公开 API](API.md) · `std::size_t`

已索引资源数。

```text
GetAssetCount():
1. lock(Mutex):
2.   return Assets.size()
```

<a id="fn-asset-createlayer"></a>
### CreateLayer()（extern "C"）

DLL 动态安装入口——宿主按符号名查找。委托静态 `FAssetRegistry::CreateLayer()`（`MAHO_DECLARE_LAYER` 生成）。

```text
CreateLayer():
1. return Asset::FAssetRegistry::CreateLayer()
```

- [Asset.md](Asset.md) — 概念 · [公开 API](API.md) — 签名入口
