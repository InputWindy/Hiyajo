# Run via Tools/create_plugin.bat / maho_python.bat — engine Tools/python only.
"""Maho new-plugin UI (create_plugin.bat)."""

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
	is_valid_project_name,
	list_engine_plugins,
)

_CHECKED = "☑"
_UNCHECKED = "☐"

_TYPE_TOOL = "Tool"
_TYPE_ENGINE = "Extension"


class CreatePluginApp(tk.Tk):
	def __init__(self) -> None:
		super().__init__()
		self.title("Maho — New Plugin")
		self.geometry("640x520")
		self.minsize(560, 420)
		self.resizable(True, True)

		self.var_name = tk.StringVar(value="MyPlugin")
		self.var_plugins_dir = tk.StringVar(value=str(ENGINE_ROOT / "Maho" / "Plugins"))
		self.var_type = tk.StringVar(value=_TYPE_TOOL)

		self._plugins: list[dict] = []
		self._inheritables: list[str] = []
		self._checked: dict[str, bool] = {}

		self._build()
		self.protocol("WM_DELETE_WINDOW", self.destroy)

	def _build(self) -> None:
		pad = {"padx": 12, "pady": 6}
		frm = ttk.Frame(self, padding=12)
		frm.pack(fill=tk.BOTH, expand=True)

		ttk.Label(frm, text="Create a new Maho plugin", font=("Segoe UI", 12, "bold")).grid(
			row=0, column=0, columnspan=3, sticky="w", **pad
		)

		ttk.Label(frm, text="Plugin Name").grid(row=1, column=0, sticky="w", **pad)
		ttk.Entry(frm, textvariable=self.var_name).grid(row=1, column=1, columnspan=2, sticky="ew", **pad)

		ttk.Label(frm, text="Plugins Path").grid(row=2, column=0, sticky="w", **pad)
		ttk.Entry(frm, textvariable=self.var_plugins_dir).grid(row=2, column=1, sticky="ew", **pad)
		ttk.Button(frm, text="Browse…", command=self._browse_plugins_dir).grid(row=2, column=2, sticky="e", **pad)

		ttk.Label(frm, text="Type").grid(row=3, column=0, sticky="w", **pad)
		self.cmb_type = ttk.Combobox(frm, state="readonly", values=[_TYPE_TOOL, _TYPE_ENGINE])
		self.cmb_type.grid(row=3, column=1, columnspan=2, sticky="ew", **pad)
		self.cmb_type.bind("<<ComboboxSelected>>", self._on_type_change)

		ttk.Label(frm, text="Inherit").grid(row=4, column=0, sticky="nw", **pad)
		self.inherit_panel = ttk.LabelFrame(frm, text="可继承 Tool")
		self.inherit_panel.grid(row=4, column=1, columnspan=2, sticky="nsew", **pad)
		self.inherit_panel.columnconfigure(0, weight=1)
		self.inherit_panel.rowconfigure(0, weight=1)
		self.inherit_tree = ttk.Treeview(self.inherit_panel, show="tree", selectmode="none")
		scroll = ttk.Scrollbar(self.inherit_panel, orient=tk.VERTICAL, command=self.inherit_tree.yview)
		self.inherit_tree.configure(yscrollcommand=scroll.set)
		self.inherit_tree.grid(row=0, column=0, sticky="nsew")
		scroll.grid(row=0, column=1, sticky="ns")
		self.inherit_tree.bind("<Button-1>", self._on_tree_click)

		ttk.Label(frm, text="Description").grid(row=5, column=0, sticky="nw", **pad)
		self.txt_desc = tk.Text(frm, height=2, wrap=tk.WORD)
		self.txt_desc.grid(row=5, column=1, columnspan=2, sticky="nsew", **pad)

		ttk.Button(frm, text="Create Plugin", command=self._create).grid(
			row=6, column=1, columnspan=2, sticky="e", **pad
		)

		frm.columnconfigure(1, weight=1)
		frm.rowconfigure(4, weight=1)

		self._reload_plugins()

	# ── data ────────────────────────────────────────────────────────────

	def _browse_plugins_dir(self) -> None:
		path = filedialog.askdirectory(initialdir=self.var_plugins_dir.get() or str(ENGINE_ROOT))
		if path:
			self.var_plugins_dir.set(path)

	def _reload_plugins(self) -> None:
		plugins_dir = Path(self.var_plugins_dir.get().strip())
		engine_root = self._engine_root_of(plugins_dir)
		self._plugins = list_engine_plugins(engine_root)
		self._apply_type()

	def _engine_root_of(self, plugins_dir: Path) -> Path:
		# <engine>/Maho/Plugins → engine root is two levels up.
		plugins_dir = Path(plugins_dir)
		if plugins_dir.name == "Plugins" and plugins_dir.parent.name == "Maho":
			return plugins_dir.parent.parent
		return ENGINE_ROOT

	def _apply_type(self) -> None:
		for item in self.inherit_tree.get_children():
			self.inherit_tree.delete(item)
		self._inheritables = []
		self._checked = {}

		is_tool = self.cmb_type.get() == _TYPE_TOOL
		stage = "EToolStage" if is_tool else "EEngineStage"
		label = "可继承 Tool" if is_tool else "可继承 Extension"
		self.inherit_panel.configure(text=label)

		for p in self._plugins:
			ext = p.get("Extension") or {}
			if ext.get("Stage") != stage:
				continue
			name = p["Name"]
			self._inheritables.append(name)
			self._checked[name] = False
			self.inherit_tree.insert("", tk.END, iid=name, text=self._label(name), open=True)

	def _label(self, name: str) -> str:
		mark = _CHECKED if self._checked.get(name) else _UNCHECKED
		return f"{mark} {name}"

	def _on_type_change(self, _event=None) -> None:
		self._apply_type()

	def _on_tree_click(self, event: tk.Event) -> None:
		item = event.widget.identify_row(event.y)
		if item and item in self._checked:
			self._checked[item] = not self._checked[item]
			self.inherit_tree.item(item, text=self._label(item))

	# ── create ───────────────────────────────────────────────────────────

	def _create(self) -> None:
		name = self.var_name.get().strip()
		plugins_dir = Path(self.var_plugins_dir.get().strip())
		if not is_valid_project_name(name):
			messagebox.showerror("Maho", "Invalid plugin name.\nUse Letter + A-Z a-z 0-9 _ -")
			return
		if not plugins_dir.is_dir():
			messagebox.showerror("Maho", f"Plugins path does not exist:\n{plugins_dir}")
			return

		is_tool = self.cmb_type.get() == _TYPE_TOOL
		stage = "EToolStage" if is_tool else "EEngineStage"
		inherits = sorted(n for n in self._inheritables if self._checked.get(n))
		desc = self.txt_desc.get("1.0", tk.END).strip()

		engine_root = self._engine_root_of(plugins_dir)
		try:
			path = create_plugin(
				name,
				engine_root,
				description=desc,
				stage=stage,
				inherits=inherits,
				plugins_dir=plugins_dir,
			)
		except Exception as ex:  # noqa: BLE001
			messagebox.showerror("Maho", f"Create plugin failed:\n{ex}")
			return

		messagebox.showinfo("Maho", f"Plugin created:\n{path}")
		self.var_name.set("MyPlugin")


def main() -> int:
	app = CreatePluginApp()
	app.mainloop()
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
