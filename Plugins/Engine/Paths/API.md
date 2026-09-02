# Paths — API 文档

服务层：`FPaths` 是 `FLayer<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>`（`Paths.dll`）。**虚拟路径解析**——把引擎 / 项目根别名（`Engine`、`Game`…）映射到物理路径；`Resolve` 吃 `"Alias/Sub"` 或 `"Alias:Sub"` 形式。

## Paths.h

### FPaths <class>

路径解析层：注册根别名 → 解析虚拟路径。内部 `Roots`（`std::map<std::string, std::filesystem::path>`）。

#### 接口

| 签名 | 说明 |
|------|------|
| `MAHO_DECLARE_LAYER(FPaths, "Paths.dll")` | 层声明宏（DLL 导出入口） |
| `void SetRoot(std::string_view Alias, std::filesystem::path Path)` | 注册一个根别名（如 `"Engine"` → 引擎目录） |
| `std::filesystem::path Resolve(std::string_view VirtualPath) const` | 解析 `"Alias/Sub/Path"`（或 `"Alias:Sub/Path"`）到物理路径；无分隔符时若整串是别名则返回根，否则原样返回 |
| `bool HasRoot(std::string_view Alias) const` | 别名是否已注册 |

### GetPaths <自由函数>

全局路径解析器访问器——跨 DLL 经函数访问。

#### 接口

| 签名 | 说明 |
|------|------|
| `MAHO_PATHS_API FPaths* GetPaths()` | 返回已初始化的 `FPaths*`；`Initialize` 前 / `Shutdown` 后为 `nullptr` |

## PathsApi.h

### MAHO_PATHS_API <宏>

DLL 导出/导入宏——`MAHO_PATHS_MODULE_EXPORTS` 定义时展开为 `MAHO_EXPORT`，否则为 `MAHO_IMPORT`（详见 `Core/Export.h`）。

#### 宏

| 签名 | 说明 |
|------|------|
| `MAHO_PATHS_API` | 修饰本 DLL 导出的符号（`GetPaths`、`CreateLayer`） |

- [Paths.md](Paths.md) — 概念 · [实现字典](ImplAPI.md) — 算法
