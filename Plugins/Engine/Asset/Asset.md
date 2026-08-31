# Asset

## Code files

- [Asset.h](Public/Asset.h) — 资源注册表头：`EAssetType` / `FAssetPath` / `FAssetData` / `FAssetRegistry`
- [AssetApi.h](Public/AssetApi.h) — DLL 导出宏 `MAHO_ASSET_API`
- [Asset.cpp](Private/Asset.cpp) — `Scan`/`Find`/`Resolve`/`Load` 实现 + 跨 DLL 访问器 `GetAssetRegistry`

## Concept - Logical Path → Metadata → Physical File

Asset 把磁盘资源组织成三层映射：

**逻辑路径**（`/Game/Materials/M_Metal`，`FAssetPath`）→ **元数据**（`FAssetData`，含类型与物理文件）→ **物理文件**（`Content/Materials/M_Metal.material`）。引擎只消费逻辑路径；真实位置经 `FPaths` 根别名（mount）抽象，跨平台根可替换。

### 1. 索引（Scan）

`Scan(ContentDir, MountAlias)` 递归遍历内容目录，把每个**带扩展名的常规文件**登记成一条元数据：

- 相对路径去扩展名 → 逻辑路径：`Content/Materials/M_Metal.material` → `/Game/Materials/M_Metal`。
- 扩展名 → 类型：`.material` → `Material`，`.texture` → `Texture`，其余 → `Unknown`。
- 无扩展名的文件跳过。
- 副作用：`MountAlias` 会被注册进 `FPaths` 的根别名表（`SetRoot`），于是 `Resolve` 能经 `FPaths` 映射回物理文件。

### 2. 查找与解析（Find / Resolve / Load）

- `Find`：按逻辑路径在 `Assets` 表里查元数据，返回指针（缺失为 `nullptr`）。
- `Resolve`：剥掉逻辑路径的前导 `/`，委托 `FPaths::Resolve`——`/Game/Materials/M_Metal` → 根 `Game` + `Materials/M_Metal`。
- `Load`：先 `Find` 拿物理路径，再以二进制流读全部字节；缺失 / 打不开返回 `nullopt`。

```cpp
#include <Asset.h>

using namespace Maho;

// 索引内容目录（引擎初始化时）
Asset::GetAssetRegistry()->Scan("Content", "Game");

// 查元数据 / 解析物理路径 / 读原始字节
const Asset::FAssetPath P("/Game/Materials/M_Metal");
if (const Asset::FAssetData* Data = Asset::GetAssetRegistry()->Find(P))
{
    std::filesystem::path File = Asset::GetAssetRegistry()->Resolve(P);
    auto Bytes = Asset::GetAssetRegistry()->Load(P);   // optional<vector<uint8_t>>
}
```

注意：`FAssetRegistry` 需先经引擎层系统初始化（`GetAssetRegistry()` 非空）再使用。

## Third-party dependencies

- None (pure std).

## Related docs

- [API.md](API.md) - API documentation
- [ImplAPI.md](ImplAPI.md) - 实现算法字典
- [EngineDoc.md](../../../Source/Public/Engine/EngineDoc.md) - 层架构
