# Private

## 代码文件

- [Asset.cpp](Asset.cpp)

## 实现算法字典

Asset 插件的实现集中在 `Asset.cpp`——资产注册表。

| 函数 | 说明 |
|------|------|
| `TypeFromExtension`（内部） | `.material`→Material，`.texture`→Texture，其余 Unknown |
| `FAssetRegistry::ExecuteStage` | Init/Shutdown 均清空 `Assets` + `MountRoots` |
| `FAssetRegistry::Scan` | 注册 mount alias → root；递归遍历目录，去扩展名构造 `/Alias/Relative` 逻辑路径并索引 |
| `FAssetRegistry::Find` | 逻辑路径查 map，缺失返回 nullptr |
| `FAssetRegistry::Resolve` | 去前导 `/`，取首段 alias，查 `MountRoots` 后拼接剩余路径 |
| `FAssetRegistry::Load` | 由 Find 得物理文件，读二进制字节，失败返回 nullopt |
| `FAssetRegistry::GetAssetCount` | 返回 `Assets.size()` |

## 相关文档

- [../Asset.md](../Asset.md) — 概念 + 用法
- [../Public/PublicDoc.md](../Public/PublicDoc.md) — 接口层

