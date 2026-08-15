#include <Shader/ShaderLoader.h>

#include <Core/Misc/Log.h>
#include <Core/Misc/Paths.h>
#include <Shader/ShaderCache.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_set>

namespace Maho
{

// ═══════════════════════════════════════════
// Internal helpers
// ═══════════════════════════════════════════

namespace
{

void TrimInline(std::string& S)
{
	while (!S.empty() && std::isspace(static_cast<unsigned char>(S.back())))
	{
		S.pop_back();
	}
	while (!S.empty() && std::isspace(static_cast<unsigned char>(S.front())))
	{
		S.erase(0, 1);
	}
}

bool StartsWith(const std::string& S, const char* Prefix)
{
	std::size_t Len = std::strlen(Prefix);
	return (S.size() >= Len) && (S.compare(0, Len, Prefix) == 0);
}

static std::string NormalizePropertyType(const std::string& Raw)
{
	std::string Lower;
	for (char C : Raw)
	{
		Lower += static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
	}
	return Lower;
}

// ─── Vertex semantic → location / format ───

struct FSemanticInfo
{
	const char* Name;
	EShaderVertexSemantic Semantic;
	std::uint32_t Location;
	std::uint32_t Binding;
	ERHIFormat Format;
};

static const FSemanticInfo SemanticTable[] =
{
	{"POSITION",    EShaderVertexSemantic::Position,    0, 0,  ERHIFormat::R32G32B32_SFLOAT},
	{"NORMAL",      EShaderVertexSemantic::Normal,      1, 1,  ERHIFormat::R32G32B32_SFLOAT},
	{"TEXCOORD0",   EShaderVertexSemantic::TexCoord0,   2, 2,  ERHIFormat::R32G32_SFLOAT},
	{"TEXCOORD1",   EShaderVertexSemantic::TexCoord1,   3, 3,  ERHIFormat::R32G32_SFLOAT},
	{"TANGENT",     EShaderVertexSemantic::Tangent,     4, 4,  ERHIFormat::R32G32B32_SFLOAT},
	{"COLOR0",      EShaderVertexSemantic::Color0,      5, 5,  ERHIFormat::R32G32B32_SFLOAT},
	{"BONEINDICES", EShaderVertexSemantic::BoneIndices, 13, 6, ERHIFormat::R32G32B32_SFLOAT},
	{"BONEWEIGHTS", EShaderVertexSemantic::BoneWeights, 14, 7, ERHIFormat::R32G32B32_SFLOAT},
};

static bool ParseRenderStateLine(const std::string& Line, FShaderRenderState& State)
{
	std::string L = Line;
	TrimInline(L);
	if (L.empty())
	{
		return false;
	}

	if (StartsWith(L, "Cull "))
	{
		std::string V = L.substr(5);
		if (V == "Back")
		{
			State.CullMode = ERHICullMode::Back;
		}
		else if (V == "Front")
		{
			State.CullMode = ERHICullMode::Front;
		}
		else if (V == "Off" || V == "None")
		{
			State.CullMode = ERHICullMode::None;
		}
		return true;
	}
	if (StartsWith(L, "ZWrite "))
	{
		State.bDepthWrite = (L.substr(7) == "On");
		return true;
	}
	if (StartsWith(L, "ZTest "))
	{
		std::string V = L.substr(6);
		if (V == "Never")
		{
			State.DepthCompare = ERHICompareOp::Never;
		}
		else if (V == "Less")
		{
			State.DepthCompare = ERHICompareOp::Less;
		}
		else if (V == "LEqual")
		{
			State.DepthCompare = ERHICompareOp::LessOrEqual;
		}
		else if (V == "Equal")
		{
			State.DepthCompare = ERHICompareOp::Equal;
		}
		else if (V == "Greater")
		{
			State.DepthCompare = ERHICompareOp::Greater;
		}
		else if (V == "NotEqual")
		{
			State.DepthCompare = ERHICompareOp::NotEqual;
		}
		else if (V == "GEqual")
		{
			State.DepthCompare = ERHICompareOp::GreaterOrEqual;
		}
		else if (V == "Always")
		{
			State.DepthCompare = ERHICompareOp::Always;
		}
		State.bDepthTest = (V != "Always");
		return true;
	}
	if (StartsWith(L, "Blend "))
	{
		std::string V = L.substr(6);
		if (V == "Off")
		{
			State.bBlendEnabled = false;
			return true;
		}
		if (V == "Opaque")
		{
			State.bBlendEnabled = false;
			State.SrcBlend = ERHIBlendFactor::One;
			State.DstBlend = ERHIBlendFactor::Zero;
			return true;
		}
		if (V == "Alpha")
		{
			State.bBlendEnabled = true;
			State.SrcBlend = ERHIBlendFactor::SrcAlpha;
			State.DstBlend = ERHIBlendFactor::OneMinusSrcAlpha;
			return true;
		}
		if (V == "Additive")
		{
			State.bBlendEnabled = true;
			State.SrcBlend = ERHIBlendFactor::One;
			State.DstBlend = ERHIBlendFactor::One;
			return true;
		}
		// Manual: "Blend SrcFactor DstFactor"
		State.bBlendEnabled = true;
		return true;
	}
	return false;
}

static std::string FNV1a64Hex(const std::string& Input)
{
	std::uint64_t H = 14695981039346656037ULL;
	for (char C : Input)
	{
		H ^= static_cast<std::uint64_t>(static_cast<unsigned char>(C)); H *= 1099511628211ULL;
	}
	char Buf[17];
	std::snprintf(Buf, sizeof(Buf), "%016llX", static_cast<unsigned long long>(H));
	return {Buf};
}

} // namespace

// ═══════════════════════════════════════════
// FShaderParser
// ═══════════════════════════════════════════

std::string FShaderParser::ResolveInclude(const std::string& IncludeName,
                                          const std::vector<std::string>& IncludePaths)
{
	for (const auto& Dir : IncludePaths)
	{
		std::string Candidate = Dir;
		if (!Candidate.empty() && Candidate.back() != '/' && Candidate.back() != '\\')
		{
			Candidate += '/';
		}
		Candidate += IncludeName;
		std::ifstream Test(Candidate);
		if (Test.good())
		{
			return Candidate;
		}
	}
	return {};
}

std::string FShaderParser::PreprocessIncludes(const std::string& Source,
                                              const std::vector<std::string>& IncludePaths)
{
	std::istringstream In(Source);
	std::ostringstream Out;
	std::string Line;
	while (std::getline(In, Line))
	{
		TrimInline(Line);
		if (StartsWith(Line, "#include"))
		{
			auto Start = Line.find('"');
			auto End = Line.rfind('"');
			if (Start != std::string::npos && End != std::string::npos && End > Start)
			{
				std::string IncludeFile = Line.substr(Start + 1, End - Start - 1);
				std::string Resolved = ResolveInclude(IncludeFile, IncludePaths);
				if (!Resolved.empty())
				{
					std::ifstream IncFile(Resolved, std::ios::binary);
					if (IncFile)
					{
						std::ostringstream Ss; Ss << IncFile.rdbuf(); Out << Ss.str() << "\n"; continue;
					}
				}
			}
		}
		Out << Line << '\n';
	}
	return Out.str();
}

void FShaderParser::ParseProperties(const std::string& Source, std::size_t& Pos,
                                    std::vector<FShaderProperty>& OutProps)
{
	for (; Pos < Source.size(); ++Pos)
	{
		if (Source[Pos] == '}')
		{
			return;
		}

		// Skip whitespace
		if (std::isspace(static_cast<unsigned char>(Source[Pos])))
		{
			continue;
		}

		// Read name until '('
		std::size_t NameStart = Pos;
		while (Pos < Source.size() && Source[Pos] != '(' && Source[Pos] != '\n')
		{
			++Pos;
		}
		std::string Name(Source, NameStart, Pos - NameStart);
		while (!Name.empty() && std::isspace(static_cast<unsigned char>(Name.back())))
		{
			Name.pop_back();
		}
		while (!Name.empty() && std::isspace(static_cast<unsigned char>(Name.front())))
		{
			Name.erase(0, 1);
		}
		if (Name.empty() || Name[0] != '_')
		{
			while (Pos < Source.size() && Source[Pos] != '\n')
			{
				++Pos;
			}
			continue;
		}

		// Read display name in quotes
		if (Pos >= Source.size() || Source[Pos] != '(')
		{
			continue;
		}
		++Pos; // skip '('
		while (Pos < Source.size() && Source[Pos] == ' ')
		{
			++Pos;
		}
		std::size_t DisplayStart = 0;
		if (Pos < Source.size() && Source[Pos] == '"')
		{
			++Pos;
			DisplayStart = Pos;
			while (Pos < Source.size() && Source[Pos] != '"')
			{
				++Pos;
			}
			++Pos; // skip closing quote
		}
		std::string DisplayName(Source, DisplayStart, Pos - DisplayStart - (DisplayStart > 0 ? 1 : 0));

		// Skip ',' and whitespace
		while (Pos < Source.size() && (Source[Pos] == ',' || Source[Pos] == ' '))
		{
			++Pos;
		}

		// Read type
		std::size_t TypeStart = Pos;
		while (Pos < Source.size() && Source[Pos] != ')' && Source[Pos] != '\n')
		{
			++Pos;
		}
		std::string TypeStr(Source, TypeStart, Pos - TypeStart);
		while (!TypeStr.empty() && std::isspace(static_cast<unsigned char>(TypeStr.back())))
		{
			TypeStr.pop_back();
		}
		TypeStr = NormalizePropertyType(TypeStr);

		FShaderProperty Prop;
		Prop.Name = Name;
		Prop.DisplayName = DisplayName;

		if (TypeStr == "color")
		{
			Prop.Type = EShaderPropertyType::Color;
		}
		else if (TypeStr == "float")
		{
			Prop.Type = EShaderPropertyType::Float, Prop.DefaultFloat[0] = 1.0f;
		}
		else if (TypeStr == "int")
		{
			Prop.Type = EShaderPropertyType::Int, Prop.DefaultFloat[0] = 1.0f;
		}
		else if (TypeStr == "2d")
		{
			Prop.Type = EShaderPropertyType::Texture2D;
		}
		else if (TypeStr == "cube")
		{
			Prop.Type = EShaderPropertyType::TextureCube;
		}
		else if (StartsWith(TypeStr, "range"))
		{
			Prop.Type = EShaderPropertyType::Range, Prop.MinValue = 0.f, Prop.MaxValue = 1.f;
		}

		OutProps.push_back(Prop);
		if (Pos < Source.size() && Source[Pos] == ')')
		{
			++Pos;
		}
	}
}

void FShaderParser::ParseSubShader(const std::string& Source, std::size_t& Pos,
                                   FShaderFile& OutFile,
                                   const std::vector<std::string>& IncludePaths)
{
	// Apply global render state from subshader block
	FShaderRenderState SubState = OutFile.GlobalRenderState;

	for (; Pos < Source.size(); ++Pos)
	{
		if (Source[Pos] == '}')
		{
			return;
		}
		if (std::isspace(static_cast<unsigned char>(Source[Pos])))
		{
			continue;
		}

		// Read line
		std::size_t LineStart = Pos;
		while (Pos < Source.size() && Source[Pos] != '\n')
		{
			++Pos;
		}
		std::string Line(Source, LineStart, Pos - LineStart);
		TrimInline(Line);
		if (Line.empty())
		{
			continue;
		}

		// Render state lines
		ParseRenderStateLine(Line, SubState);

		// Pass block
		if (StartsWith(Line, "Pass"))
		{
			// Read pass name
			std::size_t NameStart = Line.find('"');
			std::size_t NameEnd = Line.find('"', NameStart + 1);
			std::string PassName = "BasePass";
			if (NameStart != std::string::npos && NameEnd != std::string::npos)
			{
				PassName = Line.substr(NameStart + 1, NameEnd - NameStart - 1);
			}

			// Find opening brace
			while (Pos < Source.size() && Source[Pos] != '{')
			{
				++Pos;
			}
			++Pos; // skip '{'

			// Read pass body
			const std::size_t PassBodyStart = Pos;  // saved — first char after '{'
			std::size_t LineBodyStart = Pos;         // tracks start of current line within body
			std::int32_t BraceDepth = 1;
			FShaderRenderState PassState = SubState;
			std::string PragmaVert, PragmaFrag;

			while (Pos < Source.size() && BraceDepth > 0)
			{
				if (Source[Pos] == '{')
				{
					++BraceDepth;
				}
				else if (Source[Pos] == '}')
				{
					--BraceDepth;
				}
				if (BraceDepth == 0)
				{
					break;
				}
				if (Source[Pos] == '\n')
				{
					std::string CurLine(Source, LineBodyStart, Pos - LineBodyStart);
					TrimInline(CurLine);
					ParseRenderStateLine(CurLine, PassState);
					if (StartsWith(CurLine, "#pragma vertex"))
					{
						auto SpacePos = CurLine.find_first_of(" \t", 14);
						PragmaVert = (SpacePos != std::string::npos) ? CurLine.substr(SpacePos+1) : "";
					}
					else if (StartsWith(CurLine, "#pragma fragment"))
					{
						auto SpacePos = CurLine.find_first_of(" \t", 16);
						PragmaFrag = (SpacePos != std::string::npos) ? CurLine.substr(SpacePos+1) : "";
					}
					LineBodyStart = Pos + 1;
				}
				++Pos;
			}
			// Pos is at '}' (break prevents ++Pos) — body spans [PassBodyStart, Pos)

			// Extract pass GLSL source and strip pragma + comment lines
			std::string PassGLSL(Source, PassBodyStart, Pos - PassBodyStart);

			// Strip #pragma and // comment lines, plus trailing comments on code lines.
			{
				std::string Stripped;
				Stripped.reserve(PassGLSL.size());
				std::size_t LineStart = 0;
				for (std::size_t I = 0; I <= PassGLSL.size(); ++I)
				{
					if (I == PassGLSL.size() || PassGLSL[I] == '\n')
					{
						std::string Line(PassGLSL, LineStart, I - LineStart);
						std::string Trimmed = Line; TrimInline(Trimmed);
						if (!Trimmed.empty() && !StartsWith(Trimmed, "//") &&
						    (!StartsWith(Trimmed, "#pragma ") || StartsWith(Trimmed, "#pragma include")))
						{
							// Strip // trailing comments from code lines
							auto CommentPos = Line.find("//");
							if (CommentPos != std::string::npos)
							{
								Line = Line.substr(0, CommentPos);
							}
							Stripped.append(Line);
							if (I < PassGLSL.size())
							{
								Stripped += '\n';
							}
						}
						LineStart = I + 1;
					}
				}
				PassGLSL = std::move(Stripped);
			}

			// Preprocess includes + inject uniforms
			PassGLSL = PreprocessIncludes(PassGLSL, IncludePaths);

			// Insert MaterialUBO after #version / #extension preamble
			{
				// Find the actual #version line (it may not be first after preprocessor).
				std::size_t VersionPos = PassGLSL.find("#version");
				std::size_t InsertPos = 0;
				if (VersionPos != std::string::npos)
				{
					InsertPos = PassGLSL.find('\n', VersionPos);
					if (InsertPos == std::string::npos)
					{
						InsertPos = 0;
					}
					else
					{
						InsertPos = InsertPos + 1;
					}
				}
				// Skip any #extension lines
				while (InsertPos < PassGLSL.size() && PassGLSL.compare(InsertPos, 10, "#extension") == 0)
				{
					std::size_t NL = PassGLSL.find('\n', InsertPos);
					if (NL == std::string::npos)
					{
						break;
					}
					InsertPos = NL + 1;
				}
				std::string MaterialUBO = GenerateMaterialUniforms(OutFile.Properties);
				if (!MaterialUBO.empty())
				{
					PassGLSL.insert(InsertPos, MaterialUBO + "\n");
				}
			}

		// Parse v2f + vertex inputs
		FShaderPassSource PS;
		PS.PassName = PassName;
		PS.VertEntry = PragmaVert;
		PS.FragEntry = PragmaFrag;
		PS.RenderState = PassState;

		ParseCombinedStructs(PassGLSL, PragmaVert, PragmaFrag, PS.VertexInputs, PS.GlslSource, PS.VaryingCount);
		ParseVertexInputs(PS.GlslSource, PragmaVert, PS.VertexInputs, PS.GlslSource);

			OutFile.Passes.push_back(std::move(PS));
		}
	}
}

void FShaderParser::ParseCombinedStructs(const std::string& PassSource,
                                         const std::string& VertEntry,
                                         const std::string& FragEntry,
                                         std::vector<FShaderVertexAttribute>& OutAttrs,
                                         std::string& OutGLSL,
                                         std::uint32_t& OutVaryingCount)
{
	OutVaryingCount = 0;
	OutGLSL = PassSource;
	std::string FragOutputInjection; // populated in step 3b below

	// ── 1. Parse a2v struct ──
	struct FA2VMember { std::string Type; std::string Name; int Location; };
	std::vector<FA2VMember> A2VMembers;

	auto A2vPos = OutGLSL.find("struct a2v");
	if (A2vPos != std::string::npos)
	{
		auto BraceOpen = OutGLSL.find('{', A2vPos);
		auto BraceClose = OutGLSL.find('}', BraceOpen);
		if (BraceOpen != std::string::npos && BraceClose != std::string::npos)
		{
			std::string Body(OutGLSL, BraceOpen + 1, BraceClose - BraceOpen - 1);
			std::istringstream In(Body);
			std::string Line;
			while (std::getline(In, Line))
			{
				TrimInline(Line);
				if (Line.empty())
				{
					continue;
				}
				if (StartsWith(Line, "//"))
				{
					continue;
				}
				auto Colon = Line.rfind(':');
				auto Semi = Line.rfind(';');
				if (Colon == std::string::npos || Semi == std::string::npos || Colon > Semi)
				{
					continue;
				}
				std::string Left(Line, 0, Colon); TrimInline(Left);
				auto LastSpace = Left.rfind(' ');
				if (LastSpace == std::string::npos)
				{
					continue;
				}
				std::string MemberType(Left, 0, LastSpace);
				std::string MemberName(Left, LastSpace + 1);
				while (!MemberType.empty() && std::isspace(static_cast<unsigned char>(MemberType.back())))
				{
					MemberType.pop_back();
				}
				std::string Semantic(Line, Colon + 1, Semi - Colon - 1); TrimInline(Semantic);

				int Loc = static_cast<int>(A2VMembers.size());
				for (const auto& SI : SemanticTable)
				{
					if (Semantic == SI.Name)
					{
						Loc = SI.Location;
						break;
					}
				}

				FA2VMember M{MemberType, MemberName, Loc};
				A2VMembers.push_back(M);

				FShaderVertexAttribute Attr;
				Attr.SemanticName = Semantic;
				Attr.Location = Loc;
				Attr.Binding = 0;
				OutAttrs.push_back(Attr);
			}

			// Strip struct a2v { ... }; from source
			std::size_t A2vEnd = BraceClose + 1;
			while (A2vEnd < OutGLSL.size() && std::isspace(static_cast<unsigned char>(OutGLSL[A2vEnd])))
			{
				++A2vEnd;
			}
			if (A2vEnd < OutGLSL.size() && OutGLSL[A2vEnd] == ';')
			{
				++A2vEnd;
			}
			std::string Stripped;
			Stripped.reserve(OutGLSL.size());
			Stripped = OutGLSL.substr(0, A2vPos);
			Stripped.append(OutGLSL, A2vEnd, std::string::npos);
			OutGLSL = std::move(Stripped);

			// Strip "in a2v v" from vert_main
			{
				for (const char* T : {"in a2v v,", "in a2v v )", "in a2v v)"})
				{
					auto Hit = OutGLSL.find(T);
					if (Hit != std::string::npos)
					{
						int N = (int)std::char_traits<char>::length(T);
						int EraseN = N;            // default: erase full match (removes comma)
						if (N >= 2 && T[N-2] == ' ' && T[N-1] == ')')
						{
							EraseN = N - 2; // keep " )"
						}
						else if (T[N-1] == ')')
						{
							EraseN = N - 1; // keep ")"
						}
						OutGLSL.erase(Hit, EraseN);
						break;
					}
				}
			}
		}
	}

	// ── 2. Parse v2f struct ──
	struct FV2FMember { std::string Type; std::string Name; std::string Semantic; bool bSVPosition; };
	std::vector<FV2FMember> V2FMembers;

	auto V2fPos = OutGLSL.find("struct v2f");
	if (V2fPos != std::string::npos)
	{
		auto BraceOpen = OutGLSL.find('{', V2fPos);
		auto BraceClose = OutGLSL.find('}', BraceOpen);
		if (BraceOpen != std::string::npos && BraceClose != std::string::npos)
		{
			std::string Body(OutGLSL, BraceOpen + 1, BraceClose - BraceOpen - 1);
			std::istringstream In(Body);
			std::string Line;
			while (std::getline(In, Line))
			{
				TrimInline(Line);
				if (Line.empty())
				{
					continue;
				}
				if (StartsWith(Line, "//"))
				{
					continue;
				}
				auto Colon = Line.rfind(':');
				auto Semi = Line.rfind(';');
				if (Colon == std::string::npos || Semi == std::string::npos || Colon > Semi)
				{
					continue;
				}
				std::string Left(Line, 0, Colon); TrimInline(Left);
				auto LastSpace = Left.rfind(' ');
				if (LastSpace == std::string::npos)
				{
					continue;
				}
				std::string MemberType(Left, 0, LastSpace);
				std::string MemberName(Left, LastSpace + 1);
				while (!MemberType.empty() && std::isspace(static_cast<unsigned char>(MemberType.back())))
				{
					MemberType.pop_back();
				}
				std::string Semantic(Line, Colon + 1, Semi - Colon - 1); TrimInline(Semantic);
				V2FMembers.push_back({MemberType, MemberName, Semantic, Semantic == "SV_POSITION"});
				if (Semantic != "SV_POSITION")
				{
					++OutVaryingCount;
				}
			}

			// Strip struct v2f { ... }; from source
			std::size_t V2fEnd = BraceClose + 1;
			while (V2fEnd < OutGLSL.size() && std::isspace(static_cast<unsigned char>(OutGLSL[V2fEnd])))
			{
				++V2fEnd;
			}
			if (V2fEnd < OutGLSL.size() && OutGLSL[V2fEnd] == ';')
			{
				++V2fEnd;
			}
			std::string Stripped;
			Stripped.reserve(OutGLSL.size());
			Stripped = OutGLSL.substr(0, V2fPos);
			Stripped.append(OutGLSL, V2fEnd, std::string::npos);
			OutGLSL = std::move(Stripped);

			// Strip "out v2f o" from vert_main
			for (const char* T : {"out v2f o,", "out v2f o )", "out v2f o)"})
			{
				auto Hit = OutGLSL.find(T);
				if (Hit != std::string::npos)
				{
					int N = (int)std::char_traits<char>::length(T);
					int EraseN = N;
					if (N >= 2 && T[N-2] == ' ' && T[N-1] == ')')
					{
						EraseN = N - 2;
					}
					else if (T[N-1] == ')')
					{
						EraseN = N - 1;
					}
					OutGLSL.erase(Hit, EraseN);
					break;
				}
			}
			// Strip "in v2f i" from frag_main
			for (const char* T : {"in v2f i,", "in v2f i )", "in v2f i)"})
			{
				auto Hit = OutGLSL.find(T);
				if (Hit != std::string::npos)
				{
					int N = (int)std::char_traits<char>::length(T);
					int EraseN = N;
					if (N >= 2 && T[N-2] == ' ' && T[N-1] == ')')
					{
						EraseN = N - 2;
					}
					else if (T[N-1] == ')')
					{
						EraseN = N - 1;
					}
					OutGLSL.erase(Hit, EraseN);
					break;
				}
			}
		}
	}

	// ── 3. Strip fragment output semantics (: COLOR0 etc) from frag_main ──
	for (const char* S : {": COLOR0", ": COLOR1", ": COLOR2", ": COLOR3", ": COLOR4", ": COLOR5", ": COLOR6", ": COLOR7"})
	{
		std::size_t Pos = 0;
		std::size_t Len = std::char_traits<char>::length(S);
		while ((Pos = OutGLSL.find(S, Pos)) != std::string::npos)
		{
			OutGLSL.erase(Pos, Len);
		}
	}

	// ── 3b. Strip fragment output parameters (out vec4 name) from frag_main ──
	// GLSL frag main() must be void; convert to global layout(location=N) out.
	{
		auto FragPos = OutGLSL.find("void frag_main(");
		if (FragPos == std::string::npos)
		{
			FragPos = OutGLSL.find("frag_main(");
		}
		if (FragPos != std::string::npos)
		{
			auto ParenOpen = OutGLSL.find('(', FragPos);
			auto ParenClose = OutGLSL.find(')', ParenOpen);
			if (ParenOpen != std::string::npos && ParenClose != std::string::npos)
			{
				std::string Params = OutGLSL.substr(ParenOpen + 1, ParenClose - ParenOpen - 1);
				std::string CleanParams;
				std::string FragOutputDecls;
				int OutLoc = 0;
				std::istringstream In(Params);
				std::string Token;
				std::vector<std::string> Tokens;
				while (In >> Token)
				{
					Tokens.push_back(Token);
				}

				for (std::size_t I = 0; I < Tokens.size(); ++I)
				{
					if (Tokens[I] == "out" && I + 2 < Tokens.size() && Tokens[I+2] != "out")
					{
						std::string OutType = Tokens[I+1];
						std::string OutName = Tokens[I+2];
						// Strip trailing "," from name if present
						if (!OutName.empty() && OutName.back() == ',')
						{
							OutName.pop_back();
						}
						FragOutputDecls += "layout(location=" + std::to_string(OutLoc++) + ") out " + OutType + " " + OutName + ";\n";
						// Also remove the trailing comma if any
						if (I + 3 < Tokens.size() && Tokens[I+3] == ",")
						{
							I += 3; // skip out, type, name, comma
						}
						else
						{
							I += 2; // skip out, type, name
						}
					}
					else
					{
						if (!CleanParams.empty())
						{
							CleanParams += " ";
						}
						CleanParams += Tokens[I];
					}
				}

				// Rebuild signature
				OutGLSL.replace(ParenOpen + 1, ParenClose - ParenOpen - 1, CleanParams);
				// Append global output declarations to Injection later; store temporarily
				FragOutputInjection = FragOutputDecls;
			}
		}
	}

	// ── 4. Clean up stray commas left after param removal ──
	// Replace ", )" → " )", ",\n)" → "\n)", ",\n\t)" → "\n\t)", ", \n)" → " \n)"
	for (const char* Pat : {", )", ",\n)"})
	{
		std::size_t Pos = 0;
		while ((Pos = OutGLSL.find(Pat, Pos)) != std::string::npos)
		{
			OutGLSL.replace(Pos, 2, " )");
		}
	}
	for (const char* Pat : {",\n\t)", ",\n )"})
	{
		std::size_t Pos = 0;
		while ((Pos = OutGLSL.find(Pat, Pos)) != std::string::npos)
		{
			OutGLSL.replace(Pos, 3, "\n )");
		}
	}

	// ── 4. Replace member references ──
	// Find function body ranges by brace matching
	auto FindFuncBody = [&](const std::string& FuncName) -> std::pair<std::size_t, std::size_t>
	{
		auto FuncPos = OutGLSL.find(FuncName + "(");
		if (FuncPos == std::string::npos)
		{
			FuncPos = OutGLSL.find(FuncName + " (");
		}
		if (FuncPos == std::string::npos)
		{
			return {std::string::npos, std::string::npos};
		}

		auto BraceOpen = OutGLSL.find('{', FuncPos);
		if (BraceOpen == std::string::npos)
		{
			return {std::string::npos, std::string::npos};
		}

		// Match braces
		int Depth = 1;
		std::size_t I = BraceOpen + 1;
		for (; I < OutGLSL.size() && Depth > 0; ++I)
		{
			if (OutGLSL[I] == '{')
			{
				++Depth;
			}
			else if (OutGLSL[I] == '}')
			{
				--Depth;
			}
		}
		return {BraceOpen + 1, I - 1}; // body start, body end (exclusive of braces)
	};

	auto VertBody = FindFuncBody(VertEntry);  // "vert_main"
	auto FragBody = FindFuncBody(FragEntry);  // "frag_main"

	// Replace in vertex body: v.member → a2v_member
	for (const auto& M : A2VMembers)
	{
		std::string Pattern = "v." + M.Name;
		std::string Replacement = "a2v_" + M.Name;
		std::size_t Pos = VertBody.first;
		while (Pos < VertBody.second && (Pos = OutGLSL.find(Pattern, Pos)) != std::string::npos && Pos < VertBody.second)
		{
			OutGLSL.replace(Pos, Pattern.size(), Replacement);
			Pos += Replacement.size();
		}
	}

	// Replace in vertex body: o.member → gl_Position or v2f_member
	// Replace in fragment body: i.member → v2f_member
	for (const auto& M : V2FMembers)
	{
		if (M.bSVPosition)
		{
			// o.clipPos → gl_Position (vertex body only)
			std::string Pattern = "o." + M.Name;
			std::size_t Pos = VertBody.first;
			while (Pos < VertBody.second && (Pos = OutGLSL.find(Pattern, Pos)) != std::string::npos && Pos < VertBody.second)
			{
				OutGLSL.replace(Pos, Pattern.size(), "gl_Position");
				Pos += 12;
			}
		}
		else
		{
			std::string Replacement = "v2f_" + M.Name;
			// o.color → v2f_color (vertex body)
			{
				std::string Pattern = "o." + M.Name;
				std::size_t Pos = VertBody.first;
				while (Pos < VertBody.second && (Pos = OutGLSL.find(Pattern, Pos)) != std::string::npos && Pos < VertBody.second)
				{
					OutGLSL.replace(Pos, Pattern.size(), Replacement);
					Pos += Replacement.size();
				}
			}
			// i.color → v2f_color (fragment body)
			{
				std::string Pattern = "i." + M.Name;
				std::size_t Pos = FragBody.first;
				while (Pos < FragBody.second && (Pos = OutGLSL.find(Pattern, Pos)) != std::string::npos && Pos < FragBody.second)
				{
					OutGLSL.replace(Pos, Pattern.size(), Replacement);
					Pos += Replacement.size();
				}
			}
		}
	}

	// ── 5. Inject all declarations between #version/#extension block and function definitions ──
	std::string Injection;

	// Vertex input declarations
	if (!A2VMembers.empty())
	{
		Injection += "// --- injected vertex inputs (a2v) ---\n";
		Injection += "#ifdef MAHO_SHADER_STAGE_VERTEX\n";
		for (const auto& M : A2VMembers)
		{
			Injection += "layout(location=" + std::to_string(M.Location) + ") in " + M.Type + " a2v_" + M.Name + ";\n";
		}
		Injection += "#endif // MAHO_SHADER_STAGE_VERTEX\n\n";
	}

	// v2f inter-stage declarations
	if (!V2FMembers.empty())
	{
		Injection += "// --- injected v2f inter-stage (vertex output) ---\n";
		Injection += "#ifdef MAHO_SHADER_STAGE_VERTEX\n";
		int Loc = 0;
		for (const auto& M : V2FMembers)
		{
			if (!M.bSVPosition)
			{
				Injection += "layout(location=" + std::to_string(Loc++) + ") out " + M.Type + " v2f_" + M.Name + ";\n";
			}
		}
		Injection += "#endif // MAHO_SHADER_STAGE_VERTEX\n\n";
		Injection += "// --- injected v2f inter-stage (fragment input) ---\n";
		Injection += "#ifdef MAHO_SHADER_STAGE_FRAGMENT\n";
		Loc = 0;
		for (const auto& M : V2FMembers)
		{
			if (!M.bSVPosition)
			{
				Injection += "layout(location=" + std::to_string(Loc++) + ") in " + M.Type + " v2f_" + M.Name + ";\n";
			}
		}
		Injection += "#endif // MAHO_SHADER_STAGE_FRAGMENT\n\n";
	}

	// Fragment output declarations from frag_main out params
	if (!FragOutputInjection.empty())
	{
		Injection += "// --- fragment output declarations ---\n";
		Injection += "#ifdef MAHO_SHADER_STAGE_FRAGMENT\n";
		Injection += FragOutputInjection;
		Injection += "#endif // MAHO_SHADER_STAGE_FRAGMENT\n\n";
	}

	if (!Injection.empty())
	{
		// Find the position after #version / #extension block to inject declarations
		std::size_t InsertPos = 0;
		auto VerPos = OutGLSL.find("#version");
		if (VerPos != std::string::npos)
		{
			InsertPos = OutGLSL.find('\n', VerPos);
			if (InsertPos != std::string::npos)
			{
				InsertPos = InsertPos + 1;
			}
		}
		while (InsertPos < OutGLSL.size() && OutGLSL.compare(InsertPos, 10, "#extension") == 0)
		{
			auto NL = OutGLSL.find('\n', InsertPos);
			if (NL == std::string::npos)
			{
				break;
			}
			InsertPos = NL + 1;
		}
		OutGLSL.insert(InsertPos, Injection);
	}

	// ── Wrap functions with stage #ifdef guards ──
	// Both vert_main and frag_main are in the same source, but only one set of
	// declarations (a2v inputs, v2f outputs/inputs) is active per compile stage.
	// Wrap each function so the other stage's compiler doesn't see undeclared refs.
	for (const auto* Entry : {VertEntry.c_str(), FragEntry.c_str()})
	{
		const char* Guard = (std::string(Entry) == VertEntry) ? "MAHO_SHADER_STAGE_VERTEX" : "MAHO_SHADER_STAGE_FRAGMENT";
		std::string Search = std::string(Entry) + "(";
		auto FuncPos = OutGLSL.find(Search);
		if (FuncPos == std::string::npos)
		{
			Search = std::string(Entry) + " ("; FuncPos = OutGLSL.find(Search);
		}
		if (FuncPos == std::string::npos)
		{
			continue;
		}

		// Find matching closing brace
		auto Open = OutGLSL.find('{', FuncPos);
		if (Open == std::string::npos)
		{
			continue;
		}
		int Depth = 1;
		std::size_t Close = Open + 1;
		for (; Close < OutGLSL.size() && Depth > 0; ++Close)
		{
			if (OutGLSL[Close] == '{')
			{
				++Depth;
			}
			else if (OutGLSL[Close] == '}')
			{
				--Depth;
			}
		}
		if (Depth != 0)
		{
			continue; // unmatched braces
		}

		// Inject guard after the closing brace
		std::string GuardClose = "\n#endif // ";
		GuardClose += Guard;
		GuardClose += "\n";
		OutGLSL.insert(Close, GuardClose);

		// Inject guard before the function, at the start of its line
		auto LineStart = OutGLSL.rfind('\n', FuncPos);
		if (LineStart == std::string::npos)
		{
			LineStart = 0;
		}
		else
		{
			LineStart += 1; // after \n
		}
		std::string GuardOpen = "#ifdef ";
		GuardOpen += Guard;
		GuardOpen += "\n";
		OutGLSL.insert(LineStart, GuardOpen);
	}
}

void FShaderParser::ParseA2VStruct(const std::string& PassSource,
                                    std::vector<FShaderVertexAttribute>& OutAttrs,
                                    std::string& OutInjectedGLSL)
{
	// Find "struct a2v"
	auto StructPos = PassSource.find("struct a2v");
	if (StructPos == std::string::npos)
	{
		return;
	}

	auto BraceOpen = PassSource.find('{', StructPos);
	auto BraceClose = PassSource.find('}', BraceOpen);
	if (BraceOpen == std::string::npos || BraceClose == std::string::npos)
	{
		return;
	}

	// Parse struct body: each member is "type name : SEMANTIC;"
	std::string Body(PassSource, BraceOpen + 1, BraceClose - BraceOpen - 1);
	std::istringstream In(Body);
	std::string Line;
	std::ostringstream VertexInputGLSL;
	VertexInputGLSL << "// --- injected vertex inputs (a2v) ---\n";

	// Find "struct a2v" end (semicolon after closing brace)
	std::size_t StructEnd = BraceClose + 1;
	while (StructEnd < PassSource.size() && std::isspace(static_cast<unsigned char>(PassSource[StructEnd])))
	{
		++StructEnd;
	}
	if (StructEnd < PassSource.size() && PassSource[StructEnd] == ';')
	{
		++StructEnd;
	}

	// Strip struct a2v { ... }; block from source (build into temp to avoid self-alias)
	std::string Stripped;
	Stripped.reserve(PassSource.size());
	Stripped = PassSource.substr(0, StructPos);
	Stripped.append(PassSource, StructEnd, std::string::npos);
	OutInjectedGLSL = std::move(Stripped);

	std::vector<std::string> MemberNames;

	while (std::getline(In, Line))
	{
		TrimInline(Line);
		if (Line.empty())
		{
			continue;
		}

		auto Colon = Line.rfind(':');
		auto Semi = Line.rfind(';');
		if (Colon == std::string::npos || Semi == std::string::npos)
		{
			continue;
		}
		if (Colon > Semi)
		{
			continue;
		}

		std::string Left(Line, 0, Colon);
		TrimInline(Left);
		auto LastSpace = Left.rfind(' ');
		if (LastSpace == std::string::npos)
		{
			continue;
		}
		std::string MemberType(Left, 0, LastSpace);
		std::string MemberName(Left, LastSpace + 1);
		while (!MemberType.empty() && std::isspace(static_cast<unsigned char>(MemberType.back())))
		{
			MemberType.pop_back();
		}

		std::string Semantic(Line, Colon + 1, Semi - Colon - 1);
		TrimInline(Semantic);

		MemberNames.push_back(MemberName);

		for (const auto& SI : SemanticTable)
		{
			if (Semantic == SI.Name)
			{
				FShaderVertexAttribute Attr;
				Attr.SemanticName = SI.Name;
				Attr.Semantic = SI.Semantic;
				Attr.Location = SI.Location;
				Attr.Binding = SI.Binding;
				Attr.Format = SI.Format;
				OutAttrs.push_back(Attr);

				VertexInputGLSL << "layout(location=" << SI.Location
				                << ") in " << MemberType
				                << " a2v_" << MemberName << ";\n";
				break;
			}
		}
	}

	// Strip the "in a2v v" parameter from vert_main (if present)
	{
		std::string PatternA("in a2v v,");
		auto Hit = OutInjectedGLSL.find(PatternA);
		if (Hit != std::string::npos)
		{
			OutInjectedGLSL.erase(Hit, PatternA.size());
		}
		else
		{
			std::string PatternB("in a2v v)");
			Hit = OutInjectedGLSL.find(PatternB);
			if (Hit != std::string::npos)
			{
				OutInjectedGLSL.erase(Hit, PatternB.size() - 1); // keep the ')'
			}
			else
			{
				PatternB = "in a2v v )";
				Hit = OutInjectedGLSL.find(PatternB);
				if (Hit != std::string::npos)
				{
					OutInjectedGLSL.erase(Hit, PatternB.size() - 2); // keep " )"
				}
			}
		}
	}

	// Inject vertex input globals BEFORE vert_main (global scope), guarded for stage
	auto VertMainPos = OutInjectedGLSL.find("vert_main(");
	if (VertMainPos == std::string::npos)
	{
		VertMainPos = OutInjectedGLSL.find("vert_main (");
	}
	if (VertMainPos != std::string::npos)
	{
		std::string GuardedInputs;
		GuardedInputs += "#ifdef MAHO_SHADER_STAGE_VERTEX\n";
		GuardedInputs += VertexInputGLSL.str();
		GuardedInputs += "#endif // MAHO_SHADER_STAGE_VERTEX\n\n";
		OutInjectedGLSL.insert(VertMainPos, GuardedInputs);
	}

	// Replace v.memberName → a2v_memberName in vertex body (after "vert_main" and before "frag_main")
	auto FragMainPos = OutInjectedGLSL.find("frag_main");
	auto VertBodyEnd = (FragMainPos != std::string::npos) ? FragMainPos : OutInjectedGLSL.size();

	for (const auto& Name : MemberNames)
	{
		std::string Pattern = "v." + Name;
		std::string Replacement = "a2v_" + Name;
		std::size_t SearchFrom = VertMainPos;
		while (true)
		{
			auto Hit = OutInjectedGLSL.find(Pattern, SearchFrom);
			if (Hit == std::string::npos)
			{
				break;
			}
			if (Hit >= VertBodyEnd)
			{
				break;
			}
			OutInjectedGLSL.replace(Hit, Pattern.size(), Replacement);
			SearchFrom = Hit + Replacement.size();
		}
	}
}

void FShaderParser::ParseV2FStruct(const std::string& PassSource,
                                   std::string& OutInjectedGLSL,
                                   std::uint32_t& OutVaryingCount)
{
	OutVaryingCount = 0;

	// Find "struct v2f"
	auto StructPos = PassSource.find("struct v2f");
	if (StructPos == std::string::npos)
	{
		OutInjectedGLSL = PassSource; return;
	}

	auto BraceOpen = PassSource.find('{', StructPos);
	auto BraceClose = PassSource.find('}', BraceOpen);
	if (BraceOpen == std::string::npos || BraceClose == std::string::npos)
	{
		OutInjectedGLSL = PassSource; return;
	}

	// Find end of "struct v2f { ... };"
	std::size_t StructEnd = BraceClose + 1;
	while (StructEnd < PassSource.size() && std::isspace(static_cast<unsigned char>(PassSource[StructEnd])))
	{
		++StructEnd;
	}
	if (StructEnd < PassSource.size() && PassSource[StructEnd] == ';')
	{
		++StructEnd;
	}

	// Strip the struct block from source (temp to avoid self-alias)
	std::string Stripped;
	Stripped.reserve(PassSource.size());
	Stripped = PassSource.substr(0, StructPos);
	Stripped.append(PassSource, StructEnd, std::string::npos);
	OutInjectedGLSL = std::move(Stripped);

	// Parse struct body — each member: "type name : SEMANTIC;"
	std::string Body(PassSource, BraceOpen + 1, BraceClose - BraceOpen - 1);
	std::istringstream In(Body);
	std::string Line;
	std::uint32_t VaryingLoc = 0;
	std::ostringstream VsOutInjected;
	std::ostringstream FsInInjected;

	struct FV2FMember
	{
		std::string Type;
		std::string Name;
		std::string Semantic;
		bool bIsSVPosition = false;
	};

	std::vector<FV2FMember> Members;

	while (std::getline(In, Line))
	{
		TrimInline(Line);
		if (Line.empty())
		{
			continue;
		}

		// Find ':' before ';'
		auto Colon = Line.rfind(':');
		auto Semi = Line.rfind(';');
		if (Colon == std::string::npos || Semi == std::string::npos)
		{
			continue;
		}
		if (Colon > Semi)
		{
			continue;
		}

		std::string Left(Line, 0, Colon);
		TrimInline(Left);
		auto LastSpace = Left.rfind(' ');
		if (LastSpace == std::string::npos)
		{
			continue;
		}
		std::string MemberType(Left, 0, LastSpace);
		std::string MemberName(Left, LastSpace + 1);
		while (!MemberType.empty() && std::isspace(static_cast<unsigned char>(MemberType.back())))
		{
			MemberType.pop_back();
		}

		std::string Semantic(Line, Colon + 1, Semi - Colon - 1);
		TrimInline(Semantic);

		FV2FMember M;
		M.Type = MemberType;
		M.Name = MemberName;
		M.Semantic = Semantic;

		if (Semantic == "SV_POSITION")
		{
			M.bIsSVPosition = true;
		}
		else
		{
			VsOutInjected << "layout(location=" << VaryingLoc << ") out " << MemberType << " v2f_" << MemberName << ";\n";
			FsInInjected  << "layout(location=" << VaryingLoc << ") in "  << MemberType << " v2f_" << MemberName << ";\n";
		}

		Members.push_back(M);
		if (!M.bIsSVPosition)
		{
			++VaryingLoc;
		}
	}

	OutVaryingCount = VaryingLoc;

	// Inject inter-stage declarations before vert_main, guarded by stage.
	{
		std::string Injection;
		Injection += "\n// --- injected v2f inter-stage (vertex output) ---\n";
		Injection += "#ifdef MAHO_SHADER_STAGE_VERTEX\n";
		Injection += VsOutInjected.str();
		Injection += "#endif // MAHO_SHADER_STAGE_VERTEX\n";
		Injection += "\n// --- injected v2f inter-stage (fragment input) ---\n";
		Injection += "#ifdef MAHO_SHADER_STAGE_FRAGMENT\n";
		Injection += FsInInjected.str();
		Injection += "#endif // MAHO_SHADER_STAGE_FRAGMENT\n\n";

		// Insert right before vert_main (so both stages see them)
		auto VertPos = OutInjectedGLSL.find("vert_main(");
		if (VertPos == std::string::npos)
		{
			VertPos = OutInjectedGLSL.find("vert_main (");
		}
		if (VertPos != std::string::npos)
		{
			OutInjectedGLSL.insert(VertPos, Injection);
		}
	}

	// Strip "out v2f o" param from vert_main and "in v2f i" param from frag_main
	{
		// vert_main: strip "out v2f o," or "out v2f o )" or "out v2f o)"
		for (auto Pattern : {"out v2f o,", "out v2f o)", "out v2f o )"})
		{
			auto Hit = OutInjectedGLSL.find(Pattern);
			if (Hit != std::string::npos)
			{
				if (Pattern[9] == ')')
				{
					OutInjectedGLSL.erase(Hit, 9); // "out v2f o" 9 chars
				}
				else if (Pattern[9] == ' ')
				{
					OutInjectedGLSL.erase(Hit, 10); // "out v2f o " 10 chars
				}
				else
				{
					OutInjectedGLSL.erase(Hit, 10); // "out v2f o," 10 chars
				}
				break;
			}
		}

		// frag_main: strip "in v2f i," or "in v2f i)" or "in v2f i )"
		for (auto Pattern : {"in v2f i,", "in v2f i)", "in v2f i )"})
		{
			auto Hit = OutInjectedGLSL.find(Pattern);
			if (Hit != std::string::npos)
			{
				if (Pattern[8] == ')')
				{
					OutInjectedGLSL.erase(Hit, 8); // "in v2f i" 8 chars
				}
				else if (Pattern[8] == ' ')
				{
					OutInjectedGLSL.erase(Hit, 9); // "in v2f i " 9 chars
				}
				else
				{
					OutInjectedGLSL.erase(Hit, 9); // "in v2f i," 9 chars
				}
				break;
			}
		}
	}

	// Replace ALL member references in both stages
	auto FragMainPos = OutInjectedGLSL.find("frag_main");
	if (FragMainPos == std::string::npos)
	{
		FragMainPos = OutInjectedGLSL.find("frag_main ");
	}

	for (const auto& M : Members)
	{
		if (M.bIsSVPosition)
		{
			// SV_POSITION: o.<member> → gl_Position in vertex stage
			std::string Pattern = "o." + M.Name;
			std::size_t SearchFrom = 0;
			while (true)
			{
				auto Hit = OutInjectedGLSL.find(Pattern, SearchFrom);
				if (Hit == std::string::npos)
				{
					break;
				}
				if (FragMainPos != std::string::npos && Hit > FragMainPos)
				{
					break;
				}
				OutInjectedGLSL.replace(Hit, Pattern.size(), "gl_Position");
				SearchFrom = Hit + 12;
			}
		}
		else
		{
			// Non-SV: o.<member> → v2f_<member> in vertex stage
			{
				std::string Pattern = "o." + M.Name;
				std::string Replacement = "v2f_" + M.Name;
				std::size_t SearchFrom = 0;
				while (true)
				{
					auto Hit = OutInjectedGLSL.find(Pattern, SearchFrom);
					if (Hit == std::string::npos)
					{
						break;
					}
					if (FragMainPos != std::string::npos && Hit > FragMainPos)
					{
						break;
					}
					OutInjectedGLSL.replace(Hit, Pattern.size(), Replacement);
					SearchFrom = Hit + Replacement.size();
				}
			}
			// Non-SV: i.<member> → v2f_<member> in fragment stage
			{
				std::string Pattern = "i." + M.Name;
				std::string Replacement = "v2f_" + M.Name;
				std::size_t SearchFrom = (FragMainPos != std::string::npos) ? FragMainPos : 0;
				while (true)
				{
					auto Hit = OutInjectedGLSL.find(Pattern, SearchFrom);
					if (Hit == std::string::npos)
					{
						break;
					}
					OutInjectedGLSL.replace(Hit, Pattern.size(), Replacement);
					SearchFrom = Hit + Replacement.size();
				}
			}
		}
	}

	// Strip ": COLOR0", ": COLOR1" etc. from fragment output parameters
	{
		for (auto Semantic : {": COLOR0", ": COLOR1", ": COLOR2", ": COLOR3",
		                      ": COLOR4", ": COLOR5", ": COLOR6", ": COLOR7"})
		{
			std::size_t SearchFrom = 0;
			while (true)
			{
				auto Hit = OutInjectedGLSL.find(Semantic, SearchFrom);
				if (Hit == std::string::npos)
				{
					break;
				}
				OutInjectedGLSL.erase(Hit, std::strlen(Semantic));
				SearchFrom = Hit;
			}
		}
	}
}

void FShaderParser::ParseVertexInputs(const std::string& PassSource,
                                      const std::string& VertEntry,
                                      std::vector<FShaderVertexAttribute>& OutAttrs,
                                      std::string& OutInjectedGLSL)
{
	// Find vert function signature
	std::string Search = VertEntry + "(";
	auto FuncPos = PassSource.find(Search);
	if (FuncPos == std::string::npos)
	{
		return;
	}

	// Extract params between (...)
	std::size_t ParamStart = FuncPos + Search.size();
	std::size_t ParamEnd = PassSource.find(')', ParamStart);
	if (ParamEnd == std::string::npos)
	{
		return;
	}

	std::string Params(PassSource, ParamStart, ParamEnd - ParamStart);

	std::ostringstream VertexInputGLSL;
	VertexInputGLSL << "// --- injected vertex inputs ---\n";

	// Split by comma; each param may have "in"/"out" direction keyword
	std::size_t Cur = 0;
	while (Cur < Params.size())
	{
		while (Cur < Params.size() && (Params[Cur] == ',' || std::isspace(static_cast<unsigned char>(Params[Cur]))))
		{
			++Cur;
		}
		if (Cur >= Params.size())
		{
			break;
		}

		// Read entire param text until next ',' or end
		std::size_t ParamStart2 = Cur;
		while (Cur < Params.size() && Params[Cur] != ',')
		{
			++Cur;
		}
		std::string ParamText(Params, ParamStart2, Cur - ParamStart2);
		TrimInline(ParamText);

		// Check for "out v2f" — skip, these are inter-stage outputs not vertex inputs
		if (ParamText.find("out v2f") != std::string::npos || ParamText.find("out") == 0)
		{
			continue;
		}

		// Check for "in type name : SEMANTIC"
		// Direction keyword may or may not be present
		std::size_t ColonPos = ParamText.rfind(':');
		if (ColonPos == std::string::npos)
		{
			continue;
		}

		// Semantic after ':'
		std::size_t SemStart = ColonPos + 1;
		while (SemStart < ParamText.size() && std::isspace(static_cast<unsigned char>(ParamText[SemStart])))
		{
			++SemStart;
		}
		std::string Semantic(ParamText, SemStart);

		// Type + name before ':'
		std::string TypeAndName(ParamText, 0, ColonPos);
		TrimInline(TypeAndName);

		// Strip leading "in " if present
		if (StartsWith(TypeAndName, "in "))
		{
			TypeAndName = TypeAndName.substr(3);
		}
		TrimInline(TypeAndName);

		// Extract type and name
		auto LastSpace = TypeAndName.rfind(' ');
		if (LastSpace == std::string::npos)
		{
			continue;
		}
		std::string Type(TypeAndName, 0, LastSpace);
		std::string VarName(TypeAndName, LastSpace + 1);
		while (!Type.empty() && std::isspace(static_cast<unsigned char>(Type.back())))
		{
			Type.pop_back();
		}

		// Match against semantic table
		for (const auto& SI : SemanticTable)
		{
			if (Semantic == SI.Name)
			{
				FShaderVertexAttribute Attr;
				Attr.SemanticName = SI.Name;
				Attr.Semantic = SI.Semantic;
				Attr.Location = SI.Location;
				Attr.Binding = SI.Binding;
				Attr.Format = SI.Format;
				OutAttrs.push_back(Attr);

				VertexInputGLSL << "layout(location=" << SI.Location << ") in " << Type << " " << VarName << ";\n";
				break;
			}
		}
	}

	// Inject vertex input declarations BEFORE function (global scope)
	// Only inject if we found actual vertex inputs (not just the comment header).
	std::string VIGS = VertexInputGLSL.str();
	bool HasDecls = (VIGS.find("layout") != std::string::npos);
	if (HasDecls)
	{
		OutInjectedGLSL.insert(FuncPos, "\n" + VIGS);
	}
}

std::string FShaderParser::GenerateMaterialUniforms(const std::vector<FShaderProperty>& Props)
{
	std::ostringstream Out;
	bool HasUBO = false;
	bool HasSamplers = false;
	std::uint32_t UBOBinding = 0;
	std::uint32_t SamplerBinding = 0;

	for (const auto& P : Props)
	{
		if (P.Type == EShaderPropertyType::Texture2D || P.Type == EShaderPropertyType::TextureCube)
		{
			if (!HasSamplers)
			{
				HasSamplers = true;
			}
		}
		else
		{
			if (!HasUBO)
			{
				HasUBO = true;
				Out << "layout(set=2, binding=" << UBOBinding << ") uniform MaterialUBO\n{\n";
			}
			switch (P.Type)
			{
			case EShaderPropertyType::Color:
				Out << "    vec4 " << P.Name << ";\n"; break;
			case EShaderPropertyType::Float:
			case EShaderPropertyType::Range:
				Out << "    float " << P.Name << ";\n"; break;
			case EShaderPropertyType::Int:
				Out << "    int " << P.Name << ";\n"; break;
			default: break;
			}
		}
	}
	if (HasUBO)
	{
		Out << "} u_Material;\n";
	}

	for (const auto& P : Props)
	{
		if (P.Type == EShaderPropertyType::Texture2D)
		{
			Out << "layout(set=2, binding=" << (++SamplerBinding) << ") uniform sampler2D " << P.Name << ";\n";
		}
		else if (P.Type == EShaderPropertyType::TextureCube)
		{
			Out << "layout(set=2, binding=" << (++SamplerBinding) << ") uniform samplerCube " << P.Name << ";\n";
		}
	}

	return Out.str();
}

FShaderFile FShaderParser::Parse(const std::string& FullPath,
                                 const std::vector<std::string>& IncludePaths)
{
	FShaderFile Result;
	Result.ShaderPath = FullPath;

	std::ifstream File(FullPath, std::ios::binary);
	if (!File)
	{
		return Result;
	}

	std::ostringstream Ss;
	Ss << File.rdbuf();
	std::string Source = Ss.str();

	// Extract Shader "Path" { ... }
	auto ShaderStart = Source.find("Shader");
	if (ShaderStart == std::string::npos)
	{
		return Result;
	}
	auto OpenBrace = Source.find('{', ShaderStart);
	if (OpenBrace == std::string::npos)
	{
		return Result;
	}

	std::size_t Pos = OpenBrace + 1;
	for (; Pos < Source.size(); ++Pos)
	{
		if (Source[Pos] == '}')
		{
			break;
		}
		if (std::isspace(static_cast<unsigned char>(Source[Pos])))
		{
			continue;
		}

		// Read line
		std::size_t LineStart = Pos;
		while (Pos < Source.size() && Source[Pos] != '\n')
		{
			++Pos;
		}
		std::string Line(Source, LineStart, Pos - LineStart);
		TrimInline(Line);

		if (StartsWith(Line, "Properties"))
		{
			while (Pos < Source.size() && Source[Pos] != '{')
			{
				++Pos;
			}
			++Pos;
			ParseProperties(Source, Pos, Result.Properties);
		}
		else if (StartsWith(Line, "SubShader"))
		{
			while (Pos < Source.size() && Source[Pos] != '{')
			{
				++Pos;
			}
			++Pos;
			ParseSubShader(Source, Pos, Result, IncludePaths);
		}
	}
	return Result;
}

// ═══════════════════════════════════════════
// FShaderDatabase
// ═══════════════════════════════════════════

bool FShaderDatabase::LoadShader(const std::string& ShaderPath,
                                 const std::vector<std::string>& ShaderSearchPaths,
                                 const std::vector<std::string>& IncludePaths,
                                 const std::string& CacheDir)
{
	std::string FullPath;
	for (const auto& Dir : ShaderSearchPaths)
	{
		std::string Candidate = Dir;
		if (!Candidate.empty() && Candidate.back() != '/' && Candidate.back() != '\\')
		{
			Candidate += '/';
		}
		Candidate += ShaderPath;
		std::ifstream Test(Candidate);
		if (Test.good())
		{
			FullPath = Candidate; break;
		}
	}
	if (FullPath.empty())
	{
		MAHO_CORE_ERROR("FShaderDatabase: shader not found: {}", ShaderPath); return false;
	}

	FShaderFile File = FShaderParser::Parse(FullPath, IncludePaths);
	if (File.Passes.empty())
	{
		MAHO_CORE_ERROR("FShaderDatabase: no passes in {}", ShaderPath); return false;
	}

	std::vector<std::size_t> ThisPassIndices;
	for (auto& PS : File.Passes)
	{
		if (!FShaderCompiler::Initialize())
		{
			return false;
		}

		// Compile vertex
		FShaderCompileDesc VsDesc{PS.GlslSource, PS.VertEntry, PS.FragEntry, {}};
		auto VsResult = FShaderCompiler::CompileStage(VsDesc, ERHIShaderStage::Vertex, PS.VertEntry);
		if (!VsResult.bSuccess)
		{
			MAHO_CORE_ERROR("FShaderDatabase: {}:{} VS fail\n{}", ShaderPath, PS.PassName, VsResult.ErrorLog);
			return false;
		}

		// Compile fragment
		FShaderCompileDesc FsDesc{PS.GlslSource, PS.VertEntry, PS.FragEntry, {}};
		auto FsResult = FShaderCompiler::CompileStage(FsDesc, ERHIShaderStage::Fragment, PS.FragEntry);
		if (!FsResult.bSuccess)
		{
			MAHO_CORE_ERROR("FShaderDatabase: {}:{} FS fail\n{}", ShaderPath, PS.PassName, FsResult.ErrorLog);
			return false;
		}

		// Compute bytecode hash (vert+ frag SPIR‑V)
		std::string Combine(reinterpret_cast<const char*>(VsResult.Bytecode.data()),
		                    VsResult.Bytecode.size() * 4);
		Combine.append(reinterpret_cast<const char*>(FsResult.Bytecode.data()),
		               FsResult.Bytecode.size() * 4);
		std::uint64_t Hash = std::hash<std::string>{}(Combine);

		FShaderPassCompiled Pass;
		Pass.PassName = PS.PassName;
		Pass.ShaderPath = ShaderPath;
		Pass.VertexBytecode = std::move(VsResult.Bytecode);
		Pass.FragmentBytecode = std::move(FsResult.Bytecode);
		Pass.VertexSpv = &Pass.VertexBytecode;
		Pass.FragmentSpv = &Pass.FragmentBytecode;
		Pass.RenderState = PS.RenderState;
		Pass.VertexAttributes = PS.VertexInputs;
		Pass.VaryingCount = PS.VaryingCount;
		Pass.BytecodeHash = Hash;

		// De‑duplication: if bytecode hash already exists, reuse existing index
		auto It = HashToIndex.find(Hash);
		if (It == HashToIndex.end())
		{
			std::size_t Idx = Passes.size();
			HashToIndex[Hash] = Idx;
			Passes.push_back(Pass);
			ThisPassIndices.push_back(Idx);
		}
		else
		{
			MAHO_CORE_INFO("FShaderDatabase: {}::{} de‑duplicated (hash={:016X})", ShaderPath, PS.PassName, Hash);
			ThisPassIndices.push_back(It->second);
		}

		std::string Key = ShaderPath + "::" + PS.PassName;
		KeyToPass[Key] = ThisPassIndices.back();
	}

	ShaderToPasses[ShaderPath] = std::move(ThisPassIndices);
	return true;
}

const FShaderPassCompiled* FShaderDatabase::FindPass(const std::string& ShaderPath,
                                                     const std::string& PassName) const
{
	std::string Key = ShaderPath + "::" + PassName;
	auto It = KeyToPass.find(Key);
	return (It != KeyToPass.end()) ? &Passes[It->second] : nullptr;
}

const FShaderPassCompiled* FShaderDatabase::FindPassByHash(std::uint64_t BytecodeHash) const
{
	auto It = HashToIndex.find(BytecodeHash);
	return (It != HashToIndex.end()) ? &Passes[It->second] : nullptr;
}

// ═══════════════════════════════════════════
// Legacy FShaderLoader
// ═══════════════════════════════════════════

FShaderLoader::FShaderLoader(FShaderCache& Cache,
                             std::vector<std::string> SearchPaths,
                             std::vector<std::string> LocalIncludes)
	: CachePtr(&Cache)
	, ShaderPaths(std::move(SearchPaths))
	, IncludePaths(std::move(LocalIncludes))
{
}

std::string FShaderLoader::ReadFile(const std::string& Path)
{
	std::ifstream File(Path, std::ios::binary);
	if (!File)
	{
		return {};
	}
	std::ostringstream Ss; Ss << File.rdbuf(); return Ss.str();
}

std::string FShaderLoader::PreprocessIncludes(const std::string& Source)
{
	return FShaderParser::PreprocessIncludes(Source, IncludePaths);
}

std::string FShaderLoader::ResolveInclude(const std::string& IncludeName)
{
	return FShaderParser::ResolveInclude(IncludeName, IncludePaths);
}

std::string FShaderLoader::LocateFile(const std::string& RelativePath)
{
	for (const auto& Dir : ShaderPaths)
	{
		std::string Candidate = Dir;
		if (!Candidate.empty() && Candidate.back() != '/' && Candidate.back() != '\\')
		{
			Candidate += '/';
		}
		Candidate += RelativePath;
		std::ifstream Test(Candidate);
		if (Test.good())
		{
			return Candidate;
		}
	}
	return {};
}

void FShaderLoader::ParseShaderSource(const std::string& RawSource, FShaderCompileDesc& OutDesc)
{
	OutDesc.Source = RawSource;
	std::istringstream In(RawSource);
	std::string Line;
	while (std::getline(In, Line))
	{
		TrimInline(Line);
		if (StartsWith(Line, "#pragma vertex"))
		{
			auto Pos = Line.find_first_not_of(" \t", 14);
			if (Pos != std::string::npos)
			{
				OutDesc.VertexEntry = Line.substr(Pos);
			}
		}
		else if (StartsWith(Line, "#pragma fragment"))
		{
			auto Pos = Line.find_first_not_of(" \t", 16);
			if (Pos != std::string::npos)
			{
				OutDesc.FragmentEntry = Line.substr(Pos);
			}
		}
	}
}

FShaderPackage FShaderLoader::LoadShader(const std::string& ShaderPath)
{
	FShaderPackage Package;
	std::string FullPath = LocateFile(ShaderPath);
	if (FullPath.empty())
	{
		MAHO_CORE_ERROR("FShaderLoader: not found: {}", ShaderPath); return Package;
	}
	std::string RawSource = ReadFile(FullPath);
	if (RawSource.empty())
	{
		MAHO_CORE_ERROR("FShaderLoader: empty: {}", FullPath); return Package;
	}

	std::string ProcessedSource = PreprocessIncludes(RawSource);
	FShaderCompileDesc Desc;
	ParseShaderSource(ProcessedSource, Desc);

	if (Desc.VertexEntry.empty() || Desc.FragmentEntry.empty())
	{
		MAHO_CORE_ERROR("FShaderLoader: missing #pragma in {}", ShaderPath);
		return Package;
	}

	std::string VtxKey = FShaderCache::MakeKey(ShaderPath, ERHIShaderStage::Vertex, Desc.VertexEntry, Desc.Defines);
	if (!CachePtr->TryLoad(VtxKey, Package.Vertex.Bytecode))
	{
		Package.Vertex = FShaderCompiler::CompileStage(Desc, ERHIShaderStage::Vertex, Desc.VertexEntry);
		if (Package.Vertex.bSuccess)
		{
			CachePtr->Store(VtxKey, Package.Vertex.Bytecode, Package.Vertex.Reflection);
		}
		else
		{
			MAHO_CORE_ERROR("FShaderLoader: VS fail {}", ShaderPath); return Package;
		}
	}
	else
	{
		Package.Vertex.bSuccess = true; CachePtr->TryLoadReflection(VtxKey, Package.Vertex.Reflection);
	}

	std::string FragKey = FShaderCache::MakeKey(ShaderPath, ERHIShaderStage::Fragment, Desc.FragmentEntry, Desc.Defines);
	if (!CachePtr->TryLoad(FragKey, Package.Fragment.Bytecode))
	{
		Package.Fragment = FShaderCompiler::CompileStage(Desc, ERHIShaderStage::Fragment, Desc.FragmentEntry);
		if (Package.Fragment.bSuccess)
		{
			CachePtr->Store(FragKey, Package.Fragment.Bytecode, Package.Fragment.Reflection);
		}
		else
		{
			MAHO_CORE_ERROR("FShaderLoader: FS fail {}", ShaderPath); return Package;
		}
	}
	else
	{
		Package.Fragment.bSuccess = true; CachePtr->TryLoadReflection(FragKey, Package.Fragment.Reflection);
	}

	return Package;
}

FRHIDescriptorSetLayoutDesc BuildDescriptorSetLayoutFromReflection(
	const FShaderReflection& Reflection)
{
	FRHIDescriptorSetLayoutDesc Desc;
	struct FBindingKey { std::uint32_t Set; std::uint32_t Binding; bool operator<(const FBindingKey& O) const { if (Set!=O.Set) return Set<O.Set; return Binding<O.Binding; } };
	std::map<FBindingKey, FRHIDescriptorBinding> BM;

	for (const auto& Block : Reflection.UniformBlocks)
	{
		FBindingKey K{Block.Set, Block.Binding};
		FRHIDescriptorBinding& B = BM[K];
		B.Binding = Block.Binding;
		B.Type = ERHIDescriptorType::UniformBuffer;
		B.Count = 1;
		B.Stages = Block.Stages;
	}
	for (const auto& Sam : Reflection.Samplers)
	{
		FBindingKey K{Sam.Set, Sam.Binding};
		FRHIDescriptorBinding& B = BM[K];
		B.Binding = Sam.Binding;
		B.Type = ERHIDescriptorType::CombinedImageSampler;
		B.Count = 1;
		B.Stages = Sam.Stages;
	}
	for (const auto& [K, B] : BM)
	{
		Desc.Bindings.push_back(B);
	}
	return Desc;
}

FRHIPipelineLayoutDesc BuildPipelineLayoutFromReflection(
	const FShaderReflection& VRefl, const FShaderReflection& FRefl)
{
	FRHIPipelineLayoutDesc D;
	D.PushConstants.insert(D.PushConstants.end(), VRefl.PushConstants.begin(), VRefl.PushConstants.end());
	D.PushConstants.insert(D.PushConstants.end(), FRefl.PushConstants.begin(), FRefl.PushConstants.end());
	return D;
}

} // namespace Maho
