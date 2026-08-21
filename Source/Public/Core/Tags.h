#pragma once

#include <Core/TypeList.h>

namespace Maho
{

// ───────────────────────────────────────────────────────────────────────
// Extension tags — UCLASS-style markers.
//
// Every extension template's default `using FTags` already carries its identity
// tag (TTool → FToolTag, TLayer → FLayerTag, TEngine → FEngineTag). A plugin
// that wants EXTRA markers appends them with FWithTags so the identity tag is
// kept.
//
// Tools are self-managed by design (plug-in-and-play) and need no extra marker;
// Layers are owned by the scheduler. FWithTags exists for future markers.
// ───────────────────────────────────────────────────────────────────────

/** Identity tag of the engine application root. */
struct FEngineTag {};

/** Append extra tags to an existing tag list, preserving the identity tag. */
template <typename TTags, typename... TExtra>
struct TAppendTags;

template <typename TTags>
struct TAppendTags<TTags>
{
	using Type = TTags;
};

template <typename TTags, typename THead, typename... TRest>
struct TAppendTags<TTags, THead, TRest...>
{
	using Type = typename TAppendTags<TAppend_t<TTags, THead>, TRest...>::Type;
};

template <typename TTags, typename... TExtra>
using FWithTags = typename TAppendTags<TTags, TExtra...>::Type;

} // namespace Maho
