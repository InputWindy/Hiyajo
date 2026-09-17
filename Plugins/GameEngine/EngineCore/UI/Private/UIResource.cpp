#include <UIResource.h>

#include <UITheme.h>
#include <UIViewRegistry.h>

#include <iterator>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace Maho { namespace UI {

// 解析能力的槽**不在本 TU**：它是 UI 层的成员（`FUIViewRegistry::Resolver`），生命期由图驱动。
// 这里的自由函数只做转发。为什么必须是转发而不是文件级 static —— 见 UIViewRegistry.h 的能力槽
// 注释：注册者死在别的 collector 的子树里（FExampleEditor 在 FRender 下），槽若挂在 CRT 的
// atexit 上，两个时刻毫无关联，析构时就会调用已卸载模块的闭包。

FSubscriptionID BindUIResourceResolver(std::string_view Owner, FUIResourceResolver Resolver)
{
	FUIViewRegistry* Registry = GetUIViewRegistry();
	return Registry != nullptr
		? Registry->BindResourceResolver(Owner, std::move(Resolver))
		: FSubscriptionID{ 0 };
}

void UnbindUIResourceResolver(FSubscriptionID Token)
{
	// 注册表已关（它在 Shutdown 里已经清过槽并报过错）时无需再交还。
	if (FUIViewRegistry* Registry = GetUIViewRegistry())
	{
		Registry->UnbindResourceResolver(Token);
	}
}

bool HasUIResourceResolver()
{
	FUIViewRegistry* Registry = GetUIViewRegistry();
	return Registry != nullptr && Registry->HasResourceResolver();
}

FUIResolvedResource ResolveUIResource(const FUIName& Resource, bool bIsFont)
{
	FUIViewRegistry* Registry = GetUIViewRegistry();
	if (Registry == nullptr)
	{
		FUIResolvedResource Out;
		Out.Name = Resource;
		return Out;
	}
	return Registry->ResolveResource(Resource, bIsFont);
}

// -- 字体图集登记 -------------------------------------------------------------------------

namespace
{
struct FFontKey
{
	void*         Context = nullptr;
	std::uint32_t Id = 0;
	float         Size = 0.f;

	bool operator==(const FFontKey& Other) const
	{
		return Context == Other.Context && Id == Other.Id && Size == Other.Size;
	}
};

struct FFontKeyHash
{
	std::size_t operator()(const FFontKey& Key) const
	{
		const std::size_t H = std::hash<void*>{}(Key.Context);
		const std::size_t N = std::hash<std::uint32_t>{}(Key.Id);
		return H ^ (N << 6) ^ (N >> 2) ^ (std::hash<float>{}(Key.Size) + 0x9e3779b9u);
	}
};

std::mutex GFontMutex;
std::unordered_map<FFontKey, void*, FFontKeyHash> GFonts;
} // namespace

void RegisterUIFont(void* Context, const FUIName& Font, float Size, void* NativeFont)
{
	std::scoped_lock Lock(GFontMutex);
	GFonts[FFontKey{ Context, Font.GetId(), Size }] = NativeFont;
}

void ClearUIFonts(void* Context)
{
	std::scoped_lock Lock(GFontMutex);
	if (Context == nullptr)
	{
		GFonts.clear();
		return;
	}
	for (auto It = GFonts.begin(); It != GFonts.end();)
	{
		It = (It->first.Context == Context) ? GFonts.erase(It) : std::next(It);
	}
}

void* FindUIFont(void* Context, const FUIName& Font, float Size)
{
	std::scoped_lock Lock(GFontMutex);
	const float Snapped = SnapFontSize(Size);
	if (const auto It = GFonts.find(FFontKey{ Context, Font.GetId(), Snapped }); It != GFonts.end())
	{
		return It->second;
	}
	if (const auto It = GFonts.find(FFontKey{ Context, Font.GetId(), 0.f }); It != GFonts.end())
	{
		return It->second;
	}
	return nullptr;
}

}} // namespace Maho::UI
