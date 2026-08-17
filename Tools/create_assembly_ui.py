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

_PRODUCT_DLL = "动态链接库 (DLL)"
_PRODUCT_EXE = "可执行文件 (EXE)"
_KIND_TOOL = "工具 (Tool)"
_KIND_LAYER = "层 (Layer)"

_PLATFORMS = ["Windows", "Linux", "Android", "IOS", "Xbox"]


class CreateAssemblyApp(tk.Tk):
	def __init__(self) -> None:
		super().__init__()
		self.title("Maho — 创建程序集")
		self.geometry("680x560")
		self.minsize(600, 460)
		self.resizable(True, True)

		self.var_name = tk.StringVar(value="MyAssembly")
		self.var_dir = tk.StringVar(value=str(ENGINE_ROOT / "Maho" / "Plugins"))
		self.var_product = tk.StringVar(value=_PRODUCT_DLL)
		self.var_kind = tk.StringVar(value=_KIND_TOOL)
		self.var_platform = tk.StringVar(value="Windows")

		self._plugins: list[dict] = []
		self._inheritables: list[str] = []
		self._checked: dict[str, bool] = {}

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

		ttk.Label(frm, text="产物").grid(row=3, column=0, sticky="w", **pad)
		self.cmb_product = ttk.Combobox(frm, state="readonly", values=[_PRODUCT_DLL, _PRODUCT_EXE])
		self.cmb_product.grid(row=3, column=1, columnspan=2, sticky="ew", **pad)
		self.cmb_product.bind("<<ComboboxSelected>>", self._on_product_change)

		# 种类（DLL 时显示：工具 / 层）
		self.lbl_kind = ttk.Label(frm, text="种类")
		self.lbl_kind.grid(row=4, column=0, sticky="w", **pad)
		self.cmb_kind = ttk.Combobox(frm, state="readonly", values=[_KIND_TOOL, _KIND_LAYER])
		self.cmb_kind.grid(row=4, column=1, columnspan=2, sticky="ew", **pad)

		# 开发平台（EXE 时显示）
		self.lbl_platform = ttk.Label(frm, text="开发平台")
		self.cmb_platform = ttk.Combobox(frm, state="readonly", values=_PLATFORMS)
		self.cmb_platform.set("Windows")

		ttk.Label(frm, text="继承").grid(row=5, column=0, sticky="nw", **pad)
		self.inherit_panel = ttk.LabelFrame(frm, text="可继承")
		self.inherit_panel.grid(row=5, column=1, columnspan=2, sticky="nsew", **pad)
		self.inherit_panel.columnconfigure(0, weight=1)
		self.inherit_panel.rowconfigure(0, weight=1)
		self.inherit_tree = ttk.Treeview(self.inherit_panel, show="tree", selectmode="none")
		scroll = ttk.Scrollbar(self.inherit_panel, orient=tk.VERTICAL, command=self.inherit_tree.yview)
		self.inherit_tree.configure(yscrollcommand=scroll.set)
		self.inherit_tree.grid(row=0, column=0, sticky="nsew")
		scroll.grid(row=0, column=1, sticky="ns")
		self.inherit_tree.bind("<Button-1>", self._on_tree_click)

		ttk.Label(frm, text="说明").grid(row=6, column=0, sticky="nw", **pad)
		self.txt_desc = tk.Text(frm, height=2, wrap=tk.WORD)
		self.txt_desc.grid(row=6, column=1, columnspan=2, sticky="nsew", **pad)

		ttk.Button(frm, text="创建", command=self._create).grid(row=7, column=1, columnspan=2, sticky="e", **pad)

		frm.columnconfigure(1, weight=1)
		frm.rowconfigure(5, weight=1)

		self._reload_plugins()
		self._apply_product()

	def _browse_dir(self) -> None:
		path = filedialog.askdirectory(initialdir=self.var_dir.get() or str(ENGINE_ROOT))
		if path:
			self.var_dir.set(path)

	def _reload_plugins(self) -> None:
		plugins_dir = Path(self.var_dir.get().strip())
		engine_root = self._engine_root_of(plugins_dir)
		self._plugins = list_engine_plugins(engine_root)
		self._apply_product()

	def _engine_root_of(self, plugins_dir: Path) -> Path:
		plugins_dir = Path(plugins_dir)
		if plugins_dir.name == "Plugins" and plugins_dir.parent.name == "Maho":
			return plugins_dir.parent.parent
		return ENGINE_ROOT

	def _on_product_change(self, _event=None) -> None:
		self._apply_product()

	def _apply_product(self) -> None:
		for item in self.inherit_tree.get_children():
			self.inherit_tree.delete(item)
		self._inheritables = []
		self._checked = {}

		is_exe = self.cmb_product.get() == _PRODUCT_EXE

		# 种类/平台行的显隐
		if is_exe:
			self.lbl_kind.grid_remove()
			self.cmb_kind.grid_remove()
			self.lbl_platform.grid(row=4, column=0, sticky="w", padx=12, pady=6)
			self.cmb_platform.grid(row=4, column=1, columnspan=2, sticky="ew", padx=12, pady=6)
		else:
			self.lbl_kind.grid(row=4, column=0, sticky="w", padx=12, pady=6)
			self.cmb_kind.grid(row=4, column=1, columnspan=2, sticky="ew", padx=12, pady=6)
			self.lbl_platform.grid_remove()
			self.cmb_platform.grid_remove()

		# 继承列表：EXE 继承全部；DLL 只继承同类
		stage = None
		if not is_exe:
			stage = "EToolStage" if self.cmb_kind.get() == _KIND_TOOL else "EEngineStage"
		self.inherit_panel.configure(text="可继承" if is_exe else ("可继承 Tool" if stage == "EToolStage" else "可继承 Layer"))

		for p in self._plugins:
			ext = p.get("Extension") or {}
			if stage is not None and ext.get("Stage") != stage:
				continue
			name = p["Name"]
			self._inheritables.append(name)
			self._checked[name] = False
			parent = ""
			group = p.get("Group") or []
			for i in range(len(group)):
				gid = "group:" + "/".join(group[: i + 1])
				if not self.inherit_tree.exists(gid):
					self.inherit_tree.insert(parent, tk.END, iid=gid, text=group[i], open=True)
				parent = gid
			self.inherit_tree.insert(parent, tk.END, iid=name, text=self._label(name), open=True)

	def _label(self, name: str) -> str:
		mark = _CHECKED if self._checked.get(name) else _UNCHECKED
		return f"{mark} {name}"

	def _on_tree_click(self, event: tk.Event) -> None:
		item = event.widget.identify_row(event.y)
		if item and item in self._checked:
			self._checked[item] = not self._checked[item]
			self.inherit_tree.item(item, text=self._label(item))

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

		inherits = sorted(n for n in self._inheritables if self._checked.get(n))
		desc = self.txt_desc.get("1.0", tk.END).strip()
		engine_root = self._engine_root_of(target_dir)

		problems = inheritance_problems(engine_root, name, inherits)
		if problems:
			messagebox.showerror("Maho", "继承冲突：\n\n" + "\n".join(problems))
			return

		is_exe = self.cmb_product.get() == _PRODUCT_EXE
		try:
			if is_exe:
				path = create_project(
					name,
					target_dir,
					engine_root,
					description=desc,
					author="土豆泥大王",
					plugins=inherits,
					dev_platform=self.cmb_platform.get(),
					app_type="IDE",
				)
			else:
				stage = "EToolStage" if self.cmb_kind.get() == _KIND_TOOL else "EEngineStage"
				path = create_plugin(
					name,
					engine_root,
					description=desc,
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
