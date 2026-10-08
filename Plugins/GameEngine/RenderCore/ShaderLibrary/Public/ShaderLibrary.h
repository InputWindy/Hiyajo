#pragma once

// ShaderLibrary —— Unity 风格 .shader 文本库（design: openspec/changes/add-unity-style-shader-material）。
//
// 这里只有一个问题要回答："给我一个 shader 名，我还原出渲染侧需要的一切"。答案是一个
// FShaderDesc（两个阶段的 SPIR-V、材质布局、render state），名 = 文件名（`Content/Shaders/<名>.shader`）。
//
// 为什么布局与 GLSL 一起由解析器产出（D1）：材质常量块如果"CPU 侧算一遍、GLSL 里再写一遍"，
// 就会有一份可以悄悄错位的重复数据 —— 症状是"颜色对、位置全错"这种要查一整天的 bug。所以
// 两者由**同一份 Properties 声明序**生成，并用编译出来的 SPIR-V 反查偏移做断言。

#include "ShaderLibraryApi.h"
#include <Engine/Frame.h>
#include <Maho.h>
#include <Name.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Maho
{

/** 材质属性类型 —— Properties 里支持的 6 种（D1）。前四种进 set2 binding0 的常量块，
 *  后两种各占一个采样器 binding（D3）。 */
enum class EMaterialPropertyType : std::uint8_t
{
	Float,         // `_A("A", Float) = 0.5`            -> float
	Range,         // `_A("A", Range(0.0, 1.0)) = 0.5`  -> float（范围只是 UI 提示，不改布局）
	Int,           // `_A("A", Int) = 3`                -> int
	Color,         // `_A("A", Color) = (1,1,1,1)`      -> vec4
	Texture2D,     // `_A("A", 2D) = "white" {}`        -> sampler2D
	TextureCube,   // `_A("A", Cube) = "white" {}`      -> samplerCube
};

/** `Properties` 的一条声明 + 它在 std140 布局里的位置。 */
struct FMaterialProperty
{
	std::string Name;                   // `_BaseColor`（解析器强制 `_` 开头，见 2.1）
	EMaterialPropertyType Type = EMaterialPropertyType::Float;
	std::uint32_t Offset = 0;           // 常量块内的字节偏移；纹理属性恒 0（它不在块里）
	std::uint32_t Size = 0;             // 常量块内的占用字节；纹理属性恒 0
	std::uint32_t SamplerBinding = 0;   // set2 的 binding（1..N，按声明序）；非纹理属性恒 0
	float Default[4] = {};              // 标量/向量默认值（Int 按 int 的位模式放 Default[0]）
	std::string DefaultTexture;         // 纹理属性的默认纹理名（`"white" {}` 里的那个名字）
};

/** set2 binding0 那个常量块的 CPU 侧布局。 */
struct FMaterialLayout
{
	/** 声明序。GLSL 块的成员按**同一份序**生成，所以两边不可能错位（D1）。 */
	std::vector<FMaterialProperty> Properties;

	/** 常量块总大小（std140：最后成员的末尾再对齐到 16）。渲染侧按它开缓冲。 */
	std::uint32_t UniformSize = 0;

	/** binding1..N 的采样器数量。 */
	std::uint32_t SamplerCount = 0;

	[[nodiscard]] const FMaterialProperty* Find(std::string_view PropName) const;
};

enum class ECullMode : std::uint8_t
{
	Back,
	Front,
	Off,
};

/** 只有这两种混合：其余组合在本切片里是"没实现"，遇到就报错，不静默当不透明（D7）。 */
enum class EBlendMode : std::uint8_t
{
	Opaque,       // `Blend Off`
	AlphaBlend,   // `Blend SrcAlpha OneMinusSrcAlpha`
};

/** Pass 的 render state（D7 的四个键，全部映射到 FRHIGraphicsPipelineDesc）。 */
struct FPassState
{
	ECullMode Cull = ECullMode::Back;
	bool bZWrite = true;
	bool bZTest = true;        // `ZTest LEqual` = true，`ZTest Off` = false
	EBlendMode Blend = EBlendMode::Opaque;
};

/** 一个 .shader 的编译产物 —— 渲染侧（下一轮的 MeshFeature）需要的全部。 */
struct FShaderDesc
{
	Name::FName Name;                   // 文件名（`DefaultLit`），也是 Get() 的键
	std::string SourcePath;             // 物理路径（诊断用）；内置 fallback 是 "<builtin>"
	std::uint64_t SourceHash = 0;       // 源码字节的哈希（缓存键的一半）
	std::uint32_t InjectedBlockVersion = 0;  // 引擎注入块的版本（缓存键的另一半）

	std::vector<std::uint32_t> VertexBytecode;     // SPIR-V words
	std::vector<std::uint32_t> FragmentBytecode;
	std::string VertexEntry = "main";             // #pragma vertex 指定的入口名
	std::string FragmentEntry = "main";

	FMaterialLayout Material;           // set2 布局：CPU 侧怎么填参数，看它
	FPassState Pass;                    // Cull/ZWrite/ZTest/Blend

	/** false = 解析或编译失败：渲染侧**必须**改用 GetFallback()，不要拿半个描述去建 PSO。 */
	bool bValid = false;
};

/** 自检读数：扫描了几个文件、成功了几个、失败了几个、各自的内容哈希（任务 1.2）。 */
struct FShaderDiagnostics
{
	struct FEntry
	{
		std::string Name;
		std::string Path;
		std::uint64_t SourceHash = 0;
		bool bValid = false;
	};

	std::uint32_t Scanned = 0;   // Content/Shaders 里发现的 .shader 文件数
	std::uint32_t Loaded = 0;    // 解析 + 编译全部成功（含内置 fallback）
	std::uint32_t Failed = 0;
	std::uint32_t Reused = 0;    // 命中内容哈希缓存、没有重编的

	std::vector<FEntry> Entries;
};

class FShaderLibrary;

/** 全局访问器 —— 与 GetPrimitiveRegistry()/GetPaths() 同形：跨 DLL 的导出**函数**，不是导出变量。
 *  Initialize 之前是 nullptr（别在它之前取）。 */
MAHO_SHADERLIBRARY_API FShaderLibrary* GetShaderLibrary();

/**
 * .shader 文本库 —— 一个引擎侧 frame（只有 IInit/IShutdown）。
 *
 * 安装位置很重要：它挂的是**引擎阶段**，所以必须装进驱动这些阶段的收集者，即宿主的子节点
 * （`ExampleEngine.cproject` 的 `Plugins` 数组），而不是 `Render.cplugin` 的 `Plugins` ——
 * 后者是 FRender 的渲染阶段收集者，装进去会拿不到节点、Initialize 永不执行（`PrimitiveRegistry`
 * 踩过这个坑）。`Render.cplugin` 只需要它的 PrivateIncludes，好让渲染侧能 include 这个头。
 *
 * 线程：Initialize 阶段一次性写完（扫描 + 编译），之后只读；渲染线程读 Get()/GetDiagnostics()
 * 安全。这就是"改 shader 要重启"的实现（本切片不做热重载，D2 的明确选择）。
 */
class MAHO_SHADERLIBRARY_API FShaderLibrary
	: public FFrameExtension
	, public IPipeline<IInit, IShutdown>
{
	MAHO_DECLARE_FRAME(FShaderLibrary);

public:
	FShaderLibrary();
	~FShaderLibrary() override;

	FShaderLibrary(const FShaderLibrary&) = delete;
	FShaderLibrary& operator=(const FShaderLibrary&) = delete;

	/** 按名取（名 = 文件名去掉 `.shader`）。名字不存在或那个 shader 无效 → nullptr，
	 *  调用方回退 `GetFallback()`；**不返回**"编译失败的半个描述"。 */
	[[nodiscard]] const FShaderDesc* Get(Name::FName ShaderName) const;

	/** 内置 fallback：编在 C++ 里的最小 unlit，不依赖 `Content/Shaders` 里任何文件。
	 *  目录为空、或某个 shader 解析/编译失败时，渲染侧用它 —— 缺 shader 也不该黑屏到底。
	 *  连它都编不过（例如 glslang 不可用）时返回 nullptr。 */
	[[nodiscard]] const FShaderDesc* GetFallback() const;

	[[nodiscard]] FShaderDiagnostics GetDiagnostics() const;

private:
	// -- engine stages --
	void Initialize(FEngineBase& Engine, FEngineContext& Frame) override;   // 扫描 + 解析 + 编译 + 缓存
	void Shutdown(FEngineBase& Engine, FEngineContext& Frame) override;     // 清空（把活着的指针还给每个人）

	/** 一个文件走完整条链：内容哈希 -> 缓存/解析 -> 编译 -> 校验布局 -> 出一条日志。 */
	void LoadOneFile(const std::string& Name, const std::filesystem::path& Path);

	/** 内置 fallback 走完全同一条链（它就是"没有文件时的那个 shader"）。 */
	void LoadBuiltinFallback();

	/** 扫描 `/Engine/Shaders/*.shader` 并逐个 LoadOneFile。 */
	void ScanShaderDirectory();

	/** 源码 -> 可用的 FShaderDesc（解析 + 注入 + 编译 + SPIR-V 布局校验）。
	 *  失败时自己打日志并返回 false —— 失败不静默是这一层的契约。 */
	[[nodiscard]] bool BuildShaderDesc(std::string_view Name, const std::string& Source,
		const std::string& SourcePath, FShaderDesc& OutDesc);

	/** 内容哈希 → 编译产物。同字节复用：两个内容相同的 .shader 只编一次（D2）。 */
	struct FCacheKey
	{
		std::uint64_t SourceHash = 0;
		std::uint32_t BlockVersion = 0;

		[[nodiscard]] bool operator==(const FCacheKey& O) const = default;
	};

	struct FCacheKeyHash
	{
		[[nodiscard]] std::size_t operator()(const FCacheKey& K) const noexcept
		{
			// 两个独立的 64/32 位量：直接混一混，够用且不引入第三个哈希库。
			return static_cast<std::size_t>(K.SourceHash ^ (static_cast<std::uint64_t>(K.BlockVersion) << 32));
		}
	};

	std::unordered_map<Name::FName, FShaderDesc> Shaders;
	std::unordered_map<FCacheKey, FShaderDesc, FCacheKeyHash> Cache;
	FShaderDesc Fallback;
	bool bHasFallback = false;
	FShaderDiagnostics Diag;
};

} // namespace Maho
