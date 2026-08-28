# Maho

<div style="background:#143d2b;border:1px solid #3f8f63;border-radius:8px;padding:12px 16px;color:#cdeadd;">

<b>作者寄语：Coding前请务必让AI通读 AGENTS.md 学习规范 —— 因为写代码的是AI，而不是程序员。</b>

</div>

## 简介

Maho 是一个纯 C++20 的游戏引擎，核心设计围绕**并行依赖图调度**与**插件式架构**展开。它借鉴 Unity DOTS 的理念：把应用拆解成一组职责单一的**匿名层（layer）**，每层按**有序 stage 管线**（BeginFrame → Tick → EndFrame）展开成任务图节点，由依赖图调度器自动编排——有依赖则串行，无依赖则并行，依赖边即隐式 barrier。

一个功能就是一个插件，一个插件就是一个 DLL。引擎核心只提供少量通用积木，所有能力——日志、序列化、物理、渲染——都以插件形式存在，按需安装、按依赖卸载。

## 特性

- **匿名层 + stage 管线**：`FLayerBase` 只闭合自己（身份名 + 逐 stage 依赖表），不管理依赖生命周期；`IPipeline<TStages...>` 定义有序 stage 序列，`FLayerTaskGraph` 全局调度。
- **依赖图调度**：节点 = (对象名, stage)，边来自依赖元组。节点在其所有直接依赖完成后立即释放（无 stage barrier，跨 stage 流水线）。
- **动态安装/卸载**：`FEngineBase` 经 `FAssembly` 动态加载 feature DLL；卸载按反向依赖数小顶堆贪心，被依赖的层拒绝卸载、依赖者先弹连锁卸载。
- **输入驱动闭环**：输入处理本身是一个 feature（GameInputLayer），在 Tick 阶段经引擎调度安装/卸载/退出——引擎只提供调度能力，策略全是插件。
- **能力可选组合**：`IInit`/`IShutdown`/`IMain`/`IExit` 经 `IPlugin<Caps...>` 显式组合；`TSingleton<T>` 纯标识基类，无强制接口。
- **零三方依赖的核心**：引擎核心不含任何第三方库，每个插件在自己的 `.cmake` 里用 FetchContent 拉取依赖，镜像与代理可配置。
- **纯通用核心**：核心不预设 stage 枚举、不预设应用形态，全部下放应用层。

## 构建流程

### 0. 首次自举

```bat
Setup.bat
```

安装引擎本地 Python（`Tools/python` junction → `%LOCALAPPDATA%\Maho\python\tooling`）。优先复用宿主机的 Python（带 tkinter）建 venv，否则下载 python.org installer 静默安装。**全程不依赖系统 PATH 上的 python**。

### 1. 创建项目

```bat
CreateProject.bat
```

打开 UI，填项目名 + 勾选引擎插件（依赖自动解析）。生成：

```
<项目>/
  <项目>.cproject
  Plugins/                    ← 项目自己的插件
    <项目>/Public/            ← 入口插件接口（FEngineBase 宿主）
    <项目>/Private/           ← 入口插件实现
    <子插件>/Public/          ← 功能插件（FEngineLayer feature）
  Extension/                  ← 三方插件目录
  Intermediate/Main.cpp       ← 入口（code-gen，不改）
  CMakeLists.txt / package.bat / CreatePlugin.bat
```

### 2. 生成工程

双击 `<项目>.cproject`（或 `Tools/generateProject.bat <项目>.cproject`）：

- codegen 生成 `Intermediate/Generated/<项目>.gen.h`（插件 includes + 扩展宏）
- 扫描引擎 `Plugins/`、项目 `Plugins/`、项目 `Extension/`，重写 CMakeLists（每个插件一个 DLL target）
- 探测本机 Visual Studio（vswhere）选 CMake 生成器
- 产出 `<项目>.sln`

### 3. 构建

用 Visual Studio 打开 `.sln` 构建，或命令行：

```bat
cmake -S . -B Intermediate -G "Visual Studio 17 2022" -A x64
cmake --build Intermediate --config Debug
```

产物：`Intermediate/Binaries/<Config>/` 下各插件 DLL + `EntryPoint.exe`。构建前自动运行插件依赖环检测（`MahoCheckCycle`）。

### 4. 打包

```bat
package.bat
```

打开 UI 选 platform / config，构建并拷贝 exe + 全部 DLL 到 `Packaged/<Platform>/<Config>/`。

## 核心概念

### 三类插件

新建插件时按角色选模板（`CreatePlugin.bat` → codegen）：

