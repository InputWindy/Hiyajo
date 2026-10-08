#include "FShaderParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

// 解析 = 一条直线：预处理（去注释）-> Properties -> SubShader/Pass 指令 -> struct -> 入口签名
// -> 语义重写成 `layout(location = N)` 全局量 -> 注入引擎块 -> 拼出两个阶段的 GLSL。
// 全程不猜：认识就改写，不认识就报 "文件:行: 原因"。

namespace Maho::Shader
{
namespace
{

// ============================== 词法小工具 ==============================

bool IsIdentStart(char C)
{
	return std::isalpha(static_cast<unsigned char>(C)) != 0 || C == '_';
}

bool IsIdentChar(char C)
{
	return std::isalnum(static_cast<unsigned char>(C)) != 0 || C == '_';
}

bool IsSpace(char C)
{
	return C == ' ' || C == '\t' || C == '\r' || C == '\n';
}

std::string_view TrimView(std::string_view S)
{
	std::size_t Begin = 0;
	std::size_t End = S.size();
	while (Begin < End && IsSpace(S[Begin])) { ++Begin; }
	while (End > Begin && IsSpace(S[End - 1])) { --End; }
	return S.substr(Begin, End - Begin);
}

std::string Trim(std::string_view S)
{
	return std::string(TrimView(S));
}

std::string ToUpper(std::string_view S)
{
	std::string Out(S);
	for (char& C : Out)
	{
		C = static_cast<char>(std::toupper(static_cast<unsigned char>(C)));
	}
	return Out;
}

/** 注释 -> 空格，**长度与换行位置不变**：后面所有偏移量都还能换算成原文件的行号。
 *  报错必须带行号，所以这一步不能"删掉"注释。 */
std::string StripComments(const std::string& Source)
{
	std::string Out = Source;
	std::size_t I = 0;
	while (I < Out.size())
	{
		if (Out[I] == '/' && I + 1 < Out.size() && Out[I + 1] == '/')
		{
			while (I < Out.size() && Out[I] != '\n')
			{
				Out[I] = ' ';
				++I;
			}
		}
		else if (Out[I] == '/' && I + 1 < Out.size() && Out[I + 1] == '*')
		{
			Out[I] = ' ';
			Out[I + 1] = ' ';
			I += 2;
			while (I + 1 < Out.size() && !(Out[I] == '*' && Out[I + 1] == '/'))
			{
				if (Out[I] != '\n')
				{
					Out[I] = ' ';
				}
				++I;
			}
			if (I + 1 < Out.size())
			{
				Out[I] = ' ';
				Out[I + 1] = ' ';
				I += 2;
			}
			I = Out.size();   // 未闭合块注释：剩下的都不算代码
		}
		else
		{
			++I;
		}
	}
	return Out;
}

/** 1 基行号。 */
std::uint32_t LineOf(const std::string& Text, std::size_t Offset)
{
	std::uint32_t Line = 1;
	const std::size_t End = std::min(Offset, Text.size());
	for (std::size_t I = 0; I < End; ++I)
	{
		if (Text[I] == '\n')
		{
			++Line;
		}
	}
	return Line;
}

/** 关键字查找：必须前后都不是标识符字符（避免 `Pass` 命中 `PassThrough`）。 */
std::size_t FindKeyword(const std::string& Text, std::string_view Keyword, std::size_t From = 0)
{
	if (Keyword.empty() || Text.size() < Keyword.size())
	{
		return std::string::npos;
	}
	for (std::size_t I = From; I + Keyword.size() <= Text.size(); ++I)
	{
		if (Text.compare(I, Keyword.size(), Keyword) != 0)
		{
			continue;
		}
		if (I > 0 && IsIdentChar(Text[I - 1]))
		{
			continue;
		}
		const std::size_t After = I + Keyword.size();
		if (After < Text.size() && IsIdentChar(Text[After]))
		{
			continue;
		}
		return I;
	}
	return std::string::npos;
}

std::uint32_t CountKeyword(std::string_view Text, std::string_view Keyword)
{
	const std::string Owned(Text);
	std::uint32_t Count = 0;
	for (std::size_t At = FindKeyword(Owned, Keyword); At != std::string::npos;
		At = FindKeyword(Owned, Keyword, At + Keyword.size()))
	{
		++Count;
	}
	return Count;
}

/** 从 `Open`（必须是 '{'）找到配对的 '}'；找不到返回 npos。 */
std::size_t MatchBrace(const std::string& Text, std::size_t Open)
{
	if (Open >= Text.size() || Text[Open] != '{')
	{
		return std::string::npos;
	}
	int Depth = 0;
	for (std::size_t I = Open; I < Text.size(); ++I)
	{
		if (Text[I] == '{')
		{
			++Depth;
		}
		else if (Text[I] == '}')
		{
			if (--Depth == 0)
			{
				return I;
			}
		}
	}
	return std::string::npos;
}

/** `Keyword { ... }` 的块体区间（不含花括号）。 */
bool TryGetBlockBody(const std::string& Text, std::string_view Keyword, std::size_t From,
	std::size_t& OutBegin, std::size_t& OutEnd)
{
	const std::size_t KeywordAt = FindKeyword(Text, Keyword, From);
	if (KeywordAt == std::string::npos)
	{
		return false;
	}
	const std::size_t Open = Text.find('{', KeywordAt);
	if (Open == std::string::npos)
	{
		return false;
	}
	const std::size_t Close = MatchBrace(Text, Open);
	if (Close == std::string::npos)
	{
		return false;
	}
	OutBegin = Open + 1;
	OutEnd = Close;
	return true;
}

/** 代码区偏移 -> 原文件行号。代码区是"Pass 里非指令的行"拼出来的一段文本，报错要翻回原文件的行。 */
std::uint32_t CodeLineOf(const std::string& Code, const std::vector<std::uint32_t>& LineNumbers, std::size_t Offset)
{
	if (LineNumbers.empty())
	{
		return 1;
	}
	std::size_t Index = 0;
	const std::size_t End = std::min(Offset, Code.size());
	for (std::size_t I = 0; I < End; ++I)
	{
		if (Code[I] == '\n')
		{
			++Index;
		}
	}
	return Index < LineNumbers.size() ? LineNumbers[Index] : LineNumbers.back();
}

/** 报错只留第一条：后面所有检查都基于"已经错了"的状态，第二条只会更迷惑。 */
struct FDiag
{
	std::string Path;
	std::string Text;

	void Fail(std::uint32_t Line, std::string Message)
	{
		if (Text.empty())
		{
			Text = Path + ":" + std::to_string(Line) + ": " + std::move(Message);
		}
	}

