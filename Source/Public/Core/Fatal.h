/// @file
/// @brief 致命 / 错误上报路径与断言宏：`MAHO_CHECK` / `MAHO_VERIFY` / `MAHO_ENSURE` 及它们的格式化版本。
///
/// 三档语义（按"该不该继续"分，不是按严重程度分）：
/// * `MAHO_CHECK` / `MAHO_CHECKF` —— **契约**被破坏就 `fatal`：这是一个 bug，继续跑只会把错误藏得更深。
/// * `MAHO_VERIFY` —— 永远生效（不受 `MAHO_DO_CHECK` 影响）：用于 `Shipping` 也要检查的不变量。
/// * `MAHO_ENSURE` / `MAHO_ENSURE_BREAK` —— **只报一次**、不中断：外部数据 / 环境导致的可疑情况。
///
/// `MAHO_CHECK` / `MAHO_CHECKF` / `MAHO_ENSURE*` 在关闭检查的构建（`Shipping`）里编译成 `((void)0)`：
/// 所以它们**只能**放表达式（不能放有副作用的语句），参数在那种构建里不会被求值。
/// `MAHO_DO_CHECK` / `MAHO_DO_ENSURE` 由构建配置注入（见 AGENTS 的 Build Configuration）。
#pragma once

#include <Core/Export.h>

#include <cstdio>

namespace Maho
{

/// @brief 报告一条致命错误并**中止**：打印到 stderr，写入日志，然后 `abort()`。
/// @param Message 已格式化好的说明（调用方负责给出可定位的信息：哪个契约、哪里、什么值）。
[[noreturn]] MAHO_API void ReportFatal(const char* Message);

/// @brief 报告一条错误但**继续执行**：用于"能重试 / 能降级"的失败路径。
/// @param Message 已格式化好的说明。
MAHO_API void ReportError(const char* Message);

/// @brief 报告一条 `MAHO_ENSURE` 系列的失败并在调试器里中断（`Shipping` 下退化为普通错误上报）。
/// @param Message 已格式化好的说明。
MAHO_API void ReportEnsureBreak(const char* Message);

/// @brief 装上进程级兜底：未捕获异常 / 访问违例 / 信号都走日志而不是直接消失。
MAHO_API void InstallFatalHandlers();

}


#if MAHO_DO_CHECK

/// @brief 契约检查：`Expr` 为假即 `ReportFatal`（带表达式原文与文件）。关检查的构建里为空操作。
/// @param Expr 要检查的表达式。
#define MAHO_CHECK(Expr)                                                              \
	do {                                                                              \
		if (!(Expr)) {                                                                \
			::Maho::ReportFatal("MAHO_CHECK failed: " #Expr " at " __FILE__);        \
		}                                                                             \
	} while (0)

/// @brief 契约检查 + 自定义消息（`std::snprintf` 格式）。
/// @param Expr 要检查的表达式。
/// @param Fmt `printf` 风格格式串。
#define MAHO_CHECKF(Expr, Fmt, ...)                                                   \
	do {                                                                              \
		if (!(Expr)) {                                                                \
			char MAHO_CheckMsg[512];                                                  \
			std::snprintf(MAHO_CheckMsg, sizeof(MAHO_CheckMsg), Fmt, ##__VA_ARGS__);  \
			::Maho::ReportFatal(MAHO_CheckMsg);                                       \
		}                                                                             \
	} while (0)

#else

#define MAHO_CHECK(Expr) ((void)0)

#define MAHO_CHECKF(Expr, Fmt, ...) ((void)0)

#endif

/// @brief 契约检查，**任何构建都生效**（不受 `MAHO_DO_CHECK` 影响）。
/// @param Expr 要检查的表达式。
#define MAHO_VERIFY(Expr)                                                             \
	do {                                                                              \
		if (!(Expr)) {                                                                \
			::Maho::ReportFatal("MAHO_VERIFY failed: " #Expr " at " __FILE__);       \
		}                                                                             \
	} while (0)

#if MAHO_DO_ENSURE

/// @brief 可疑情况检查：只报一次、不中断（适合外部输入 / 环境导致的问题）。
/// @param Expr 要检查的表达式。
#define MAHO_ENSURE(Expr)                                                             \
	do {                                                                              \
		static bool MAHO_EnsureOnce = false;                                          \
		if (!(Expr) && !MAHO_EnsureOnce) {                                            \
			MAHO_EnsureOnce = true;                                                   \
			::Maho::ReportError("MAHO_ENSURE failed: " #Expr " at " __FILE__);       \
		}                                                                             \
	} while (0)

/// @brief 可疑情况检查 + 自定义消息：只报一次、不中断，且**在调试器里断住**便于当场看清现场。
#define MAHO_ENSURE_BREAK(Expr, Fmt, ...)                                             \
	do {                                                                              \
		static bool MAHO_EnsureBreakOnce = false;                                     \
		if (!(Expr) && !MAHO_EnsureBreakOnce) {                                       \
			MAHO_EnsureBreakOnce = true;                                              \
			char MAHO_EnsureBreakMsg[512];                                            \
			std::snprintf(MAHO_EnsureBreakMsg, sizeof(MAHO_EnsureBreakMsg), Fmt,      \
				##__VA_ARGS__);                                                        \
			::Maho::ReportEnsureBreak(MAHO_EnsureBreakMsg);                           \
		}                                                                             \
	} while (0)

#else

#define MAHO_ENSURE(Expr) ((void)0)

#define MAHO_ENSURE_BREAK(Expr, Fmt, ...) ((void)0)

#endif

/// @brief 取出一个指针并顺带 `MAHO_ENSURE` 它非空，然后进入一个"指针有效才走"的块。
/// @param PtrExpr 指针表达式（会被求值一次）。
/// @param Name 块内可见的指针名字。
#define MAHO_ENSURE_NOT_NULL(PtrExpr, Name)                                           \
	MAHO_ENSURE((PtrExpr) != nullptr);                                                \
	for (auto* Name = (PtrExpr); Name != nullptr; Name = nullptr)

/// @brief 指针非空才进入的块（不检查、不报错：调用方已知它可能为空，只想跳过）。
/// @param PtrExpr 指针表达式（会被求值一次）。
/// @param Name 块内可见的指针名字。
#define MAHO_IF_NOT_NULL(PtrExpr, Name)                                               \
	for (auto* Name = (PtrExpr); Name != nullptr; Name = nullptr)
