# Asset

资产注册表单例——逻辑路径（`/Game/...`）→ 资产元数据。`Scan()` 递归索引 content 目录，`Find()` 查逻辑路径，`Resolve()` 经 FPaths 映射到物理文件，`Load()` 读原始字节。

## 提供

- `EAssetType`：`Unknown` / `Material` / `Texture`（从磁盘扩展名推断）。
- `FAssetPath`：逻辑路径（无扩展名、无对象部分）。
- `FAssetData`：`{Path, Type, File, Dependencies}`（Dependencies 延迟解析）。
- `FAssetRegistry`：`TSingleton<FAssetRegistry>` + `IPlugin<IInit, IShutdown>`。
  - `Scan(ContentDir, MountAlias = "Game")`：递归索引。
  - `Find(Path)` / `Resolve(Path)` / `Load(Path)` / `GetAssetCount()`。

## 示例

```cpp
FAssetRegistry::Get().Scan(ContentDir);   // Content/Materials/M_Metal.material → /Game/Materials/M_Metal (Material)
const FAssetData* D = FAssetRegistry::Get().Find(FAssetPath("/Game/Materials/M_Metal"));
auto Bytes = FAssetRegistry::Get().Load(FAssetPath("/Game/Materials/M_Metal"));
```

## 依赖

- 三方：无。
- 其他插件：Paths（`Resolve` 用根别名解析物理路径，MountAlias 即根别名）。