	[[nodiscard]] bool Ok() const { return Text.empty(); }
};

// ============================== 语义表与类型表 ==============================

/** 语义 -> 顶点流/阶段间 location（任务 2.3 的那张表）。未知语义一律报错：不猜 location。 */
struct FSemanticEntry
{
	std::string_view Name;
	std::uint32_t Location;
};

constexpr FSemanticEntry kSemantics[] = {
	{ "POSITION", 0 },
	{ "NORMAL", 1 },
	{ "TEXCOORD0", 2 },
	{ "TEXCOORD1", 3 },
	{ "TANGENT", 4 },
	{ "COLOR0", 5 },
};

const FSemanticEntry* FindSemantic(std::string_view Upper)
{
	for (const FSemanticEntry& Entry : kSemantics)
	{
		if (Entry.Name == Upper)
		{
			return &Entry;
		}
	}
	return nullptr;
}

/** HLSL 风格类型名 -> GLSL。只列这些：多一个是"悄悄支持了没测过的东西"。 */
const char* GlslTypeFor(std::string_view HlslType)
{
	if (HlslType == "float") { return "float"; }
	if (HlslType == "float2") { return "vec2"; }
	if (HlslType == "float3") { return "vec3"; }
	if (HlslType == "float4") { return "vec4"; }
	if (HlslType == "int") { return "int"; }
	if (HlslType == "uint") { return "uint"; }
	if (HlslType == "float2x2") { return "mat2"; }
	if (HlslType == "float3x3") { return "mat3"; }
	if (HlslType == "float4x4") { return "mat4"; }
	return nullptr;
}

constexpr const char* kSupportedTypesHint =
	"支持的类型：float/float2/float3/float4/int/uint/float2x2/float3x3/float4x4";

// ============================== 中间结构 ==============================

struct FMember
{
	std::string Type;       // 源码里的 HLSL 类型名
	std::string GlslType;   // 映射后的 GLSL 类型名
	std::string Name;       // 成员名
	std::string Semantic;   // 大写
	std::uint32_t Line = 0;
};

struct FStructDecl
{
	std::string Name;
	std::vector<FMember> Members;
	std::uint32_t Line = 0;
	std::size_t SpanBegin = 0;   // 生成 GLSL 时要被清空的区间（保留换行，行号不变）
	std::size_t SpanEnd = 0;
};

struct FParam
{
	std::string Struct;     // 参数类型（struct 名）
	std::string Instance;   // 参数实例名
	FParam() = default;
	FParam(std::string InStruct, std::string InInstance)
		: Struct(std::move(InStruct)), Instance(std::move(InInstance)) {}
};

struct FEntry
{
	std::string Name;
	std::string ReturnType;
	std::vector<FParam> Params;
	std::size_t SigBegin = 0;    // 返回类型 token 起点
	std::size_t SigEnd = 0;      // 参数表 ')' 之后
	std::size_t BodyBegin = 0;
	std::size_t BodyEnd = 0;
	std::uint32_t Line = 0;
};

struct FDirectives
{
	std::string PassName = "Forward";
	std::string Cull = "Back";
	std::string ZWrite = "On";
	std::string ZTest = "LEqual";
	std::string Blend = "Off";
	std::string VertexEntry;
	std::string FragmentEntry;
};

struct FCodeLine
{
	std::uint32_t Number = 0;
	std::string Text;
};

/** 一条 add 到 GLSL 里的块成员声明（含它自己的行，好读、也好对着日志查）。 */
std::string Indent1(std::string_view S)
{
	return "\t" + std::string(S);
}

struct FShader
{
	std::string Path;
	FParseOutput Out;
	FMaterialLayout Layout;
	std::string MaterialMembers;          // `_Material` 块里的成员（声明序）
	std::vector<std::string> Samplers;    // binding1..N 的采样器声明（声明序）
	FDirectives Dir;
	std::vector<FStructDecl> Structs;
	std::vector<FCodeLine> CodeLines;
	std::string CodeText;
	std::vector<std::uint32_t> CodeLineNumbers;
};

// ============================== Properties ==============================

bool ParseFloatValue(std::string_view Text, float& OutValue)
{
	const std::string Copy(TrimView(Text));
	if (Copy.empty())
	{
		return false;
	}
	char* End = nullptr;
	const float Value = std::strtof(Copy.c_str(), &End);
	if (End == Copy.c_str() || (End != nullptr && *End != '\0'))
	{
		return false;
	}
	OutValue = Value;
	return true;
}

/** 逗号分隔的数字串（`(0.8, 0.8, 0.8, 1.0)` 去掉括号后的部分）。 */
std::vector<float> SplitNumbers(std::string_view Text, bool& bOutOk)
{
	std::vector<float> Values;
	bOutOk = true;
	std::size_t Begin = 0;
	while (Begin <= Text.size())
	{
		const std::size_t Comma = Text.find(',', Begin);
		const std::string_view Piece = (Comma == std::string_view::npos)
			? Text.substr(Begin)
			: Text.substr(Begin, Comma - Begin);
		float Value = 0.0f;
		if (!ParseFloatValue(Piece, Value))
		{
			bOutOk = false;
			return Values;
		}
		Values.push_back(Value);
		if (Comma == std::string_view::npos)
		{
			break;
		}
		Begin = Comma + 1;
	}
	return Values;
}

/** std140 打包：本项目只有 vec4（对齐 16）与 float/int（对齐 4）两种成员，逐条手写就好读。
 *  "vec4 之后下一个成员仍按自身对齐"就是 std140 的规则（本项目不出现 vec3，所以没有那条著名的
 *  vec3 补齐坑）。总大小最后再对齐到 16 —— 渲染侧开的缓冲就是它。
 *  这份偏移是不是和 GPU 一致，由 VerifyMaterialLayout() 读编译出来的 SPIR-V 反查（不是自证）。 */
void AppendMaterialMember(FMaterialLayout& Layout, FMaterialProperty Prop, std::string& OutGlsl)
{
	const bool bVector = Prop.Type == EMaterialPropertyType::Color;
	const std::uint32_t Alignment = bVector ? 16u : 4u;
	Prop.Offset = (Layout.UniformSize + Alignment - 1) / Alignment * Alignment;
	Prop.Size = bVector ? 16u : 4u;
	Layout.UniformSize = Prop.Offset + Prop.Size;

	const char* GlslType = Prop.Type == EMaterialPropertyType::Color ? "vec4"
		: (Prop.Type == EMaterialPropertyType::Int ? "int" : "float");
	OutGlsl += Indent1(std::string(GlslType) + " " + Prop.Name + ";") + "\n";

	Layout.Properties.push_back(std::move(Prop));
}

/** 解析一个 Properties 块（含默认值）。返回 false 时 Diag 已带 "文件:行"。 */
bool ParseProperties(const std::string& Clean, std::size_t Begin, std::size_t End, FShader& Shader, FDiag& Diag)
{
	std::size_t Cursor = Begin;
	while (Cursor < End)
	{
		const std::size_t LineEnd = std::min(Clean.find('\n', Cursor), End);
		const std::string_view RawLine(Clean.data() + Cursor, (LineEnd == std::string::npos ? End : LineEnd) - Cursor);
		const std::uint32_t Line = LineOf(Clean, Cursor);
		Cursor = (LineEnd == std::string::npos ? End : LineEnd + 1);

		std::string_view S = TrimView(RawLine);
		if (S.empty())
		{
			continue;
		}
		// 允许 Unity 习惯的行尾分号，别的多余字符都算错。
		if (S.back() == ';')
		{
			S = TrimView(S.substr(0, S.size() - 1));
		}

		if (S.empty() || S.front() != '_')
		{
			Diag.Fail(Line, "材质属性名必须以 '_' 开头（Unity 约定，也让注入的 _Material/_Frame 不会撞名）");
			return false;
		}

		std::size_t P = 0;
		while (P < S.size() && IsIdentChar(S[P]))
		{
			++P;
		}
		std::string PropName(S.substr(0, P));

		auto SkipSpaces = [&S, &P]()
		{
			while (P < S.size() && IsSpace(S[P]))
			{
				++P;
			}
		};
		auto Expect = [&SkipSpaces, &S, &P](char Wanted) -> bool
		{
			SkipSpaces();
			if (P >= S.size() || S[P] != Wanted)
			{
				return false;
			}
			++P;
			return true;
		};

		// ("显示名", 类型)
		SkipSpaces();
		if (P >= S.size() || S[P] != '(')
		{
			Diag.Fail(Line, "属性声明形如：_Name(\"显示名\", 类型) = 默认值");
			return false;
		}
		++P;
		SkipSpaces();
		if (P >= S.size() || S[P] != '"')
		{
			Diag.Fail(Line, "属性声明缺少显示名，形如：_Name(\"显示名\", 类型) = 默认值");
			return false;
		}
		++P;
		while (P < S.size() && S[P] != '"')
		{
			++P;
		}
		if (P >= S.size())
		{
			Diag.Fail(Line, "显示名的引号没有闭合");
			return false;
		}
		++P;   // 收尾引号
		if (!Expect(','))
		{
			Diag.Fail(Line, "显示名之后要跟 ',' 再跟类型");
			return false;
		}

		SkipSpaces();
		const std::size_t TypeBegin = P;
		while (P < S.size() && IsIdentChar(S[P]))
		{
			++P;
		}
		const std::string TypeToken(S.substr(TypeBegin, P - TypeBegin));
		if (TypeToken.empty())
		{
			Diag.Fail(Line, "属性声明缺少类型（Float/Range/Int/Color/2D/Cube）");
			return false;
		}
		// Range(最小, 最大)：范围只是 UI 提示，不改布局，但必须写对。
		SkipSpaces();
		if (P < S.size() && S[P] == '(')
		{
			if (TypeToken != "Range")
			{
				Diag.Fail(Line, "只有 Range 类型带括号参数，例如 Range(0.0, 1.0)");
				return false;
			}
			const std::size_t Close = S.find(')', P);
			if (Close == std::string_view::npos)
			{
				Diag.Fail(Line, "Range(...) 缺少收尾 ')'");
				return false;
			}
			bool bOk = false;
			const std::vector<float> Bounds = SplitNumbers(S.substr(P + 1, Close - P - 1), bOk);
			if (!bOk || Bounds.size() != 2)
			{
				Diag.Fail(Line, "Range 需要两个数字，例如 Range(0.0, 1.0)");
				return false;
			}
			P = Close + 1;
		}
		if (!Expect(')'))
		{
			Diag.Fail(Line, "属性声明缺少收尾 ')'");
			return false;
		}
		if (!Expect('='))
		{
			Diag.Fail(Line, "属性必须写默认值，例如 = (1, 1, 1, 1)");
			return false;
		}

		EMaterialPropertyType PropType = EMaterialPropertyType::Float;
		if (TypeToken == "Float") { PropType = EMaterialPropertyType::Float; }
		else if (TypeToken == "Range") { PropType = EMaterialPropertyType::Range; }
		else if (TypeToken == "Int") { PropType = EMaterialPropertyType::Int; }
		else if (TypeToken == "Color") { PropType = EMaterialPropertyType::Color; }
		else if (TypeToken == "2D" || TypeToken == "Texture2D") { PropType = EMaterialPropertyType::Texture2D; }
		else if (TypeToken == "Cube" || TypeToken == "TextureCube") { PropType = EMaterialPropertyType::TextureCube; }
		else
		{
			Diag.Fail(Line, "未知的属性类型 '" + TypeToken + "'（只有 Float/Range/Int/Color/2D/Cube）");
			return false;
		}

		FMaterialProperty Prop;
		Prop.Name = PropName;
		Prop.Type = PropType;

		SkipSpaces();
		const std::string_view Default = TrimView(S.substr(std::min(P, S.size())));
		if (Default.empty())
		{
			Diag.Fail(Line, "属性缺少默认值");
			return false;
		}

		if (PropType == EMaterialPropertyType::Texture2D || PropType == EMaterialPropertyType::TextureCube)
		{
			// `"white" {}` / `"white"`：默认纹理名，只有名字（缺图时渲染侧据此决定回退，D2/6.3）。
			if (Default.front() != '"')
			{
				Diag.Fail(Line, "纹理属性的默认值形如 \"white\" {}");
				return false;
			}
			const std::size_t CloseQuote = Default.find('"', 1);
			if (CloseQuote == std::string_view::npos)
			{
				Diag.Fail(Line, "默认纹理名的引号没有闭合");
				return false;
			}
			Prop.DefaultTexture = std::string(Default.substr(1, CloseQuote - 1));

			// 采样器不进常量块：按声明序拿 binding1..N（D3）。
			Prop.SamplerBinding = 1 + static_cast<std::uint32_t>(Shader.Samplers.size());
			Shader.Samplers.push_back(
				"layout(set = 2, binding = " + std::to_string(Prop.SamplerBinding) + ") uniform "
					+ (PropType == EMaterialPropertyType::TextureCube ? "samplerCube " : "sampler2D ")
					+ Prop.Name + ";");
			++Shader.Layout.SamplerCount;
			Shader.Layout.Properties.push_back(std::move(Prop));
			continue;
		}

		bool bOk = false;
		std::vector<float> Values;
		if (Default.front() == '(')
		{
			const std::size_t Close = Default.find(')');
			if (Close == std::string_view::npos)
			{
				Diag.Fail(Line, "默认值缺少收尾 ')'");
				return false;
			}
			Values = SplitNumbers(Default.substr(1, Close - 1), bOk);
			if (!bOk)
			{
				Diag.Fail(Line, "默认值里有不是数字的分量");
				return false;
			}
		}
		else
		{
			float Value = 0.0f;
			if (!ParseFloatValue(Default, Value))
			{
				Diag.Fail(Line, "默认值不是数字（向量要写 (x, y, z, w)）");
				return false;
			}
			Values.push_back(Value);
		}

		if (PropType == EMaterialPropertyType::Color && Values.size() != 4)
		{
			Diag.Fail(Line, "Color 的默认值必须是 4 个分量，例如 (1, 1, 1, 1)");
			return false;
		}
		if (PropType != EMaterialPropertyType::Color && Values.size() != 1)
		{
			Diag.Fail(Line, "标量属性的默认值是单个数字，例如 = 0.5");
			return false;
		}
		for (std::size_t I = 0; I < Values.size() && I < 4; ++I)
		{
			Prop.Default[I] = Values[I];
		}

		AppendMaterialMember(Shader.Layout, std::move(Prop), Shader.MaterialMembers);
	}
	return true;
}

// ============================== Pass 指令 + 代码行 ==============================

/** Pass 体按行切开：深度 0 的行要么是指令、要么是代码（struct/函数签名）；深度 >0 的一律是代码。 */
bool SplitPassBody(const std::string& Clean, std::size_t Begin, std::size_t End, FShader& Shader, FDiag& Diag)
{
	bool bSawVertexPragma = false;
	bool bSawFragmentPragma = false;

	int Depth = 0;
	std::size_t Cursor = Begin;
	while (Cursor < End)
	{
		std::size_t LineEnd = Clean.find('\n', Cursor);
		if (LineEnd == std::string::npos || LineEnd > End)
		{
			LineEnd = End;
		}
		const std::string_view Raw(Clean.data() + Cursor, LineEnd - Cursor);
		const std::uint32_t Line = LineOf(Clean, Cursor);
		const std::size_t Next = (LineEnd >= End) ? End : LineEnd + 1;
		std::string_view S = TrimView(Raw);

		if (Depth == 0 && !S.empty())
		{
			std::size_t Space = 0;
			while (Space < S.size() && !IsSpace(S[Space]))
			{
				++Space;
			}
			const std::string_view Key = S.substr(0, Space);
			std::string_view Rest = TrimView(S.substr(std::min(Space, S.size())));

			if (Key == "Name")
			{
				if (Rest.size() >= 2 && Rest.front() == '"' && Rest.back() == '"')
				{
					Shader.Dir.PassName = std::string(Rest.substr(1, Rest.size() - 2));
				}
				else
				{
					Diag.Fail(Line, "Pass 的 Name 形如：Name \"Forward\"");
					return false;
				}
				Cursor = Next;
				continue;
			}
			if (Key == "Cull" || Key == "ZWrite" || Key == "ZTest" || Key == "Blend")
			{
				// 这一层只记原文；合法值在 EmitPassState 里统一判定（一处表、一处报错）。
				if (Key == "Cull") { Shader.Dir.Cull = std::string(Rest); }
				else if (Key == "ZWrite") { Shader.Dir.ZWrite = std::string(Rest); }
				else if (Key == "ZTest") { Shader.Dir.ZTest = std::string(Rest); }
				else { Shader.Dir.Blend = std::string(Rest); }
				Cursor = Next;
				continue;
			}
			// 认识的 key、本切片不做：报错而不是静默忽略（D7 的原话）。
			if (Key == "Tags" || Key == "Stencil" || Key == "ColorMask" || Key == "Offset"
				|| Key == "ZClip" || Key == "LOD" || Key == "Lighting")
			{
				Diag.Fail(Line, "状态键 '" + std::string(Key) + "' 本切片未支持（只有 Cull/ZWrite/ZTest/Blend/Name）");
				return false;
			}
			if (!S.empty() && S.front() == '#')
			{
				if (Key == "#pragma")
				{
					std::size_t WordBegin = 0;
					while (WordBegin < Rest.size() && !IsSpace(Rest[WordBegin]))
					{
						++WordBegin;
					}
					const std::string_view Pragma = Rest.substr(0, WordBegin);
					const std::string_view Argument = TrimView(Rest.substr(std::min(WordBegin, Rest.size())));
					if (Pragma == "vertex")
					{
						if (bSawVertexPragma)
						{
							Diag.Fail(Line, "#pragma vertex 写了两次");
							return false;
						}
						bSawVertexPragma = true;
						Shader.Dir.VertexEntry = Trim(Argument);
					}
					else if (Pragma == "fragment")
					{
						if (bSawFragmentPragma)
						{
							Diag.Fail(Line, "#pragma fragment 写了两次");
							return false;
						}
						bSawFragmentPragma = true;
						Shader.Dir.FragmentEntry = Trim(Argument);
					}
					else
					{
						Diag.Fail(Line, "本切片只认 #pragma vertex / #pragma fragment");
						return false;
					}
				}
				else
				{
					Diag.Fail(Line, "Pass 里不支持预处理指令 '" + std::string(Key) + "'（只认 #pragma vertex/fragment）");
					return false;
				}
				Cursor = Next;
				continue;
			}
		}

		for (const char C : Raw)
		{
			if (C == '{') { ++Depth; }
			else if (C == '}') { --Depth; }
		}
		if (!S.empty())
		{
			Shader.CodeLines.push_back(FCodeLine{ Line, std::string(Raw) });
		}
		Cursor = Next;
	}

	if (Shader.Dir.VertexEntry.empty() || Shader.Dir.FragmentEntry.empty())
	{
		Diag.Fail(LineOf(Clean, Begin), "Pass 缺少 #pragma vertex / #pragma fragment（入口名不能猜）");
		return false;
	}
	if (Depth != 0)
	{
		Diag.Fail(LineOf(Clean, Begin), "Pass 的花括号不配对");
		return false;
	}
	return true;
}

// ============================== struct 与入口签名 ==============================

bool ParseStructs(FShader& Shader, FDiag& Diag)
{
	const std::string& Code = Shader.CodeText;
	std::size_t At = 0;
	while (true)
	{
		const std::size_t KeywordAt = FindKeyword(Code, "struct", At);
		if (KeywordAt == std::string::npos)
		{
			break;
		}
		const std::size_t Line = CodeLineOf(Code, Shader.CodeLineNumbers, KeywordAt);

		std::size_t P = KeywordAt + 6;
		while (P < Code.size() && IsSpace(Code[P]))
		{
			++P;
		}
		const std::size_t NameBegin = P;
		while (P < Code.size() && IsIdentChar(Code[P]))
		{
			++P;
		}
		const std::string Name = Code.substr(NameBegin, P - NameBegin);
		if (Name.empty())
		{
			Diag.Fail(Line, "struct 缺名字");
			return false;
		}
		while (P < Code.size() && IsSpace(Code[P]))
		{
			++P;
		}
		if (P >= Code.size() || Code[P] != '{')
		{
			Diag.Fail(Line, "struct " + Name + " 缺 '{'");
			return false;
		}
		const std::size_t Close = MatchBrace(Code, P);
		if (Close == std::string::npos)
		{
			Diag.Fail(Line, "struct " + Name + " 的花括号不配对");
			return false;
		}

		FStructDecl Decl;
		Decl.Name = Name;
		Decl.Line = Line;
		Decl.SpanBegin = KeywordAt;
		Decl.SpanEnd = Close + 1;
		if (Decl.SpanEnd < Code.size() && Code[Decl.SpanEnd] == ';')
		{
			++Decl.SpanEnd;
		}

		// 成员：`<类型> <名字> : <语义>;`（一条一句，不猜更花哨的写法）
		std::size_t MemberAt = P + 1;
		while (MemberAt < Close)
		{
			std::size_t Semicolon = MemberAt;
			int InnerDepth = 0;
			while (Semicolon < Close)
			{
				if (Code[Semicolon] == '{') { ++InnerDepth; }
				else if (Code[Semicolon] == '}') { --InnerDepth; }
				else if (Code[Semicolon] == ';' && InnerDepth == 0) { break; }
				++Semicolon;
			}
			const std::string_view Statement = TrimView(std::string_view(Code).substr(MemberAt, Semicolon - MemberAt));
			const std::uint32_t MemberLine = CodeLineOf(Code, Shader.CodeLineNumbers, MemberAt);

			if (!Statement.empty())
			{
				const std::size_t Colon = Statement.find(':');
				if (Colon == std::string_view::npos)
				{
					Diag.Fail(MemberLine, "struct 成员必须带语义，例如：float3 position : POSITION;");
					return false;
				}
				const std::string_view Left = TrimView(Statement.substr(0, Colon));
				const std::string_view Right = TrimView(Statement.substr(Colon + 1));

				std::size_t Split = 0;
				while (Split < Left.size() && !IsSpace(Left[Split]))
				{
					++Split;
				}
				const std::string Type(Left.substr(0, Split));
				const std::string MemberName(TrimView(Left.substr(std::min(Split, Left.size()))));
				if (Type.empty() || MemberName.empty() || MemberName.find(' ') != std::string::npos)
				{
					Diag.Fail(MemberLine, "struct 成员形如：float3 position : POSITION;");
					return false;
				}
				if (Right.empty() || Right.find(' ') != std::string_view::npos)
				{
					Diag.Fail(MemberLine, "语义必须是单个词，例如 POSITION / TEXCOORD0 / SV_POSITION");
					return false;
				}
				const char* GlslType = GlslTypeFor(Type);
				if (GlslType == nullptr)
				{
					Diag.Fail(MemberLine, "未知的类型 '" + Type + "'。" + kSupportedTypesHint);
					return false;
				}

				FMember Member;
				Member.Type = Type;
				Member.GlslType = GlslType;
				Member.Name = MemberName;
				Member.Semantic = ToUpper(Right);
				Member.Line = MemberLine;
				Decl.Members.push_back(std::move(Member));
			}

			if (Semicolon >= Close)
			{
				break;
			}
			MemberAt = Semicolon + 1;
		}

		if (Decl.Members.empty())
		{
			Diag.Fail(Line, "struct " + Name + " 是空的");
			return false;
		}
		Shader.Structs.push_back(std::move(Decl));
		At = Close + 1;
	}
	return true;
}

const FStructDecl* FindStruct(const FShader& Shader, std::string_view Name)
{
	for (const FStructDecl& Decl : Shader.Structs)
	{
		if (Decl.Name == Name)
		{
			return &Decl;
		}
	}
	return nullptr;
}

/** 找 `#pragma` 指定的入口函数定义，并把签名/函数体区间连带参数表一起取出来。 */
bool ParseEntry(FShader& Shader, std::string_view EntryName, FEntry& OutEntry, FDiag& Diag)
{
	const std::string& Code = Shader.CodeText;
	const std::size_t NameAt = FindKeyword(Code, EntryName);
	if (NameAt == std::string::npos)
	{
		Diag.Fail(CodeLineOf(Code, Shader.CodeLineNumbers, Code.size()),
			"#pragma 指定的入口 '" + std::string(EntryName) + "' 在这个 Pass 里找不到函数定义");
		return false;
	}
	const std::uint32_t Line = CodeLineOf(Code, Shader.CodeLineNumbers, NameAt);

	std::size_t P = NameAt + EntryName.size();
	while (P < Code.size() && IsSpace(Code[P]))
	{
		++P;
	}
	if (P >= Code.size() || Code[P] != '(')
	{
		Diag.Fail(Line, "入口 '" + std::string(EntryName) + "' 不是函数定义（后面不是 '('）");
		return false;
	}

	// 往回取返回类型 token（`void` / `v2f` / `float4`）。
	std::size_t TypeEnd = NameAt;
	while (TypeEnd > 0 && IsSpace(Code[TypeEnd - 1]))
	{
		--TypeEnd;
	}
	std::size_t TypeBegin = TypeEnd;
	while (TypeBegin > 0 && IsIdentChar(Code[TypeBegin - 1]))
	{
		--TypeBegin;
	}
	const std::string ReturnType = Code.substr(TypeBegin, TypeEnd - TypeBegin);
	if (ReturnType.empty())
	{
		Diag.Fail(Line, "入口 '" + std::string(EntryName) + "' 缺返回类型");
		return false;
	}

	// 参数表（括号配对；参数本身没有嵌套括号）。
	int Depth = 0;
	std::size_t CloseParen = std::string::npos;
	for (std::size_t I = P; I < Code.size(); ++I)
	{
		if (Code[I] == '(') { ++Depth; }
		else if (Code[I] == ')')
		{
			if (--Depth == 0)
			{
				CloseParen = I;
				break;
			}
		}
	}
	if (CloseParen == std::string::npos)
	{
		Diag.Fail(Line, "入口 '" + std::string(EntryName) + "' 的参数表括号不配对");
		return false;
	}

	const std::string_view ParamText = TrimView(std::string_view(Code).substr(P + 1, CloseParen - P - 1));
	if (!ParamText.empty())
	{
		std::size_t Begin = 0;
		while (Begin <= ParamText.size())
		{
			const std::size_t Comma = ParamText.find(',', Begin);
			const std::string_view Piece = (Comma == std::string_view::npos)
				? ParamText.substr(Begin)
				: ParamText.substr(Begin, Comma - Begin);

			// 允许 in/out/inout 修饰符（HLSL/Unity 习惯），只取 <struct> <实例名>。
			std::vector<std::string> Tokens;
			std::string_view Rest = TrimView(Piece);
			while (!Rest.empty())
			{
				std::size_t TokenEnd = 0;
				while (TokenEnd < Rest.size() && !IsSpace(Rest[TokenEnd]))
				{
					++TokenEnd;
				}
				Tokens.push_back(std::string(Rest.substr(0, TokenEnd)));
				Rest = TrimView(Rest.substr(TokenEnd));
			}
			if (Tokens.size() == 3 && (Tokens[0] == "in" || Tokens[0] == "out" || Tokens[0] == "inout"))
			{
				Tokens.erase(Tokens.begin());
			}
			if (Tokens.size() != 2)
			{
				Diag.Fail(Line, "入口 '" + std::string(EntryName) + "' 的参数必须是 <struct> <实例名>（可选 in/out/inout）");
				return false;
			}
			OutEntry.Params.emplace_back(Tokens[0], Tokens[1]);
			if (Comma == std::string_view::npos)
			{
				break;
			}
			Begin = Comma + 1;
		}
	}

	std::size_t BodyOpen = CloseParen + 1;
	while (BodyOpen < Code.size() && IsSpace(Code[BodyOpen]))
	{
		++BodyOpen;
	}
	if (BodyOpen >= Code.size() || Code[BodyOpen] != '{')
	{
		Diag.Fail(Line, "入口 '" + std::string(EntryName)
			+ "' 只能是函数定义（语义写在 struct 成员上，函数后面直接跟 '{'）");
		return false;
	}
	const std::size_t BodyClose = MatchBrace(Code, BodyOpen);
	if (BodyClose == std::string::npos)
	{
		Diag.Fail(Line, "入口 '" + std::string(EntryName) + "' 的花括号不配对");
		return false;
	}

	OutEntry.Name = std::string(EntryName);
	OutEntry.ReturnType = ReturnType;
	OutEntry.SigBegin = TypeBegin;
	OutEntry.SigEnd = CloseParen + 1;
	OutEntry.BodyBegin = BodyOpen + 1;
	OutEntry.BodyEnd = BodyClose;
	OutEntry.Line = Line;
	return true;
}

// ============================== 发射 GLSL ==============================

/** `实例名.成员名` 定点替换：成员名表来自解析出的 struct 声明，实例名来自入口签名 —— 两者都不是猜的。
 *  只认"已知实例 + 已知成员"，其余原样留着（编译器会报"未声明"这种正常错误）。 */
std::string RewriteMemberAccess(const std::string& Text,
	const std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& Maps)
{
	std::string Out;
	Out.reserve(Text.size());
	std::size_t I = 0;
	while (I < Text.size())
	{
		if (IsIdentStart(Text[I]))
		{
			std::size_t J = I;
			while (J < Text.size() && IsIdentChar(Text[J]))
			{
				++J;
			}
			const auto MapIt = Maps.find(Text.substr(I, J - I));
			if (MapIt != Maps.end() && J < Text.size() && Text[J] == '.')
			{
				std::size_t K = J + 1;
				while (K < Text.size() && IsIdentChar(Text[K]))
				{
					++K;
				}
				const auto MemberIt = MapIt->second.find(Text.substr(J + 1, K - (J + 1)));
				if (MemberIt != MapIt->second.end())
				{
					Out += MemberIt->second;
					I = K;
					continue;
				}
			}
			Out.append(Text, I, J - I);
			I = J;
			continue;
		}
		Out.push_back(Text[I]);
		++I;
	}
	return Out;
}

/** 清空区间内容但保留换行 —— 拼出来的 GLSL 行号与解析时的代码行一一对应，好查。 */
void BlankRange(std::string& Text, std::size_t Begin, std::size_t End)
{
	for (std::size_t I = Begin; I < End && I < Text.size(); ++I)
	{
		if (Text[I] != '\n')
		{
			Text[I] = ' ';
		}
	}
}

/** 注入的引擎块（D3）。Set0 = 帧常量（沿用渲染侧现有的 ViewConstants 名与形状，渲染侧不用改）、
 *  push = Object 矩阵、Set2 binding0 = 材质参数、binding1..N = 采样器。 */
void AppendEngineBlocks(std::string& Out, bool bVertexStage, const FShader& Shader)
{
	Out += "// -- injected by ShaderLibrary (see FShaderParser::kInjectedBlockVersion) --\n";
	Out += "layout(set = 0, binding = 0, std140) uniform ViewConstants { mat4 ViewProj; } _Frame;\n";
	if (bVertexStage)
	{
		Out += "layout(push_constant) uniform Push { mat4 ObjectMatrix; } _Push;\n";
	}
	if (!Shader.Layout.Properties.empty() && !Shader.MaterialMembers.empty())
	{
		Out += "layout(set = 2, binding = 0, std140) uniform MahoMaterial\n{\n";
		Out += Shader.MaterialMembers;
		Out += "} _Material;\n";
	}
	for (const std::string& Sampler : Shader.Samplers)
	{
		Out += Sampler + "\n";
	}
	Out += "\n";
}

/** 按语义把 struct 成员铺成 `layout(location = N) storage 类型 m_前缀_名字;`。
 *  `bVertexStage` 决定 Storage（顶点输入的 in / 阶段间的 out / 片元阶段的 in）。 */
bool EmitMembers(const FStructDecl& Decl, bool bVertexStage, bool bInterstage, std::string& Out, FDiag& Diag)
{
	for (const FMember& Member : Decl.Members)
	{
		const std::string Global = std::string(bInterstage ? "m_i_" : "m_v_") + Member.Name;

		if (Member.Semantic == "SV_POSITION")
		{
			if (!bInterstage)
			{
				Diag.Fail(Member.Line, "SV_POSITION 只能出现在阶段间 struct 里");
				return false;
			}
			// 顶点阶段写内建 gl_Position；片元阶段不声明它（它是输出，不是输入）。
			continue;
		}
		if (Member.Semantic == "SV_TARGET")
		{
			if (!bInterstage)
			{
				Diag.Fail(Member.Line, "SV_TARGET 只能出现在阶段间 struct 里（作为片元输出成员）");
				return false;
			}
			if (Member.GlslType != "vec4")
			{
				Diag.Fail(Member.Line, "SV_TARGET 必须是 float4（单一颜色输出）");
				return false;
			}
			continue;   // 单独发射（见 EmitStage），这里只是不当作 varying
		}

		const FSemanticEntry* Semantic = FindSemantic(Member.Semantic);
		if (Semantic == nullptr)
		{
			Diag.Fail(Member.Line, "未知语义 '" + Member.Semantic
				+ "'（语义表：POSITION/NORMAL/TEXCOORD0/TEXCOORD1/TANGENT/COLOR0，"
				  "阶段间另有 SV_POSITION/SV_TARGET）");
			return false;
		}
		if (!bInterstage && !bVertexStage)
		{
			// 片元阶段的输入是阶段间 struct 的成员，不该再出现顶点输入 struct。
			Diag.Fail(Member.Line, "片元阶段不能直接使用顶点输入 struct");
			return false;
		}

		const char* Storage = bVertexStage ? (bInterstage ? "out" : "in") : "in";
		Out += "layout(location = " + std::to_string(Semantic->Location) + ") " + Storage + " "
			+ Member.GlslType + " " + Global + ";\n";
	}
	return true;
}

/** 拼一个阶段的完整 GLSL。一个 Pass 里的两个入口都在这段代码里，所以本阶段只保留自己的那个：
 *  另一个入口的签名里那个 struct 已经被溶解掉了，留着它不是"多写一个函数"，而是语法错误。 */
bool EmitStage(FShader& Shader, bool bVertexStage, const FStructDecl& VertexInput,
	const FStructDecl* Interstage, const FEntry& Entry, const FEntry& OtherEntry,
	std::string& OutGlsl, FDiag& Diag)
{
	std::string Out;
	Out += "#version 460\n\n";
	AppendEngineBlocks(Out, bVertexStage, Shader);

	std::unordered_map<std::string, std::unordered_map<std::string, std::string>> Maps;

	if (bVertexStage)
	{
		Out += "// vertex inputs (a2v)\n";
		if (!EmitMembers(VertexInput, true, false, Out, Diag))
		{
			return false;
		}
		Maps[Entry.Params[0].Instance] = {};
		for (const FMember& Member : VertexInput.Members)
		{
			Maps[Entry.Params[0].Instance][Member.Name] = "m_v_" + Member.Name;
		}
	}

	if (Interstage != nullptr)
	{
		Out += bVertexStage ? "\n// varyings out (v2f)\n" : "\n// varyings in (v2f)\n";
		if (!EmitMembers(*Interstage, bVertexStage, true, Out, Diag))
		{
			return false;
		}

		std::unordered_map<std::string, std::string> Map;
		for (const FMember& Member : Interstage->Members)
		{
			if (Member.Semantic == "SV_POSITION")
			{
				Map[Member.Name] = "gl_Position";   // 顶点阶段写；片元阶段用不到
			}
			else if (Member.Semantic == "SV_TARGET")
			{
				// 只用片元阶段声明：顶点阶段写它没有意义（写进去会得到"未声明"的编译错误）。
				Map[Member.Name] = "m_out_color";
				if (!bVertexStage)
				{
					Out += "layout(location = 0) out vec4 m_out_color;\n";
				}
			}
			else
			{
				Map[Member.Name] = "m_i_" + Member.Name;
			}
		}
		if (bVertexStage)
		{
			// 顶点阶段的阶段间实例名来自第二个参数（`inout v2f i`），没有就没人写 varying。
			if (Entry.Params.size() >= 2)
			{
				Maps[Entry.Params[1].Instance] = std::move(Map);
			}
		}
		else
		{
			Maps[Entry.Params[0].Instance] = std::move(Map);
		}
	}

	// 函数体：把 struct 声明清空、另一个阶段的入口整段清空、本阶段的入口签名换成 `void 名字()`，
	// 最后做成员定点替换。清空都保留换行，所以拼出来的行号与解析时一一对应。
	std::string Code = Shader.CodeText;
	for (const FStructDecl& Decl : Shader.Structs)
	{
		BlankRange(Code, Decl.SpanBegin, Decl.SpanEnd);
	}
	BlankRange(Code, OtherEntry.SigBegin, OtherEntry.BodyEnd + 1);

	std::string Signature = "void " + Entry.Name + "()";
	if (Signature.size() >= Entry.SigEnd - Entry.SigBegin)
	{
		Code.replace(Entry.SigBegin, Entry.SigEnd - Entry.SigBegin, Signature);
	}
	else
	{
		// 签名比原文短不可能（`v2f VSMain(a2v v, inout v2f i)` 一定更长），但别让它变成 UB。
		Code.replace(Entry.SigBegin, Entry.SigEnd - Entry.SigBegin, Signature + std::string(
			Entry.SigEnd - Entry.SigBegin - Signature.size(), ' '));
	}

	Out += "\n";
	Out += RewriteMemberAccess(Code, Maps);
	return (OutGlsl = std::move(Out), true);
}

/** `Cull/ZWrite/ZTest/Blend` 原文 -> FPassState。未知值报错（不静默当默认值）。 */
bool EmitPassState(const FDirectives& Dir, FPassState& OutState, FDiag& Diag, std::uint32_t Line)
{
	FPassState State;
	if (Dir.Cull == "Back") { State.Cull = ECullMode::Back; }
	else if (Dir.Cull == "Front") { State.Cull = ECullMode::Front; }
	else if (Dir.Cull == "Off") { State.Cull = ECullMode::Off; }
	else
	{
		Diag.Fail(Line, "Cull 只支持 Back/Front/Off，收到 '" + Dir.Cull + "'");
		return false;
	}

	if (Dir.ZWrite == "On") { State.bZWrite = true; }
	else if (Dir.ZWrite == "Off") { State.bZWrite = false; }
	else
	{
		Diag.Fail(Line, "ZWrite 只支持 On/Off，收到 '" + Dir.ZWrite + "'");
		return false;
	}

	if (Dir.ZTest == "LEqual") { State.bZTest = true; }
	else if (Dir.ZTest == "Off") { State.bZTest = false; }
	else
	{
		Diag.Fail(Line, "ZTest 只支持 LEqual/Off，收到 '" + Dir.ZTest + "'（本切片的 ZTest 是个开关，不是比较函数）");
		return false;
	}

	if (Dir.Blend == "Off") { State.Blend = EBlendMode::Opaque; }
	else if (Dir.Blend == "SrcAlpha OneMinusSrcAlpha") { State.Blend = EBlendMode::AlphaBlend; }
	else
	{
		Diag.Fail(Line, "Blend 只支持 'Off' 与 'SrcAlpha OneMinusSrcAlpha'，收到 '" + Dir.Blend + "'");
		return false;
	}

	OutState = State;
	return true;
}

// ============================== 主流程 ==============================

bool DoParse(const std::string& Source, FShader& Shader, FDiag& Diag)
{
	const std::string Clean = StripComments(Source);

	// -- Properties（可选：一个纯 unlit 的 shader 没有属性也合法）--
	std::size_t PropertiesBegin = 0;
	std::size_t PropertiesEnd = 0;
	if (TryGetBlockBody(Clean, "Properties", 0, PropertiesBegin, PropertiesEnd))
	{
		if (!ParseProperties(Clean, PropertiesBegin, PropertiesEnd, Shader, Diag))
		{
			return false;
		}
		Shader.Layout.UniformSize = (Shader.Layout.UniformSize + 15) / 16 * 16;
	}

	// -- SubShader / Pass --
	const std::uint32_t SubShaderCount = CountKeyword(Clean, "SubShader");
	if (SubShaderCount == 0)
	{
		Diag.Fail(1, "缺少 SubShader 块");
		return false;
	}
	if (SubShaderCount > 1)
	{
		Diag.Fail(1, "本切片只支持一个 SubShader");
		return false;
	}
	std::size_t SubShaderBegin = 0;
	std::size_t SubShaderEnd = 0;
	if (!TryGetBlockBody(Clean, "SubShader", 0, SubShaderBegin, SubShaderEnd))
	{
		Diag.Fail(1, "SubShader 的花括号不配对");
		return false;
	}
	const std::string_view SubShaderBody(Clean.data() + SubShaderBegin, SubShaderEnd - SubShaderBegin);
	const std::uint32_t PassCount = CountKeyword(SubShaderBody, "Pass");
	if (PassCount == 0)
	{
		Diag.Fail(LineOf(Clean, SubShaderBegin), "SubShader 里没有 Pass");
		return false;
	}
	if (PassCount > 1)
	{
		Diag.Fail(LineOf(Clean, SubShaderBegin), "本切片只支持单 Pass");
		return false;
	}
	std::size_t PassBegin = 0;
	std::size_t PassEnd = 0;
	if (!TryGetBlockBody(Clean, "Pass", SubShaderBegin, PassBegin, PassEnd))
	{
		Diag.Fail(1, "Pass 的花括号不配对");
		return false;
	}

	if (!SplitPassBody(Clean, PassBegin, PassEnd, Shader, Diag))
	{
		return false;
	}

	// 代码行拼成一段文本（连同文件行号），后面所有区间都相对这段文本。
	for (const FCodeLine& Line : Shader.CodeLines)
	{
		Shader.CodeText += Line.Text;
		Shader.CodeText += '\n';
		Shader.CodeLineNumbers.push_back(Line.Number);
	}

	if (!ParseStructs(Shader, Diag))
	{
		return false;
	}

	// -- 入口 -> struct 角色 --
	FEntry VertexEntry;
	FEntry FragmentEntry;
	if (!ParseEntry(Shader, Shader.Dir.VertexEntry, VertexEntry, Diag))
	{
		return false;
	}
	if (!ParseEntry(Shader, Shader.Dir.FragmentEntry, FragmentEntry, Diag))
	{
		return false;
	}
	if (VertexEntry.Params.empty())
	{
		Diag.Fail(VertexEntry.Line, "顶点入口的第一个参数必须是顶点输入 struct（a2v）");
		return false;
	}
	const FStructDecl* VertexInput = FindStruct(Shader, VertexEntry.Params[0].Struct);
	if (VertexInput == nullptr)
	{
		Diag.Fail(VertexEntry.Line, "顶点入口的参数类型 '" + VertexEntry.Params[0].Struct
			+ "' 不是本 Pass 里声明的 struct");
		return false;
	}
	if (VertexEntry.Params.size() > 2)
	{
		Diag.Fail(VertexEntry.Line, "顶点入口最多两个参数：<a2v> 与 <inout 阶段间 struct>");
		return false;
	}
	if (FragmentEntry.Params.size() != 1)
	{
		Diag.Fail(FragmentEntry.Line, "片元入口必须且只能有一个参数：阶段间 struct（片元输出写在它的 SV_Target 成员上）");
		return false;
	}

	// 阶段间 struct：顶点入口的第二个参数 / 片元入口的第一个参数，必须一致（只有两个 struct，D4）。
	std::string InterstageName;
	if (VertexEntry.Params.size() == 2)
	{
		InterstageName = VertexEntry.Params[1].Struct;
		if (InterstageName != FragmentEntry.Params[0].Struct)
		{
			Diag.Fail(VertexEntry.Line, "顶点入口的阶段间 struct '" + InterstageName
				+ "' 与片元入口的 '" + FragmentEntry.Params[0].Struct + "' 不一致");
			return false;
		}
	}
	else
	{
		InterstageName = FragmentEntry.Params[0].Struct;
	}
	const FStructDecl* Interstage = FindStruct(Shader, InterstageName);
	if (Interstage == nullptr)
	{
		Diag.Fail(FragmentEntry.Line, "阶段间类型 '" + InterstageName + "' 不是本 Pass 里声明的 struct");
		return false;
	}

	// 阶段间 struct 必须恰好一个 SV_Target（片元输出），最多一个 SV_POSITION。
	std::uint32_t TargetCount = 0;
	std::uint32_t PositionCount = 0;
	for (const FMember& Member : Interstage->Members)
	{
		if (Member.Semantic == "SV_TARGET") { ++TargetCount; }
		if (Member.Semantic == "SV_POSITION") { ++PositionCount; }
	}
	if (TargetCount == 0)
	{
		Diag.Fail(Interstage->Line, "阶段间 struct '" + Interstage->Name
			+ "' 缺少 SV_Target 成员（片元的颜色输出写在那里边）");
		return false;
	}
	if (TargetCount > 1)
	{
		Diag.Fail(Interstage->Line, "只支持一个 SV_Target（多渲染目标不在本切片内）");
		return false;
	}
	if (PositionCount == 0)
	{
		Diag.Fail(Interstage->Line, "阶段间 struct '" + Interstage->Name
			+ "' 缺少 SV_POSITION 成员（它对应 gl_Position）");
		return false;
	}

	// -- Pass state（报错用 Pass 起始行，state 行号这里不再细分）--
	FPassState PassState;
	if (!EmitPassState(Shader.Dir, PassState, Diag, LineOf(Clean, PassBegin)))
	{
		return false;
	}

	// -- 发射 --
	FParseOutput Output;
	if (!EmitStage(Shader, true, *VertexInput, Interstage, VertexEntry, FragmentEntry, Output.VertexGlsl, Diag))
	{
		return false;
	}
	if (!EmitStage(Shader, false, *VertexInput, Interstage, FragmentEntry, VertexEntry, Output.FragmentGlsl, Diag))
	{
		return false;
	}
	Output.VertexEntry = VertexEntry.Name;
	Output.FragmentEntry = FragmentEntry.Name;
	Output.Material = std::move(Shader.Layout);
	Output.Pass = PassState;
	Shader.Out = std::move(Output);
	return true;
}

} // namespace

std::uint64_t HashShaderSource(std::string_view Source)
{
	// FNV-1a 64：够散、够短、没有依赖。缓存键要的是"改一个字节就换键"。
	std::uint64_t Hash = 1469598103934665603ULL;
	for (const char C : Source)
	{
		Hash ^= static_cast<unsigned char>(C);
		Hash *= 1099511628211ULL;
	}
	return Hash;
}

bool Parse(const std::string& Source, std::string_view FilePath, FParseOutput& Out, std::string& OutError)
{
	FShader Shader;
	FDiag Diag;
	Diag.Path = std::string(FilePath);
	Shader.Path = Diag.Path;

	if (!DoParse(Source, Shader, Diag))
	{
		OutError = Diag.Text.empty() ? (Diag.Path + ": 解析失败") : Diag.Text;
		return false;
	}
	Out = std::move(Shader.Out);
	return true;
}

bool VerifyMaterialLayout(const std::vector<std::uint32_t>& SpirvWords, const FMaterialLayout& Layout,
	std::string& OutError, bool& bOutChecked)
{
	bOutChecked = false;
	if (Layout.Properties.empty())
	{
		return true;   // 没有材质常量块，没什么好校验的
	}

	// 只统计进常量块的成员（纹理属性不在块里）。
	std::vector<const FMaterialProperty*> BlockMembers;
	for (const FMaterialProperty& Prop : Layout.Properties)
	{
		if (Prop.Type != EMaterialPropertyType::Texture2D && Prop.Type != EMaterialPropertyType::TextureCube)
		{
			BlockMembers.push_back(&Prop);
		}
	}
	if (BlockMembers.empty())
	{
		return true;
	}
	if (SpirvWords.size() < 5)
	{
		return true;
	}

	// SPIR-V：OpName(5) 取块名 -> OpTypeStruct(30) 拿成员数 -> OpMemberDecorate(72, Offset=35) 拿偏移。
	// （OpMemberDecorate 是 72，71 是 OpDecorate —— 写错的话偏移永远取不到，症状是
	//   "SPIR-V 偏移(4294967295) 与 CPU 布局(x) 不一致"，看起来像 std140 打包错了。）
	// 这是"手工 std140 打包"唯一的独立证据：读的是 glslang 真写下的数字。
	constexpr std::uint32_t OpName = 5;
	constexpr std::uint32_t OpTypeStruct = 30;
	constexpr std::uint32_t OpMemberDecorate = 72;
	constexpr std::uint32_t DecorationOffset = 35;

	std::uint32_t MaterialId = 0;
	for (std::size_t I = 5; I < SpirvWords.size();)
	{
		const std::uint32_t Word = SpirvWords[I];
		const std::uint32_t WordCount = Word >> 16;
		const std::uint32_t Opcode = Word & 0xFFFFu;
		if (WordCount == 0 || I + WordCount > SpirvWords.size())
		{
			break;
		}
		if (Opcode == OpName && WordCount >= 3)
		{
			std::string Name;
			for (std::uint32_t W = I + 2; W < I + WordCount; ++W)
			{
				for (int Byte = 0; Byte < 4; ++Byte)
				{
					const char C = static_cast<char>((SpirvWords[W] >> (Byte * 8)) & 0xFFu);
					if (C == '\0')
					{
						break;
					}
					Name.push_back(C);
				}
			}
			if (Name == "MahoMaterial")
			{
				MaterialId = SpirvWords[I + 1];
			}
		}
		I += WordCount;
	}
	if (MaterialId == 0)
	{
		// 块在这个阶段没被用到（已被优化掉）：没有错位风险，不算失败。
		return true;
	}
	bOutChecked = true;

	std::vector<std::uint32_t> Offsets(BlockMembers.size(), 0xFFFFFFFFu);
	std::uint32_t DeclaredMembers = 0;
	for (std::size_t I = 5; I < SpirvWords.size();)
	{
		const std::uint32_t Word = SpirvWords[I];
		const std::uint32_t WordCount = Word >> 16;
		const std::uint32_t Opcode = Word & 0xFFFFu;
		if (WordCount == 0 || I + WordCount > SpirvWords.size())
		{
			break;
		}
		if (Opcode == OpTypeStruct && WordCount >= 2 && SpirvWords[I + 1] == MaterialId)
		{
			DeclaredMembers = WordCount - 2;
		}
		if (Opcode == OpMemberDecorate && WordCount >= 4 && SpirvWords[I + 1] == MaterialId)
		{
			const std::uint32_t Member = SpirvWords[I + 2];
			const std::uint32_t Decoration = SpirvWords[I + 3];
			if (Decoration == DecorationOffset && Member < Offsets.size())
			{
				Offsets[Member] = SpirvWords[I + 4];
			}
		}
		I += WordCount;
	}

	if (DeclaredMembers != BlockMembers.size())
	{
		OutError = "MahoMaterial 的 SPIR-V 成员数(" + std::to_string(DeclaredMembers)
			+ ") 与 CPU 布局(" + std::to_string(BlockMembers.size()) + ") 不一致";
		return false;
	}
	for (std::size_t Index = 0; Index < BlockMembers.size(); ++Index)
	{
		if (Offsets[Index] != BlockMembers[Index]->Offset)
		{
			OutError = "MahoMaterial." + BlockMembers[Index]->Name + " 的 SPIR-V 偏移("
				+ std::to_string(Offsets[Index]) + ") 与 CPU 布局("
				+ std::to_string(BlockMembers[Index]->Offset) + ") 不一致（std140 打包错了）";
			return false;
		}
	}
	return true;
}

} // namespace Maho::Shader
