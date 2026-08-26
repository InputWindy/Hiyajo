# Asset

## 代码文件

- [Asset.h](Asset.h) — 资产注册表（`EAssetType` / `FAssetPath` / `FAssetData` / `FAssetRegistry`）

## 概念——资产注册表

资产注册表单例服务——**逻辑路径（`/Game/...`）→ 资产元数据**。`Scan()` 递归索引 content 目录并登记每个资产文件，`Find()` 按逻辑路径查询，`Resolve()` 经 FPaths 根别名映射到物理文件，`Load()` 读原始字节。逻辑路径**无扩展名、无对象部分**（本引擎无 UObject 系统）。

### FAssetPath —— 逻辑资产路径

无扩展名逻辑路径（`"/Game/Materials/M_Metal"`），默认构造空。`GetPath()` / 全比较运算；可作 map key。

### FAssetData —— 资产元数据

`{Path, Type, File(物理文件), Dependencies}`——`Dependencies` 由反序列化填充（延迟解析）。

### FAssetRegistry —— 注册表单例

`TSingleton<FAssetRegistry>` + `IPlugin<IInit, IShutdown>`（`Mutex` 保护）：

- `Scan(ContentDir, MountAlias = "Game")`：递归索引；`Content/Materials/M_Metal.material → /Game/Materials/M_Metal (Material)`。类型按磁盘扩展名推断（`.material` / `.texture`）；**MountAlias 同时注册为 FPaths 根别名**。
- `Find(Path)`：查逻辑路径，无则 `nullptr`。
- `Resolve(Path)`：`"/Game/..." → FPaths 根 "Game" + 子路径` 拼物理路径。
- `Load(Path)`：读资产文件原始字节（`std::optional<vector<uint8_t>>`）。

```cpp
FAssetRegistry::Get().Scan(ContentDir);   // Content/Materials/M_Metal.material → /Game/Materials/M_Metal (Material)
const FAssetData* D = FAssetRegistry::Get().Find(FAssetPath("/Game/Materials/M_Metal"));
auto Bytes = FAssetRegistry::Get().Load(FAssetPath("/Game/Materials/M_Metal"));
```

## 三方依赖

- 无。
- 其他插件：**Paths**（`.cplugin` Dependencies = `["Paths"]`）——`Resolve` 用根别名解析物理路径，MountAlias 即根别名。

## 相关文档

- [API.html](API.html) — API 文档
