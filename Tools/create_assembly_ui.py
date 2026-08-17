# Run via Tools/create_assembly.bat / maho_python.bat — engine Tools/python only.
"""Maho new-assembly UI (CreateAssembly.bat) — unify plugin + project creation."""

from __future__ import annotations

import sys
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from maho_tools import (  # noqa: E402
	ENGINE_ROOT,
	create_plugin,
	create_project,
	inheritance_problems,
	is_valid_project_name,
	list_engine_plugins,
)

_CHECKED = "☑"
_UNCHECKED = "☐"

_PRODUCT_DLL = "动态链接库"
_PRODUCT_EXE = "可执行程序"


class CreateAssemblyApp(tk.Tk):
	def __init__(self) -> None:
		super().__init__()
		self.title("Maho — 创建程序集")
		self.geometry("720x560")
		self.minsize(640, 460)
		self.resizable(True, True)

		self.var_name = tk.StringVar(value="MyAssembly")
		self.var_dir = tk.StringVar(value=str(ENGINE_ROOT / "Maho" / "Plugins"))
		self.var_product = tk.StringVar(value=_PRODUCT_DLL)

		self._plugins: list[dict] = []
		self._checked: dict[str, bool] = {}
		self._app_tool_tree: ttk.Treeview
		self._app_app_tree: ttk.Treeview
		self._tool_tree: ttk.Treeview

		self._build()
		self.protocol("WM_DELETE_WINDOW", self.destroy)

	def _build(self) -> None:
		pad = {"padx": 12, "pady": 6}
		frm = ttk.Frame(self, padding=12)
		frm.pack(fill=tk.BOTH, expand=True)

		ttk.Label(frm, text="创建程序集 (Assembly)", font=("Segoe UI", 12, "bold")).grid(
			row=0, column=0, columnspan=3, sticky="w", **pad
		)

		ttk.Label(frm, text="名称").grid(row=1, column=0, sticky="w", **pad)
		ttk.Entry(frm, textvariable=self.var_name).grid(row=1, column=1, columnspan=2, sticky="ew", **pad)

		ttk.Label(frm, text="目录").grid(row=2, column=0, sticky="w", **pad)
		ttk.Entry(frm, textvariable=self.var_dir).grid(row=2, column=1, sticky="ew", **pad)
		ttk.Button(frm, text="浏览…", command=self._browse_dir).grid(row=2, column=2, sticky="e", **pad)

		ttk.Label(frm, text="程序集").grid(row=3, column=0, sticky="w", **pad)
		self.cmb_product = ttk.Combobox(frm, state="readonly", values=[_PRODUCT_DLL, _PRODUCT_EXE])
		self.cmb_product.grid(row=3, column=1, columnspan=2, sticky="ew", **pad)
		self.cmb_product.bind("<<ComboboxSelected>>", self._on_product_change)

		# 项目（tab：应用 / 工具）
		ttk.Label(frm, text="项目").grid(row=4, column=0, sticky="nw", **pad)
		self.notebook = ttk.Notebook(frm)
		self.notebook.grid(row=4, column=1, columnspan=2, sticky="nsew", **pad)

		# 应用 tab：左右两个勾选框（工具 | 应用）
		self._app_tab = ttk.Frame(self.notebook)
		self._app_tab.columnconfigure(0, weight=1, uniform="app")
		self._app_tab.columnconfigure(1, weight=1, uniform="app")
		self._app_tab.rowconfigure(0, weight=1)
		self._app_tool_panel = ttk.LabelFrame(self._app_tab, text="工具")
		self._app_app_panel = ttk.LabelFrame(self._app_tab, text="应用")
		self._app_tool_panel.grid(row=0, column=0, sticky="nsew", padx=(0, 4))
		self._app_app_panel.grid(row=0, column=1, sticky="nsew", padx=(4, 0))
		self._app_tool_tree = self._make_tree(self._app_tool_panel)
		self._app_app_tree = self._make_tree(self._app_app_panel)

		# 工具 tab：单个勾选框
		self._tool_tab = ttk.Frame(self.notebook)
		self._tool_tab.columnconfigure(0, weight=1)
		self._tool_tab.rowconfigure(0, weight=1)
		self._tool_panel = ttk.LabelFrame(self._tool_tab, text="工具")
		self._tool_panel.grid(row=0, column=0, sticky="nsew")
		self._tool_tree = self._make_tree(self._tool_panel)

		self.notebook.add(self._app_tab, text="应用")
		self.notebook.add(self._tool_tab, text="工具")

		ttk.Button(frm, text="创建", command=self._create).grid(row=5, column=1, columnspan=2, sticky="e", **pad)

		frm.columnconfigure(1, weight=1)
		frm.rowconfigure(4, weight=1)

		self._reload_plugins()

	def _make_tree(self, parent: ttk.Frame) -> ttk.Treeview:
		tree = ttk.Treeview(parent, show="tree", selectmode="none")
		scroll = ttk.Scrollbar(parent, orient=tk.VERTICAL, command=tree.yview)
		tree.configure(yscrollcommand=scroll.set)
		tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
		scroll.pack(side=tk.RIGHT, fill=tk.Y)
		tree.bind("<Button-1>", self._on_tree_click)
		return tree

	def _browse_dir(self) -> None:
		path = filedialog.askdirectory(initialdir=self.var_dir.get() or str(ENGINE_ROOT))
		if path:
			self.var_dir.set(path)

	def _reload_plugins(self) -> None:
		plugins_dir = Path(self.var_dir.get().strip())
		engine_root = self._engine_root_of(plugins_dir)
		self._plugins = list_engine_plugins(engine_root)
		self._apply()

	def _engine_root_of(self, plugins_dir: Path) -> Path:
		plugins_dir = Path(plugins_dir)
		if plugins_dir.name == "Plugins" and plugins_dir.parent.name == "Maho":
			return plugins_dir.parent.parent
		return ENGINE_ROOT

	def _on_product_change(self, _event=None) -> None:
		# EXE (project) lives outside the engine; DLL lives in Maho/Plugins.
		if self.cmb_product.get() == _PRODUCT_EXE:
			if self.var_dir.get() == str(ENGINE_ROOT / "Maho" / "Plugins"):
				self.var_dir.set(str(ENGINE_ROOT.parent))
		else:
			if self.var_dir.get() == str(ENGINE_ROOT.parent):
				self.var_dir.set(str(ENGINE_ROOT / "Maho" / "Plugins"))
		self._apply()

	def _apply(self) -> None:
		for tree in (self._app_tool_tree, self._app_app_tree, self._tool_tree):
			for item in tree.get_children():
				tree.delete(item)
		self._checked = {}

		is_exe = self.cmb_product.get() == _PRODUCT_EXE

		# EXE 本身就是应用，禁用"工具"tab
		self.notebook.tab(self._tool_tab, state="disabled" if is_exe else "normal")
		if is_exe:
			self.notebook.select(self._app_tab)

		for p in self._plugins:
			ext = p.get("Extension") or {}
			stage = ext.get("Stage", "EEngineStage")
			name = p["Name"]
			group = p.get("Group") or []

			self._checked[name] = False
			if stage == "EToolStage":
				# 工具：两个 tab 都放（应用 tab 左侧 + 工具 tab）
				self._insert(self._app_tool_tree, name, group, "tool")
				self._insert(self._tool_tree, name, group, "tool")
			else:
				# 应用：只在应用 tab 右侧
				self._insert(self._app_app_tree, name, group, "app")

	def _insert(self, tree: ttk.Treeview, name: str, group: list[str], stage: str) -> None:
		parent = ""
		for i in range(len(group)):
			gid = f"{stage}:group:" + "/".join(group[: i + 1])
			if not tree.exists(gid):
				tree.insert(parent, tk.END, iid=gid, text=group[i], open=True)
			parent = gid
		tree.insert(parent, tk.END, iid=name, text=self._label(name), open=True)

	def _label(self, name: str) -> str:
		mark = _CHECKED if self._checked.get(name) else _UNCHECKED
		return f"{mark} {name}"

	def _on_tree_click(self, event: tk.Event) -> None:
		item = event.widget.identify_row(event.y)
		if item and item in self._checked:
			self._checked[item] = not self._checked[item]
			# 同步所有树里的同名项
			event.widget.item(item, text=self._label(item))
			for tree in (self._app_tool_tree, self._app_app_tree, self._tool_tree):
				if tree is not event.widget and tree.exists(item):
					tree.item(item, text=self._label(item))

	def _create(self) -> None:
		name = self.var_name.get().strip()
		target_dir = Path(self.var_dir.get().strip())
		if not is_valid_project_name(name):
			messagebox.showerror("Maho", "无效名称。用 Letter + A-Z a-z 0-9 _ -")
			return
		if not target_dir.is_dir():
			try:
				target_dir.mkdir(parents=True, exist_ok=True)
			except OSError as ex:
				messagebox.showerror("Maho", f"无法创建目录：\n{target_dir}\n\n{ex}")
				return

		inherits = sorted(n for n in self._checked if self._checked[n])
		engine_root = self._engine_root_of(target_dir)

		problems = inheritance_problems(engine_root, name, inherits)
		if problems:
			messagebox.showerror("Maho", "继承冲突：\n\n" + "\n".join(problems))
			return

		is_exe = self.cmb_product.get() == _PRODUCT_EXE
		dev_platform = "Windows" if sys.platform == "win32" else "Linux"
		try:
			if is_exe:
				path = create_project(
					name,
					target_dir,
					engine_root,
					description="",
					author="土豆泥大王",
					plugins=inherits,
					dev_platform=dev_platform,
					app_type="IDE",
				)
			else:
				# 当前选中的 tab 决定 DLL 种类
				stage = "EToolStage" if self.notebook.index(self.notebook.select()) == 1 else "EEngineStage"
				path = create_plugin(
					name,
					engine_root,
					description="",
					stage=stage,
					inherits=inherits,
					plugins_dir=target_dir,
				)
		except Exception as ex:  # noqa: BLE001
			messagebox.showerror("Maho", f"创建失败：\n{ex}")
			return

		messagebox.showinfo("Maho", f"程序集已创建：\n{path}")
		self.var_name.set("MyAssembly")


def main() -> int:
	app = CreateAssemblyApp()
	app.mainloop()
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