| 模板 | 角色 | 导出 | 说明 |
|------|------|------|------|
| `entry` | 应用根 | `CreateEngine()` → `FEngineBase*` | 继承 `FEngineBase`，主循环 + 安装调度 |
| `feature` | 功能层 | `CreateLayer()` → `FEngineLayer*` | 继承 `FEngineLayer`，实现 BeginFrame/Tick/EndFrame |
| `engine` | 纯库 | 无 | `namespace` 作用域，无生命周期 |

```cpp
// entry —— 应用根
class FMyGame : public Maho::FEngineBase
{
    MAHO_DECLARE_ENGINE(FMyGame, "MyGame.dll");
public:
    void Initialize(int, char**) override;
    void Shutdown() override;
};

// feature —— 功能层
class FRenderer : public Maho::FEngineLayer
{
    MAHO_DECLARE_LAYER(FRenderer);
public:
    void BeginFrame() override;
    void Tick() override;
    void EndFrame() override;
};
```

### 匿名层 + stage 管线

**FLayerBase** 只闭合自己：

```cpp
class FLayerBase
{
    virtual std::string_view GetName() const = 0;   // 稳定身份名（拓扑键）
    const FDependencyTable& GetDependencies() const; // 逐 stage 依赖表
protected:
    template <typename TMyStage, typename TDepObj, typename TDepStage>
    void AddDependency();   // 编译期依赖：this 在 TMyStage 依赖 TDepObj 在 TDepStage
    void AddDependency(type_index, string_view, type_index);  // 运行时依赖（跨 DLL 字符串寻址）
};
```

**IPipeline** 定义有序 stage 序列；**FLayer<TPipeline>** 把层 + 管线绑一起；**FLayerTaskGraph** 把一组层展开成每 stage 一个节点（自递进 + 跨对象依赖）并拓扑调度。

### 引擎主循环

`FEngineBase::Main()` 驱动一张 `FLayerTaskGraph<IEnginePipeline, FEngineBase>`：

```
while (true)
{
    Flush → FlushPendingUpdatePipelines()   // 应用挂起安装/卸载
    Init → Compile → Execute → Flush         // BeginFrame → Tick → EndFrame
    检查 RequestExit 标志
}
```

跨 feature 依赖（feature 构造时声明）：

```cpp
class FWorld : public FEngineLayer
{
    MAHO_DECLARE_LAYER(FWorld);
public:
    FWorld()
    {
        // 我的 Tick 依赖 FLog 的 BeginFrame（编译期）
        AddDependency<ITick, FLog, IBeginFrame>();
        // 或运行时字符串寻址（跨 DLL）
        AddDependency(std::type_index(typeid(ITick)), "FDynLog", std::type_index(typeid(IBeginFrame)));
    }
};
```

### 动态安装 / 卸载

```cpp
// 动态加载 + 安装（引擎持有 DLL + 实例所有权）
Install("Renderer.dll");

// 匿名卸载（按层名），依赖安全：
//   - 被依赖 → 卸载失败放弃
//   - 无依赖 → 小顶堆贪心，依赖者先弹连锁卸载
TryUninstall("FRenderer");

// 退出主循环
RequestExit();
```

### 能力可选组合

引擎不强制任何能力，插件自己决定：

- **单例**（`TSingleton<T>`）：`Get()` 进程唯一（头声明 + cpp 定义在各自 DLL）
- **生命周期**（`IInit`/`IShutdown`/`IMain`/`IExit`）：经 `IPlugin<Caps...>` 显式组合
- **安装**（`FEngineBase`）：入口插件唯一宿主，`Install`/`TryUninstall` 调度 feature

## 示例项目代码组成

以 `TestFull` 项目为例，展示引擎的完整用法。它动态安装三个 feature（Log/World/Render），并用一个**输入驱动层**（GameInput）逐帧驱动安装/卸载/退出，验证依赖图排序与动态卸载安全。

### 目录结构

```
TestFull/
  TestFull.cproject              ← 项目清单（勾选引擎插件）
  Plugins/
    TestFull/                    ← 入口插件（entry 模板）
      Public/TestFull.h          ← FTestFull : FEngineBase
      Private/TestFull.cpp       ← Initialize/Shutdown + CreateEngine bridge
      TestFull.cplugin           ← 依赖表
    DynLog/                      ← feature：日志层（无依赖）
      Public/DynLog.h            ← FDynLog : FEngineLayer
      Private/DynLog.cpp         ← 三阶段打点
    DynWorld/                    ← feature：世界层
      Public/DynWorld.h          ← FDynWorld : FEngineLayer（Tick 依赖 DynLog.BeginFrame）
      Private/DynWorld.cpp
    DynRender/                   ← feature：渲染层
      Public/DynRender.h         ← FDynRender : FEngineLayer（EndFrame 依赖 DynWorld.Tick）
      Private/DynRender.cpp
    GameInput/                   ← feature：输入驱动层
      Public/GameInput.h         ← FGameInput : FEngineLayer
      Private/GameInput.cpp      ← Tick 里逐帧 Install/TryUninstall/RequestExit
  Intermediate/Main.cpp          ← 入口（code-gen，不改）
  CMakeLists.txt / package.bat / CreatePlugin.bat
```

