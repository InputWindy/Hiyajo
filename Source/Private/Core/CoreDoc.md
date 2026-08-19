<!-- mahogen -->
# Core

## 代码文件

- [Assembly.cpp](Assembly.cpp)
- [Fatal.cpp](Fatal.cpp)
<!-- mahogen end -->

## 实现算法字典

引擎核心只在两个 `.cpp` 有实现（其余全 header-only 模板）。

### Assembly.cpp —— 动态加载原语

`FAssembly` 的 OS 加载实现。

| 函数 | 说明 |
|------|------|
| `FAssembly(std::string_view Path)` | `Load` 构造 |
| `~FAssembly()` | `Unload` 析构 |
| `Load(Path)` | `LoadLibraryA` / `dlopen`，返回是否成功 |
| `Unload()` | `FreeLibrary` / `dlclose`，幂等 |
| `GetProcAddress(Name)` | `::GetProcAddress` / `dlsym`，空句柄返回 `nullptr` |

### Fatal.cpp —— 崩溃兜底

| 函数 | 说明 |
|------|------|
| `ReportFatal(...)` | 输出致命错误 + 终止进程 |
| `InstallFatalHandlers()` | 注册结构化异常 / 信号处理器 |

## 相关文档

- [../../Public/Core/CoreDoc.md](../../Public/Core/CoreDoc.md) — 根概念（Public）
- [../../PrivateDoc.md](../../PrivateDoc.md) — Private 层
