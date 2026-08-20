# Public

## 代码文件

- [Timer.h](Timer.h)
- [TimerApi.h](TimerApi.h)

## 接口字典

| 声明 | 说明 |
|------|------|
| `FTimer : TExtensionList<FTimer>` | 计时器单例（栈式作用域剖析器，纯单例，无 Main/IAssembly） |
| `FTimer::ExecuteStage(ETimerStage)` | 阶段分发（Init / Shutdown → Reset） |
| `FTimer::BeginScope(string_view)` | 压入一个作用域（需与 EndScope / FScopedTimer 平衡） |
| `FTimer::EndScope()` | 弹出当前作用域并累加耗时 |
| `FTimer::Reset()` | 清空所有累计计时 |
| `FTimer::DumpToString()` | 格式化计时树为文本（毫秒） |
| `FScopedTimer` | RAII 作用域计时器（析构自动 EndScope） |
| `FGameClock : TSingleton<FGameClock>` | 游戏时钟单例（真实/游戏时间 + 缩放 + 暂停） |
| `ETimerStage` | 本插件自定义 drive stage |

## 相关文档

- [../Timer.md](../Timer.md) — 概念 + 用法
- [../Private/PrivateDoc.md](../Private/PrivateDoc.md) — 实现算法字典

