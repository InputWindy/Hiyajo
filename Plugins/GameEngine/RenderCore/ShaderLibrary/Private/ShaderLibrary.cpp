#include "ShaderLibrary.h"

#include "FShaderParser.h"

#include <Log.h>
#include <Name.h>
#include <Paths.h>
#include <RHI/RHIEnums.h>
#include <Trace.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if MAHO_WITH_GLSLANG
#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#endif

namespace Maho
{

namespace
{

/** 发布给渲染侧的访问器：Initialize 里设、Shutdown 里清 —— 和 GetPaths()/GetPrimitiveRegistry()
 *  一样，"活着"这件事由调度决定，而不是让静态析构顺序碰运气。 */
FShaderLibrary* GShaderLibrary = nullptr;

/** 内置 fallback 的源码：一个最小 unlit，颜色故意用醒目的洋红 —— 屏幕上看到它，就说明
 *  "这个材质没找到 shader"。
 *
 *  它**不是**特例路径：和 Content/Shaders 里的文件走一模一样的解析/注入/编译链。所以"目录不存在
 *  或文件全错"只会少几个 shader，不会没有 shader —— 也是下一轮渲染侧在 shader 无效时的落点。 */
constexpr const char* kBuiltinFallbackSource = R"(
Shader "BuiltinFallback"
{
	SubShader
	{
		Pass
		{
			Name "Forward"
			Cull Back
			ZWrite On
			ZTest LEqual
			Blend Off

			#pragma vertex VSMain
			#pragma fragment FSMain

			struct a2v
			{
				float3 position : POSITION;
			};

			struct v2f
			{
				float4 position : SV_POSITION;
				float4 color    : SV_Target;
			};

			void VSMain(a2v v, inout v2f i)
			{
				i.position = _Frame.ViewProj * (_Push.ObjectMatrix * vec4(v.position, 1.0));
			}

			void FSMain(v2f i)
			{
				i.color = vec4(1.0, 0.0, 1.0, 1.0);
			}
		}
	}
}
)";

/** 路径 -> UTF-8 文本，只为日志/诊断。std::filesystem::path::string() 走的是 ANSI 代码页，
 *  而这个仓库的绝对路径里有中文 —— 直接用它打出来的日志是乱码。 */
std::string PathToUtf8(const std::filesystem::path& Path)
{
	const std::u8string U8 = Path.u8string();
	return std::string(reinterpret_cast<const char*>(U8.data()), U8.size());
}

const char* CullToString(ECullMode Cull)
{
	switch (Cull)
	{
	case ECullMode::Back: return "Back";
	case ECullMode::Front: return "Front";
	case ECullMode::Off: return "Off";
	}
	return "?";
}

const char* BlendToString(EBlendMode Blend)
{
	return Blend == EBlendMode::AlphaBlend ? "SrcAlpha/OneMinusSrcAlpha" : "Off";
}

/** glslang：一个阶段 -> SPIR-V。同步（运行在调用线程上，也就是 IInit 的那条线程）。
 *
 *  为什么这里自己调 glslang 而不是复用 FShaderCompilerServer（在 Render 里）：依赖方向是
 *  Render -> ShaderLibrary（下一轮 MeshFeature 要用它），反过来拿 Render 的编译器就成环了。
 *  资源上限用 glslang 自己的 GetDefaultResources()（同 RHI 那份），不手抄一份会漂移的常量表。 */
bool CompileGlsl(const std::string& Source, ERHIShaderStage Stage, const std::string& EntryPoint,
	std::vector<std::uint32_t>& OutBytecode, std::string& OutError)
{
#if MAHO_WITH_GLSLANG
	const EShLanguage Language = (Stage == ERHIShaderStage::Vertex) ? EShLangVertex : EShLangFragment;

	glslang::TShader Shader(Language);
	const char* SourcePtr = Source.c_str();
	const int SourceLength = static_cast<int>(Source.size());
	Shader.setStringsWithLengths(&SourcePtr, &SourceLength, 1);
	// 两个 setter 必须成对：setEntryPoint 只是**改名**（"把源里那个 main 改名成这个名字"），
	// 它不会去源里找 `EntryPoint` 这个函数。只调它时 glslang 的 link 报
	// "Missing entry point: Each stage requires one entry point"（源里没有 main）——
	// 而我们的 GLSL 里入口就叫 `_pragma` 指定的名字（VSMain/PSMain），所以必须先用
	// setSourceEntryPoint 告诉它"源里的入口函数叫这个名字"。
	Shader.setSourceEntryPoint(EntryPoint.c_str());
	Shader.setEntryPoint(EntryPoint.c_str());
	Shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
	Shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);

	if (!Shader.parse(GetDefaultResources(), 460, false, EShMsgDefault))
	{
		OutError = Shader.getInfoLog();
		OutError += "\n";
		OutError += Shader.getInfoDebugLog();
		return false;
	}

