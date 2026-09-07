#pragma once

// Text - localized text (engine Common, engine layer). FText handles + FTextManager
// catalog: SetCulture picks the culture, FText::Resolve() looks up the current
// culture's translation and falls back to the source. Depends on engine-fixed
// Json (nlohmann) for LoadTranslationsFromJson.
#include <Maho.h>

#include "TextApi.h"

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace Maho
{
namespace Text
{

/** Supported cultures. Strings are UTF-8; "support" here means the catalog can hold them. */
namespace Culture
{
	inline constexpr std::string_view English = "en-US";
	inline constexpr std::string_view Chinese = "zh-CN";
	inline constexpr std::string_view Japanese = "ja-JP";
}

/**
 * Localized text handle - stores {Namespace, Key, Source}. Resolve() looks up
 * the translation for the current culture and falls back to Source.
 *
 *   using namespace Maho::Text;
 *
 *   // Register translations once at startup (e.g. from config files).
 *   GetTextManager()->AddTranslation("MainMenu", "Title", Culture::Chinese, "Main Menu (zh)");
 *   GetTextManager()->AddTranslation("MainMenu", "Title", Culture::Japanese, "Main Menu (ja)");
 *
 *   const FText Title = FText("MainMenu", "Title", "Main Menu");
 *
 *   GetTextManager()->SetCulture(std::string(Culture::Chinese));
 *   const std::string Shown = Title.Resolve();   // "Main Menu (zh)"
 */
class FText
{
public:
	FText() = default;
	FText(std::string InNamespace, std::string InKey, std::string InSource);

	[[nodiscard]] std::string_view GetNamespace() const { return Namespace; }
	[[nodiscard]] std::string_view GetKey() const { return Key; }
	[[nodiscard]] std::string_view GetSource() const { return Source; }

	/** Resolve against the current culture - falls back to Source when absent. */
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

class FTextManager;

/** Global text manager accessor - returns FTextManager* (cross-DLL via function). */
MAHO_TEXT_API FTextManager* GetTextManager();

/** Localization manager: current culture + translation catalog (engine layer). */
class FTextManager
	: public FLayer<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>
{
public:
	MAHO_DECLARE_LAYER(FTextManager, "Text.dll");

	[[nodiscard]] std::string_view GetCulture() const;
	void SetCulture(std::string InCulture);

	/** Register one translation for (Namespace, Key) under a culture. Thread-safe. */
	void AddTranslation(std::string_view InNamespace, std::string_view InKey, std::string_view InCulture, std::string Text);

	/**
	 * Load translations from a JSON array of objects:
	 *   [{ "Namespace": "...", "Key": "...", "Culture": "...", "Text": "..." }, ...]
	 */
	void LoadTranslationsFromJson(std::string_view JsonText);

	/** Look up a translation; returns nullptr when absent. Thread-safe. */
	[[nodiscard]] const std::string* FindTranslation(std::string_view InNamespace, std::string_view InKey, std::string_view InCulture) const;

private:
	// -- engine pipeline stages (scheduler-only) --
	void PreInitialize(FEngineBase&) override {}
	void Initialize(FEngineBase& Engine) override;
	void PostInitialize(FEngineBase&) override {}
	void PreShutdown(FEngineBase&) override {}
	void Shutdown(FEngineBase& Engine) override;
	void PostShutdown(FEngineBase&) override {}

protected:
	FTextManager() = default;

	mutable std::mutex Mutex;
	std::string CurrentCulture = "en-US";
	std::unordered_map<std::string, std::string> Catalog;
};

} // namespace Text
} // namespace Maho
