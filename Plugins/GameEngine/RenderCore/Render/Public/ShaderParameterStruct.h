#pragma once

/*=============================================================================
	ShaderParameterStruct.h: Compile-time shader parameter structures (Maho).

	Faithful port of UE's SHADER_PARAMETER_STRUCT macro family. Like UE, a single
	declaration is the single source of truth: the same token declares the actual
	struct field AND appends the member's compile-time metadata, so declaration
	order == metadata order. Unlike UE (which resolves descriptor slots at RUNTIME
	from shader reflection via FShaderParameterBindings), Maho has NO reflection
	hook -- shaders are GLSL->SPIR-V with no resource-name introspection. So the
	set / binding / stages are BAKED into the resource macros here; codegen
	substitutes the constants it computed from the Unity shader's reflection.

	The member-collection mechanism is UE's exact "member-id type chain + append
	backtrack": an open-ended typedef chain built by the preprocessor (no external
	codegen). Each SHADER_PARAMETER_* completes the previous chain typedef, declares
	the field, declares the next chain node, and an overloaded zzAppendMemberGetPrev
	that pushes one FShaderParameterMember and returns a pointer to the PREVIOUS
	member's append function. END_SHADER_PARAMETER_STRUCT backtracks from the last
	member to zzFirstMemberId (which returns nullptr) and reverses to restore
	declaration order.

	Maho adaptation notes:
	  - Alignment uses alignas() (C++20) instead of MS_ALIGN/GCC_ALIGN.
	  - Metadata lives in FShaderParameterStructMetadata (std::vector-based).
	  - Constant members (bIsResource == false) pack into ONE push-constant block,
	    staged AllGraphics by default.
	  - Resource members carry {set, binding, stages} baked in the macro and map to
	    Maho descriptor types (SampledImage / CombinedImageSampler / StorageBuffer).
=============================================================================*/

