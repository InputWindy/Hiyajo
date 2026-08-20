# Public

## 代码文件

- [Asset.h](Asset.h)
- [AssetApi.h](AssetApi.h)

## 接口字典

| 声明 | 说明 |
|------|------|
| `EAssetStage` | 本插件自定义 drive stage（Init / Shutdown�?|
| `EAssetType` | 资产类型（Unknown / Material / Texture�?|
| `FAssetPath` | 逻辑资产路径（无扩展名） |
| `FAssetData` | 已解析资产元数据（Path / Type / File / Dependencies�?|
| `FAsset : TExtensionList<FAsset>` | 注册表单例（�?Main/IAssembly�?|
| `FAsset::ExecuteStage(EAssetStage)` | 阶段分发（Init / Shutdown 清空�?|
| `Scan(ContentDir, MountAlias)` | 递归索引内容目录 |
| `Find(Path)` | 逻辑路径查元数据，缺失返�?nullptr |
| `Resolve(Path)` | 逻辑路径 →?物理文件 |
| `Load(Path)` | 读资产原始字节，失败返回 nullopt |
| `GetAssetCount()` | 已索引资产数 |

## 相关文档

- [../Asset.md](../Asset.md) —?概念 + 用法
- [../Private/PrivateDoc.md](../Private/PrivateDoc.md) —?实现算法字典

