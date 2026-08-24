# Run via Tools/maho_python.bat (or Tools/*.bat) — engine Tools/python only.
"""Maho new-project UI (CreateProject.bat) — pick ONE plugin to mount, build the project."""

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
	discover_cplugin_files,
	install_cproject_association,
	install_cplugin_association,
	is_valid_project_name,
	open_in_file_manager,
	read_cplugin,
)

_CHECKED = "☑"
_UNCHECKED = "☐"


class CreateProjectApp(tk.Tk):
	def __init__(self) -> None:
		super().__init__()
		self.title("Maho — New Project")
		self.geometry("760x640")
		self.minsize(680, 520)
		self.resizable(True, True)

		self.var_name = tk.StringVar(value="MyGame")
		self.var_parent = tk.StringVar(value=str(Path.home() / "Documents" / "MahoProjects"))
		self.var_engine = tk.StringVar(value=str(ENGINE_ROOT))
		self.var_author = tk.StringVar(value="土豆泥大王")
		self.var_open_folder = tk.BooleanVar(value=True)

		self._parents: list[dict] = []
		self._checked: dict[str, bool] = {}
		self._selected: str | None = None  # the single chosen plugin to mount

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

		# Optional: a plugin to mount as the project's child (empty project allowed).
		ttk.Label(frm, text="Plugins（可选，勾选装配为一个子层）").grid(row=6, column=0, sticky="nw", **pad)
		tree_frame = ttk.Frame(frm)
		tree_frame.grid(row=6, column=1, columnspan=2, sticky="nsew", **pad)
		tree_frame.columnconfigure(0, weight=1)
		tree_frame.rowconfigure(0, weight=1)
		self.parent_tree = ttk.Treeview(tree_frame, show="tree", selectmode="none")
		scroll = ttk.Scrollbar(tree_frame, orient=tk.VERTICAL, command=self.parent_tree.yview)
		self.parent_tree.configure(yscrollcommand=scroll.set)
		self.parent_tree.grid(row=0, column=0, sticky="nsew")
		scroll.grid(row=0, column=1, sticky="ns")
		self.parent_tree.bind("<Button-1>", self._on_tree_click)

		# Bottom row: open-folder checkbox (left) + Create Project (right).
		bottom = ttk.Frame(frm)
		bottom.grid(row=7, column=0, columnspan=3, sticky="ew", **pad)
		ttk.Checkbutton(bottom, text="Open project folder", variable=self.var_open_folder).pack(side=tk.LEFT)
		ttk.Button(bottom, text="Create Project", command=self._create).pack(side=tk.RIGHT)

		frm.columnconfigure(1, weight=1)
		frm.rowconfigure(5, weight=1)
		frm.rowconfigure(6, weight=3)

		self._reload_plugins()

	# ── parent tree (single selection) ────────────────────────────────────

	def _reload_plugins(self) -> None:
		for item in self.parent_tree.get_children():
			self.parent_tree.delete(item)
		self._parents = []
		self._checked = {}
		self._selected = None

		engine = Path(self.var_engine.get().strip())
		roots = [engine / "Plugins"]
		seen: set[str] = set()
		for root in roots:
			root = root.resolve()
			if not root.is_dir():
				continue
			for cplugin_path in discover_cplugin_files([root]):
				data = read_cplugin(cplugin_path)
				name = data.get("Name") or cplugin_path.parent.name
				group = list(cplugin_path.parent.relative_to(root).parts[:-1])
				key = "/".join(group + [name])
				if key in seen:
					continue
				seen.add(key)
				self._parents.append({"Name": name, "Group": group})
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
		if not self.parent_tree.exists(name):
			self.parent_tree.insert(parent, tk.END, iid=name, text=self._label(name), open=True)

	def _label(self, name: str) -> str:
		mark = _CHECKED if self._checked.get(name) else _UNCHECKED
		return f"{mark} {name}"

	def _refresh_item(self, name: str) -> None:
		if self.parent_tree.exists(name):
			self.parent_tree.item(name, text=self._label(name))

	def _on_tree_click(self, event: tk.Event) -> None:
		iid = self.parent_tree.identify_row(event.y)
		if iid and iid.startswith("group:"):
			return
		if iid in self._checked:
			self._select(iid)

	def _select(self, name: str) -> None:
		"""Single selection: pick one plugin to mount, clear the rest."""
		if self._selected == name:
			return
		if self._selected:
			self._checked[self._selected] = False
			self._refresh_item(self._selected)
		self._selected = name
		self._checked[name] = True
		self._refresh_item(name)

	# ── actions ──────────────────────────────────────────────────────────

	def _browse_parent(self) -> None:
		path = filedialog.askdirectory(initialdir=self.var_parent.get() or str(Path.home()))
		if path:
			self.var_parent.set(path)

	def _browse_engine(self) -> None:
		path = filedialog.askdirectory(initialdir=self.var_engine.get() or str(ENGINE_ROOT))
		if path:
			self.var_engine.set(path)
			self._reload_plugins()

	def log_line(self, message: str) -> None:
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
			plugins = [self._selected] if self._selected else []
			cproject = create_project(
				name,
				parent,
				engine,
				description=desc,
				author=author,
				plugins=plugins,
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
