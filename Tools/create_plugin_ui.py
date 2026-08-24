# Run via the project's CreatePlugin.bat — creates a plugin.
"""Maho new-plugin UI — pick engine-plugin or project-plugin target + template."""

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
	discover_cplugin_files,
	is_valid_project_name,
	read_cplugin,
)

_CHECKED = "☑"
_UNCHECKED = "☐"

_ENGINE_KIND = "引擎插件 (Engine/Plugins)"
_PROJECT_KIND = "项目插件 (项目根/Plugins)"


class CreatePluginApp(tk.Tk):
	def __init__(self, default_plugins_dir: Path | None = None) -> None:
		super().__init__()
		self.title("Maho — New Plugin")
		self.geometry("620x560")
		self.minsize(520, 460)
		self.resizable(True, True)

		self.var_kind = tk.StringVar(value=_PROJECT_KIND)
		self.var_name = tk.StringVar(value="MyPlugin")
		self.var_plugins_dir = tk.StringVar(value=str(default_plugins_dir or Path.cwd() / "Plugins"))
		self.var_project_root = tk.StringVar(value=str(Path.cwd()))

		self._parents: list[dict] = []
		self._checked: dict[str, bool] = {}

		self._build()
		self.protocol("WM_DELETE_WINDOW", self.destroy)
		self.var_kind.trace_add("write", lambda *_: self._sync_target())
		self.var_plugins_dir.trace_add("write", lambda *_: self._reload_parents())
		self._sync_target()
		self._reload_parents()

	def _build(self) -> None:
		pad = {"padx": 12, "pady": 6}
		frm = ttk.Frame(self, padding=12)
		frm.pack(fill=tk.BOTH, expand=True)

		ttk.Label(frm, text="Create a new plugin", font=("Segoe UI", 12, "bold")).grid(
			row=0, column=0, columnspan=3, sticky="w", **pad
		)

		# 插件归属：引擎插件 → 引擎根/Plugins；项目插件 → 项目根/Plugins
		ttk.Label(frm, text="Plugin Location").grid(row=1, column=0, sticky="w", **pad)
		self.cmb_kind = ttk.Combobox(
			frm, textvariable=self.var_kind, state="readonly",
			values=[_ENGINE_KIND, _PROJECT_KIND],
		)
		self.cmb_kind.grid(row=1, column=1, columnspan=2, sticky="ew", **pad)

		ttk.Label(frm, text="Plugin Name").grid(row=2, column=0, sticky="w", **pad)
		ttk.Entry(frm, textvariable=self.var_name).grid(row=2, column=1, columnspan=2, sticky="ew", **pad)

		ttk.Label(frm, text="Plugins Path").grid(row=3, column=0, sticky="w", **pad)
		self.ent_plugins = ttk.Entry(frm, textvariable=self.var_plugins_dir)
		self.ent_plugins.grid(row=3, column=1, sticky="ew", **pad)
		self.btn_browse = ttk.Button(frm, text="Browse…", command=self._browse)
		self.btn_browse.grid(row=3, column=2, sticky="e", **pad)

		# Parent plugin — the ONE base class this plugin inherits (optional).
		ttk.Label(frm, text="父插件（可选，单选继承基类）").grid(row=4, column=0, sticky="nw", **pad)
		parent_frame = ttk.Frame(frm)
		parent_frame.grid(row=4, column=1, columnspan=2, sticky="nsew", **pad)
		parent_frame.columnconfigure(0, weight=1)
		parent_frame.rowconfigure(0, weight=1)
		self.parent_tree = ttk.Treeview(parent_frame, show="tree", selectmode="none")
		scroll = ttk.Scrollbar(parent_frame, orient=tk.VERTICAL, command=self.parent_tree.yview)
		self.parent_tree.configure(yscrollcommand=scroll.set)
		self.parent_tree.grid(row=0, column=0, sticky="nsew")
		scroll.grid(row=0, column=1, sticky="ns")
		self.parent_tree.bind("<Button-1>", self._on_tree_click)

		ttk.Label(frm, text="Description").grid(row=5, column=0, sticky="nw", **pad)
		self.txt_desc = tk.Text(frm, height=3, wrap=tk.WORD)
		self.txt_desc.grid(row=5, column=1, columnspan=2, sticky="nsew", **pad)

		ttk.Button(frm, text="Create Plugin", command=self._create).grid(
			row=6, column=1, columnspan=2, sticky="e", **pad
		)

		frm.columnconfigure(1, weight=1)
		frm.rowconfigure(4, weight=3)
		frm.rowconfigure(5, weight=1)

	def _sync_target(self) -> None:
		"""引擎插件 → 固定到 引擎根/Plugins；项目插件 → 项目根/Plugins（可 Browse）。"""
		if self.var_kind.get() == _ENGINE_KIND:
			self.var_plugins_dir.set(str((ENGINE_ROOT / "Plugins").resolve()))
			self.ent_plugins.config(state="readonly")
			self.btn_browse.config(state="disabled")
		else:
			root = Path(self.var_project_root.get())
			self.var_plugins_dir.set(str((root / "Plugins").resolve()))
			self.ent_plugins.config(state="normal")
			self.btn_browse.config(state="normal")

	def _browse(self) -> None:
		if self.var_kind.get() == _ENGINE_KIND:
			return
		# project kind: pick the project ROOT (containing the .cproject)
		initial = self.var_project_root.get() or str(Path.cwd())
		path = filedialog.askdirectory(initialdir=initial, title="Select Project Root (contains .cproject)")
		if path:
			self.var_project_root.set(path)
			self.var_plugins_dir.set(str((Path(path) / "Plugins").resolve()))

	def _reload_parents(self) -> None:
		for item in self.parent_tree.get_children():
			self.parent_tree.delete(item)
		self._parents = []
		self._checked = {}

		# scan the engine Plugins/ + the target Plugins/ for candidate parents,
		# building the tree from the FILESYSTEM hierarchy under each Plugins/ root.
		roots = [ENGINE_ROOT / "Plugins", Path(self.var_plugins_dir.get().strip())]
		seen: set[str] = set()
		merged: list[dict] = []
		for root in roots:
			root = root.resolve()
			if not root.is_dir():
				continue
			for cplugin_path in discover_cplugin_files([root]):
				data = read_cplugin(cplugin_path)
				name = data.get("Name") or cplugin_path.parent.name
				# folder hierarchy under the Plugins root, EXCLUDING the plugin's
				# own directory (create_plugin nests each plugin in its own folder,
				# so the last segment is the plugin itself, not a group).
				group = list(cplugin_path.parent.relative_to(root).parts[:-1])
				key = "/".join(group + [name])
				if key in seen:
					continue
				seen.add(key)
				merged.append({"Name": name, "Group": group})
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
		# single selection — pick ONE parent plugin (or click again to clear)
		if self._checked.get(iid):
			self._checked[iid] = False
		else:
			for n in list(self._checked):
				self._checked[n] = False
			self._checked[iid] = True
		self.parent_tree.item(iid, text=self._label(iid))
		for n in list(self._checked):
			if n != iid and self.parent_tree.exists(n):
				self.parent_tree.item(n, text=self._label(n))

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
		parent = next((n for n in self._checked if self._checked[n]), None)
		try:
			path = create_plugin(
				name, ENGINE_ROOT, description=desc, inherits=[parent] if parent else [], plugins_dir=plugins_dir
			)
		except Exception as ex:  # noqa: BLE001
			messagebox.showerror("Maho", f"Create plugin failed:\n{ex}")
			return
		messagebox.showinfo("Maho", f"Plugin created:\n{path}")
		self.var_name.set("MyPlugin")
		self._reload_parents()


def main() -> int:
	# Optional positional arg: default project root (or plugins dir).
	default_dir: Path | None = None
	for arg in sys.argv[1:]:
		p = Path(arg).expanduser()
		if p.is_dir() or not p.exists():
			default_dir = p.resolve()
			break
	app = CreatePluginApp(default_plugins_dir=default_dir)
	app.mainloop()
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
