#pragma once

// ResourceName -- the STATIC NAME of a "resource" (change add-resource-metadata-edges, §1).
//
// ── WHY A TRAIT, AND NOT A BASE CLASS OR A MEMBER ────────────────────────────────────────────────
//
// A resource is not a new type of object. It is "the thing itself": an ALREADY EXISTING type that a
// stage body touches (`FLog`, `IRHI`, `FPlatform`, ...). Such a type is authored elsewhere (often in
// another plugin) and must NOT grow a base class or a member just to carry a name, so the name is
// attached from the OUTSIDE, as an explicit specialization of one trait:
//
//     MAHO_DECLARE_RESOURCE(FLog)
//     // => template <> struct TResourceName<FLog> { static constexpr std::string_view value = "FLog"; };
//
// The name is a `static constexpr` string literal -- the SAME shape as a frame's `StaticName()`
// (`MAHO_DECLARE_FRAME`, Engine/Frame.h) and a stage's `StaticName()` (`MAHO_DECLARE_STAGE`): a
// compile-time constant, readable, stable across a DLL boundary, and *independent of RTTI* -- which
// is the whole reason the graph's node identity is being moved off `typeid`/`type_index`.
//
// A class that provides SEVERAL resources is split with a NESTED type (the resource then IS that
// nested type, e.g. `FGameWorld::FComponentPools`); a resource that has no class of its own (an
// ImGui context, a window's message queue) gets an empty TAG type instead -- see the bottom of this
// file.
//
// ── THE TABLE BELOW IS NAMES ONLY ────────────────────────────────────────────────────────────────
//
// Every forward declaration below is a NAME, never a dependency: nothing here includes a plugin
// header, links a plugin, or assumes the type's layout. It exists so that ONE place states which
// resources the engine currently recognizes -- the table a later step derives ordering edges from.
// Registering against a forward-declared type is legal (the specialization does not use the type),
// and it is the same pattern the plugin headers already use for their own peers.

#include <Core/Export.h>

#include <string_view>

namespace Maho
{

/** The static name of a resource type; EMPTY for a type that never registered one. Empty means "not
 *  a resource" (the metadata layer reads it as such) rather than a compile failure, so a type may be
 *  named without every user of it having to know that it is one. */
template <typename T>
struct TResourceName
{
	static constexpr std::string_view value{};
};

} // namespace Maho

namespace Maho
{

namespace Detail
{
	/** The TYPE this header's declarations are filed under in the generic store (Core/MetaData.h).
	 *  A plain literal, because the store has no schema: the type IS this string, spelled here once and
	 *  read by every consumer that wants the resource names. */
	inline constexpr std::string_view KResourceType = "Resource";

	/** The registrar `MAHO_DECLARE_RESOURCE` expands: it files the resource's NAME, so a resource shows
	 *  up as soon as it is declared -- before any stage access row names it, and even if nothing ever
	 *  does (a declared resource nothing uses is visible rather than silently absent).
	 *
	 *  The CAPABILITIES are not filed here: a name is declared next to a forward declaration (that is
	 *  the point of this header), and an incomplete type cannot be asked what it supports -- those bits
	 *  travel in the access rows, where the declaring site does have the type (see StageMetadata.h). */
	struct MAHO_API FResourceRegistrar
	{
		explicit FResourceRegistrar(std::string_view Resource) noexcept;
	};
}

} // namespace Maho

/**
 * Name a resource: `MAHO_DECLARE_RESOURCE(X)` => `TResourceName<X>::value == "X"`, and one row for it
 * in the metadata table.
 *
 * Written INSIDE `namespace Maho` at file scope (a specialization must live in the namespace of its
 * template). `X` may be qualified (`Name::FNamePool`) -- the name it yields is then qualified too,
 * which is what keeps two resources of different plugins apart in a dump.
 *
 * The expansion ends in `;`, so it reads as one declaration: `MAHO_DECLARE_RESOURCE(FLog)`
 */
#define MAHO_DECLARE_RESOURCE(X)                                  \
	template <>                                                   \
	struct TResourceName<X>                                       \
	{                                                             \
		static constexpr std::string_view value = #X;             \
		static inline const ::Maho::Detail::FResourceRegistrar Registration{ value }; \
	};

// ── the identified resources ─────────────────────────────────────────────────────────────────────
//
// Forward declarations only (the types themselves are defined by the plugins that own them; this
// header names them and nothing else).

namespace Maho
{
	class FLog;
	class FPrimitiveRegistry;
	struct IRHI;

	namespace Name
	{
		class FNamePool;
	}

	namespace Platform
	{
		class FPlatform;
	}

	namespace Script
	{
		class FScriptSystem;
	}

	namespace UI
	{
		class FUIViewRegistry;
	}

	namespace Resource
	{
		class FResourceSystem;
	}

	namespace GameWorld
	{
		class FGameWorld;

		/** The world's component pools (SoA storage per component type). A TAG type: the pools are
		 *  members of the world, not a class of their own, and a resource has to BE a type (design D3).
		 *  The world owns the pools; a system reads/writes components THROUGH them. */
		struct FComponentPools
		{
		};
	}

	/** The ImGui context a UI layer owns. A TAG type: the context is an opaque library handle
	 *  (`ImGuiContext*`), so there is no class to name here. */
	struct FImGuiContext
	{
	};

	MAHO_DECLARE_RESOURCE(FLog)
	MAHO_DECLARE_RESOURCE(Name::FNamePool)
	MAHO_DECLARE_RESOURCE(IRHI)
	MAHO_DECLARE_RESOURCE(Platform::FPlatform)
	MAHO_DECLARE_RESOURCE(GameWorld::FComponentPools)
	MAHO_DECLARE_RESOURCE(Script::FScriptSystem)
	MAHO_DECLARE_RESOURCE(FPrimitiveRegistry)
	MAHO_DECLARE_RESOURCE(UI::FUIViewRegistry)
	MAHO_DECLARE_RESOURCE(Resource::FResourceSystem)
	MAHO_DECLARE_RESOURCE(FImGuiContext)

} // namespace Maho
