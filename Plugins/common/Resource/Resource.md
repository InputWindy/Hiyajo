# Resource

## 代码文件

- [Resource.h](Resource.h) — 类型化异步资源系统（`FImportConfig` / `FExportConfig` / `FResource` / `TResourceImporter` / `TResourceExporter` / `FResourceSystem`）

## 概念——异步资源系统

类型化异步资源系统单例服务——专用 **IO 线程**（`FThreadedServer`）做异步导入/导出，目录以 **FName** 为 key，虚拟路径经 **FPaths** 解析。导入器/导出器是用户按资源类型**特化的模板**，只负责编解码原始字节（见到的是 `std::span` / `std::vector` of bytes）。

### FResourceSystem —— 异步传输服务器（单例服务）

`TSingleton<FResourceSystem>` + `FThreadedServer` + `IPlugin<IInit, IShutdown>`：

- **Initialize**：启动 IO 线程（`FThreadedServer::Initialize`）。
- **Tick**：游戏线程每帧调用——轮询就绪传输并在游戏线程执行解码 / `OnDone` 回调（`kMaxAppliesPerTick = 1`）。
- **Shutdown**：停线程 + join，清空 pending 与目录。
- **Import\<TResource\>(Config, OnDone)**：异步导入。虚拟源路径经 FPaths 解析成物理路径，IO 线程读原始字节 → `Tick` 时游戏线程调 `TResourceImporter<TResource>::Import(Config, Bytes, *Resource)` 解码 → `RegisterResource` 入目录 → `OnDone(const FResource*)`。
- **Export\<TResource\>(Config, AssetPath, OnDone)**：异步导出。**编码在调用方（游戏）线程**（同步读目录资源，无跨线程共享），只把字节交给 IO 线程 `WriteBytes`；`OnDone(bool)` 在游戏线程（经 Tick）。调用方必须保证资源在导出期间存活且不变。
- **Find / TryLoad**：按资产路径（去掉扩展名的虚拟路径）查目录。

### 特化模板

- `TResourceImporter<TResource>`：需提供 `using FConfig = ...;` + `static bool Import(const FConfig&, std::span<const std::uint8_t>, TResource&)`。
- `TResourceExporter<TResource>`：需提供 `using FConfig = ...;` + `static bool Export(const FConfig&, const TResource&, std::vector<std::uint8_t>&)`。
- 默认未定义——按资源类型特化。

### 配置基类

- `FImportConfig`：`SourcePath`（虚拟路径，如 `"Raw/mesh.fbx"`）——资产路径（目录 key）由去掉扩展名推导。
- `FExportConfig`：`DestinationPath`（物理路径，如 `"C:/Out/mesh.fbx"`）。

```cpp
template <>
struct Maho::Resource::TResourceImporter<FMesh>
{
    using FConfig = FMeshImportConfig;
    static bool Import(const FConfig&, std::span<const std::uint8_t>, FMesh&);
};

Resource::FResourceSystem::Get().Import<FMesh>({ "Raw/mesh.fbx" },
    [](const Resource::FResource* R) { /* 加载完成，游戏线程 */ });
const Resource::FResource* R = Resource::FResourceSystem::Get().TryLoad("Raw/mesh");
```

## 三方依赖

- 无。
- 其他插件：**Name**（目录 key）、**Paths**（虚拟路径解析）——`.cplugin` Dependencies = `["Name", "Paths"]`。

## 相关文档

- [API.html](API.html) — API 文档
