# Run via Tools/maho_python.bat (or Tools/*.bat) — engine Tools/python only.
"""Maho new-project UI (CreateProject.bat)."""

from __future__ import annotations

import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from maho_tools import (  # noqa: E402
	ENGINE_ROOT,
	create_project,
	install_cproject_association,
	install_cplugin_association,
	is_valid_project_name,
	list_engine_plugins,
	list_engine_templates,
	open_in_file_manager,
)

_CHECKED = "☑"
_UNCHECKED = "☐"


class CreateProjectApp(tk.Tk):
	def __init__(self) -> None:
		super().__init__()
		self.title("Maho — New Project")
		self.geometry("820x640")
		self.minsize(720, 520)
		self.resizable(True, True)

		self.var_name = tk.StringVar(value="MyGame")
		self.var_parent = tk.StringVar(value=str(Path.home() / "Documents" / "MahoProjects"))
		self.var_engine = tk.StringVar(value=str(ENGINE_ROOT))
		self.var_author = tk.StringVar(value="土豆泥大王")
		self.var_open_folder = tk.BooleanVar(value=True)
		self.var_template = tk.StringVar(value="")

		self._plugins: list[dict] = []
		self._checked: dict[str, bool] = {}
		self._templates: list[str] = []

		self._build()
		self.protocol("WM_DELETE_WINDOW", self.destroy)
		self.after(100, self._auto_associate_cproject_async)

	def _build(self) -> None:
		pad = {"padx": 12, "pady": 6}
		frm = ttk.Frame(self, padding=12)
		frm.pack(fill=tk.BOTH, expand=True)

		ttk.Label(frm, text="Create a new Maho project", font=("Segoe UI", 12, "bold")).grid(
			row=0, column=0, columnspan=3, sticky="w", **pad
		)

		ttk.Label(frm, text="Project Name").grid(row=1, column=0, sticky="w", **pad)
		ttk.Entry(frm, textvariable=self.var_name).grid(row=1, column=1, columnspan=2, sticky="ew", **pad)

		ttk.Label(frm, text="Parent Folder").grid(row=2, column=0, sticky="w", **pad)
		ttk.Entry(frm, textvariable=self.var_parent).grid(row=2, column=1, sticky="ew", **pad)
		ttk.Button(frm, text="Browse…", command=self._browse_parent).grid(row=2, column=2, sticky="e", **pad)

		ttk.Label(frm, text="Engine Root").grid(row=3, column=0, sticky="w", **pad)
		ttk.Entry(frm, textvariable=self.var_engine).grid(row=3, column=1, sticky="ew", **pad)
		ttk.Button(frm, text="Browse…", command=self._browse_engine).grid(row=3, column=2, sticky="e", **pad)

		ttk.Label(frm, text="Author").grid(row=4, column=0, sticky="w", **pad)
		ttk.Entry(frm, textvariable=self.var_author).grid(row=4, column=1, columnspan=2, sticky="ew", **pad)

		ttk.Label(frm, text="Description").grid(row=5, column=0, sticky="nw", **pad)
		self.txt_desc = tk.Text(frm, height=3, wrap=tk.WORD)
		self.txt_desc.grid(row=5, column=1, columnspan=2, sticky="nsew", **pad)
		self.txt_desc.insert("1.0", "哈哈，我是土豆泥大王！")

		# Project template — scan engine Extension/Engine/.
		ttk.Label(frm, text="Project Template").grid(row=6, column=0, sticky="w", **pad)
		self.cmb_template = ttk.Combobox(frm, textvariable=self.var_template, state="readonly", values=[])
		self.cmb_template.grid(row=6, column=1, sticky="ew", **pad)
		ttk.Button(frm, text="↻", width=2, command=self._reload_templates).grid(row=6, column=2, sticky="e", **pad)

		# Plugins — split into Tool (left) + Layer (right) columns.
		ttk.Label(frm, text="Plugins").grid(row=7, column=0, sticky="nw", **pad)
		plugins_frame = ttk.Frame(frm)
		plugins_frame.grid(row=7, column=1, columnspan=2, sticky="nsew", **pad)
		plugins_frame.columnconfigure(0, weight=1)
		plugins_frame.columnconfigure(1, weight=1)
		plugins_frame.rowconfigure(0, weight=1)

		# Left: tools.
		tool_col = ttk.Frame(plugins_frame)
		tool_col.grid(row=0, column=0, sticky="nsew", padx=(0, 6))
		ttk.Label(tool_col, text="Tool").pack(anchor="w")
		self.tool_tree = ttk.Treeview(tool_col, show="tree", selectmode="none", indent=16)
		tool_scroll = ttk.Scrollbar(tool_col, orient=tk.VERTICAL, command=self.tool_tree.yview)
		self.tool_tree.configure(yscrollcommand=tool_scroll.set)
		self.tool_tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
		tool_scroll.pack(side=tk.RIGHT, fill=tk.Y)
		self.tool_tree.bind("<Button-1>", self._on_tree_click)

		# Right: layers.
		layer_col = ttk.Frame(plugins_frame)
		layer_col.grid(row=0, column=1, sticky="nsew")
		ttk.Label(layer_col, text="Layer").pack(anchor="w")
		self.layer_tree = ttk.Treeview(layer_col, show="tree", selectmode="none", indent=16)
		layer_scroll = ttk.Scrollbar(layer_col, orient=tk.VERTICAL, command=self.layer_tree.yview)
		self.layer_tree.configure(yscrollcommand=layer_scroll.set)
		self.layer_tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
		layer_scroll.pack(side=tk.RIGHT, fill=tk.Y)
		self.layer_tree.bind("<Button-1>", self._on_tree_click)

		# Bottom row: open-folder checkbox (left) + Create Project (right).
		bottom = ttk.Frame(frm)
		bottom.grid(row=8, column=0, columnspan=3, sticky="ew", **pad)
		ttk.Checkbutton(bottom, text="Open project folder", variable=self.var_open_folder).pack(side=tk.LEFT)
		ttk.Button(bottom, text="Create Project", command=self._create).pack(side=tk.RIGHT)

		frm.columnconfigure(1, weight=1)
		frm.rowconfigure(5, weight=1)
		frm.rowconfigure(7, weight=3)

		self._reload_templates()
		self._reload_plugins()

	# ── plugin tree ──────────────────────────────────────────────────────

	def _deps_of(self, name: str) -> list[str]:
		for p in self._plugins:
			if p["Name"] == name:
				return p["Dependencies"]
		return []

	def _reload_templates(self) -> None:
		engine = Path(self.var_engine.get().strip())
		self._templates = list_engine_templates(engine)
		self.cmb_template["values"] = self._templates
		if self._templates and not self.var_template.get():
			self.var_template.set(self._templates[0])

	def _reload_plugins(self) -> None:
		for tree in (self.tool_tree, self.layer_tree):
			for item in tree.get_children():
				tree.delete(item)
		engine = Path(self.var_engine.get().strip())
		self._plugins = list_engine_plugins(engine)
		self._checked = {}

		for p in self._plugins:
			tree = self.tool_tree if p.get("Kind") == "tool" else self.layer_tree
			self._insert_plugin(tree, p, default=False)

	def _insert_plugin(self, tree: ttk.Treeview, p: dict, default: bool) -> None:
		name = p["Name"]
		self._checked[name] = default
		parent = ""
		# Skip the first group element — it is the Tool/Layer dir, already
		# implied by which column the plugin lives in.
		group = (p.get("Group") or [])[1:]
		for i in range(len(group)):
			gid = "group:" + "/".join(group[: i + 1])
			if not tree.exists(gid):
				tree.insert(parent, tk.END, iid=gid, text=group[i], open=True)
			parent = gid
		tree.insert(parent, tk.END, iid=name, text=self._label(name), open=True)

	def _label(self, name: str) -> str:
		mark = _CHECKED if self._checked.get(name) else _UNCHECKED
		return f"{mark} {name}"

	def _refresh_item(self, name: str) -> None:
		for tree in (self.tool_tree, self.layer_tree):
			if tree.exists(name):
				tree.item(name, text=self._label(name))

	def _on_tree_click(self, event: tk.Event) -> None:
		item = event.widget.identify_row(event.y)
		if item and item in self._checked:
			self._toggle_plugin(item)

	def _toggle_plugin(self, name: str) -> None:
		if self._checked.get(name, False):
			self._uncheck(name)
			self._uncheck_dependents(name)
		else:
			self._check(name)
			self._check_deps(name)

	def _check(self, name: str) -> None:
		self._checked[name] = True
		self._refresh_item(name)

	def _uncheck(self, name: str) -> None:
		self._checked[name] = False
		self._refresh_item(name)

	def _check_deps(self, name: str) -> None:
		for dep in self._deps_of(name):
			if dep in self._checked and not self._checked[dep]:
				self._check(dep)
				self._check_deps(dep)

	def _uncheck_dependents(self, name: str) -> None:
		for p in self._plugins:
			if name not in p["Dependencies"]:
				continue
			dep_name = p["Name"]
			if dep_name in self._checked and self._checked[dep_name]:
				self._uncheck(dep_name)
				self._uncheck_dependents(dep_name)

	# ── actions ──────────────────────────────────────────────────────────

	def _browse_parent(self) -> None:
		path = filedialog.askdirectory(initialdir=self.var_parent.get() or str(Path.home()))
		if path:
			self.var_parent.set(path)

	def _browse_engine(self) -> None:
		path = filedialog.askdirectory(initialdir=self.var_engine.get() or str(ENGINE_ROOT))
		if path:
			self.var_engine.set(path)
			self._reload_templates()
			self._reload_plugins()

	def log_line(self, message: str) -> None:
		# No log box in this UI; forward to stdout for console debugging.
		print(message, end="" if message.endswith("\n") else "\n")

	def _run_associate(self) -> None:
		try:
			install_cproject_association(log=self.log_line)
			install_cplugin_association(log=self.log_line)
		except Exception as ex:  # noqa: BLE001
			self.log_line(f"[Maho] Associate failed: {ex}")

	def _auto_associate_cproject_async(self) -> None:
		if sys.platform != "win32":
			return
		threading.Thread(target=self._run_associate, daemon=True).start()

	def _create(self) -> None:
		name = self.var_name.get().strip()
		parent = Path(self.var_parent.get().strip())
		engine = Path(self.var_engine.get().strip())
		author = self.var_author.get().strip()
		desc = self.txt_desc.get("1.0", tk.END).strip()
		want_open = self.var_open_folder.get()

		checked_plugins = sorted(n for n in self._checked if self._checked[n])
		template = self.var_template.get().strip()

		if not is_valid_project_name(name):
			messagebox.showerror("Maho", "Invalid project name.\nUse Letter + A-Z a-z 0-9 _ -")
			return
		if not parent:
			messagebox.showerror("Maho", "Parent folder is required.")
			return
		if not (engine / "Source").is_dir():
			messagebox.showerror("Maho", f"Engine root must contain Source/:\n{engine}")
			return
		py_root = engine / "Tools" / "python"
		if not (py_root / "python.exe").is_file() and not (py_root / "Scripts" / "python.exe").is_file():
			messagebox.showerror(
				"Maho",
				f"Engine local Python missing.\nRun Setup.bat in:\n{engine}",
			)
			return

		try:
			cproject = create_project(
				name,
				parent,
				engine,
				description=desc,
				author=author,
				plugins=checked_plugins,
				template=template,
			)
		except Exception as ex:  # noqa: BLE001
			messagebox.showerror("Maho", str(ex))
			return
		if want_open:
			open_in_file_manager(cproject.parent)
		messagebox.showinfo(
			"Maho",
			f"Project created:\n{cproject}\n\nDouble-click {cproject.name} to generate the build files\n(first run downloads third-party into Intermediate/).",
		)


def main() -> int:
	app = CreateProjectApp()
	app.mainloop()
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