	glslang::TProgram Program;
	Program.addShader(&Shader);
	if (!Program.link(EShMsgDefault))
	{
		OutError = Program.getInfoLog();
		OutError += "\n";
		OutError += Program.getInfoDebugLog();
		return false;
	}

	spv::SpvBuildLogger Logger;
	glslang::SpvOptions Options;
	// 名字必须留下：VerifyMaterialLayout() 靠 OpName("MahoMaterial") 找到材质块，再读它的
	// OpMemberDecorate Offset —— 那是"手写 std140"唯一的独立证据。
	Options.generateDebugInfo = true;
	Options.disableOptimizer = false;
	glslang::GlslangToSpv(*Program.getIntermediate(Language), OutBytecode, &Logger, &Options);
	return true;
#else
	(void)Source;
	(void)Stage;
	(void)EntryPoint;
	(void)OutBytecode;
	OutError = "glslang is not available in this configuration (MAHO_WITH_GLSLANG = 0)";
	return false;
#endif
}

} // namespace

FShaderLibrary* GetShaderLibrary()
{
	return GShaderLibrary;
}

const FMaterialProperty* FMaterialLayout::Find(std::string_view PropName) const
{
	for (const FMaterialProperty& Prop : Properties)
	{
		if (Prop.Name == PropName)
		{
			return &Prop;
		}
	}
	return nullptr;
}

FShaderLibrary::FShaderLibrary()
{
	// 三条必须在前面跑完的边，方向都由**消费者**声明（生产者不枚举消费者）：
	//   Paths@IInit   —— 解析 "/Engine/Shaders" 才知道去哪扫；
	//   FLog@IInit    —— 下面每一步都要打日志（日志层没起来时 GetLog() 还是空）；
	//   FNamePool@IInit —— 表按 FName 索引，构造 FName 要 name pool 活着。
	MyStage<IInit>().IsWaiting<Paths::FPaths>().ForStage<IInit>();
	MyStage<IInit>().IsWaiting<FLog>().ForStage<IInit>();
	MyStage<IInit>().IsWaiting<Name::FNamePool>().ForStage<IInit>();
}

FShaderLibrary::~FShaderLibrary() = default;

const FShaderDesc* FShaderLibrary::Get(Name::FName ShaderName) const
{
	const auto It = Shaders.find(ShaderName);
	if (It == Shaders.end() || !It->second.bValid)
	{
		// 无效的名字不返回"半个描述"：调用方要么拿到一个能建 PSO 的东西，要么明确没有（回退）。
		return nullptr;
	}
	return &It->second;
}

const FShaderDesc* FShaderLibrary::GetFallback() const
{
	return bHasFallback ? &Fallback : nullptr;
}

FShaderDiagnostics FShaderLibrary::GetDiagnostics() const
{
	return Diag;
}

void FShaderLibrary::Initialize(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(IInit, "ShaderLibrary init", "scan Content/Shaders, parse/inject/compile each .shader");
	GShaderLibrary = this;

#if MAHO_WITH_GLSLANG
	// glslang 的进程级初始化是引用计数的（ShInitialize/ShFinalize），和 FRender 的编译服务器
	// 各自配对即可，谁先谁后都安全。
	glslang::InitializeProcess();
#endif

	// 内置 fallback 先编：它也是"目录空 / 某个 shader 坏了"时渲染侧唯一的落点（任务 3.4）。
	LoadBuiltinFallback();
	ScanShaderDirectory();

	// 口径写在一条行里，不用对表：文件数 + 内置 fallback 数 = 可用的 shader 数。旧版
	// `N file(s) scanned, M loaded` 里的 M 含内置 fallback，两数放在一起像对不上（scanned 3 loaded 4）。
	const std::uint32_t BuiltinCount = bHasFallback ? 1u : 0u;
	MAHO_LOG_CORE_INFO("ShaderLibrary: {} file(s) + {} builtin = {} shader(s) ready ({} failed, {} reused from cache)",
		Diag.Scanned, BuiltinCount, Diag.Loaded, Diag.Failed, Diag.Reused);
}

void FShaderLibrary::LoadBuiltinFallback()
{
	FShaderDesc Desc;
	if (!BuildShaderDesc("<builtin>", kBuiltinFallbackSource, "<builtin>", Desc))
	{
		// 连 fallback 都编不过（例如 glslang 不可用）：明确报出来，渲染侧会在 GetFallback()
		// 拿到 nullptr 后拒绝建 pass —— 比"画出一个看不懂的东西"好排查。
		MAHO_LOG_CORE_ERROR("ShaderLibrary: the builtin fallback did not compile -- no usable shader at all");
		return;
	}
	Fallback = std::move(Desc);
	bHasFallback = true;
	++Diag.Loaded;
	Diag.Entries.push_back(FShaderDiagnostics::FEntry{
		"<builtin>", Fallback.SourcePath, Fallback.SourceHash, Fallback.bValid });
}

