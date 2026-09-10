# Resource

## Code files

- [Resource.h](Public/Resource.h) — 类型化异步资源系统（`FResourceSystem` / `FResource` / 导入导出模板）
- [ResourceApi.h](Public/ResourceApi.h) — `MAHO_RESOURCE_API` 导出宏
- [Resource.cpp](Private/Resource.cpp) — IO 线程 + 目录实现 + `CreateLayer` 导出

## Concept - Typed Async Resource System

Resource 是类型化异步资源系统：**导入/导出在专用 IO 线程上读写裸字节，游戏线程 Tick 应用**。目录按 `FName` 键控，虚拟路径经 `FPaths` 解析。导入器/导出器是用户特化的模板，只负责解码/编码原始字节。

### 1. 数据基类 FResource

`FResource` 只携带路径（`GetPath()`）；派生类持有实际载荷。目录键 = 虚拟路径去扩展名（`detail::FindLastDot` 切分）。

### 2. 导入器 / 导出器 / 构造器（用户特化点）

`TResourceImporter<TResource>` / `TResourceExporter<TResource>` / `TResourceCreator<TResource>` 未定义，按资源类型特化。导入器经 `FImportConfig`（虚拟源路径）接收 `std::span<const std::uint8_t>` 并解码；导出器在调用线程编码，把 `std::vector<uint8_t>` 交给 IO 线程写盘。

`TResourceCreator<T>` 提供实例构造（`std::unique_ptr<T>`），**定义必须放在该类型自己的插件 .cpp 里**（out-of-line）：`Import<T>` / `CreateResource<T>` 是头文件模板，若在模板里 `make_unique`，vtable + 删除析构会落在调用方模块；调用方可能是稍后被动态卸载的子插件（编辑器面板），而资源仍在目录里，Shutdown 删除它就会跳进已卸载的映像（崩溃）。经构造器后，所有实例的 vtable 都归属长期存活的类型模块。

```cpp
template <>
struct Maho::Resource::TResourceImporter<FMesh>
{
    using FConfig = FMeshImportConfig;
    static bool Import(const FConfig&, std::span<const std::uint8_t>, FMesh&);
};

Resource::GetResourceSystem()->Import<FMesh>({ "Raw/mesh.fbx" });
```

### 3. 异步传输（内部）

导入流程：`Import<T>` 切出资产路径 → `EnqueueImport` 解析物理路径 + `RequestLoad` 提交 IO 线程读盘 → 字节就绪后挂在 `FTransferHandle` 上 → 游戏线程 `Tick → ProcessReadyIO` 轮询，成功后**在游戏线程**用 `TResourceCreator<T>` 构造实例、调导入器解码并 `RegisterResource` 注册，最后广播 `OnAssetImported`。

导出流程：`Export<T>` 在**调用线程**同步编码（安全：目录只读访问），只把写盘延迟到 IO 线程；成败经 Tick 由 `OnAssetExported` 广播。

### 4. 生命周期

```cpp
Resource::GetResourceSystem()->Import<FMesh>({ "Raw/mesh.fbx" });   // 异步
const Resource::FResource* R = Resource::GetResourceSystem()->TryLoad("Raw/mesh");
```

引擎循环驱动：`Initialize()` 启动 IO 线程，`Tick()` 每帧应用就绪传输，`Shutdown()` 停止线程并清空目录。

## Third-party dependencies

- 无第三方库（纯 std + 引擎层）。
- 其他插件：`Name`（目录键 `Name::FName`）、`Paths`（虚拟路径解析 `Paths::GetPaths()->Resolve`）——`.cplugin` Dependencies = `["Name", "Paths"]`

## Related docs

- [API.md](API.md) - API documentation
