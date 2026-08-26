# Resource

类型化异步资源系统单例——专用 IO 线程（`FThreadedServer`）做异步导入/导出，目录以 `FName` 为 key，虚拟路径经 FPaths 解析。导入器/导出器是用户按资源类型特化的模板，只负责编解码原始字节。

## 提供

- `FImportConfig`（虚拟源路径）/ `FExportConfig`（物理目标路径）。
- `FResource`：类型化资源基类（持路径）。
- `TResourceImporter<T>` / `TResourceExporter<T>`：按资源类型特化的编解码模板（未定义默认）。
- `FResourceSystem`：`TSingleton` + `FThreadedServer` + `IPlugin<IInit, IShutdown>`。
  - `Import<T>(Config, OnDone)` / `Export<T>(Config, AssetPath, OnDone)`（异步）。
  - `Find(AssetPath)` / `TryLoad(AssetPath)`（同步查目录）。
  - `Tick()`：游戏线程每帧调用，应用就绪传输。

## 示例

```cpp
template <>
struct Maho::Resource::TResourceImporter<FMesh> { ... };   // 特化：解码字节 → FMesh

Resource::FResourceSystem::Get().Import<FMesh>({ "Raw/mesh.fbx" },
    [](const Resource::FResource* R) { /* 加载完成，游戏线程 */ });
```

## 依赖

- 三方：无。
- 其他插件：Name（目录 key）、Paths（虚拟路径解析）。
