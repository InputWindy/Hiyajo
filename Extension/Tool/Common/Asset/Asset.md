# Asset

Asset path + registry (logical path →?physical file + type).

## 扩展�?
| 字段 | �?|
|------|-----|
| Class | `Maho::Asset::FAsset` |
| Header | `Asset.h` |
| Stage | `EAssetStage`（本插件自定义） |
| Dependencies | —?|

## 说明

资产注册表插件。`FAssetPath` 是逻辑资产路径（无扩展名、无对象部分），`FAsset` 单例管逻辑路径 →?元数据的映射。`Scan()` 遍历内容目录并索引每个资产文件，`Find()` 查逻辑路径，`Resolve()` 把逻辑路径映射回物理文件，`Load()` 读原始字节�?
> 迁移说明：旧引擎�?`FPaths`（路径根解析）插件未随本次迁移，�?mount alias →?root 的解析逻辑已内联进 `FAsset`（`MountRoots`），Asset 不再依赖 `Paths`�?
### 驱动

宿主�?stage �?Execute 驱动�?
```cpp
// 宿主 Main �?FParallelScheduler S;
S.Execute<Maho::Asset::EAssetStage, FExtensions>();
// →?对每个插件调 T::Get().ExecuteStage(EAssetStage{...})
```

`FAsset::ExecuteStage` 处理两个阶段�?
| Stage | 行为 |
|-------|------|
| `EAssetStage::Init` | 清空 `Assets` + `MountRoots` |
| `EAssetStage::Shutdown` | 清空 `Assets` + `MountRoots` |

### 用法

```cpp
#include <Asset.h>
using namespace Maho::Asset;

FAsset::Get().Scan("Content", "Game");        // index
const FAssetData* D = FAsset::Get().Find(FAssetPath("/Game/Materials/M_Metal"));
auto Bytes = FAsset::Get().Load(FAssetPath("/Game/Materials/M_Metal"));
```

## 三方依赖

无�?
## 相关文档

- [API.html](API.html) —?API 文档
- [Public/PublicDoc.md](Public/PublicDoc.md) —?接口字典
- [Private/PrivateDoc.md](Private/PrivateDoc.md) —?实现算法字典
- [../../Source/Public/Core/CoreDoc.md](../../Source/Public/Core/CoreDoc.md) —?核心基础设施

