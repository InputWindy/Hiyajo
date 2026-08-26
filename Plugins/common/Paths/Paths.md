# Paths

路径解析单例——注册根别名（如 `Engine` → 引擎目录），把虚拟路径解析成物理路径。

## 提供

- `FPaths`：`TSingleton<FPaths>` + `IPlugin<IInit, IShutdown>`。
  - `SetRoot(Alias, Path)`：注册根别名。
  - `Resolve(VirtualPath)`：解析 `"Alias/Sub/Path"`（或 `"Alias:Sub/Path"`）为物理路径。
  - `HasRoot(Alias)`：别名是否已注册。

## 示例

```cpp
FPaths::Get().SetRoot("Engine", engineDir);
const auto Full = FPaths::Get().Resolve("Engine/Config/Base.ini");
```

## 依赖

- 三方：无。
- 其他插件：无。
