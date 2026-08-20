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
// kept:
//
//   class FConsoleVariableTool : public Maho::TTool<FConsoleVariableTool>
//   {
//   public:
//       using FTags = Maho::FWithTags<
//           typename Maho::TTool<FConsoleVariableTool>::FTags,
//           Maho::FStandaloneTag>;
//   };
//
// The code-gen tool and the interface-layering linter read FTags to classify
// the extension (FStandaloneTag → self-managed, linter leaves it alone).
// ───────────────────────────────────────────────────────────────────────

/** Identity tag of the engine application root. */
struct FEngineTag {};

/** Self-managed — outside the scheduler's write model. The linter leaves a
 *  class carrying this tag alone (it guards its own concurrency). */
struct FStandaloneTag {};

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
