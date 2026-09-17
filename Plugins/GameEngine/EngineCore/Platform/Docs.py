#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Platform 插件的文档内容。

运行器（Tools/plugin_docs.py）把 docs_builder 作为变量 D 注入本文件 —— 只声明，不 import。
"""

# ══════════════════════════════════════════════════════════════════════════════
# Public/Platform.h
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Platform.h", Title="Platform.h —— 原生表面 + 输入事件",
         Desc="平台层只提供两样东西：**原生表面**（给 RHI）与**原始输入**（给输入事件经纪 / ImGui 驱动）。"
              "「窗口」这个概念被收进单一访问函数 `IPlatform::GetNativeWindow()` 里 —— "
              "因为不是每个平台都有窗口（无头、Android surface、iOS view），"
              "把窗口语义藏在一个访问器背后，上层就不必到处写 `if (headless)`。\n"
              "输入也刻意分成两条语义不同的通道：**可重复读的快照**（`ReadInput`：鼠标位置、"
              "按键按下状态、修饰键、焦点 —— 任何读者任何时刻读到的都一样，不消费）与"
              "**可排空的事件流**（`DrainInputEvents`：边沿 / 字符 / 按钮动作，读走即清）。"
              "滚轮是唯一的例外：它没有「持久位置」可做快照，所以累积后按读取交换清零 —— "
              "因此 `ReadInput` 里的滚轮字段**不是**快照，单消费者要用 `ConsumeMouseWheelXY/Y`。\n"
              "引擎 core 完全不认识 ImGui / GLFW 类型：键码就是稳定的整数（GLFW 键码），"
              "由 UI 层自行映射 —— 这样平台层不会因为某个 UI 后端而长出一条依赖。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Interface.h", "`IPipeline` 能力组合器 —— `FPlatform` 声明的阶段序列靠它")
D.Row("Engine/Engine.h", "10 个 stage 接口（`IPreInit` / `IInit` / … / `IShutdown`）与 `FEngineBase`")
D.Row("<Maho.h>", "聚合头：拿到 `FFrameExtension` + `MAHO_DECLARE_FRAME`（本插件经它一条就够）")
D.Row("<cstdint>", "输入字段全部用定宽整型 —— 事件负载要与底层回调逐位一致，不能随平台改变宽度")
D.Row("<functional>", "三个 `std::function` 钩子：事件泵 / 关闭查询 / 工具包窗口取用 —— "
                      "让后端差异（GLFW / EGL / 空后端）在头里只留下函数签名")
D.Row("<memory>", "`std::unique_ptr<IPlatform> Surface` —— 后端实现只在 cpp 的匿名命名空间里，"
                  "头里只留抽象接口")
D.Row("<mutex>", "`InputMutex`：输入快照的生产者与读者分属不同线程（见下方字段说明）")
D.Row("<string> / <string_view> / <vector>", "窗口标题；排空式事件流与拖入文件列表的追加目标")

D.Card("前向声明与宏守卫（不是 include，但同样是接口的一部分）")
D.Table("声明", "为什么需要它")
D.Row("struct GLFWwindow;",
      "GLFW 是**不透明工具包句柄**：只要指针，不要 `glfw.h`。于是 ImGui 的 glfw 后端能拿到它挂输入回调，"
      "而本头不必把 GLFW 的宏污染带进每一个包含它的文件")
D.Row("#ifdef CreateWindow / #undef CreateWindow",
      "Win32 的 `<Windows.h>`（经 GLFW 间接引入）把 `CreateWindow` 定义成 `CreateWindowW`，"
      "会毁掉 `FPlatform::CreateWindow` 这个名字。守卫必须在头里 —— 删除它，"
      "全工程的 `CreateWindow` 调用会在某些包含顺序下静默改名")

D.Card("自由函数 / 别名")
D.Table("签名", "说明")
D.Row("MAHO_API FPlatform* GetPlatform()",
      "全局实例访问器。**跨 DLL 走函数而不是导出裸变量** —— 裸变量的地址在每个 DLL 里可能各有一份，"
      "而函数的地址唯一；RHI 通过它拿到实例再读 `GetNativeWindow()`，因此不需要单例机制")
D.Row("using FNativeSurface = void*",
      "原生表面句柄的不透明化（`GLFWwindow*` / `EGLContext` / `ANativeWindow*` / `UIView*` …）。"
      "引擎内部只把它当「一个指针」转交给 RHI，因此平台数量不会增加类型数量")

D.Enum("MInputEventType", Base="std::uint8_t",
       Desc="GLFW 在 `glfwPollEvents` 期间能投递的**全部**输入事件类型。没有精选子集 —— "
            "平台能产生的每一条输入都被转发，由消费者（输入经纪 / ImGui 驱动）自己决定用哪部分。"
            "命名带 `M` 前缀（`MInputEventType` / `MInputEvent` / `MInputContext`）是刻意的："
            "它们是**中立记录**，不属于任何具体平台后端。")
D.Field("None = 0", "无事件（默认值）")
D.Field("Key", "键盘按键：填 Key、Scancode、Action、Mods")
D.Field("Char", "Unicode 文本输入：填 Codepoint")
D.Field("MouseMove", "光标移动：填 X、Y")
D.Field("MouseButton", "鼠标按键：填 Key（GLFW 按钮下标）、Action、Mods")
D.Field("Scroll", "滚轮：填 X、Y（增量）")
D.Field("CursorEnter", "光标进出窗口：填 Bool（是否进入）")
D.Field("WindowFocus", "窗口焦点变化：填 Bool（是否获得焦点）")

D.Struct("MInputEvent", Desc="单条原始输入事件，与对应 GLFW 回调的负载**逐字段对齐**。"
                             "做成无 union 的扁平记录：union 让「哪个字段有效」依赖外部约定且容易读错，"
                             "而这里多出的几个 int / float 相对一次事件泵的开销可以忽略 —— "
                             "换取的是读代码时不需要查表。与该 Type 无关的字段保持默认值。")
D.SetAccess("public")
D.Field("MInputEventType Type = MInputEventType::None", "事件类型（决定下面哪些字段有效）")
D.Field("std::int32_t Key = 0", "GLFW 键码（Key）/ GLFW 按钮下标（MouseButton）")
D.Field("std::int32_t Scancode = 0", "硬件扫描码（Key）")
D.Field("std::uint8_t Action = 0", "`GLFW_PRESS`/`RELEASE`/`REPEAT` 或鼠标按钮动作")
D.Field("std::uint32_t Codepoint = 0", "Unicode 码点（Char）")
D.Field("std::uint16_t Mods = 0", "`GLFW_MOD_*` 位掩码")
D.Field("float X = 0.f", "光标位置（MouseMove）/ 滚动增量（Scroll）")
D.Field("float Y = 0.f", "光标位置（MouseMove）/ 滚动增量（Scroll）")
D.Field("bool Bool = false", "CursorEnter / WindowFocus 的布尔负载")

D.Struct("MInputContext", Desc="平台与 UI 功能之间的**中立输入快照**。写者是 Platform"
                               "（它的 GLFW 回调在窗口循环线程里跑），读者是 UI 功能（渲染线程），"
                               "所以它按「可重复读的状态快照」设计：任何读者拿到同一份值，什么都不消费 —— "
                               "这使多个消费者可以各自独立地读，而不需要谁先谁后的约定。\n"
                               "例外是滚轮：它没有可持久保存的「位置」，因此按累积 + 读取交换处理，"
                               "语义上也只允许单消费者（编辑器滚 Console；游戏 UI 不滚）。"
                               "边沿 / 字符 / 焦点同样不属于快照 —— 它们是瞬时的，走 `DrainInputEvents`。")
D.SetAccess("public")
D.Field("static constexpr int KeyCount = 512", "按键状态表长度（远大于 `GLFW_KEY_LAST`，"
                                               "留出余量以免键码越界写坏相邻字段）")
D.Field("float MouseX = 0.0f / float MouseY = 0.0f", "光标位置（快照）")
D.Field("bool MouseButtons[3] = { false, false, false }", "左 / 中 / 右按下状态（快照）")
D.Field("float MouseWheelX = 0.0f / float MouseWheelY = 0.0f", "自上次读取以来累积的滚轮增量"
                                                              "（**交换清零**，因此不是快照 —— "
                                                              "单消费者请用 `ConsumeMouseWheelXY/Y`）")
D.Field("bool KeyDown[KeyCount] = {}", "键盘按下状态，按 GLFW 键码索引（下标 0..KeyCount-1）")
D.Field("std::uint16_t Mods = 0", "当前 `GLFW_MOD_*`（由键 / 按钮回调推送）")
D.Field("bool MouseEntered = false", "光标是否在窗口内容区内")
D.Field("bool WindowFocused = false", "窗口是否具有输入焦点")

D.Class("IPlatform", Desc="平台层向后端暴露的**最小**接口：只有原生表面。"
                          "把它保持这么小是刻意的 —— 每多一个虚函数，就把更多平台概念固化进上层；"
                          "而 RHI 真正需要的只有「表面句柄」这一件事，其余（尺寸、事件、"
                          "关闭请求）都由具体实现类直接提供，不经这层抽象。")
D.SetAccess("public")
D.Interface("virtual ~IPlatform() = default", "虚析构：实现类（GLFW / EGL 后端）在 cpp 的匿名命名空间里，"
                                              "必须能通过基类指针正确销毁")
D.Interface("[[nodiscard]] virtual FNativeSurface GetNativeWindow() const = 0",
            "原生窗口 / 表面句柄；无头或创建失败时返回 `nullptr` —— 调用方必须把它当合法返回值处理，"
            "不能假定表面一定存在")

D.Class("FPlatform", Base="FFrameExtension + IPipeline<IPreInit, IInit, IPostInit, IBeginFrame, ITick, "
                          "IEndFrame, IExit, IPreShutdown, IShutdown, IPostShutdown>",
        Desc="平台系统（一个引擎帧）：原生表面 + 事件。**它挂在完整阶段序列上**，"
             "因为引擎循环正是通过它推进的：`Tick` → 泵事件 + `ShouldClose` → "
             "请求引擎退出；`BeginFrame`/`EndFrame` 则把「泵事件」放在帧的两端，"
             "让输入在整帧内保持稳定。\n"
             "后端是**惰性创建**的：`Initialize` 不做任何事，`CreateWindow` / `CreateHeadlessContext` "
             "才按平台选后端（GLFW 或 EGL pbuffer），`DestroyWindow` 可以随时切回无头 —— "
             "于是「无表面」是一个正常状态而不是错误状态，这也解释了 `GetNativeWindow()` 为什么会返回 `nullptr`。\n"
             "三个 `std::function` 钩子（事件泵 / 关闭查询 / 工具包窗口）是后端差异在头里的全部残留："
             "具体后端类型（`FGlfwWindow` / `FEGLHeadlessWindow`）只活在 cpp 的匿名命名空间里。")
D.SetAccess("public")
D.Interface("MAHO_DECLARE_FRAME(FPlatform)",
            "声明帧身份与工厂符号（稳定的字符串名字）。**必须放在类体最前** —— "
            "它是这个类能被 `FFrameBuilder` 按 DLL 路径装载的凭据")
D.Interface("FPlatform()", "构造：只初始化字段，不创建任何后端（后端惰性创建，见 `Initialize` 的说明）")
D.Interface("~FPlatform() override", "析构：释放后端。引擎的拆解顺序保证帧在宿主卸载前被反初始化")
D.Interface("bool CreateWindow(int Width, int Height, std::string_view Title)",
            "按当前平台选窗口后端并创建窗口（无头构建 / 非目标平台返回空后端，返回 false）")
D.Interface("bool CreateHeadlessContext(int Width, int Height)",
            "创建无头渲染上下文（Linux 上是 EGL pbuffer）—— 供无窗口的渲染 / 测试路径使用")
D.Interface("void DestroyWindow()",
            "销毁后端（切到无头）。它是**合法的常规操作**而不是错误路径："
            "此后 `IsHeadless()` 为真、`GetNativeWindow()` 为 `nullptr`")
D.Interface("void PollEvents()", "泵一次平台事件（由 `Tick` 调用）。引擎没有隐式阶段循环，"
                                 "所以事件泵必须是显式的 —— 不泵就不刷新输入")
D.Interface("[[nodiscard]] FNativeSurface GetNativeWindow() const",
            "交给 RHI 的原生表面句柄；无头时为 `nullptr`")
D.Interface("[[nodiscard]] bool IsHeadless() const", "判据就是 `Surface == nullptr` —— 无头与"
                                                     "「创建失败」在这里是同一个可观察状态")
D.Interface("[[nodiscard]] bool ShouldClose() const",
            "是否有窗口关闭请求（无头或无事件时为 false）")
D.Interface("[[nodiscard]] std::uint32_t GetWindowWidth() const", "窗口宽（来自 `DefaultEngine.ini`）")
D.Interface("[[nodiscard]] std::uint32_t GetWindowHeight() const", "窗口高")
D.Interface("[[nodiscard]] GLFWwindow* GetToolkitWindowHandle() const",
            "原始工具包窗口句柄，专供 ImGui 的 glfw 后端（它要在上面挂输入回调）。"
            "这是一条**窄桥**：`GetNativeWindow()` 给的 HWND 仍是 RHI 用的那个 —— "
            "两个用途（创建交换链 / 挂工具包回调）需要不同句柄，混用会各自失败")
D.Interface("void ReadInput(MInputContext& Out) const",
            "把当前输入快照拷进 `Out`（鼠标位置/按钮、键盘按下状态、修饰键、光标在内、焦点）。"
            "任何读者每帧都可以调，**可重复读、不消费**；其中滚轮字段不是快照")
D.Interface("void DrainInputEvents(std::vector<MInputEvent>& Out) const",
            "排空自上次调用以来累积的**完整原始事件流**（边沿、字符、光标/按钮/键/滚轮事件、"
            "光标进出、窗口焦点）：追加到 `Out` 并清空平台缓冲。它是 `ReadInput` 的增量对应物")
D.Interface("void DrainDroppedFiles(std::vector<std::string>& Out) const",
            "排空自上次调用以来被拖到窗口上的文件路径（文件管理器的 OS 拖放）：追加到 `Out` 并清空缓冲。"
            "路径是**物理绝对路径**（引擎的窄字符约定）；平台不做虚拟路径映射 —— "
            "那是 `FPaths` 在上层的事")
D.Interface("void ConsumeMouseWheelXY(float& OutX, float& OutY)",
            "取走累积的滚轮增量（GLFW 刻度单位）并清零。**单消费者**（编辑器）")
D.Interface("[[nodiscard]] float ConsumeMouseWheelY()",
            "只取 Y 轴滚轮增量并清零；自上次调用以来没滚过则返回 0")
D.SetAccess("private")
D.Interface("void PreInitialize(FEngineBase&) override {}", "阶段入口（空实现）：此阶段无工作")
D.Interface("void Initialize(FEngineBase& Engine) override",
            "阶段入口：**空的启动** —— 后端惰性创建，所以这里不建窗口也不读配置")
D.Interface("void PostInitialize(FEngineBase&) override {}", "阶段入口（空实现）")
D.Interface("void Shutdown(FEngineBase& Engine) override",
            "阶段入口：按与初始化对称的顺序拆掉后端。平台是「表面」的提供者，"
            "所以它的 Shutdown 必须晚于使用表面的渲染层（那条次序由消费方声明依赖表达）")
D.Interface("void BeginFrame(FEngineBase& Engine) override", "阶段入口：帧首（泵事件的第一个落点）")
D.Interface("void Tick(FEngineBase& Engine) override",
            "阶段入口：每帧的核心 —— `PollEvents` + `ShouldClose` → `Engine.RequestExit()`")
D.Interface("void EndFrame(FEngineBase& Engine) override", "阶段入口：帧尾（泵事件的第二个落点）")
D.Interface("void RequestExit(FEngineBase& Engine) override", "阶段入口：`IExit` 阶段，响应退出请求")
D.Interface("void PreShutdown(FEngineBase&) override {}", "阶段入口（空实现）")
D.Interface("void PostShutdown(FEngineBase&) override {}", "阶段入口（空实现）")
D.Field("std::unique_ptr<IPlatform> Surface",
        "后端实例（唯一所有权）。**它同时是「是否无头」的判据** —— 不为空则有原生表面")
D.Field("std::function<void()> PollEventsFn", "后端的事件泵钩子")
D.Field("std::function<bool()> QueryShouldClose", "后端的关闭查询钩子")
D.Field("std::function<GLFWwindow*()> GlfwWindowFn",
        "取工具包窗口的钩子；空后端时为空函数 ⇒ `GetToolkitWindowHandle()` 返回 `nullptr`")
D.Field("mutable std::mutex InputMutex",
        "保护下面三项输入缓冲：写者是窗口循环线程（GLFW 回调），读者是渲染线程（UI 功能），"
        "因此是**跨线程**共享状态而不是单线程私有")
D.Field("MInputContext Input", "输入快照（持有该锁读写）")
D.Field("mutable std::vector<MInputEvent> InputEvents", "累积的原始事件流，由 `DrainInputEvents` 排空")
D.Field("mutable std::vector<std::string> DroppedFiles", "拖入的文件路径缓冲；生产者/消费者划分与 `InputEvents` 相同")
D.Field("std::uint32_t WindowWidth = 0", "窗口宽（0 = 尚未创建窗口）")
D.Field("std::uint32_t WindowHeight = 0", "窗口高")
