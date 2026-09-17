# -*- coding: utf-8 -*-
# Compress 插件文档内容（由 Tools/plugin_docs.py 执行，docs_builder 以变量 D 注入）。

# ══════════════════════════════════════════════════════════════════════════════
# Public/Compress.h —— zstd 压缩 / 解压
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Compress.h", Title="Compress.h —— zstd 压缩 / 解压（纯库）",
         Desc="字节缓冲的压缩积木：三个**自由函数**，没有任何状态、单例或生命周期。\n"
              "**为什么是纯库而不是服务层**：压缩是「给一段字节、还一段字节」的纯计算，"
              "不持有跨帧状态；做成服务层只会逼调用方去等一个 stage，"
              "而它既不初始化什么、也不需要在关闭时排空什么。\n"
              "**为什么没有暴露 zstd 的 API**：`Private/Compress.cpp` 是唯一的 zstd 边界 —— "
              "头文件只认 `std::vector<std::uint8_t>`，将来换实现（换库 / 换算法）不需要动任何调用点。\n"
              "**为什么失败返回 `nullopt` 而不抛异常**：压缩数据可能来自磁盘或网络（损坏、截断都属常态），"
              "调用方必须显式处理坏数据 —— 返回值风格把「可能失败」写在签名里，"
              "而不是藏在异常里让人忘记接。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("<cstddef>", "`std::size_t` —— `GetDecompressedSize` 的返回类型")
D.Row("<cstdint>", "`std::uint8_t` —— **字节**的规范表示，避免 `char` 的符号性歧义")
D.Row("<optional>", "`std::optional` —— 把「失败」编码进返回类型（无异常）")
D.Row("<vector>", "输入 / 输出都用连续字节缓冲（可直接对接文件与网络包）")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("std::optional<std::vector<std::uint8_t>> Compress(const std::vector<std::uint8_t>& Data, int Level = 0)",
      "用 zstd 压缩。`Level` 是 zstd 的 1..22（0 = 用默认档；负档 / fast 模式不支持）。"
      "失败（编码器报错 / 目标缓冲不足）返回 `nullopt` —— **不抛异常**，坏输入不该打断主循环")
D.Row("std::optional<std::vector<std::uint8_t>> Decompress(const std::vector<std::uint8_t>& Data)",
      "解压一个 zstd 载荷。输入不是合法 zstd、或解压后长度不可信 / 算不出来时返回 `nullopt`。"
      "**为什么必须先拿到长度再分配**：zstd 帧头里带解压后大小，先取值就能一次分配到位，"
      "不必用「猜一个大小、不够再翻倍」的循环")
D.Row("std::optional<std::size_t> GetDecompressedSize(const std::vector<std::uint8_t>& Data)",
      "只读帧头，不真解压：用来预知解压后大小（损坏输入返回 `nullopt`）。"
      "存放压缩数据时把长度一并记下，读取端就能一次性分配")
