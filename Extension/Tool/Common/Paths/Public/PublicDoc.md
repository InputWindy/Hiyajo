# Public

## 代码文件

- [Paths.h](Paths.h)
- [PathsApi.h](PathsApi.h)

## 接口字典

| 声明 | 说明 |
|------|------|
| `FPaths : TExtensionList<FPaths>` | 路径解析单例（纯单例，无 Main/IAssembly） |
| `FPaths::ExecuteStage(EPathsStage)` | 阶段分发（Init / Shutdown → clear） |
| `FPaths::SetRoot(string_view, path)` | 注册根路径别名 |
| `FPaths::Resolve(string_view)` | 解析虚拟路径到物理路径 |
| `FPaths::HasRoot(string_view)` | 别名是否已注册 |
| `EPathsStage` | 本插件自定义 drive stage |

## 相关文档

- [../Paths.md](../Paths.md) — 概念 + 用法
- [../Private/PrivateDoc.md](../Private/PrivateDoc.md) — 实现算法字典