void FShaderLibrary::ScanShaderDirectory()
{
	Paths::FPaths* Paths = Paths::GetPaths();
	if (Paths == nullptr)
	{
		MAHO_LOG_CORE_ERROR("ShaderLibrary: GetPaths() is NULL -- cannot resolve the shader directory; "
			"only the builtin fallback is available");
		return;
	}
	const std::filesystem::path Directory = Paths->Resolve("/Engine/Shaders");

	std::error_code Ec;
	if (!std::filesystem::is_directory(Directory, Ec) || Ec)
	{
		// 空目录不是错误（内置 fallback 就是为这一刻准备的），但必须说出来 —— 否则"三个 shader
		// 一个都没加载"会被当成正常。
		MAHO_LOG_CORE_WARN("ShaderLibrary: shader directory '{}' does not exist -- only the builtin fallback "
			"is available", PathToUtf8(Directory));
		return;
	}

	std::vector<std::filesystem::path> Files;
	for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(Directory, Ec))
	{
		if (Ec)
		{
			break;
		}
		if (Entry.is_regular_file(Ec) && !Ec && Entry.path().extension() == ".shader")
		{
			Files.push_back(Entry.path());
		}
	}
	// 排序：日志顺序稳定，两台机器的日志才能直接对着看。
	std::sort(Files.begin(), Files.end());

	for (const std::filesystem::path& File : Files)
	{
		++Diag.Scanned;
		LoadOneFile(File.stem().string(), File);
	}
}

void FShaderLibrary::LoadOneFile(const std::string& Name, const std::filesystem::path& Path)
{
	const std::string PathText = PathToUtf8(Path);

	// 二进制读：文本模式会替我们改换行（CRLF -> LF），而哈希必须是"文件字节的哈希"。
	std::ifstream In(Path, std::ios::binary);
	if (!In)
	{
		++Diag.Failed;
		Diag.Entries.push_back(FShaderDiagnostics::FEntry{ Name, PathText, 0, false });
		MAHO_LOG_CORE_ERROR("ShaderLibrary: cannot open '{}'", PathText);
		return;
	}
	std::ostringstream Buffer;
	Buffer << In.rdbuf();
	const std::string Source = Buffer.str();

	// 缓存键 = 源码字节哈希 + 引擎注入块版本（D2）。内容没变就复用 —— 修掉旧引擎
	// "只哈希路径 ⇒ 改了 shader 不失效"的坑：这里路径根本不参与键。
	const std::uint64_t SourceHash = Shader::HashShaderSource(Source);
	const FCacheKey Key{ SourceHash, Shader::kInjectedBlockVersion };

	if (const auto It = Cache.find(Key); It != Cache.end())
	{
		FShaderDesc Cached = It->second;
		Cached.Name = Name::FName(Name);   // 名字不参与缓存键：两个同内容的文件共用一次编译
		Cached.SourcePath = PathText;
		++Diag.Reused;
		Diag.Entries.push_back(FShaderDiagnostics::FEntry{ Name, PathText, SourceHash, true });
		MAHO_LOG_CORE_INFO("ShaderLibrary: '{}' reuses the cached compile (source hash {:#x}, block version {}, file {})",
			Name, SourceHash, Shader::kInjectedBlockVersion, PathText);
		Shaders[Cached.Name] = std::move(Cached);
		return;
	}

	FShaderDesc Desc;
	if (!BuildShaderDesc(Name, Source, PathText, Desc))
	{
		// 失败不静默：报错（带文件:行或 GLSL 行号）+ 该 shader 记为无效，Get() 返回 nullptr。
		++Diag.Failed;
		Diag.Entries.push_back(FShaderDiagnostics::FEntry{ Name, PathText, SourceHash, false });
		return;
	}

	Cache.emplace(Key, Desc);   // 只缓存成功的：坏文件每次启动都应该各报一条自己的错
	++Diag.Loaded;
	Diag.Entries.push_back(FShaderDiagnostics::FEntry{ Name, PathText, SourceHash, true });
	Shaders[Desc.Name] = std::move(Desc);
}

