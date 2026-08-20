#pragma once

#include "TextApi.h"
#include <Maho.h>
#include <Engine/Tool.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace Maho
{

namespace Text
{

/** Supported cultures. Strings are UTF-8. */
namespace Culture
{
	inline constexpr std::string_view English = "en-US";
	inline constexpr std::string_view Chinese = "zh-CN";
	inline constexpr std::string_view Japanese = "ja-JP";
}

/**
 * Localized text handle — stores {Namespace, Key, Source}. Resolve() looks up
 * the translation for the current culture and falls back to Source.
 */
class MAHO_TEXT_API FText
{
public:
	FText() = default;
	FText(std::string InNamespace, std::string InKey, std::string InSource);

	[[nodiscard]] std::string_view GetNamespace() const { return Namespace; }
	[[nodiscard]] std::string_view GetKey() const { return Key; }
	[[nodiscard]] std::string_view GetSource() const { return Source; }

	/** Resolve against the current culture — falls back to Source when absent. */
	[[nodiscard]] std::string Resolve() const;

	[[nodiscard]] bool operator==(const FText& Other) const
	{
		return Namespace == Other.Namespace && Key == Other.Key;
	}

private:
	std::string Namespace;
	std::string Key;
	std::string Source;
};

/** Localization manager: current culture + translation catalog (a plain singleton). */
class MAHO_TEXT_API FTextTool : public Maho::TTool<FTextTool>
{
public:
	/** Identity tag — this is a Tool. */
	using FTags = TTypeList<FToolTag>;

	// ── 读（const，public——任意方可读）──

	[[nodiscard]] std::string_view GetCulture() const;

	/** Look up a translation; returns nullptr when absent. Thread-safe. */
	[[nodiscard]] const std::string* FindTranslation(std::string_view InNamespace, std::string_view InKey, std::string_view InCulture) const;

protected:
	// ── 写（protected，仅调度器 / friend 自由函数）──

	void SetCulture(std::string InCulture);

	/** Register one translation for (Namespace, Key) under a culture. Thread-safe. */
	void AddTranslation(std::string_view InNamespace, std::string_view InKey, std::string_view InCulture, std::string Text);

	/** Load translations from a JSON array of objects. */
	void LoadTranslationsFromJson(std::string_view JsonText);

	/** Clear the translation catalog and reset culture to "en-US". Lifecycle write. */
	void Clear();

	template <typename TExtension, typename TStage>
	friend bool Maho::ExecuteExtension(TStage Stage);

private:
	mutable std::mutex Mutex;
	std::string CurrentCulture = "en-US";
	std::unordered_map<std::string, std::string> Catalog;
};

} // namespace Text

} // namespace Maho
