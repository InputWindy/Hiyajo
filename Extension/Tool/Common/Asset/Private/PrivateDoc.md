# Private

## 代码文件

- [Asset.cpp](Asset.cpp)

## 实现算法字典

Asset 插件的实现集中在 `Asset.cpp`——资产注册表�?
| 函数 | 说明 |
|------|------|
| `TypeFromExtension`（内部） | `.material`→Material，`.texture`→Texture，其�?Unknown |
| `FAsset::ExecuteStage` | Init/Shutdown 均清�?`Assets` + `MountRoots` |
| `FAsset::Scan` | 注册 mount alias →?root；递归遍历目录，去扩展名构�?`/Alias/Relative` 逻辑路径并索�?|
| `FAsset::Find` | 逻辑路径�?map，缺失返�?nullptr |
| `FAsset::Resolve` | 去前�?`/`，取首段 alias，查 `MountRoots` 后拼接剩余路�?|
| `FAsset::Load` | �?Find 得物理文件，读二进制字节，失败返�?nullopt |
| `FAsset::GetAssetCount` | 返回 `Assets.size()` |

## 相关文档

- [../Asset.md](../Asset.md) —?概念 + 用法
- [../Public/PublicDoc.md](../Public/PublicDoc.md) —?接口�?
