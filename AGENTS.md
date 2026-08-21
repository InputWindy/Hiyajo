# Maho — Agent 入口（核心插件）

所有 AI Agent 进本插件前先读本文件。

## 设计约束（强约束）

- 本插件是**引擎核心**——纯通用基础设施：零 app 假设、零三方依赖、零 stage 预设。
- 只提供积木：`TypeList`（`TTypeList` + `ForEach`）/ `Topology`（`TDependsOn`/`TDependsPack` + 排序/分层/环检测）/ `Delegate` / `Singleton` / `Scheduler`（`IScheduler` 空基 + 双 `Execute`）/ `Extension`（`TExtension` 单例 + `TExtensionList` 组装）/ `Assembly`（`IAssembly` + `FAssembly`）/ `Fatal`。
- **插件 = 依赖表 + 身份**：`TExtension<TExtensions...>` 是纯依赖表（编译期 TTypeList），不预设单例/可安装。`TTool` 加 `TSingleton`（即插即用、全 public），`TLayer` 加 `IAssembly`（可动态安装、并行调度自己 FExtensions）。不再区分 Engine——应用根就是一个 Layer。
- **驱动机制**：编译期 `ForEach`（`TTag<T>` + Scheduler 串/并行）+ Tool `T::Get()` 单例直调；`IScheduler` 双 `Execute`——stage 版（硬编码 `ExecuteStage`）+ lambda 版（`FDefaultSlot` 排序 + Visitor 自定义）。
- **依赖声明两层**：编译期 `TDependsOn`/`TDependsPack`（插件内，level 排序）；项目装配 `.cplugin` `Dependencies`（CMake + codegen）。核心与核心之间走 `TDependsOn`，插件与宿主走 `.cplugin`。
- stage 枚举、app 形态、线程池策略**全部下放插件层**，不写回 core（`Engine/` 只放可选的 `FSerialScheduler`/`FParallelScheduler` 示例）。
- 遵循根 [AGENTS.md](../AGENTS.md)。

## 接口分层（强约束，违反 = 违背引擎架构）

Tool 与 Layer 性质不同，访问规则相反：

**Tool = 即插即用，自行管理（Standalone）**——读接口和写接口**全 public**，不设 friend。Tool 是轻量服务积木，谁用谁直调 `Get().xxx()`，生命周期由调用方决定，不依赖调度器。引擎没有逻辑写管制 Tool，也不给 Tool 收写。

```cpp
class FPlatformTool : public Maho::TTool<FPlatformTool>
{
public:
	[[nodiscard]] FNativeSurface GetNativeWindow() const;  // 读 → public
	bool CreateWindow(int, int, std::string_view);         // 写 → public（Tool 自带管理）
	void PollEvents();                                     // 写 → public
};
```

**Layer = 重代码，调度器统一负责写**——Tool / Layer 全是单例，`Get()` 是 public，任意插件都能拿到对方单例。核心规则：

- `const` 读接口 → **public**。读不改状态，无多线程竞争，任意方可读。
- 非 `const` 写接口 → **protected**。写改变自身状态，多线程下危险，**只有调度器能写**。
- 调度器唯一的写入口 = `ExecuteExtension<T, TStage>(TStage)`，通过类内 friend 模板声明授权：

```cpp
template <typename TExtension, typename TStage>
friend bool Maho::ExecuteExtension(TStage Stage);
```

**为什么 Layer 要收紧**：若 Layer 直接 `Get()` 对方单例 + 调 public 写接口，等于绕过调度器去写别的插件，破坏调度器的线程安全写入模型。所以 Layer 的非 const 写接口必须收进 protected，只有调度器（经 friend `ExecuteExtension`）能触达。

> 编译前代码合规审查工具（`Tools/check_interface_layers.py`）强制这条规则：Layer 的 public 非 const 方法、多余 friend、缺失 friend `ExecuteExtension` 全部直接报错卡住构建。Tool 天生 Standalone，审查器跳过。

**插件不感知 stage**：Tool/Layer 不定义 stage 枚举、不特化 `ExecuteExtension`，只提供能力方法。stage 由宿主（Engine/Layer）定义，宿主特化 `ExecuteExtension<Plugin, HostStage>(HostStage)` 把 stage 映射到插件的能力方法。

```cpp
// 插件（Tool）：全 public，自带管理
class FPlatformTool : public Maho::TTool<FPlatformTool>
{
public:
	[[nodiscard]] FNativeSurface GetNativeWindow() const;
	bool CreateWindow(int, int, std::string_view);
	void PollEvents();
};

// 宿主（Engine/Layer）：定义 stage，特化映射（Layer 才走这条写路径）
template <>
bool ExecuteExtension<Platform::FPlatformTool, EEngineStage>(EEngineStage Stage)
{
	switch (Stage)
	{
	case EEngineStage::Tick:     Platform::FPlatformTool::Get().PollEvents(); break;
	case EEngineStage::Shutdown: Platform::FPlatformTool::Get().DestroyWindow(); break;
	}
	return true;
}
```

## 项目侧开发约束（强约束）

拓展项目侧代码时，遵守以下三条：

### ① 接口定义与实现分离

- **定义接口**：全部写到项目入口插件的 `Public/` 目录下，按功能分好文件夹层级。
- **实现接口**：新建一个插件写到入口插件的**外面**（`Source/<新插件名>/`），在它自己的 `Private/` 里实现。
- 入口插件**不关心任何接口的实现原理**，只负责在 `Main` 里调度。

```
Source/
  Main.cpp
  <入口插件>/            ← 只放接口（Public/）+ 调度（Private/Main）
    Public/FeatureA/IXxx.h
    Public/FeatureB/IYyy.h
    Private/<入口插件>.cpp   ← 只调度，不实现
  <功能插件>/            ← 接口实现（新插件，写入口外面）
    Public/  Private/
```

### ② 代码实现用工具创建

- 新建插件请调用项目侧的 `CreatePlugin.bat` 自动创建，**不要手写目录/`.cplugin`**。
- 工具会：生成 `Public/` + `Private/` + `.cplugin`，并自动把项目入口插件加进 `Dependencies`（锚定父插件）。

### ③ 三方插件整体安装到 Extension

- 要用外部的第三方插件包，**整个安装到项目侧的 `Extension/` 目录下**。
- 构建项目时（双击 `.cproject`）会自动补全代码依赖（include 路径 + DLL target），无需手动配。

**沙漏依赖**（背景）：引擎插件 → 项目入口插件 → 功能子插件。入口插件是唯一宿主——只有它继承 `IAssembly` 并导出 `CreateExtension()`，其余插件是纯 `TExtension` 单例（无 Main）。

## 文档

- [Source/SourceDoc.md](Source/SourceDoc.md) — 源码根（Public/Private 分工）
- [Source/Public/Core/CoreDoc.md](Source/Public/Core/CoreDoc.md) — 基础设施概念
- [Source/Public/Core/CoreAPI.html](Source/Public/Core/CoreAPI.html) — API 文档
- [Source/Public/Engine/EngineDoc.md](Source/Public/Engine/EngineDoc.md) — 调度策略（Serial/Parallel/ThreadPool）
