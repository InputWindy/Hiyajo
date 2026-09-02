# Resource — API 文档

Resource 插件 = 类型化异步资源系统：服务层 + 帧阶段 + 专用 IO 线程（`FThreadedServer`）。导入/导出在 IO 线程读写裸字节，游戏线程 Tick 解码/应用。目录按 `FName` 键控，虚拟路径经 `FPaths` 解析。`ResourceApi.h` 提供 `MAHO_RESOURCE_API` 导出宏。

## Resource.h

### FResource <class>

类型化资源的数据基类——只携带路径，派生类持有载荷。用户把 `TResourceImporter<T>` 特化成"字节 → 资源"的解码器。

#### 接口

| 签名 | 说明 |
|------|------|
| `explicit FResource(std::string InPath)` | 构造；路径即目录键 |
| `[[nodiscard]] std::string_view GetPath() const` | 资源路径（虚拟，如 `"Raw/mesh"`） |
| `virtual ~FResource()` | 虚析构 |

### FImportConfig <struct>

导入配置基类——**虚拟**源路径，经 `FPaths::Resolve` 解析成物理路径。资产路径（目录键）由去掉扩展名推导。

#### 成员变量

| 字段 | 类型 | 说明 |
|------|------|------|
| `SourcePath` | `std::string` | 虚拟源路径，如 `"Raw/mesh.fbx"`（目录键 = 去扩展名） |

### FExportConfig <struct>

导出配置基类——显式物理目标路径。

#### 成员变量

| 字段 | 类型 | 说明 |
|------|------|------|
| `DestinationPath` | `std::string` | 显式物理路径，如 `"C:/Out/mesh.fbx"` |

### TResourceImporter<TResource> <struct（用户特化）>

导入特化点——**未定义，按资源类型特化**。只接收原始字节并解码。

```cpp
template <>
struct Maho::Resource::TResourceImporter<FMesh>
{
    using FConfig = FMeshImportConfig;   // 继承 FImportConfig
    static bool Import(const FConfig&, std::span<const std::uint8_t>, FMesh&);
};
```

#### 约束

| 成员 | 说明 |
|------|------|
| `using FConfig` | 导入配置类型（须可从 FImportConfig 派生） |
| `static bool Import(const FConfig&, std::span<const std::uint8_t>, TResource&)` | 解码裸字节；返回 false 视为失败 |

### TResourceExporter<TResource> <struct（用户特化）>

导出特化点——**未定义，按资源类型特化**。在调用线程（游戏线程）编码，只把编码后的字节交给 IO 线程写盘。

```cpp
template <>
struct Maho::Resource::TResourceExporter<FMesh>
{
    using FConfig = FMeshExportConfig;   // 继承 FExportConfig
    static bool Export(const FConfig&, const FMesh&, std::vector<std::uint8_t>&);
};
```

#### 约束

| 成员 | 说明 |
|------|------|
| `using FConfig` | 导出配置类型 |
| `static bool Export(const FConfig&, const TResource&, std::vector<std::uint8_t>&)` | 编码资源到字节；返回 false 视为失败 |

### FResourceSystem <class>

异步传输服务器 + 类型化导入/导出。`FLayer<IPreInit..IPostShutdown>`（10 阶段引擎服务层）+ `FThreadedServer`（IO 线程）。异步传输机制（句柄/批量数据/挂起队列）完全内部——头文件只前向声明。导入器只见 `std::span` / `std::vector` 原始字节。

#### 接口

| 签名 | 说明 |
|------|------|
| `~FResourceSystem() override` | 析构（FImpl 完整后在这里删除） |
| `template <typename TResource> bool Import(typename TResourceImporter<TResource>::FConfig Config, std::function<void(const FResource*)> OnDone = {})` | 异步导入；OnDone 收到注册的资源或 nullptr（游戏线程） |
| `template <typename TResource> bool Export(typename TResourceExporter<TResource>::FConfig Config, std::string_view AssetPath, std::function<void(bool)> OnDone = {})` | 异步导出；OnDone(bool) 报告成败（游戏线程）。调用方必须在导出期间保持资源存活且不变 |
| `[[nodiscard]] const FResource* Find(std::string_view AssetPath) const` | 查已加载资源；无则 nullptr |
| `[[nodiscard]] const FResource* TryLoad(std::string_view AssetPath)` | 尝试加载 = Find；未加载时 nullptr |

#### 生命周期阶段（10 个 stage）

| 阶段 | 方法 | 行为 |
|------|------|------|
| `IInit` | `Initialize(FEngineBase&)` | 启动 IO 线程（`FThreadedServer::Initialize`）+ 发布 `GResourceSystem` |
| `ITick` | `Tick(FEngineBase&)` | `ProcessReadyIO()`——在游戏线程应用就绪传输（解码 + OnDone） |
| `IShutdown` | `Shutdown(FEngineBase&)` | 清 `GResourceSystem`；停止 + join IO 线程；清空挂起队列 + 目录 |
| 其余 7 个 | — | no-op |

#### 内部（private）

`EnqueueImport` / `EnqueueExport`（入队）、`RegisterResource`（注册进目录）、`RequestLoad`（提交 IO 线程读盘）、`ProcessReadyIO`（游戏线程轮询应用）、`WriteBytes`（写盘）、`FImpl`（PImpl：锁 + 挂起导入/导出 + 目录）。

### GetResourceSystem <自由函数>

全局资源系统访问器（跨 DLL）。

#### 接口

| 签名 | 说明 |
|------|------|
| `MAHO_RESOURCE_API FResourceSystem* GetResourceSystem()` | 返回全局 `FResourceSystem*` |

### detail::FindLastDot <function>

按最后一个 `.` 切分虚拟路径：`"Raw/mesh.fbx"` → 资产路径 `"Raw/mesh"`。

#### 接口

| 签名 | 说明 |
|------|------|
| `std::size_t FindLastDot(std::string_view Path)` | 返回最后一个 `.` 的下标；无点返回 `npos` |

- [Resource.md](Resource.md) — 概念 · [实现字典](ImplAPI.md) — 算法
