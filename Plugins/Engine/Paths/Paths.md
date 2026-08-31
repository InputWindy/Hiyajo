# Paths

## Code files

- [Paths.h](Public/Paths.h) — 路径解析层头：`FPaths`
- [PathsApi.h](Public/PathsApi.h) — DLL 导出宏 `MAHO_PATHS_API`
- [Paths.cpp](Private/Paths.cpp) — `SetRoot`/`Resolve`/`HasRoot` 实现 + 跨 DLL 访问器 `GetPaths`

## Concept - Virtual Path Resolution

Paths 把**根别名**抽象成平台无关的路径前缀：引擎代码只写 `"Engine/Shaders/..."` 或 `"Game/Materials/M_Metal"`，真实根目录集中注册、按平台 / 部署可替换。`Asset::Resolve`、配置加载等底层都经它落地。

### 1. 注册（SetRoot）

`SetRoot(Alias, Path)` 把别名映射到物理目录。`Asset::Scan` 会把 mount 别名（如 `"Game"`）注册成根。

### 2. 解析（Resolve）

`Resolve(VirtualPath)` 的规则：

- 含 `/` 或 `:` 分隔符 → 首个分隔符前是别名：`"Game/Materials/M_Metal"` / `"Game:Materials/M_Metal"` → 根 `Game` + 剩余部分。
- **无分隔符** → 整串视作别名：已注册返回根；未注册按物理路径原样返回。

```cpp
#include <Paths.h>

using namespace Maho;

Paths::GetPaths()->SetRoot("Engine", "C:/maho/Engine");
Paths::GetPaths()->Resolve("Engine/Shaders/common.ush");  // C:/maho/Engine/Shaders/common.ush
Paths::GetPaths()->Resolve("Engine:Shaders/common.ush");  // 同上（冒号分隔）
Paths::GetPaths()->HasRoot("Engine");                     // true
```

### 3. 生命周期

`FPaths::Initialize` 清空根表并发布 `this`；`Shutdown` 清空并撤回。

## Third-party dependencies

- None (pure std).

## Related docs

- [API.md](API.md) - API documentation
- [ImplAPI.md](ImplAPI.md) - 实现算法字典
- [EngineDoc.md](../../../Source/Public/Engine/EngineDoc.md) - 层架构
