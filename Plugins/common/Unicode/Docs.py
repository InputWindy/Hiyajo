# -*- coding: utf-8 -*-
# Unicode 插件文档内容（由 Tools/plugin_docs.py 执行，docs_builder 以变量 D 注入）。

# ══════════════════════════════════════════════════════════════════════════════
# Public/Unicode.h —— UTF-8/16/32 与平台原生字符串转换
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Unicode.h", Title="Unicode.h —— UTF-8/16/32 + 平台原生字符串转换（纯库）",
         Desc="编码转换的积木：校验 + UTF-8 / UTF-16 / UTF-32 互转 + 平台原生字符串转换。"
              "**零第三方、零状态、零单例** —— 全是自由函数。\n"
              "**核心约定：引擎内部一律 UTF-8 `std::string`**。这条约定是本插件存在的理由："
              "如果每个模块各自决定用 UTF-16 还是本地代码页，那么每个跨模块的字符串参数"
              "都要重新谈一次编码。统一成 UTF-8 之后，转换只发生在**平台边界**"
              "（Windows API、文件路径），而不是到处采样。\n"
              "**为什么要 `ToNative` / `FromNative` 这对函数**：Windows 的 WinAPI 需要 UTF-16，"
              "其它平台基本是 UTF-8 passthrough；把「平台差异」收进这两个函数，"
              "调用点就只写一次而与平台无关（在非 Windows 上它们是零拷贝的转发）。\n"
              "**为什么要 `IsValidUtf8`**：外部来的字节（配置文件、网络、被别的工具写坏的文件）"
              "不保证合法；先校验再使用，可以把「非法编码」变成一个明确的返回值，"
              "而不是让下游在遍历字符时越界。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("<string>", "`std::string` / `std::wstring` / `std::u16string` / `std::u32string` —— 各编码的载体")
D.Row("<string_view>", "入参一律视图（**转换是只读操作**，不需要夺走调用方的字符串所有权）")

D.Card("平台原生字符串别名",
       "两个别名把「平台原生」这个词变成**类型**：调用 WinAPI / 文件系统时用 `FNativeString`，"
       "其余代码只认 `std::string`。于是「哪里需要转换」在编译期就能看出来（类型不同编译不过），"
       "而不是靠文档提醒")
D.Table("别名", "取值")
D.Row("FNativeString", "Windows：`std::wstring`（UTF-16，WinAPI 的 `W` 版接口）；"
                       "其它平台：`std::string`（UTF-8）")
D.Row("FNativeStringView", "对应视图：Windows `std::wstring_view`；其它平台 `std::string_view`")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("bool IsValidUtf8(std::string_view In)",
      "是否是**良构** UTF-8（拒绝孤立代理项与超长编码）。返回 `bool` 而不是抛异常："
      "「这段字节不合法」是可以正常处理的分支，不是程序错误")
D.Row("std::u16string Utf8ToUtf16(std::string_view In)",
      "UTF-8 → UTF-16（`std::u16string`，与 Windows 原生宽字符串同布局但**不等于** `std::wstring`，"
      "跨平台时别直接互换）")
D.Row("std::string Utf16ToUtf8(std::u16string_view In)",
      "UTF-16 → UTF-8（从平台边界收回来的方向）")
D.Row("std::u32string Utf8ToUtf32(std::string_view In)",
      "UTF-8 → UTF-32 —— 每个元素**恰好一个码点**，做逐字符处理（如排版、字形选择）时比 UTF-8 直接遍历安全")
D.Row("std::string Utf32ToUtf8(std::u32string_view In)",
      "UTF-32 → UTF-8（处理完收回标准表示）")
D.Row("FNativeString ToNative(std::string_view Utf8)",
      "UTF-8 → 平台原生（Windows 走 WinAPI 转 UTF-16；其它平台是 **passthrough，无拷贝语义**）。"
      "**只在真正的平台边界用**：把它当通用类型转换会制造无谓的宽窄串摆动")
D.Row("std::string FromNative(FNativeStringView Native)",
      "平台原生 → UTF-8（`ToNative` 的逆；Windows 上同时抹平了「WinAPI 返回的是原生串」这件事）")
D.Row("void EnsureConsoleUtf8()",
      "保证控制台按 UTF-8 输出（Windows：`SetConsoleOutputCP(CP_UTF8)`；其它平台空操作）。"
      "**为什么需要**：不设这一步，中文日志在 Windows 控制台会变成乱码 —— "
      "而这是开发期最常看的输出通道")

D.Alias("FNativeString", Target="Windows: std::wstring ｜ 其它平台: std::string",
        Desc="平台原生字符串类型（用 `#if defined(_WIN32)` 切换）。\n"
             "**为什么要这个别名而不是到处写 `std::wstring`**：调用点从此与平台无关，"
             "「这里需要原生串」这件事由类型名表达；将来若某平台需要别的表示，只改这一处。\n"
             "**注意 `std::wstring` 的宽度是实现定义的**：Windows 上是 2 字节（UTF-16），"
             "Linux 上常见 4 字节 —— 因此**跨平台代码不要用 `wstring` 当 UTF-16 的载体**，"
             "要 UTF-16 就用 `std::u16string`")

D.Alias("FNativeStringView", Target="Windows: std::wstring_view ｜ 其它平台: std::string_view",
        Desc="`FNativeString` 对应的只读视图 —— 传给 WinAPI / 文件系统 API 时不复制、不分配。"
             "**入参收视图**是这套转换函数的统一风格：转换本来就是「读源、产新」，"
             "没有理由要求调用方把源所有权交出来")
