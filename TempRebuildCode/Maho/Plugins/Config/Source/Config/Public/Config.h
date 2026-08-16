#pragma once

#include "ConfigApi.h"
#include <Engine.h>

#include <optional>
#include <string_view>
#include <toml++/toml.hpp>

namespace Maho
{

namespace Config
{

/** Configuration file extension (TOML). Pre-app toolkit (driven by EToolStage). */
class MAHO_CONFIG_API FConfig final : public TExtension<EToolStage, FConfig>
{
public:
	[[nodiscard]] bool ExecuteStage(EToolStage Stage) override;

	/** Parse a TOML file; returns false on failure. */
	bool Load(std::string_view Path);

	/** Dotted-key lookup (e.g. "Window.Title"). Returns nullopt when missing or type-mismatched. */
	template <typename T>
	std::optional<T> Get(std::string_view Key) const;

	bool Has(std::string_view Key) const;

private:
	friend TSingleton<FConfig>;
	FConfig() = default;

	const toml::node* FindNode(std::string_view Key) const;

	toml::table Table{};
};

template <typename T>
std::optional<T> FConfig::Get(std::string_view Key) const
{
	const toml::node* Node = FindNode(Key);
	if (Node == nullptr)
	{
		return std::nullopt;
	}

	return Node->value<T>();
}

} // namespace Config

} // namespace Maho
