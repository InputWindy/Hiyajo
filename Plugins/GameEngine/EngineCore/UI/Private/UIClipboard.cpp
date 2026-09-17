#include <UIClipboard.h>

#include <UIViewRegistry.h>

#include <utility>

namespace Maho { namespace UI {

// 同 UIResource.cpp：槽是 UI 层的成员（`FUIViewRegistry::ClipboardGet/Set`），不是文件级 static。
// 这里只转发 —— 理由见 UIViewRegistry.h 的能力槽注释。

FSubscriptionID BindUIClipboardHandlers(std::string_view Owner,
	FUIClipboardGetHandler Get, FUIClipboardSetHandler Set)
{
	FUIViewRegistry* Registry = GetUIViewRegistry();
	return Registry != nullptr
		? Registry->BindClipboardHandlers(Owner, std::move(Get), std::move(Set))
		: FSubscriptionID{ 0 };
}

void UnbindUIClipboardHandlers(FSubscriptionID Token)
{
	// 注册表已关（它在 Shutdown 里已经清过槽并报过错）时无需再交还。
	if (FUIViewRegistry* Registry = GetUIViewRegistry())
	{
		Registry->UnbindClipboardHandlers(Token);
	}
}

std::string GetUIClipboardText()
{
	FUIViewRegistry* Registry = GetUIViewRegistry();
	return Registry != nullptr ? Registry->ReadClipboard() : std::string{};
}

void SetUIClipboardText(std::string_view Text)
{
	if (FUIViewRegistry* Registry = GetUIViewRegistry())
	{
		Registry->WriteClipboard(Text);
	}
}

}} // namespace Maho::UI