### 入口插件（TestFull）

```cpp
// TestFull.h
class FTestFull : public FEngineBase
{
    MAHO_DECLARE_ENGINE(FTestFull, "TestFull.dll");
public:
    void Initialize(int Argc, char** Argv) override;
    void Shutdown() override;
};
```

```cpp
// TestFull.cpp
void FTestFull::Initialize(int Argc, char** Argv)
{
    FLog::Get().Initialize(Argc, Argv);
    // 只装输入驱动层；其余 feature 由 GameInput 逐帧动态安装。
    Install("GameInput.dll");
}

void FTestFull::Shutdown()
{
    FLog::Get().Shutdown();   // feature + DLL 已由 FEngineBase::Shutdown 释放
}

extern "C" MAHO_TESTFULL_API Maho::FEngineBase* CreateEngine()
{
    return Maho::FTestFull::CreateEngine();
}
```

### 输入驱动层（GameInput）

```cpp
// GameInput.cpp —— Tick 里模拟用户输入，驱动安装/卸载/退出
void FGameInput::Tick()
{
    ++TickCount;
    switch (TickCount)
    {
    case 1: Owner->Install("DynLog.dll");    break;   // 每帧装一个
    case 2: Owner->Install("DynWorld.dll");   break;
    case 3: Owner->Install("DynRender.dll");  break;
    case 5: Owner->TryUninstall("FDynWorld"); break;   // 被依赖 → 放弃
    case 7:
        Owner->TryUninstall("FDynWorld");              // 同帧两请求
        Owner->TryUninstall("FDynRender");             // 依赖者先弹连锁卸载
        break;
    default: Owner->RequestExit();            break;   // 退出主循环
    }
}
```

`Owner` 是 `FEngineLayer` 成员，`Install` 时由引擎自动注入 `FEngineBase*`——feature 经它调度安装/卸载/退出，与其它 feature 平级。

### feature 依赖声明（DynWorld / DynRender）

```cpp
// DynWorld.h —— Tick 依赖 DynLog 的 BeginFrame（跨 DLL 字符串寻址）
class FDynWorld : public FEngineLayer
{
    MAHO_DECLARE_LAYER(FDynWorld);
    MAHO_DECLARE_FEATURE(FDynWorld, "DynWorld.dll");
public:
    FDynWorld()
    {
        AddDependency(std::type_index(typeid(ITick)), "FDynLog", std::type_index(typeid(IBeginFrame)));
    }
    void BeginFrame() override;
    void Tick() override;
    void EndFrame() override;
};
```

```cpp
// DynRender.h —— EndFrame 依赖 DynWorld 的 Tick
class FDynRender : public FEngineLayer
{
    MAHO_DECLARE_LAYER(FDynRender);
    MAHO_DECLARE_FEATURE(FDynRender, "DynRender.dll");
public:
    FDynRender()
    {
        AddDependency(std::type_index(typeid(IEndFrame)), "FDynWorld", std::type_index(typeid(ITick)));
    }
    ...
};
```

### 运行验证

```
tick 1-3 : 动态安装 Log → World → Render
tick 4   : 3 features 并行调度（无依赖的 stage 并行）
tick 5   : 请求卸载 World（被 Render 依赖）→ 放弃
tick 6   : World 仍在
tick 7   : 同帧请求卸载 World + Render → Render 先弹、World 连锁弹出
tick 8   : 只剩 Log
tick 9   : RequestExit → 退出
```

完整闭环：`EntryPoint` → `FAssembly` 加载 `TestFull.dll` → `CreateEngine()` → `FEngineBase*` → `Initialize`（装 GameInput）→ `Main`（主循环）→ GameInput.Tick 动态驱动 → 依赖图拓扑调度 → `Shutdown`。

## 接口与实现分离

入口插件的 `Public/` 只放接口（按功能分文件夹），实现写在入口之外的新插件 `Private/` 里。入口不关心实现原理，只负责调度。插件的 Public 头不泄露第三方依赖。
