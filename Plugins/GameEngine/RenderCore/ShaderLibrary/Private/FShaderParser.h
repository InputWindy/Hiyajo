#pragma once

// FShaderParser —— .shader 文本 -> (两个阶段的 GLSL, 材质布局, render state)。
// 这是 design D1/D3/D4 的实现，**不是**通用 HLSL 前端：
//
//   * 只认 `#pragma vertex/fragment` 指定的两个入口函数所引用的那两个 struct；
//   * 只把 struct 成员按语义表重写成 `layout(location = N) in|out` 的全局量；
//   * 函数体里只做"实例名.成员名"的定点替换（成员名表来自解析出的 struct 声明）。
//
// 任何不认识的东西（未知语义、未知状态键、缺 pragma、未知类型）都**报错并给出文件:行**。
// 不猜的理由很实际：猜错的典型症状是"颜色对、位置全错"，查它比读一条报错贵得多。

#include "ShaderLibrary.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Maho::Shader
{

/** 引擎注入块的版本号：注入内容一改就 +1，缓存键跟着变，旧缓存自然失效（D2）。
 *
 *  为什么是个常量而不是"自动算"：缓存键必须能表达"引擎那边变了"，而注入文本的哈希要跨越
 *  "解析器" 与 "缓存" 两个模块；一个显式版本号把这件事变成一次必须动手的修改，比一个没人
 *  会想起来更新的一致性检查可靠。 */
inline constexpr std::uint32_t kInjectedBlockVersion = 1;

/** 解析产物：与那份 CPU 布局同源的 GLSL，以及 Pass 的状态。 */
struct FParseOutput
{
	std::string VertexGlsl;      // 已注入引擎块、已重写语义的完整 GLSL（`#version` 已带）
	std::string FragmentGlsl;
	std::string VertexEntry;
	std::string FragmentEntry;
	FMaterialLayout Material;
	FPassState Pass;
};

/** 解析 + 重写 + 注入。失败返回 false，OutError = "<文件>:<行>: <原因>"。 */
[[nodiscard]] bool Parse(const std::string& Source, std::string_view FilePath,
	FParseOutput& Out, std::string& OutError);

/** 源码字节的哈希（FNV-1a 64）—— 缓存键的一半（D2）。改一个字符就换键。 */
[[nodiscard]] std::uint64_t HashShaderSource(std::string_view Source);

/** 用编译出来的 SPIR-V 反查材质常量块的真实 std140 偏移，与 CPU 布局逐条比对（D1 的断言）。
 *  这是"手工打包"唯一的独立证据：不是再算一遍同一个公式，而是读 glslang 真写下的偏移。
 *
 *  块在 SPIR-V 里找不到（shader 没用到它，已被优化掉）时返回 true、bOutChecked = false ——
 *  没用到的块没有错位风险，但调用方要能区分"校验过了"和"没得校验"。 */
[[nodiscard]] bool VerifyMaterialLayout(const std::vector<std::uint32_t>& SpirvWords,
	const FMaterialLayout& Layout, std::string& OutError, bool& bOutChecked);

} // namespace Maho::Shader