bool FShaderLibrary::BuildShaderDesc(std::string_view Name, const std::string& Source,
	const std::string& SourcePath, FShaderDesc& OutDesc)
{
	const auto StartTime = std::chrono::steady_clock::now();

	Shader::FParseOutput Parsed;
	std::string Error;
	if (!Shader::Parse(Source, SourcePath, Parsed, Error))
	{
		MAHO_LOG_CORE_ERROR("ShaderLibrary: parse FAILED for '{}' -- {}", Name, Error);
		return false;
	}

	FShaderDesc Desc;
	Desc.Name = Name::FName(Name);
	Desc.SourcePath = SourcePath;
	Desc.SourceHash = Shader::HashShaderSource(Source);
	Desc.InjectedBlockVersion = Shader::kInjectedBlockVersion;
	Desc.VertexEntry = Parsed.VertexEntry;
	Desc.FragmentEntry = Parsed.FragmentEntry;
	Desc.Material = Parsed.Material;
	Desc.Pass = Parsed.Pass;

	MAHO_LOG_CORE_INFO("ShaderLibrary: parsed '{}' ok (file {}, source hash {:#x}, {} propertie(s), {} sampler(s), "
		"cull {}, zwrite {}, ztest {}, blend {})",
		Name, SourcePath, Desc.SourceHash, Desc.Material.Properties.size(), Desc.Material.SamplerCount,
		CullToString(Desc.Pass.Cull), Desc.Pass.bZWrite ? "On" : "Off", Desc.Pass.bZTest ? "On" : "Off",
		BlendToString(Desc.Pass.Blend));

	if (!CompileGlsl(Parsed.VertexGlsl, ERHIShaderStage::Vertex, Parsed.VertexEntry,
			Desc.VertexBytecode, Error))
	{
		MAHO_LOG_CORE_ERROR("ShaderLibrary: vertex compile FAILED for '{}' (file {}, source hash {:#x}) --\n{}",
			Name, SourcePath, Desc.SourceHash, Error);
		return false;
	}
	if (!CompileGlsl(Parsed.FragmentGlsl, ERHIShaderStage::Fragment, Parsed.FragmentEntry,
			Desc.FragmentBytecode, Error))
	{
		MAHO_LOG_CORE_ERROR("ShaderLibrary: fragment compile FAILED for '{}' (file {}, source hash {:#x}) --\n{}",
			Name, SourcePath, Desc.SourceHash, Error);
		return false;
	}

	// D1 的断言：CPU 布局与 GPU 布局必须一致。不是重算一遍同一个公式 —— 是读编译出来的 SPIR-V
	// 里 MahoMaterial 的真偏移。片元阶段通常是用到材质块的那一边，先查它，再查顶点。
	bool bChecked = false;
	bool bCheckedAny = false;
	if (!Shader::VerifyMaterialLayout(Desc.FragmentBytecode, Desc.Material, Error, bChecked))
	{
		MAHO_LOG_CORE_ERROR("ShaderLibrary: material layout mismatch for '{}' (file {}, source hash {:#x}) -- {}",
			Name, SourcePath, Desc.SourceHash, Error);
		return false;
	}
	bCheckedAny = bCheckedAny || bChecked;
	if (!Shader::VerifyMaterialLayout(Desc.VertexBytecode, Desc.Material, Error, bChecked))
	{
		MAHO_LOG_CORE_ERROR("ShaderLibrary: material layout mismatch for '{}' (file {}, source hash {:#x}) -- {}",
			Name, SourcePath, Desc.SourceHash, Error);
		return false;
	}
	bCheckedAny = bCheckedAny || bChecked;

	const double ElapsedMs = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - StartTime).count();
	MAHO_LOG_CORE_INFO("ShaderLibrary: compiled '{}' ok (vs {} words, fs {} words, vs entry '{}', fs entry '{}', "
		"material block {} byte(s), layout {}, {:.1f} ms)",
		Name, Desc.VertexBytecode.size(), Desc.FragmentBytecode.size(), Desc.VertexEntry, Desc.FragmentEntry,
		Desc.Material.UniformSize, bCheckedAny ? "verified in SPIR-V" : "not used (nothing to verify)", ElapsedMs);

	Desc.bValid = true;
	OutDesc = std::move(Desc);
	return true;
}

void FShaderLibrary::Shutdown(FEngineBase&, FEngineContext&)
{
	MAHO_TRACE_STAGE(IShutdown, "ShaderLibrary shutdown", "drop the compiled table before anything unloads");

	// 先把访问器收回来，再清数据：反过来的话，一个还在 IInit 里的读者会拿到半空的表。
	// 清理本身也是"这些指针活不过卸载"的证据。
	GShaderLibrary = nullptr;
	Shaders.clear();
	Cache.clear();
	Fallback = FShaderDesc{};
	bHasFallback = false;
	Diag = FShaderDiagnostics{};

#if MAHO_WITH_GLSLANG
	// 与 Initialize 里的 InitializeProcess() 配对（引用计数）。这里不需要"排空"什么：本层的编译
	// 是同步跑在 IInit 线程上的，Shutdown 时没有任何在途工作。
	glslang::FinalizeProcess();
#endif
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_SHADERLIBRARY_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::FShaderLibrary::CreateFrame();
}
