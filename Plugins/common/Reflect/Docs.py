# -*- coding: utf-8 -*-
# Reflect 插件文档内容（由 Tools/plugin_docs.py 执行，docs_builder 以变量 D 注入）。

# ══════════════════════════════════════════════════════════════════════════════
# Public/Reflect.h —— 编译期反射（refl-cpp 包装）
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Reflect.h", Title="Reflect.h —— 编译期反射（第三方库 + 语法糖，纯库）",
         Desc="把 `refl-cpp`（header-only、MIT）连同少量语法糖一起打包："
              "消费方 `#include <Reflect.h>` 就能用 `refl::*` 与本插件的 `MAHO_*` 宏。\n"
              "**为什么由一个插件来打包第三方库**：引擎核心按设计**零第三方依赖**，"
              "而反射又是若干上层功能（序列化、编辑器属性面板）的共同需要；"
              "把它做成一个可安装的插件，谁需要谁依赖 —— 不需要的模块既不多此一次编译，"
              "也不在链接里多背一份。\n"
              "**为什么外层还要包一层宏**：`refl-cpp` 的名字与用法细节属于库的内部约定，"
              "包一层后引擎侧只依赖 `MAHO_*` 这一小组语义化名字，将来换库 / 升级只改这一处。\n"
              "**本插件没有单例、没有状态、没有生命周期**：纯函数式模板，"
              "给成服务层只会凭空制造一个需要等待的 stage。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("<refl.hpp>", "`refl-cpp` 本体（header-only）。**为什么随插件打包而不是让项目自己装**："
                     "反射的产品形态决定了「版本必须一致」—— 元数据特化的布局一旦不同版本混用，"
                     "报错会出现在很远的地方；打包成一个可安装单元就把版本钉死在一处")
D.Row("<cstddef>", "`std::size_t` —— 成员遍历时回调的**编译期索引**类型")

D.Card("使用须知（为什么这么用）",
       "`refl-cpp` 靠**全局作用域的元数据特化**工作，这决定了两条必须遵守的用法约束 —— "
       "违反它们时编译器给出的错误（如 `does not support reflection`）离真正原因很远，"
       "所以在这里显式写明：")
D.Table("约束", "原因")
D.Row("`MAHO_REFLECT` 必须在**全局命名空间**展开",
      "库会在全局作用域注入 `namespace refl_impl::metadata`；"
      "写在某个 namespace 里就会在错误的地方做特化，编译直接失败")
D.Row("被反射的类型需先有完整定义",
      "`field(x)` 要取成员指针，必须能看到类型的完整布局（前向声明不够）")

D.Card("宏一览")
D.Table("宏", "签名 → 展开")
D.Row("MAHO_REFLECT(Type, ...)", "`REFL_AUTO(type(Type), __VA_ARGS__)` —— "
                                 "声明一个可反射类型；`...` 是 `field(成员), field(成员), ...` 列表，"
                                 "也可以加 `bases<基类>`（如 `MAHO_REFLECT(FUnit, bases<FEntity>, field(hp));`）")
D.Row("MAHO_FOR_EACH_MEMBER(Type, Visitor)", "`refl::util::for_each(refl::reflect<Type>().members, Visitor)` —— "
                                             "遍历**全部**成员（含成员函数）")
D.Row("MAHO_MEMBER_COUNT(Type)", "`refl::reflect<Type>().members.size` —— 成员个数（**编译期**常量）")
D.Row("MAHO_MEMBER_DESCRIPTOR(Type)", "`refl::reflect<Type>().members` —— 成员描述符集合本身")
D.Row("MAHO_FOR_EACH_FIELD(Type, Visitor)", "`refl::for_each(refl::reflect<Type>().readable_members, Visitor)` —— "
                                            "只遍历**可读字段**（跳过成员函数）")

D.Macro("MAHO_REFLECT", "REFL_AUTO(type(Type), __VA_ARGS__)",
        Desc="为一个类型声明反射元数据（`refl-cpp` 的 `REFL_AUTO` 包装）。\n"
             "`Type` 之后的参数是成员描述列表：`field(名字)` 描述数据成员、"
             "`bases<...>` 声明基类（基类成员也会被纳入）。\n"
             "**必须在全局命名空间展开**（见上表）—— 这是最容易踩、且报错信息最不直观的一条。")

D.Macro("MAHO_FOR_EACH_MEMBER", "refl::util::for_each(refl::reflect<Type>().members, Visitor)",
        Desc="遍历一个可反射类型的**全体成员**。回调用 `[](auto member, auto index)` 接："
             "`member.name`（`const char*`）、`member.value`、"
             "`member.is_readable()` / `is_writable()`（成员函数用 `callable()`）。\n"
             "**为什么遍历用回调而不是返回容器**：成员描述符是编译期生成的异构集合，"
             "没有一个运行期容器能装下它们；回调是唯一能对内联展开的形态（零运行期开销）。")

D.Macro("MAHO_MEMBER_COUNT", "refl::reflect<Type>().members.size",
        Desc="成员个数（编译期常量）。**用在哪儿**：静态断言、数组大小、"
             "以及「按索引生成字段」的展开 —— 都是需要常量表达式的位置。")

D.Macro("MAHO_MEMBER_DESCRIPTOR", "refl::reflect<Type>().members",
        Desc="成员描述符集合本身（不遍历）。需要把「成员列表」当对象继续传给别的模板时用它，"
             "例如再套一层 `refl::util::for_each` 或做编译期筛选。")

D.Macro("MAHO_FOR_EACH_FIELD", "refl::for_each(refl::reflect<Type>().readable_members, Visitor)",
        Desc="只遍历**可读字段**（成员函数被自动跳过）。\n"
             "**为什么与 `FOR_EACH_MEMBER` 并存**：序列化 / 属性面板这类场景只关心数据成员，"
             "如果遍历含成员函数，每个调用点都要自己 `if` 一次 —— "
             "把这个判断下沉成「取 readable_members」，调用点就不再重复同一段过滤。")
