#include <UIClipboard.h>

#include <mutex>
#include <utility>

namespace Maho { namespace UI {

namespace
{
std::mutex GClipboardMutex;
FUIClipboardGetHandler GClipboardGet;
FUIClipboardSetHandler GClipboardSet;
} // namespace

void SetUIClipboardHandlers(FUIClipboardGetHandler Get, FUIClipboardSetHandler Set)
{
	std::scoped_lock Lock(GClipboardMutex);
	GClipboardGet = std::move(Get);
	GClipboardSet = std::move(Set);
}

std::string GetUIClipboardText()
{
	FUIClipboardGetHandler Get;
	{
		std::scoped_lock Lock(GClipboardMutex);
		Get = GClipboardGet;          // 拷贝后在锁外调用（处理器可能回头进 UI API）
	}
	return Get ? Get() : std::string{};
}

void SetUIClipboardText(std::string_view Text)
{
	FUIClipboardSetHandler Set;
	{
		std::scoped_lock Lock(GClipboardMutex);
		Set = GClipboardSet;
	}
	if (Set) { Set(Text); }
}

}} // namespace Maho::UI
