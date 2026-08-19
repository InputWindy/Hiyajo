# Run via the project's CreatePlugin.bat — creates a plugin in the project's Extension/.
"""Maho new-plugin UI (project-side) — with parent-plugin dependency tree."""

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


class CreatePluginApp(tk.Tk):
	def __init__(self, default_plugins_dir: Path | None = None) -> None:
		super().__init__()
		self.title("Maho — New Plugin")
		self.geometry("600x560")
		self.minsize(520, 460)
		self.resizable(True, True)

		plugins_default = default_plugins_dir or (Path.cwd() / "Source")
		self.var_name = tk.StringVar(value="MyPlugin")
		self.var_plugins_dir = tk.StringVar(value=str(plugins_default))

		self._parents: list[dict] = []
		self._checked: dict[str, bool] = {}

		self._build()
		self.protocol("WM_DELETE_WINDOW", self.destroy)
		self.var_plugins_dir.trace_add("write", lambda *_: self._reload_parents())
		self._reload_parents()

	def _build(self) -> None:
		pad = {"padx": 12, "pady": 6}
		frm = ttk.Frame(self, padding=12)
		frm.pack(fill=tk.BOTH, expand=True)

		ttk.Label(frm, text="Create a new plugin", font=("Segoe UI", 12, "bold")).grid(
			row=0, column=0, columnspan=3, sticky="w", **pad
		)

		ttk.Label(frm, text="Plugin Name").grid(row=1, column=0, sticky="w", **pad)
		ttk.Entry(frm, textvariable=self.var_name).grid(row=1, column=1, columnspan=2, sticky="ew", **pad)

		ttk.Label(frm, text="Plugins Path").grid(row=2, column=0, sticky="w", **pad)
		ttk.Entry(frm, textvariable=self.var_plugins_dir).grid(row=2, column=1, sticky="ew", **pad)
		ttk.Button(frm, text="Browse…", command=self._browse).grid(row=2, column=2, sticky="e", **pad)

		# Parent plugin types — the plugins this new plugin depends on.
		ttk.Label(frm, text="父插件类型 (Dependencies)").grid(row=3, column=0, sticky="nw", **pad)
		parent_frame = ttk.Frame(frm)
		parent_frame.grid(row=3, column=1, columnspan=2, sticky="nsew", **pad)
		parent_frame.columnconfigure(0, weight=1)
		parent_frame.rowconfigure(0, weight=1)
		self.parent_tree = ttk.Treeview(parent_frame, show="tree", selectmode="none")
		scroll = ttk.Scrollbar(parent_frame, orient=tk.VERTICAL, command=self.parent_tree.yview)
		self.parent_tree.configure(yscrollcommand=scroll.set)
		self.parent_tree.grid(row=0, column=0, sticky="nsew")
		scroll.grid(row=0, column=1, sticky="ns")
		self.parent_tree.bind("<Button-1>", self._on_tree_click)

		ttk.Label(frm, text="Description").grid(row=4, column=0, sticky="nw", **pad)
		self.txt_desc = tk.Text(frm, height=3, wrap=tk.WORD)
		self.txt_desc.grid(row=4, column=1, columnspan=2, sticky="nsew", **pad)

		ttk.Button(frm, text="Create Plugin", command=self._create).grid(
			row=5, column=1, columnspan=2, sticky="e", **pad
		)

		frm.columnconfigure(1, weight=1)
		frm.rowconfigure(3, weight=3)
		frm.rowconfigure(4, weight=1)

	def _browse(self) -> None:
		path = filedialog.askdirectory(initialdir=self.var_plugins_dir.get() or str(ENGINE_ROOT))
		if path:
			self.var_plugins_dir.set(path)

	def _reload_parents(self) -> None:
		for item in self.parent_tree.get_children():
			self.parent_tree.delete(item)
		self._parents = []
		self._checked = {}

		engine_plugins = list_engine_plugins(ENGINE_ROOT)
		project_dir = Path(self.var_plugins_dir.get().strip())
		project_plugins: list[dict] = []
		if project_dir.is_dir():
			project_plugins = list_engine_plugins(project_dir.parent if project_dir.name == "Extension" else project_dir)

		# Engine catalog first, then the project's own plugins — de-dupe by
		# Name (engine-side runs scan the same Extension/ twice otherwise).
		seen: set[str] = set()
		merged: list[dict] = []
		for p in engine_plugins + project_plugins:
			if p["Name"] in seen:
				continue
			seen.add(p["Name"])
			merged.append(p)
		self._parents = merged
		for p in self._parents:
			self._insert_parent(p)

	def _insert_parent(self, p: dict) -> None:
		name = p["Name"]
		self._checked[name] = False
		parent = ""
		group = p.get("Group") or []
		for i in range(len(group)):
			gid = "group:" + "/".join(group[: i + 1])
			if not self.parent_tree.exists(gid):
				self.parent_tree.insert(parent, tk.END, iid=gid, text=group[i], open=True)
			parent = gid
		if self.parent_tree.exists(name):
			return
		self.parent_tree.insert(parent, tk.END, iid=name, text=self._label(name), open=True)

	def _label(self, name: str) -> str:
		mark = _CHECKED if self._checked.get(name) else _UNCHECKED
		return f"{mark} {name}"

	def _on_tree_click(self, event) -> None:
		iid = self.parent_tree.identify_row(event.y)
		if not iid or iid.startswith("group:"):
			return
		self._checked[iid] = not self._checked.get(iid, False)
		self.parent_tree.item(iid, text=self._label(iid))

	def _create(self) -> None:
		name = self.var_name.get().strip()
		plugins_dir = Path(self.var_plugins_dir.get().strip())
		if not is_valid_project_name(name):
			messagebox.showerror("Maho", "Invalid plugin name.\nUse Letter + A-Z a-z 0-9 _ -")
			return
		if not plugins_dir.is_dir():
			try:
				plugins_dir.mkdir(parents=True, exist_ok=True)
			except OSError as ex:
				messagebox.showerror("Maho", f"Cannot create plugins path:\n{plugins_dir}\n\n{ex}")
				return
		desc = self.txt_desc.get("1.0", tk.END).strip()
		inherits = sorted(n for n in self._checked if self._checked[n])
		try:
			path = create_plugin(
				name, ENGINE_ROOT, description=desc, inherits=inherits, plugins_dir=plugins_dir
			)
		except Exception as ex:  # noqa: BLE001
			messagebox.showerror("Maho", f"Create plugin failed:\n{ex}")
			return
		messagebox.showinfo("Maho", f"Plugin created:\n{path}")
		self.var_name.set("MyPlugin")
		self._reload_parents()


def main() -> int:
	# Optional positional arg: default plugins dir (e.g. a project's Extension/).
	default_plugins_dir: Path | None = None
	for arg in sys.argv[1:]:
		p = Path(arg).expanduser()
		if p.is_dir() or not p.exists():
			default_plugins_dir = p.resolve()
			break
	app = CreatePluginApp(default_plugins_dir=default_plugins_dir)
	app.mainloop()
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
