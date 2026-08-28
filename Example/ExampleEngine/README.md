# ExampleEngine — Maho 示例项目

一个最小但完整的 Maho 项目，展示引擎的核心用法：**匿名层 + stage 管线 + 依赖图调度 + 动态安装/卸载 + 输入驱动闭环**。

## 项目做了什么

```
tick 1-3 : GameInput 逐帧动态安装 DynLog → DynWorld → DynRender
tick 4   : 三个 feature 并行调度（无依赖的 stage 并行）
tick 5   : GameInput 请求卸载 DynWorld（被 DynRender 依赖）→ 放弃
tick 6   : DynWorld 仍在运行（请求被正确拒绝）
tick 7   : GameInput 同帧请求卸载 DynWorld + DynRender → 依赖者先弹、连锁卸载
tick 8   : 只剩 DynLog
tick 9   : GameInput 调 RequestExit → 引擎退出
```

运行日志节选：

```
[info] FExampleEngine::Initialize — input-driven install/uninstall test
[info] [DynLog] BeginFrame
[info] [DynWorld] Tick (依赖 DynLog.EndFrame)
[info] [DynRender] EndFrame (依赖 DynWorld.EndFrame)
[info] [Frame] tick=4 (3 features running)
[info] [Frame] tick=6 (World uninstall should be dropped)
[info] [Frame] tick=8 (only Log should remain)
```

## 目录结构

```
ExampleEngine/
  ExampleEngine.cproject          项目清单（勾选引擎插件 + 项目插件）
  ExampleEngine.sln               VS 解决方案（双击 .cproject 生成）
  CMakeLists.txt                  codegen 生成（勿手改）
  package.bat                     打包 UI（选 platform / config）
  CreatePlugin.bat                新建插件 UI
  Plugins/
    ExampleEngine/                ① 入口插件（entry）
      Public/ExampleEngine.h      FExampleEngine : FEngineBase
      Private/ExampleEngine.cpp   Initialize/Shutdown + CreateEngine bridge
      ExampleEngine.cplugin
    GameInput/                    ② 输入驱动层（feature）
      Public/GameInput.h          FGameInput : FEngineLayer
      Private/GameInput.cpp       Tick 逐帧驱动安装/卸载/退出
      GameInput.cplugin
    DynLog/                       ③ 日志层（feature，无依赖）
    DynWorld/                     ④ 世界层（feature，Tick 依赖 DynLog.EndFrame）
    DynRender/                    ⑤ 渲染层（feature，BeginFrame 依赖 DynWorld.EndFrame）
```

## 代码逐文件解析

### ① 入口插件（ExampleEngine）

**`Public/ExampleEngine.h`** —— 应用根，继承 `FEngineBase`：

```cpp
class FExampleEngine : public FEngineBase
{
MAHO_DECLARE_ENGINE(FExampleEngine, "ExampleEngine.dll");

public:
	void Initialize(int Argc, char** Argv) override;
	void Shutdown() override;
};
```

**`Private/ExampleEngine.cpp`** —— 只装输入层，其余交给它：

```cpp
void FExampleEngine::Initialize(int Argc, char** Argv)
{
	FLog::Get().Initialize(Argc, Argv);
	Install("GameInput.dll");   // 只装输入驱动层
}

void FExampleEngine::Shutdown()
{
	FLog::Get().Shutdown();     // feature + DLL 已由 FEngineBase::Shutdown 释放
}

// C 导出 bridge：EntryPoint 按符号名查找
extern "C" MAHO_EXAMPLEENGINE_API Maho::FEngineBase* CreateEngine()
{
	return Maho::FExampleEngine::CreateEngine();
}
```

### ② 输入驱动层（GameInput）

**`Public/GameInput.h`** —— 普通 feature，与其它层平级：

```cpp
class FGameInput : public FEngineLayer
{
MAHO_DECLARE_LAYER(FGameInput);
MAHO_DECLARE_FEATURE(FGameInput, "GameInput.dll");

public:
	void BeginFrame() override;
	void Tick() override;
	void EndFrame() override;

private:
	int TickCount = 0;
};
```

**`Private/GameInput.cpp`** —— `Tick` 里模拟用户输入，驱动引擎：

```cpp
void FGameInput::Tick()
{
	++TickCount;
	switch (TickCount)
	{
	case 1: Owner->Install("DynLog.dll");    break;   // 每帧装一个
	case 2: Owner->Install("DynWorld.dll");   break;
	case 3: Owner->Install("DynRender.dll");  break;
	case 4: MAHO_LOG_CORE_INFO("[Frame] 3 features running"); break;
	case 5: Owner->TryUninstall("FDynWorld"); break;   // 被依赖 → 放弃
	case 7:
		Owner->TryUninstall("FDynWorld");              // 同帧两请求
		Owner->TryUninstall("FDynRender");             // 依赖者先弹连锁卸载
		break;
	default: Owner->RequestExit();            break;   // 退出主循环
	}
}
```

关键点：`Owner` 是 `FEngineLayer` 的成员指针，`Install` 时由引擎自动注入 `FEngineBase*`。feature 经它调度安装/卸载/退出——**输入策略是插件，不是引擎内置**。

### ③④⑤ 三个业务 feature

**DynLog** —— 无依赖，三阶段打点：

```cpp
void FDynLog::BeginFrame() { MAHO_LOG_CORE_INFO("[DynLog] BeginFrame"); }
void FDynLog::Tick()       { MAHO_LOG_CORE_INFO("[DynLog] Tick"); }
void FDynLog::EndFrame()   { MAHO_LOG_CORE_INFO("[DynLog] EndFrame"); }
```

**DynWorld** —— 构造时声明跨 DLL 依赖（字符串寻址）：

