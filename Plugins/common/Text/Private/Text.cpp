#include "Text.h"

#include <nlohmann/json.hpp>

namespace Maho::Text
{

FTextManager& FTextManager::Get()
{
	static FTextManager Instance;
	return Instance;
}

namespace
{
	// Catalog key = Namespace + US + Key + US + Culture (US = unit separator 0x1f,
	// avoids collisions where "a" + "bc" == "ab" + "c").
	[[nodiscard]] std::string MakeKey(std::string_view InNamespace, std::string_view InKey, std::string_view InCulture)
	{
		std::string Key;
		Key.reserve(InNamespace.size() + InKey.size() + InCulture.size() + 2);
		Key.append(InNamespace);
		Key.push_back('\x1f');
		Key.append(InKey);
		Key.push_back('\x1f');
		Key.append(InCulture);
		return Key;
	}
}

FText::FText(std::string InNamespace, std::string InKey, std::string InSource)
	: Namespace(std::move(InNamespace))
	, Key(std::move(InKey))
	, Source(std::move(InSource))
{
}

std::string FText::Resolve() const
{
	const FTextManager& Manager = FTextManager::Get();
	const std::string* Translated = Manager.FindTranslation(Namespace, Key, Manager.GetCulture());
	return Translated ? *Translated : Source;
}

void FTextManager::Initialize(FEngineBase& Engine)
{
	(void)Engine;
	std::lock_guard<std::mutex> Lock(Mutex);
	Catalog.clear();
	CurrentCulture = "en-US";
}

void FTextManager::Shutdown(FEngineBase&)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	Catalog.clear();
}

std::string_view FTextManager::GetCulture() const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	return CurrentCulture;
}

void FTextManager::SetCulture(std::string InCulture)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	CurrentCulture = std::move(InCulture);
}

void FTextManager::AddTranslation(std::string_view InNamespace, std::string_view InKey, std::string_view InCulture, std::string Text)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	Catalog[MakeKey(InNamespace, InKey, InCulture)] = std::move(Text);
}

void FTextManager::LoadTranslationsFromJson(std::string_view JsonText)
{
	const nlohmann::json Root = nlohmann::json::parse(JsonText.begin(), JsonText.end());
	for (const auto& Entry : Root)
	{
		AddTranslation(
			Entry.at("Namespace").get<std::string>(),
			Entry.at("Key").get<std::string>(),
			Entry.at("Culture").get<std::string>(),
			Entry.at("Text").get<std::string>());
	}
}

const std::string* FTextManager::FindTranslation(std::string_view InNamespace, std::string_view InKey, std::string_view InCulture) const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	const auto It = Catalog.find(MakeKey(InNamespace, InKey, InCulture));
	return It != Catalog.end() ? &It->second : nullptr;
}

} // namespace Maho::Text
