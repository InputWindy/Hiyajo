# Resource（Private）— 实现算法字典

cpp 侧每个函数的算法伪代码解释。Public 侧 API 文档通过 `#fn-...` 锚点跳转落到这里。

## Resource.cpp

<a id="fn-res-get"></a>
### GetResourceSystem()

← [公开 API](API.md) · `FResourceSystem*`

```text
GetResourceSystem():
1. return GResourceSystem
```

<a id="fn-res-init"></a>
### FResourceSystem::Initialize(FEngineBase&) / Shutdown(FEngineBase&)

← [公开 API](API.md) · `void`

启动/停止 IO 线程 + 发布/清空全局实例。

```text
Initialize(Engine):
1. FThreadedServer::Initialize()   // 启动异步加载线程
2. GResourceSystem = this

Shutdown(Engine):
1. GResourceSystem = nullptr
2. FThreadedServer::Shutdown()    // 停止 + join IO 线程
3. lock(Impl->Mutex):
4.     Impl->PendingIO.clear(); Impl->PendingExports.clear(); Impl->Catalog.clear()
```

<a id="fn-res-tick"></a>
### FResourceSystem::Tick(FEngineBase&)

← [公开 API](API.md) · `void`

```text
Tick(Engine):
1. ProcessReadyIO()   // 轮询传输 + 游戏线程解码
```

<a id="fn-res-requestload"></a>
### FResourceSystem::RequestLoad(std::string Path)

内部。提交 IO 线程读文件到 `FTransferState`，置成败原子位。

```text
RequestLoad(Path):
1. State = make_shared<FTransferState>()
2. Submit([State, Path]:
      Bulk = {}
      Stream = ifstream(Path, binary)
      if Stream: Bulk.Bytes.assign(istreambuf_iterator, end)
      lock(State->Mutex): State->Bulk = move(Bulk)
      if !Bulk.Bytes.empty(): State->bSucceeded = true
      else:                    State->bFailed = true)
3. return FTransferHandle{ move(State) }
```

<a id="fn-res-enqueue"></a>
### FResourceSystem::EnqueueImport / EnqueueExport

内部。EnqueueImport 解析物理路径 + RequestLoad，登记挂起导入；EnqueueExport 提交写盘任务，登记挂起导出。

```text
EnqueueImport(SourcePath, AssetPath, OnBulkReady):
1. PhysicalPath = Paths::GetPaths()->Resolve(SourcePath).string()
2. Handle = RequestLoad(PhysicalPath)
3. lock(Impl->Mutex): Impl->PendingIO[FName(AssetPath)] = { move(Handle), move(OnBulkReady) }
4. return true

EnqueueExport(Bytes, DestinationPath, OnDone):
1. State = make_shared<FTransferState>(); Dest = move(DestinationPath)
2. Submit([State, Dest, Bytes]:
      bWritten = WriteBytes(Dest, Bytes)
      bWritten ? State->bSucceeded = true : State->bFailed = true)
3. lock(Impl->Mutex): Impl->PendingExports[FName(Dest)] = { FTransferHandle{State}, move(OnDone) }
4. return true
```

<a id="fn-res-register"></a>
### FResourceSystem::RegisterResource(std::string AssetPath, std::unique_ptr<FResource> Resource)

内部。注册进目录（锁保护），返回裸指针。

```text
RegisterResource(AssetPath, Resource):
1. Raw = Resource.get()
2. lock(Impl->Mutex): Impl->Catalog[FName(AssetPath)] = move(Resource)
3. return Raw
```

<a id="fn-res-process"></a>
### FResourceSystem::ProcessReadyIO()

内部。游戏线程每帧应用就绪传输。**整个轮询持 Impl 锁**：IO 线程并发写挂起队列，无锁迭代/删除是数据竞争。导入每帧最多应用 `kMaxAppliesPerTick` 个。

```text
ProcessReadyIO():
1. lock(Impl->Mutex)
2. Applied = 0
3. for Pending in PendingIO（遍历，Applied < kMaxAppliesPerTick）:
       if !HasSucceeded && !HasFailed: continue
       Ready = move(Pending); erase
       if HasSucceeded:
           lock(Ready.State->Mutex): Ready.OnBulkReady(Ready.State->Bulk.Bytes)  // 解码，游戏线程
       else:
           Ready.OnBulkReady({})   // 失败 - 空 span
       Applied += 1
4. for Pending in PendingExports:
       if !HasSucceeded && !HasFailed: continue
       Ready = move(Pending); erase
       if Ready.OnDone: Ready.OnDone(Ready.Handle.HasSucceeded())
```

<a id="fn-res-write"></a>
### FResourceSystem::WriteBytes(std::string_view PhysicalPath, std::span<const std::uint8_t> Bytes)

内部静态。IO 线程写盘。

```text
WriteBytes(PhysicalPath, Bytes):
1. Stream = ofstream(PhysicalPath, binary)
2. if !Stream: return false
3. Stream.write(reinterpret_cast<const char*>(Bytes.data()), Bytes.size())
4. return static_cast<bool>(Stream)
```

<a id="fn-res-find"></a>
### FResourceSystem::Find / TryLoad

← [公开 API](API.md) · `const FResource*`

```text
Find(AssetPath):
1. lock(Impl->Mutex)
2. It = Impl->Catalog.find(FName(AssetPath))
3. return It != end ? It->second.get() : nullptr

TryLoad(AssetPath):
1. return Find(AssetPath)
```

<a id="fn-res-dot"></a>
### detail::FindLastDot(std::string_view Path)

← [公开 API](API.md) · `std::size_t`

```text
FindLastDot(Path):
1. return Path.find_last_of('.')
```

<a id="fn-res-export"></a>
### CreateLayer()（C 导出）

```text
extern "C" CreateLayer():
1. return Maho::Resource::FResourceSystem::CreateLayer()
```

- [Resource.md](Resource.md) — 概念 · [公开 API](API.md) — 签名入口
