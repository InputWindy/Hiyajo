/// @file
/// @brief 模块导出标签：`MAHO_EXPORT` / `MAHO_IMPORT` / `MAHO_API` 的三种形态。
///
/// 各插件自己的标签（`MAHO_<NAME>_API`，由 codegen 生成）都以这里为基础：编译该模块的 DLL 时它是
/// `MAHO_EXPORT`，消费方是 `MAHO_IMPORT`。**只有函数跨 DLL 导出，数据符号不导**；`MAHO_API` 在
/// 插件侧展开为空（插件自己的 `MAHO_<NAME>_API` 才是有效标签）。非 `MAHO_BUILD_SHARED`（静态链接）
/// 时三者都为空，所以任何模块都可以无条件写它。
#pragma once

#if defined(MAHO_BUILD_SHARED)
#	if defined(_MSC_VER) || defined(_WIN32) || defined(_WIN64)
		/// @brief Windows 的导出标签（`__declspec(dllexport)`）。
#		define MAHO_EXPORT __declspec(dllexport)
		/// @brief Windows 的导入标签（`__declspec(dllimport)`）：把"消费方自己生成一份虚表/删除析构符"
		///        变成编译错误，而不是留到运行时在 `delete` 里静默崩。
#		define MAHO_IMPORT __declspec(dllimport)
#		if defined(MAHO_EXPORTS)
			/// @brief 本模块的 API 标签：编译本模块时 = 导出。
#			define MAHO_API MAHO_EXPORT
#		else
#			define MAHO_API MAHO_IMPORT
#		endif
#	else
#		define MAHO_EXPORT __attribute__((visibility("default")))
#		define MAHO_IMPORT __attribute__((visibility("default")))
#		define MAHO_API MAHO_EXPORT
#	endif
#else
#	define MAHO_EXPORT
#	define MAHO_IMPORT
#	define MAHO_API
#endif

#if defined(_MSC_VER)
#	pragma warning(disable : 4251)
#endif