```cpp
FDynWorld()
{
	// 我的 Tick 依赖 DynLog 的 EndFrame
	AddDependency(std::type_index(typeid(ITick)), "FDynLog", std::type_index(typeid(IEndFrame)));
}
```

**DynRender** —— 同理：

```cpp
FDynRender()
{
	// 我的 BeginFrame 依赖 DynWorld 的 EndFrame
	AddDependency(std::type_index(typeid(IBeginFrame)), "FDynWorld", std::type_index(typeid(IEndFrame)));
}
```

依赖链：`DynLog.EndFrame ← DynWorld.Tick ← DynRender.BeginFrame`，由 `FLayerTaskGraph` 拓扑排序保证执行顺序。

## 运行时匿名安装插件

主 DLL（`ExampleEngine.dll`）对 4 个 feature（DynLog/DynWorld/DynRender/GameInput）**零编译期依赖**——不 link、不 include 它们的头文件，也完全不知道它们的类型。一切都在运行时按名字加载。

### codegen 的依赖分层

`ExampleEngine.cproject` 勾选了全部插件，但 codegen 按插件归属区分两类：

| 插件类型 | 位置 | 主 DLL 编译期 | 运行时 |
|---------|------|--------------|--------|
| **引擎服务插件** | `Hiyajo/Plugins/Common/`（Log/Platform/RHI…） | ✅ link + include（主 DLL 头直接 `#include <Log.h>`） | 随主 DLL 静态依赖 |
| **项目 feature 插件** | 本项目 `Plugins/`（DynLog/GameInput…） | ❌ 不 link、不 include | ✅ `Install("Xxx.dll")` 经 FAssembly 动态加载 |

生成的 CMakeLists 里：

```cmake
# 主 DLL 只 link 引擎核心 + 引擎服务插件（无 Dyn*/GameInput）
target_link_libraries(ExampleEngine PUBLIC Maho Archive Paths ... Unicode)

# EntryPoint 依赖全部（保证 feature DLL 也构建 + 拷到 Binaries）
add_dependencies(EntryPoint ExampleEngine ... DynLog DynWorld DynRender GameInput ...)
```

### 匿名加载链路

```cpp
Install("DynLog.dll")                    // ① 只传名字，主 DLL 不知道 FDynLog 类型
  └─ FAssembly("DynLog.dll")             // ② LoadLibrary
       └─ GetProcAddress("CreateLayer")  // ③ 按符号名查 C 导出
            └─ FDynLog::CreateLayer()    // ④ 返回 FEngineLayer*（基类指针）
                 └─ Install(Layer.get()) // ⑤ 引擎持有所有权 + 注入 Owner
```

主 DLL 全程只见 `FEngineLayer*` 基类指针，`FDynLog` 具体类型只存在于 `DynLog.dll` 内部。这就是**真正的插件隔离**：

- 主 DLL 不依赖 feature 的任何编译期符号
- feature 可独立编译、独立替换、运行时插拔
- 主 DLL 与 feature 只通过 `FLayerBase`/`FEngineLayer` 基类契约交互

### 卸载同理

```cpp
TryUninstall("FDynLog")                  // 按层名（GetName()）匿名寻址
  → 反向依赖数小顶堆贪心 → 引擎 delete 实例 + FreeLibrary
```

主 DLL 同样不需要 feature 的具体类型来卸载——`GetName()`（`MAHO_DECLARE_LAYER` 字符串化类名）就是全部身份信息。

## 架构要点

| 概念 | 在本项目中的体现 |
|------|-----------------|
| **匿名层**（FLayerBase） | 每个 feature 只暴露 `GetName()`（类名）+ 依赖表，不自闭环生命周期 |
| **stage 管线**（IPipeline） | `IEnginePipeline` = BeginFrame → Tick → EndFrame |
| **依赖图调度**（FLayerTaskGraph） | `FEngineBase::Main` 每帧 `Init → Compile → Execute → Flush` |
| **动态安装** | `Install("Xxx.dll")` 经 FAssembly 加载 + 引擎持有所有权 |
| **依赖安全卸载** | `TryUninstall` 按反向依赖数小顶堆贪心，被依赖则放弃 |
| **输入驱动** | GameInput 是普通 feature，Tick 里经 `Owner` 调度引擎 |

## 构建与运行

```bat
:: 1. 首次自举（引擎根目录，只需一次）
cd ..\..
Setup.bat

:: 2. 生成工程（本项目）
双击 ExampleEngine.cproject
:: 或命令行：cd ..\.. && Tools\generateProject.bat Example\ExampleEngine\ExampleEngine.cproject

:: 3. 构建
cmake --build Intermediate --config Debug

:: 4. 运行
cd Intermediate\Binaries\Debug
EntryPoint.exe ExampleEngine.dll
```

产物：`Intermediate/Binaries/Debug/` 下 `EntryPoint.exe` + `Maho.dll` + 全部插件 DLL + 本项目 5 个插件 DLL。日志同时进控制台和 `Logs/Maho.log`（5MB 滚动 3 份）。

## 与引擎的关系

```
EntryPoint.exe
  └─ FAssembly 加载 ExampleEngine.dll
       └─ CreateEngine() → FExampleEngine*（FEngineBase）
            ├─ Initialize: Install(GameInput.dll)
            ├─ Main: 主循环（依赖图调度）
            │    └─ GameInput.Tick → Install/TryUninstall/RequestExit
            └─ Shutdown: 释放全部 feature + DLL
```

引擎（`../..`，即 `Hiyajo/`）只提供 `Core`（TypeList/Query/TaskGraph/Assembly…）+ `Engine`（Layer/Engine 层体系）；一切业务逻辑在 `Plugins/` 里。本项目不修改引擎任何代码，纯插件组装。
