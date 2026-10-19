/// @file
/// @brief The engine's metadata store: every module files rows of `(Type, Key, Value)` here, and every
///        module can look them up -- the engine's replacement for RTTI.
///
/// The manager interprets NOTHING: `Type` and `Key` are strings the registering side chose, `Value` is
/// bytes it encoded, and the store only answers "what did you file". A reader and a writer that agree
/// on the encoding therefore share data across DLL boundaries without sharing a type -- which is how a
/// declared TYPE gets an identity (`MAHO_DECLARE_TYPE`), a stage declares what it touches and a
/// resource declares its ordering semantics.
#pragma once


#include <Core/Export.h>
#include <Core/Singleton.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Maho
{

/// @brief One stored row: the registering side's type name, its key, and its encoded value.
struct FMetaDataEntry
{
	/// @brief Which axis the row belongs to (`Type = "Type"` / `"Stage"` / `"ResourceOrdering"` ...).
	std::string Type;

	/// @brief The row's identity within that type (a type name, a `(frame, stage)` pair, ...).
	std::string Key;

	/// @brief The value, as the registrant encoded it. The store never looks inside.
	std::string Value;
};

/// @brief The store: idempotent registration, lookup by `(Type, Key)`, and a dump for humans.
///
/// Registration happens at static-init time from every translation unit that carries a declaration, so
/// it MUST be idempotent: the same row registered twice is silent, and the same key with DIFFERENT
/// content is reported (two spellings of one identity is a bug, not a merge).
class MAHO_API FMetaData : public TSingleton<FMetaData>
{
public:
	/// @brief Not copyable: the store is one object per process, reached through `GetMetaData`.
	FMetaData(const FMetaData&) = delete;

	/// @brief Not copy-assignable: the store is one object per process.
	FMetaData& operator=(const FMetaData&) = delete;

	/// @brief Files a row. Idempotent for identical content; a conflicting registration is reported.
	/// @param Type The axis this row belongs to.
	/// @param Key The identity within that axis.
	/// @param Encoded The value, encoded by the registrant.
	void Register(std::string_view Type, std::string_view Key, std::string_view Encoded);

	/// @brief Finds one row.
	/// @param Type The axis to search.
	/// @param Key The identity to look for.
	/// @return The row, or `nullptr`. The pointer is owned by the store and lives as long as the
	///         process (a deregistration does not free it) -- which is what makes a row's ADDRESS a
	///         usable identity token.
	[[nodiscard]] const FMetaDataEntry* Find(std::string_view Type, std::string_view Key) const;

	/// @brief How many rows a given type holds.
	/// @param Type The axis to count.
	/// @return The row count.
	[[nodiscard]] std::size_t Count(std::string_view Type) const;

	/// @brief Every type name that has at least one row (for a dump or an editor).
	/// @return The type names, sorted.
	[[nodiscard]] std::vector<std::string> Types() const;

	/// @brief A copy of one type's rows (the caller owns it; the store keeps the originals).
	/// @param Type The axis to copy.
	/// @return The rows under that type.
	[[nodiscard]] std::vector<FMetaDataEntry> Snapshot(std::string_view Type) const;

	/// @brief Prints the whole store to stderr ONCE, when the environment says so (`MAHO_METADATA`).
	void DumpOnce() const;

	/// @brief Creates an empty store. Public because the accessor is a FREE function (a friend
	///        declaration of an exported function is what MSVC refuses: C2375, "different linkage");
	///        reach the store through `GetMetaData()` -- `TSingleton` marks the intent.
	FMetaData();

	/// @brief Destroys the store. Rows are NOT freed: a token handed out stays valid for the process.
	~FMetaData();

private:
	/// @brief The store's hidden state (rows, their lock).
	struct FImpl;

	/// @brief The hidden state (see FImpl).
	std::unique_ptr<FImpl> Impl;
};

/// @brief The process-wide store (a `dllimport` symbol, so every module sees one instance).
/// @return The store.
[[nodiscard]] MAHO_API FMetaData& GetMetaData() noexcept;

/// @brief The TYPE this header's own declared types are filed under.
///
/// One row per DECLARED TYPE, whose address is that type's identity token (see `MAHO_DECLARE_TYPE`).
inline constexpr std::string_view KDeclaredType = "Type";

/** A type's DECLARED NAME -- non-empty only for a type declared with `MAHO_DECLARE_TYPE`. It is what
 *  makes a type addressable in the store without RTTI; an empty one is the trait's way of saying
 *  "this type has no identity here". */
template <typename T>
struct TTypeName
{
	/// @brief The declared name, or empty when the type declared no identity.
	static constexpr std::string_view value{};
};

namespace Detail
{
	/** The registrar `MAHO_DECLARE_TYPE` expands: it files the type's NAME, so the row exists -- and
	 *  its ADDRESS is the token every module resolves to the same value. The value is empty on
	 *  purpose: a declaration says "this identity exists", nothing more. */
	struct MAHO_API FTypeRegistrar
	{
		/// @brief Files the declaration's row (idempotent: the same header expands once per TU).
		/// @param TypeName The declared name, as spelled at the declaration.
		explicit FTypeRegistrar(std::string_view TypeName) noexcept;
	};
}

/**
 * Declare a TYPE's identity: `MAHO_DECLARE_TYPE(FTransform)` gives it a row in the store, and the
 * ADDRESS of that row is the token `TQueryTag<T>::Get()` answers with -- the same value in every DLL,
 * which is what lets a component pool be found by component type without a `dynamic_cast`.
 *
 * Written INSIDE `namespace Maho`, next to the type it names (the same convention as
 * `MAHO_DECLARE_RESOURCE`). Use the most qualified spelling that is unambiguous: the name IS the key.
 */
/// @brief Declares a type's identity (a store row whose ADDRESS is the type's token).
/// @param X The type to declare (spelled unambiguously -- the name IS the key).
#define MAHO_DECLARE_TYPE(X)                                                     \
	template <>                                                                  \
	struct TTypeName<X>                                                          \
	{                                                                            \
		static constexpr std::string_view value = #X;                             \
		static inline const ::Maho::Detail::FTypeRegistrar Registration{ value }; \
	};

/// @brief Strips the qualification from a declared name (`GameWorld::FTransform` -> `FTransform`).
///
/// The name a declaration files is the most qualified spelling that was unambiguous; a UI or a log
/// line wants the short one. Kept here so every reader strips it the SAME way.
/// @param Qualified The declared (possibly qualified) name.
/// @return The name after the last `::` (the input itself when there is none).
[[nodiscard]] MAHO_API std::string_view BareTypeName(std::string_view Qualified) noexcept;

/// @brief The axis a type's SPELLING row is filed under (see `TypeToken`).
inline constexpr std::string_view KTypeSpelling = "TypeSpelling";

/// @brief The compiler's name for a type, as a literal -- usable for ANY type, unlike `TTypeName` (which only
///        answers for a type that declared itself with `MAHO_DECLARE_TYPE`).
///
/// This is the RTTI-free way to name an arbitrary type: it is the SOURCE spelling the compiler saw, so every
/// module that writes the same type writes the same string. It is a diagnostic-grade name (long, and spelled
/// differently by different compilers) -- what it is FOR is being a stable key.
///
/// The raw macro text is NOT the answer: `__FUNCSIG__` names this very FUNCTION ("`class
/// std::basic_string_view<...> __cdecl Maho::TypeSpelling<struct FMyType>(void) noexcept`"), so the argument is
/// cut out of it here -- a name a dump prints, and still the same string in every module. The elaborated-type
/// keyword the compiler puts in front of the outermost type is dropped with it.
/// @tparam T The type to name.
/// @return The spelling (a literal: never built, never freed).
template <typename T>
[[nodiscard]] constexpr std::string_view TypeSpelling() noexcept
{
#if defined(_MSC_VER)
	constexpr std::string_view Raw = __FUNCSIG__;
	constexpr std::string_view Open = "TypeSpelling<";
	constexpr std::string_view Close = ">(void)";
	constexpr std::size_t From = Raw.find(Open);
	constexpr std::size_t To = Raw.rfind(Close);
	if constexpr (From == std::string_view::npos || To == std::string_view::npos)
	{
		return Raw;
	}
	constexpr std::string_view Name = Raw.substr(From + Open.size(), To - From - Open.size());
#else
	constexpr std::string_view Raw = __PRETTY_FUNCTION__;
	constexpr std::string_view Open = "T = ";
	constexpr std::size_t From = Raw.find(Open);
	constexpr std::size_t To = Raw.rfind(';');
	if constexpr (From == std::string_view::npos || To == std::string_view::npos)
	{
		return Raw;
	}
	constexpr std::string_view Name = Raw.substr(From + Open.size(), To - From - Open.size());
#endif

	for (const std::string_view Keyword : { "struct ", "class ", "union ", "enum " })
	{
		if (Name.size() > Keyword.size()
			&& Name.compare(0, Keyword.size(), Keyword) == 0)
		{
			return Name.substr(Keyword.size());
		}
	}
	return Name;
}

/// @brief A type's IDENTITY TOKEN: the address of its own row in the store, keyed by its spelling.
///
/// The same mechanism as every other token here (`MAHO_DECLARE_TYPE`'s row, a declaration's row): the store
/// is one object per process, so a row's address is one value per process. Because the row is keyed by the
/// SPELLING, two modules that name the same type resolve to the SAME row -- so they agree on the token.
/// (The address of an inline static member would not do: every DLL carries its own copy of it.)
///
/// It exists so that "which type" can be folded into a key without RTTI and without a string comparison at
/// the point of use: the token is compared (and hashed) like any other pointer.
/// @tparam T The type to identify.
/// @return The token (never dereferenced, only compared and hashed).
template <typename T>
[[nodiscard]] const void* TypeToken() noexcept
{
	static const void* const Cached = []() -> const void*
	{
		constexpr std::string_view Name = TypeSpelling<T>();
		GetMetaData().Register(KTypeSpelling, Name, "");
		return static_cast<const void*>(GetMetaData().Find(KTypeSpelling, Name));
	}();
	return Cached;
}

}
