# -*- coding: utf-8 -*-
# Exception 插件文档内容（由 Tools/plugin_docs.py 执行，docs_builder 以变量 D 注入）。

# ══════════════════════════════════════════════════════════════════════════════
# Public/ExceptionApi.h —— 模块导出标签
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/ExceptionApi.h", Title="ExceptionApi.h —— 模块导出标签",
         Desc="本模块的 DLL 导出标签。独立成头，让「只想上报一次异常」的消费方"
              "只需包含这个极小的头拿到符号可见性约定。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT`（引擎核心提供，插件侧不自定义宏本体）")

D.Macro("MAHO_EXCEPTION_API", "MAHO_EXPORT / MAHO_IMPORT（随构建开关切换）",
        Desc="编译 `Exception.dll` 时构建系统定义 `MAHO_EXCEPTION_MODULE_EXPORTS` ⇒ `MAHO_EXPORT`；"
             "消费方展开成 `MAHO_IMPORT`。\n"
             "**为什么必须有**：`FException* GetExceptionCenter()` 的值要跨 DLL 使用，"
             "`FException` 的虚表与删除析构符只能在本模块生成 —— "
             "`dllimport` 禁止消费方各自生成一份，否则销毁时可能跳进已卸载的镜像（静默访问冲突）。")

# ══════════════════════════════════════════════════════════════════════════════
# Public/Exception.h —— 非致命异常广播中心
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Exception.h", Title="Exception.h —— 非致命异常广播中心（服务层）",
         Desc="**非致命**异常的中央分发点：业务代码捕获到一个可恢复的错误后调 `ReportException`，"
              "它同步广播给所有 `OnException` 订阅者（日志、遥测、崩溃上报各自订阅自己关心的）。\n"
              "**为什么这里永不 abort**：致命错误有另一条路（`Core/Fatal`）。"
              "把两者分开是刻意的 —— 「报告一个问题」和「终止进程」是两种完全不同的策略，"
              "混在一个接口里会让上层无法在不中止程序的前提下上报。\n"
              "**为什么带 `std::exception` 的重载**：绝大多数 `catch` 抓到的是标准异常，"
              "这个重载把 `what()` 取出来转发，省掉每个调用点重复写 `e.what()`。\n"
              "**为什么广播是同步的**：上报点常常紧接着就要用日志的顺序定位问题，"
              "异步投递会让日志与崩溃现场错位；广播本身只做转发，不应该有昂贵工作。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Delegate.h", "`TMulticastEvent` —— 订阅点（**关键积木**：广播语义、线程安全与解绑都在它内部）")
D.Row("<Maho.h>", "引擎聚合头（`FFrameExtension` / `IPipeline` / `Core/Export.h` 等）")
D.Row("<Engine/Engine.h>", "`FEngineBase` 与 10 个 stage 接口（本帧挂载 Init / Shutdown 那 6 个）")
D.Row("ExceptionApi.h", "`MAHO_EXCEPTION_API` —— 本模块的导出标签")
D.Row("<exception>", "`std::exception` 重载的形参类型")
D.Row("<string> / <string_view>", "消息的拥有者与观察者：订阅回调收到的是 `const std::string&`，"
                                  "上报入口用 `string_view` 零拷贝接住")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("MAHO_EXCEPTION_API FException* GetExceptionCenter()",
      "全局访问中心：`Initialize` 里发布、`Shutdown` 里清除；未上线时返回 `nullptr`"
      "（**调用方必须先判空** —— 上报路径不该依赖「中心一定在」这个假设）。"
      "返回函数而非导出裸变量：跨 DLL 的裸变量在很多编译组合下拿不到同一份实例")

D.Class("FException", Base="FFrameExtension, IPipeline<IPreInit, IInit, IPostInit, IPreShutdown, IShutdown, IPostShutdown>",
        Desc="异常中心本体 —— **一个帧**（挂 6 个 stage），建立与拆除由调度图排序。\n"
             "**为什么是帧而不是全局对象**：订阅者要在自己的 `IShutdown` 里解绑，"
             "而「先解绑再释放」这件事只有依赖图能保证；全局对象的销毁顺序是无序的，"
             "那会让广播撞上已销毁的订阅者。\n"
             "当前 4 个 stage 是空实现：把它们列进 `IPipeline` 是为了保留完整的时钟点，"
             "将来把工作挪到更早 / 更晚都不必改 stage 列表（改列表会让所有声明相对顺序的帧重新对表）。")
D.SetAccess("public")
D.Interface("MAHO_DECLARE_FRAME(FException)",
            "声明帧身份：稳定的字符串字面量名字 + `CreateFrame` 工厂 —— "
            "**名字是字面量**才让「按名字引用」这件事在模块卸载后依然安全")
D.Interface("void ReportException(std::string_view Message)",
            "上报一条非致命异常（广播给 `OnException`）。`Message` 是视图 ⇒ 实现内部会拷成 "
            "`std::string` 再广播，因为订阅者可能把消息留存到本帧之后")
D.Interface("void ReportException(const std::exception& Error)",
            "从标准异常上报：转发 `what()`。**为什么值得单独一个重载**："
            "`catch` 现场几乎总能拿到一个 `std::exception`，有这个重载就不必到处手写 `e.what()`，"
            "也避免了「忘了取 what() 只报类型名」这种信息量极低的上报")
D.Field("TMulticastEvent<void(const std::string&)> OnException",
        "订阅点：**按值传 `const std::string&`** 而不是 `string_view`，"
        "因为广播是同步的、订阅者可能在上报者返回前就持有它；"
        "`TMulticastEvent` 自带锁，因此可以在任意线程上报")
D.SetAccess("private")
D.Interface("void PreInitialize(FEngineBase&) override", "空：本帧不需要更早的时钟点")
D.Interface("void Initialize(FEngineBase& Engine) override",
            "发布全局访问中心（`GetExceptionCenter()` 从这里开始非空）")
D.Interface("void PostInitialize(FEngineBase&) override", "空")
D.Interface("void PreShutdown(FEngineBase&) override", "空")
D.Interface("void Shutdown(FEngineBase& Engine) override",
            "清除全局访问中心 —— 对称于 `Initialize`；此后 `GetExceptionCenter()` 返回 `nullptr`，"
            "调用方的判空分支因此是**必需的**，而不是防御性写法")
D.Interface("void PostShutdown(FEngineBase&) override", "空")
