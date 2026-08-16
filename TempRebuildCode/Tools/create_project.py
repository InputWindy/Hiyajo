# Run via Tools/maho_python.bat (or Tools/*.bat) — engine Tools/python only.
"""Maho new-project UI (CreateProject.bat). Logs go to the UI, not a console window."""

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
	is_valid_project_name,
	list_engine_plugins,
	open_in_file_manager,
)


# Project templates: label → default-checked engine extensions (tools are
# checked by default for client/server, none for null). Defaults are soft —
# the user may uncheck them freely. Order matches the combobox list.
ENGINE_TEMPLATES = [
	{"key": "client", "label": "游戏客户端 (Client)", "default_extensions": ["Platform", "Network", "World", "Render"]},
	{"key": "server", "label": "游戏服务器 (Server)", "default_extensions": ["Platform", "Network", "World"]},
	{"key": "null", "label": "空引擎 (Null)", "default_extensions": []},
]
DEFAULT_TEMPLATE_LABEL = "游戏客户端 (Client)"


class CreateProjectApp(tk.Tk):
	def __init__(self) -> None:
		super().__init__()
		self.title("Maho — New Project")
		self.geometry("720x640")
		self.minsize(640, 520)
		self.resizable(True, True)

		self.var_name = tk.StringVar(value="MyGame")
		self.var_parent = tk.StringVar(value=str(Path.home() / "Documents" / "MahoProjects"))
		self.var_engine = tk.StringVar(value=str(ENGINE_ROOT))
		self.var_author = tk.StringVar(value="")
		self.var_dev_platform = tk.StringVar(value="Windows")
		self.var_open_folder = tk.BooleanVar(value=True)
		self._associate_busy = False
		self._plugins: list[dict] = []
		self._plugin_vars: dict[str, tk.BooleanVar] = {}
		self._tool_names: list[str] = []
		self._extension_names: list[str] = []

		self._build()
		self.protocol("WM_DELETE_WINDOW", self.destroy)
		self.log_line("[Maho] UI ready.")
		# Defer registry work so the window paints first; run off the UI thread.
		self.after(100, self._auto_associate_cproject_async)

	def _build(self) -> None:
		pad = {"padx": 12, "pady": 6}
		frm = ttk.Frame(self, padding=12)
		frm.pack(fill=tk.BOTH, expand=True)

		ttk.Label(frm, text="Create a new Maho game project", font=("Segoe UI", 12, "bold")).grid(
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

		ttk.Label(frm, text="Dev Platform").grid(row=6, column=0, sticky="w", **pad)
		self.cmb_dev_platform = ttk.Combobox(
			frm,
			state="readonly",
			values=["Windows", "Linux"],
		)
		self.cmb_dev_platform.grid(row=6, column=1, columnspan=2, sticky="ew", **pad)
		self.cmb_dev_platform.set(self.var_dev_platform.get())

		ttk.Label(frm, text="Project Template").grid(row=7, column=0, sticky="w", **pad)
		self.cmb_template = ttk.Combobox(
			frm,
			state="readonly",
			values=[t["label"] for t in ENGINE_TEMPLATES],
		)
		self.cmb_template.grid(row=7, column=1, columnspan=2, sticky="ew", **pad)
		self.cmb_template.bind("<<ComboboxSelected>>", self._on_template_change)
		self.cmb_template.set(DEFAULT_TEMPLATE_LABEL)

		ttk.Label(frm, text="Plugins").grid(row=8, column=0, sticky="nw", **pad)
		plugin_frame = ttk.Frame(frm)
		plugin_frame.grid(row=8, column=1, columnspan=2, sticky="nsew", **pad)
		plugin_frame.columnconfigure(0, weight=1)
		plugin_frame.rowconfigure(0, weight=1)
		canvas = tk.Canvas(plugin_frame, borderwidth=0, highlightthickness=0, height=110)
		plugin_scroll = ttk.Scrollbar(plugin_frame, orient=tk.VERTICAL, command=canvas.yview)
		canvas.configure(yscrollcommand=plugin_scroll.set)
		canvas.grid(row=0, column=0, sticky="nsew")
		plugin_scroll.grid(row=0, column=1, sticky="ns")
		self.plugin_inner = ttk.Frame(canvas)
		self.plugin_inner.bind(
			"<Configure>", lambda _e: canvas.configure(scrollregion=canvas.bbox("all"))
		)
		canvas.create_window((0, 0), window=self.plugin_inner, anchor="nw")

		opts = ttk.Frame(frm)
		opts.grid(row=9, column=0, columnspan=3, sticky="w", **pad)
		ttk.Checkbutton(opts, text="Open project folder", variable=self.var_open_folder).pack(side=tk.LEFT)

		hint = (
			"Creates Parent/Name/ with Name.cproject (JSON, like .uproject).\n"
			"Double-click the .cproject to generate Name.sln beside it (first run downloads\n"
			"third-party into Intermediate/). Requires engine Setup.bat beforehand."
		)
		ttk.Label(frm, text=hint, foreground="#555").grid(row=10, column=0, columnspan=3, sticky="w", **pad)

		ttk.Label(frm, text="Log").grid(row=11, column=0, sticky="nw", **pad)
		log_frame = ttk.Frame(frm)
		log_frame.grid(row=11, column=1, columnspan=2, sticky="nsew", **pad)
		self.txt_log = tk.Text(
			log_frame,
			height=10,
			wrap=tk.WORD,
			state=tk.DISABLED,
			font=("Consolas", 9),
			bg="#1e1e1e",
			fg="#d4d4d4",
			insertbackground="#d4d4d4",
		)
		scroll = ttk.Scrollbar(log_frame, orient=tk.VERTICAL, command=self.txt_log.yview)
		self.txt_log.configure(yscrollcommand=scroll.set)
		self.txt_log.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
		scroll.pack(side=tk.RIGHT, fill=tk.Y)

		btns = ttk.Frame(frm)
		btns.grid(row=12, column=0, columnspan=3, sticky="ew", **pad)
		self.btn_clear_log = ttk.Button(btns, text="Clear Log", command=self._clear_log)
		self.btn_clear_log.pack(side=tk.LEFT)
		ttk.Button(btns, text="Create Project", command=self._create).pack(side=tk.RIGHT, padx=(8, 0))
		ttk.Button(btns, text="Close", command=self.destroy).pack(side=tk.RIGHT)

		frm.columnconfigure(1, weight=1)
		frm.rowconfigure(5, weight=1)
		frm.rowconfigure(8, weight=2)
		frm.rowconfigure(11, weight=2)

		self._reload_plugins()

	def _selected_template_key(self) -> str:
		label = self.cmb_template.get()
		for t in ENGINE_TEMPLATES:
			if t["label"] == label:
				return t["key"]
		return "client"

	def _deps_of(self, name: str) -> list[str]:
		for p in self._plugins:
			if p["Name"] == name:
				return p["Dependencies"]
		return []

	def _reload_plugins(self) -> None:
		for child in self.plugin_inner.winfo_children():
			child.destroy()
		engine = Path(self.var_engine.get().strip())
		self._plugins = list_engine_plugins(engine)
		self._plugin_vars = {}
		self._tool_names = []
		self._extension_names = []

		tools = [p for p in self._plugins if p["Extension"] and p["Extension"]["Stage"] == "ESingletonStage"]
		extensions = [p for p in self._plugins if p["Extension"] and p["Extension"]["Stage"] == "EEngineStage"]

		if tools:
			ttk.Label(self.plugin_inner, text="工具 (Tool)", font=("Segoe UI", 9, "bold")).pack(anchor="w", pady=(0, 2))
			for p in tools:
				self._add_plugin_checkbox(p, self._tool_names)
		if extensions:
			ttk.Label(self.plugin_inner, text="拓展 (Extension)", font=("Segoe UI", 9, "bold")).pack(anchor="w", pady=(8, 2))
			for p in extensions:
				self._add_plugin_checkbox(p, self._extension_names)

		self._apply_template()

	def _add_plugin_checkbox(self, p: dict, name_list: list[str]) -> None:
		name = p["Name"]
		name_list.append(name)
		var = tk.BooleanVar(value=False)
		text = name
		if p["Description"]:
			text += f"  —  {p['Description']}"
		cb = ttk.Checkbutton(
			self.plugin_inner,
			text=text,
			variable=var,
			command=lambda n=name: self._toggle_plugin(n),
		)
		cb.pack(anchor="w", fill=tk.X)
		self._plugin_vars[name] = var

	def _on_template_change(self, _event=None) -> None:
		self._apply_template()

	def _apply_template(self) -> None:
		template = next(
			(t for t in ENGINE_TEMPLATES if t["label"] == self.cmb_template.get()),
			ENGINE_TEMPLATES[0],
		)

		# Tools: all checked by default except the null engine.
		default_tools = template["key"] != "null"
		for name in self._tool_names:
			self._plugin_vars[name].set(default_tools)

		# Extensions: the template's default set.
		ext_defaults = set(template["default_extensions"])
		for name in self._extension_names:
			self._plugin_vars[name].set(name in ext_defaults)

	def _toggle_plugin(self, name: str) -> None:
		var = self._plugin_vars[name]
		if var.get():
			self._check_deps(name)
		else:
			self._uncheck_dependents(name)

	def _check_deps(self, name: str) -> None:
		for dep in self._deps_of(name):
			dep_var = self._plugin_vars.get(dep)
			if dep_var is None:
				continue
			if not dep_var.get():
				dep_var.set(True)
				self._check_deps(dep)

	def _uncheck_dependents(self, name: str) -> None:
		for p in self._plugins:
			if name not in p["Dependencies"]:
				continue
			dep_var = self._plugin_vars.get(p["Name"])
			if dep_var is None:
				continue
			if dep_var.get():
				dep_var.set(False)
				self._uncheck_dependents(p["Name"])

	def append_log(self, text: str) -> None:
		def _do() -> None:
			self.txt_log.configure(state=tk.NORMAL)
			self.txt_log.insert(tk.END, text)
			self.txt_log.see(tk.END)
			self.txt_log.configure(state=tk.DISABLED)

		if threading.current_thread() is threading.main_thread():
			_do()
		else:
			self.after(0, _do)

	def log_line(self, message: str) -> None:
		self.append_log(message if message.endswith("\n") else message + "\n")

	def _clear_log(self) -> None:
		self.txt_log.configure(state=tk.NORMAL)
		self.txt_log.delete("1.0", tk.END)
		self.txt_log.configure(state=tk.DISABLED)

	def _browse_parent(self) -> None:
		path = filedialog.askdirectory(initialdir=self.var_parent.get() or str(Path.home()))
		if path:
			self.var_parent.set(path)

	def _browse_engine(self) -> None:
		path = filedialog.askdirectory(initialdir=self.var_engine.get() or str(ENGINE_ROOT))
		if path:
			self.var_engine.set(path)
			self._reload_plugins()

	def _run_associate(self) -> None:
		"""Runs on a worker thread — do not touch Tk widgets except via log_line."""
		try:
			install_cproject_association(log=self.log_line)
		except Exception as ex:  # noqa: BLE001
			self.log_line(f"[Maho] Associate failed: {ex}")
		finally:
			self._associate_busy = False

	def _auto_associate_cproject_async(self) -> None:
		if sys.platform != "win32":
			return
		if self._associate_busy:
			return
		self._associate_busy = True
		self.log_line("[Maho] Registering .cproject association (background)…")
		threading.Thread(target=self._run_associate, daemon=True).start()

	def _create(self) -> None:
		name = self.var_name.get().strip()
		parent = Path(self.var_parent.get().strip())
		engine = Path(self.var_engine.get().strip())
		author = self.var_author.get().strip()
		desc = self.txt_desc.get("1.0", tk.END).strip()
		want_open = self.var_open_folder.get()
		checked_plugins = sorted(n for n, v in self._plugin_vars.items() if v.get())

		if not is_valid_project_name(name):
			messagebox.showerror("Maho", "Invalid project name.\nUse Letter + A-Z a-z 0-9 _ -")
			return
		if not parent:
			messagebox.showerror("Maho", "Parent folder is required.")
			return
		if not (engine / "Maho").is_dir():
			messagebox.showerror("Maho", f"Engine root must contain Maho/:\n{engine}")
			return
		py_root = engine / "Tools" / "python"
		if not (py_root / "python.exe").is_file() and not (py_root / "Scripts" / "python.exe").is_file():
			messagebox.showerror(
				"Maho",
				f"Engine local Python missing.\nRun Setup.bat in:\n{engine}",
			)
			return

		self.log_line(f"[Maho] Creating project '{name}' …")
		try:
			cproject = create_project(
				name,
				parent,
				engine,
				description=desc,
				author=author,
				plugins=checked_plugins,
				template=self._selected_template_key(),
				dev_platform=self.cmb_dev_platform.get(),
			)
		except Exception as ex:  # noqa: BLE001
			messagebox.showerror("Maho", str(ex))
			return
		self.log_line(f"[Maho] Wrote {cproject}")
		if want_open:
			open_in_file_manager(cproject.parent)
		self.log_line("[Maho] Project create finished successfully.")
		messagebox.showinfo(
			"Maho",
			f"Project created:\n{cproject}\n\nDouble-click {cproject.name} to generate the .sln\n(first run downloads third-party into Intermediate/).",
		)


def main() -> int:
	app = CreateProjectApp()
	app.mainloop()
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