#include "RDG.h"
#include <RHI/RHIEnums.h>
#include <RHI/RHIResources.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Maho
{

/** Ported from UE's SHADER_PARAMETER_STRUCT_ALIGNMENT (RHIDefinitions.h). */
constexpr std::uint32_t SHADER_PARAMETER_STRUCT_ALIGNMENT        = 16;
/** Ported from UE's SHADER_PARAMETER_ARRAY_ELEMENT_ALIGNMENT. */
constexpr std::uint32_t SHADER_PARAMETER_ARRAY_ELEMENT_ALIGNMENT = 16;
/** Ported from UE's SHADER_PARAMETER_POINTER_ALIGNMENT (sizeof(uint64)). */
constexpr std::uint32_t SHADER_PARAMETER_POINTER_ALIGNMENT       = sizeof(std::uint64_t);

/** The base type of one member -- constant (constant-buffer / push-constant
 *  stored) or a resource (descriptor). Maps Maho's shader-facing value kinds. */
enum class EShaderParameterType : std::uint8_t
{
	Invalid = 0,
	// Native / constant-buffer stored.
	Float,
	UInt32,
	Int32,
	// RHI / RDG resources.
	Texture,   // FRDGTextureRef  -> SampledImage / CombinedImageSampler
	Buffer,    // FRDGBufferRef   -> StorageBuffer
};

/** Compile-time metadata of ONE member: the struct layout + the descriptor slot
 *  it binds (baked in by codegen). Set/binding/stages live alongside the layout
 *  data because Maho has no runtime reflection to recover them. */
struct FShaderParameterMember
{
	const char* Name = nullptr;
	const char* ShaderType = nullptr;          // shader-side type string (e.g. "Texture2D")
	std::uint32_t Offset = 0;                  // byte offset of the field in the FParameters struct
	std::uint32_t Size = 0;                    // byte size of the field
	EShaderParameterType Type = EShaderParameterType::Invalid;
	ERHIDescriptorType DescriptorType = ERHIDescriptorType::Sampler;
	std::uint32_t Set = 0;                     // descriptor set index
	std::uint32_t Binding = 0;                 // descriptor binding index
	ERHIShaderStage Stages = ERHIShaderStage::None;
	bool bIsResource = false;                  // true => descriptor binding, false => constant
	std::uint32_t SamplerOffset = 0;           // offset of a companion sampler field (CombinedImageSampler) else 0
};

/** Compile-time metadata of one FParameters struct (declared with the macros). */
struct FShaderParameterStructMetadata
{
	const char* StructTypeName = nullptr;
	std::uint32_t Size = 0;
	std::vector<FShaderParameterMember> Members;
};

/** alignas() on a TYPE does not work on MSVC/clang the way it does on a variable,
 *  so this is UE's TAlignedTypedef trick: produce an aligned type alias. */
template<typename T, std::uint32_t Alignment>
struct TAlignedTypedef
{
	using Type = T alignas(Alignment);
};

/** Type info for a constant (native) member. The BaseType and alignment drive the
 *  field declaration + the metadata. bIsStoredInConstantBuffer distinguishes a
 *  constant from a resource. The primary template is deliberately invalid so any
 *  unsupported type fails at compile time with a clear static_assert. */
template<typename T>
struct TShaderParameterTypeInfo
{
	static constexpr EShaderParameterType BaseType = EShaderParameterType::Invalid;
	static constexpr ERHIDescriptorType DescriptorType = ERHIDescriptorType::UniformBuffer;
	static constexpr bool bIsStoredInConstantBuffer = false;
	static constexpr std::uint32_t Alignment = 1;
	using TAlignedType = T;
};

template<> struct TShaderParameterTypeInfo<float>
{
	static constexpr EShaderParameterType BaseType = EShaderParameterType::Float;
	static constexpr ERHIDescriptorType DescriptorType = ERHIDescriptorType::UniformBuffer;
	static constexpr bool bIsStoredInConstantBuffer = true;
	static constexpr std::uint32_t Alignment = 4;
	using TAlignedType = float;
};

template<> struct TShaderParameterTypeInfo<std::uint32_t>
{
	static constexpr EShaderParameterType BaseType = EShaderParameterType::UInt32;
	static constexpr ERHIDescriptorType DescriptorType = ERHIDescriptorType::UniformBuffer;
	static constexpr bool bIsStoredInConstantBuffer = true;
	static constexpr std::uint32_t Alignment = 4;
	using TAlignedType = std::uint32_t;
};

template<> struct TShaderParameterTypeInfo<std::int32_t>
{
	static constexpr EShaderParameterType BaseType = EShaderParameterType::Int32;
	static constexpr ERHIDescriptorType DescriptorType = ERHIDescriptorType::UniformBuffer;
	static constexpr bool bIsStoredInConstantBuffer = true;
	static constexpr std::uint32_t Alignment = 4;
	using TAlignedType = std::int32_t;
};

/** Builds the lazily-initialised compile-time metadata for StructTypeName.
 *  The nested FTypeInfo (declared in the BEGIN macro, before zzGetMembers) can
 *  still call zzGetMembers() because member-function bodies are a complete-class
 *  context -- by the time the body is analysed, the whole struct is complete. */
#define INTERNAL_SHADER_PARAMETER_GET_STRUCT_METADATA(StructTypeName) \
	{ \
		static const FShaderParameterStructMetadata M{ \
			#StructTypeName, \
			sizeof(StructTypeName), \
			StructTypeName::zzGetMembers() }; \
		return M; \
	}

/** Begins a shader parameter structure (see BEGIN_SHADER_PARAMETER_STRUCT). */
#define INTERNAL_SHADER_PARAMETER_STRUCT_BEGIN(StructTypeName, DllStorage) \
	struct alignas(SHADER_PARAMETER_STRUCT_ALIGNMENT) StructTypeName \
	{ \
	public: \
		DllStorage StructTypeName () {} \
		struct FTypeInfo \
		{ \
			static constexpr std::uint32_t GetSize() { return sizeof(StructTypeName); } \
			static const FShaderParameterStructMetadata& GetStructMetadata() \
				INTERNAL_SHADER_PARAMETER_GET_STRUCT_METADATA(StructTypeName) \
		}; \
	private: \
		typedef StructTypeName zzTThisStruct; \
		struct zzFirstMemberId { enum { HasDeclaredResource = 0 }; }; \
		typedef void* zzFuncPtr; \
		typedef zzFuncPtr(*zzMemberFunc)(zzFirstMemberId, std::vector<FShaderParameterMember>*); \
		static zzFuncPtr zzAppendMemberGetPrev(zzFirstMemberId, std::vector<FShaderParameterMember>*) \
		{ \
			return nullptr; \
		} \
		typedef zzFirstMemberId

/** The shared "push one member, return the previous append function" body. */
#define INTERNAL_SHADER_PARAMETER_APPEND_MEMBER(MemberName, PrevType, TypeInfo, Type, DescriptorType, Set, Binding, Stages, ShaderType, bIsResource, SamplerOffsetExpr) \
		struct zzNextMemberId##MemberName \
		{ \
			enum { HasDeclaredResource = zzMemberId##MemberName::HasDeclaredResource || !TypeInfo::bIsStoredInConstantBuffer }; \
		}; \
		static zzFuncPtr zzAppendMemberGetPrev(zzNextMemberId##MemberName, std::vector<FShaderParameterMember>* Members) \
		{ \
			static_assert((offsetof(zzTThisStruct, MemberName) & (TypeInfo::Alignment - 1)) == 0, \
				"Misaligned shader parameter struct member " #MemberName "."); \
			Members->push_back(FShaderParameterMember{ \
				#MemberName, \
				ShaderType, \
				offsetof(zzTThisStruct, MemberName), \
				sizeof(TypeInfo::TAlignedType), \
				Type, \
				DescriptorType, \
				Set, \
				Binding, \
				Stages, \
				bIsResource, \
				SamplerOffsetExpr }); \
			zzFuncPtr(*PrevFunc)(zzMemberId##MemberName, std::vector<FShaderParameterMember>*); \
			PrevFunc = zzAppendMemberGetPrev; \
			return (zzFuncPtr)PrevFunc; \
		} \
		typedef zzNextMemberId##MemberName

/** Internal: a constant (native) member. */
#define INTERNAL_SHADER_PARAMETER_CONST(TypeInfo, MemberName) \
	zzMemberId##MemberName; \
	public: \
		alignas(TypeInfo::Alignment) TypeInfo::TAlignedType MemberName{}; \
		static_assert(TypeInfo::BaseType != EShaderParameterType::Invalid, \
			#MemberName " must be a supported shader parameter type."); \
		static_assert(TypeInfo::bIsStoredInConstantBuffer, \
			#MemberName " must be a constant."); \
	private: \
		INTERNAL_SHADER_PARAMETER_APPEND_MEMBER(MemberName, zzMemberId##MemberName, TypeInfo, \
			TypeInfo::BaseType, ERHIDescriptorType::UniformBuffer, 0u, 0u, ERHIShaderStage::AllGraphics, "", false, 0u)

/** Internal: a constant (native) array member. The C array field is declared
 *  member[N]; offset/size cover the whole block for one push-constant range. */
#define INTERNAL_SHADER_PARAMETER_CONST_ARRAY(TypeInfo, MemberName, NumElements) \
	zzMemberId##MemberName; \
	public: \
		alignas(TypeInfo::Alignment) TypeInfo::TAlignedType MemberName[NumElements]{}; \
		static_assert(TypeInfo::BaseType != EShaderParameterType::Invalid, \
			#MemberName " must be a supported shader parameter type."); \
	private: \
		struct zzNextMemberId##MemberName \
		{ \
			enum { HasDeclaredResource = zzMemberId##MemberName::HasDeclaredResource || !TypeInfo::bIsStoredInConstantBuffer }; \
		}; \
		static zzFuncPtr zzAppendMemberGetPrev(zzNextMemberId##MemberName, std::vector<FShaderParameterMember>* Members) \
		{ \
			static_assert((offsetof(zzTThisStruct, MemberName) & (TypeInfo::Alignment - 1)) == 0, \
				"Misaligned shader parameter struct member " #MemberName "."); \
			Members->push_back(FShaderParameterMember{ \
				#MemberName, \
				"", \
				offsetof(zzTThisStruct, MemberName), \
				sizeof(TypeInfo::TAlignedType) * (NumElements), \
				TypeInfo::BaseType, \
				ERHIDescriptorType::UniformBuffer, \
				0u, \
				0u, \
				ERHIShaderStage::AllGraphics, \
				false, \
				0u }); \
			zzFuncPtr(*PrevFunc)(zzMemberId##MemberName, std::vector<FShaderParameterMember>*); \
			PrevFunc = zzAppendMemberGetPrev; \
			return (zzFuncPtr)PrevFunc; \
		} \
		typedef zzNextMemberId##MemberName

/** Internal: a resource member bound as a descriptor. FieldType is a Maho RDG
 *  handle / RHI pointer; Type is EShaderParameterType (Texture / Buffer); DescriptorType
 *  is the Maho descriptor kind. Unlike the constant path (whose TypeInfo supplies
 *  Alignment/TAlignedType), a resource has no "native scalar type info" -- its append
 *  body is written here, sized by the FieldType and aligned to the pointer alignment
 *  (same as TEXTURE_SAMPLER). */
#define INTERNAL_SHADER_PARAMETER_RESOURCE(Type, FieldType, ShaderType, MemberName, Set, Binding, Stages, DescriptorType) \
	zzMemberId##MemberName; \
	public: \
		alignas(SHADER_PARAMETER_POINTER_ALIGNMENT) FieldType MemberName{}; \
	private: \
		struct zzNextMemberId##MemberName \
		{ \
			enum { HasDeclaredResource = zzMemberId##MemberName::HasDeclaredResource || true }; \
		}; \
		static zzFuncPtr zzAppendMemberGetPrev(zzNextMemberId##MemberName, std::vector<FShaderParameterMember>* Members) \
		{ \
			static_assert((offsetof(zzTThisStruct, MemberName) & (SHADER_PARAMETER_POINTER_ALIGNMENT - 1)) == 0, \
				"Misaligned shader parameter struct member " #MemberName "."); \
			Members->push_back(FShaderParameterMember{ \
				#MemberName, \
				ShaderType, \
				offsetof(zzTThisStruct, MemberName), \
				sizeof(FieldType), \
				Type, \
				DescriptorType, \
				Set, \
				Binding, \
				Stages, \
				true, \
				0u }); \
			zzFuncPtr(*PrevFunc)(zzMemberId##MemberName, std::vector<FShaderParameterMember>*); \
			PrevFunc = zzAppendMemberGetPrev; \
			return (zzFuncPtr)PrevFunc; \
		} \
		typedef zzNextMemberId##MemberName

/** Internal: a CombinedImageSampler member -- one binding that pairs a texture
 *  field and a sampler field. Both fields are declared; the metadata records the
 *  sampler's offset so AddPass can write the combined descriptor. */
#define INTERNAL_SHADER_PARAMETER_TEXTURE_SAMPLER(ShaderType, TextureMember, SamplerMember, Set, Binding, Stages) \
	zzMemberId##TextureMember; \
	public: \
		alignas(SHADER_PARAMETER_POINTER_ALIGNMENT) FRDGTextureRef TextureMember{}; \
		alignas(SHADER_PARAMETER_POINTER_ALIGNMENT) FRHISampler* SamplerMember{}; \
	private: \
		struct zzNextMemberId##TextureMember \
		{ \
			enum { HasDeclaredResource = zzMemberId##TextureMember::HasDeclaredResource || true }; \
		}; \
		static zzFuncPtr zzAppendMemberGetPrev(zzNextMemberId##TextureMember, std::vector<FShaderParameterMember>* Members) \
		{ \
			Members->push_back(FShaderParameterMember{ \
				#TextureMember, \
				ShaderType, \
				offsetof(zzTThisStruct, TextureMember), \
				sizeof(FRDGTextureRef), \
				EShaderParameterType::Texture, \
				ERHIDescriptorType::CombinedImageSampler, \
				Set, \
				Binding, \
				Stages, \
				true, \
				offsetof(zzTThisStruct, SamplerMember) }); \
			zzFuncPtr(*PrevFunc)(zzMemberId##TextureMember, std::vector<FShaderParameterMember>*); \
			PrevFunc = zzAppendMemberGetPrev; \
			return (zzFuncPtr)PrevFunc; \
		} \
		typedef zzNextMemberId##TextureMember

/** Ends a shader parameter structure. Completes the chain and defines zzGetMembers
 *  (UE's backtrack: from the last member's append function back to the base). */
#define END_SHADER_PARAMETER_STRUCT() \
		zzLastMemberId; \
	public: \
		static std::vector<FShaderParameterMember> zzGetMembers() \
		{ \
			std::vector<FShaderParameterMember> Members; \
			zzFuncPtr(*LastFunc)(zzLastMemberId, std::vector<FShaderParameterMember>*); \
			LastFunc = zzAppendMemberGetPrev; \
			zzFuncPtr Ptr = (zzFuncPtr)LastFunc; \
			do \
			{ \
				Ptr = reinterpret_cast<zzMemberFunc>(Ptr)(zzFirstMemberId(), &Members); \
			} while (Ptr); \
			std::reverse(Members.begin(), Members.end()); \
			return Members; \
		} \
	};

// -- Public macro family -------------------------------------------------------

/** Begins a compile-time shader parameter structure.
 *  BEGIN_SHADER_PARAMETER_STRUCT(FMyParameters)
 *      SHADER_PARAMETER(float, ViewScale)
 *      SHADER_PARAMETER_TEXTURE_SAMPLER(Texture2D, FontAtlas, FontSampler, 0, 0, Fragment)
 *  END_SHADER_PARAMETER_STRUCT()
 */
#define BEGIN_SHADER_PARAMETER_STRUCT(StructTypeName) \
	INTERNAL_SHADER_PARAMETER_STRUCT_BEGIN(StructTypeName, )

/** Constant member -> push-constant block. */
#define SHADER_PARAMETER(MemberType, MemberName) \
	INTERNAL_SHADER_PARAMETER_CONST(TShaderParameterTypeInfo<MemberType>, MemberName)

/** Constant array member -> push-constant block. NumElements is an integer literal. */
#define SHADER_PARAMETER_ARRAY(MemberType, MemberName, NumElements) \
	INTERNAL_SHADER_PARAMETER_CONST_ARRAY(TShaderParameterTypeInfo<MemberType>, MemberName, NumElements)

/** Sampled texture -> a SampledImage descriptor binding. */
#define SHADER_PARAMETER_TEXTURE(ShaderType, MemberName, Set, Binding, Stages) \
	INTERNAL_SHADER_PARAMETER_RESOURCE(EShaderParameterType::Texture, FRDGTextureRef, #ShaderType, \
		MemberName, Set, Binding, Stages, ERHIDescriptorType::SampledImage)

/** Storage buffer (SRV/UAV) -> a StorageBuffer descriptor binding. */
#define SHADER_PARAMETER_BUFFER(ShaderType, MemberName, Set, Binding, Stages, DescriptorType) \
	INTERNAL_SHADER_PARAMETER_RESOURCE(EShaderParameterType::Buffer, FRDGBufferRef, #ShaderType, \
		MemberName, Set, Binding, Stages, DescriptorType)

/** CombinedImageSampler -- a texture paired with a sampler in one descriptor. */
#define SHADER_PARAMETER_TEXTURE_SAMPLER(ShaderType, TextureMember, SamplerMember, Set, Binding, Stages) \
	INTERNAL_SHADER_PARAMETER_TEXTURE_SAMPLER(#ShaderType, TextureMember, SamplerMember, Set, Binding, Stages)

} // namespace Maho

/** TScalarResourceTypeInfo is a sentinel TypeInfo for resource members (its fields
 *  are used only for the HasDeclaredResource expression fold; the resource
 *  metadata is written explicitly by INTERNAL_SHADER_PARAMETER_RESOURCE). It must
 *  live at global scope for the macro's ::Maho:: qualification. */
namespace Maho
{
struct TScalarResourceTypeInfo
{
	static constexpr bool bIsStoredInConstantBuffer = false;
};
}

namespace Maho
{

/** Result of translating a compile-time TParameters (macro-declared) into a
 *  runtime FPassParameter. Resource members become descriptor-set bindings;
 *  constant members are packed into ONE contiguous push-constant block in
 *  declaration order (Maho's push-constant analogue of UE's constant buffer --
 *  Maho has no reflection hook, so the block is built directly from the struct
 *  fields). The push data + stages are returned so AddPass can bind it. */
struct FShaderParameterBuildResult
{
	FPassParameter Layout;
	std::vector<std::byte> PushConstantData;
	ERHIShaderStage PushConstantStages = ERHIShaderStage::None;
	bool bHasPushConstant = false;
};

/** Translate a macro-declared TParameters into the runtime layout AddPass
 *  consumes. Reads the compile-time metadata (declaration order == metadata
 *  order) and copies each member's VALUE out of the struct instance. */
template <typename TParameters>
FShaderParameterBuildResult ShaderParameterBuild(const TParameters& Parameters)
{
	FShaderParameterBuildResult Result;
	const FShaderParameterStructMetadata& Meta = TParameters::FTypeInfo::GetStructMetadata();
	const std::byte* Base = reinterpret_cast<const std::byte*>(&Parameters);
	for (const FShaderParameterMember& M : Meta.Members)
	{
		if (M.bIsResource)
		{
			FRDGBinding& B = Result.Layout.Bind(M.Set, M.Binding, M.DescriptorType);
			B.Stages = M.Stages;
			if (M.Type == EShaderParameterType::Texture)
			{
				const FRDGTextureRef* Tex = reinterpret_cast<const FRDGTextureRef*>(Base + M.Offset);
				B.Resource = *Tex;
				if (M.SamplerOffset != 0)
				{
					const FRHISampler* const* Samp = reinterpret_cast<const FRHISampler* const*>(Base + M.SamplerOffset);
					B.SamplerIndex = Result.Layout.AddSampler(M.Set, const_cast<FRHISampler*>(*Samp));
				}
			}
			else if (M.Type == EShaderParameterType::Buffer)
			{
				const FRDGBufferRef* Buf = reinterpret_cast<const FRDGBufferRef*>(Base + M.Offset);
				B.Resource = *Buf;
			}
		}
		else
		{
			const std::byte* Src = Base + M.Offset;
			Result.PushConstantData.insert(Result.PushConstantData.end(), Src, Src + M.Size);
			Result.PushConstantStages = M.Stages;
		}
	}
	if (!Result.PushConstantData.empty())
	{
		FRHIPushConstantRange R;
		R.Stages = Result.PushConstantStages;
		R.Offset = 0;
		R.Size = static_cast<std::uint32_t>(Result.PushConstantData.size());
		Result.Layout.PushConstants.push_back(R);
		Result.bHasPushConstant = true;
	}
	return Result;
}

} // namespace Maho
