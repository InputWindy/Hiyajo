#include <UIResource.h>

#include <UITheme.h>

#include <iterator>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace Maho { namespace UI {

namespace
{
std::mutex GResolverMutex;
FUIResourceResolver GResolver;   // 渲染侧注入；空 = 未注入（按缺省外观绘）
}

void SetUIResourceResolver(FUIResourceResolver Resolver)
{
	std::scoped_lock Lock(GResolverMutex);
	GResolver = std::move(Resolver);
}

bool HasUIResourceResolver()
{
	std::scoped_lock Lock(GResolverMutex);
	return static_cast<bool>(GResolver);
}

FUIResolvedResource ResolveUIResource(const FUIName& Resource, bool bIsFont)
{
	FUIResourceResolver Resolver;
	{
		std::scoped_lock Lock(GResolverMutex);
		Resolver = GResolver;             // 拷贝后在锁外调用（解析器可能回头进 UI API）
	}
	if (!Resolver)
	{
		FUIResolvedResource Out;
		Out.Name = Resource;
		return Out;
	}
	return Resolver(Resource, bIsFont);
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
