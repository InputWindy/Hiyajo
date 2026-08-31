# Core（Private）

## Code Files

- [Assembly.cpp](Assembly.cpp) — FAssembly（header-only，占位文件）
- [Fatal.cpp](Fatal.cpp) — 致命路径 / 崩溃兜底
- [TaskGraph.cpp](TaskGraph.cpp) — 依赖图调度器

## Implementation Algorithm Dictionary

逐函数伪代码见 [CoreAPI.md](CoreAPI.md)（实现算法字典）：

| cpp | 函数 |
|-----|------|
| `Fatal.cpp` | `InstallFatalHandlers` / `ReportFatal` / `ReportError` / `TerminateHandler` / `AppendFatalLogFile` |
| `TaskGraph.cpp` | `Init` / `Compile` / `Reset` / `Execute` / `SubmitTask` / `ExecuteNodeFor` / `Flush` |

## Related Docs

- [CoreAPI.md](CoreAPI.md) — 实现算法字典
- [公开 API](../../Public/Core/CoreAPI.md) — 签名入口
- [CoreDoc.md](../../Public/Core/CoreDoc.md) — 概念
