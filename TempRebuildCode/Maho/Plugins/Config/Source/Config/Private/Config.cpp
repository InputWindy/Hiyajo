#include <Config.h>

namespace Maho::Config
{

bool FConfig::ExecuteStage(EToolStage Stage)
{
	switch (Stage)
	{
	case EToolStage::Init:
		Table = toml::table{};
		break;

	case EToolStage::Shutdown:
		Table = toml::table{};
		break;
	}

	return true;
}

bool FConfig::Load(std::string_view Path)
{
	try
	{
		Table = toml::parse_file(Path);
		return true;
	}
	catch (const toml::parse_error&)
	{
		return false;
	}
}

bool FConfig::Has(std::string_view Key) const
{
	return FindNode(Key) != nullptr;
}

const toml::node* FConfig::FindNode(std::string_view Key) const
{
	const toml::node* Current = &Table;
	std::size_t Begin = 0;

	while (true)
	{
		const std::size_t Dot = Key.find('.', Begin);
		const std::string_view Segment = (Dot == std::string_view::npos)
			? Key.substr(Begin)
			: Key.substr(Begin, Dot - Begin);

		if (Segment.empty())
		{
			return nullptr;
		}

		const auto* CurrentTable = Current->as_table();
		if (CurrentTable == nullptr)
		{
			return nullptr;
		}

		const auto* Next = CurrentTable->get(Segment);
		if (Next == nullptr)
		{
			return nullptr;
		}

		Current = Next;

		if (Dot == std::string_view::npos)
		{
			return Current;
		}

		Begin = Dot + 1;
	}
}

} // namespace Maho::Config

// ── Dynamic plugin entry (runtime load/unload via FPluginManager) ──

namespace
{

class FConfigAdapter final : public Maho::IExtension<Maho::EToolStage>
{
public:
	[[nodiscard]] bool ExecuteStage(Maho::EToolStage Stage) override
	{
		return Maho::Config::FConfig::Get().ExecuteStage(Stage);
	}
};

} // namespace

extern "C" MAHO_CONFIG_API Maho::IExtension<Maho::EToolStage>* CreateExtension()
{
	return new FConfigAdapter();
}
