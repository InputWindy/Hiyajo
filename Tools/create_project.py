# Run via Tools/maho_python.bat (or Tools/*.bat) — engine Tools/python only.
"""Maho new-project UI (CreateProject.bat). Logs go to the UI, not a console window."""

from __future__ import annotations

import shutil
import sys
import threading
import time
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from maho_tools import (  # noqa: E402
	ENGINE_ROOT,
	OperationCancelled,
	create_project,
	generate_from_cproject,
	install_windows_cproject_association,
	is_valid_project_name,
	list_engine_plugins,
	open_in_file_manager,
	_kill_process,
)


# Project templates: label → plugins the template requires (transitive deps
# are added automatically and locked). Order matches the combobox list.
ENGINE_TEMPLATES = [
	{"key": "client", "label": "游戏客户端 (Client)", "required": ["World", "Render"]},
	{"key": "server", "label": "游戏服务器 (Server)", "required": ["World"]},
	{"key": "null", "label": "空引擎 (Null)", "required": []},
]
DEFAULT_TEMPLATE_LABEL = "游戏客户端 (Client)"


class CreateProjectApp(tk.Tk):
	def __init__(self) -> None:
		super().__init__()
		self.title("Maho — New Project")
		self.geometry("720x720")
		self.minsize(640, 560)
		self.resizable(True, True)

		self.var_name = tk.StringVar(value="MyGame")
		self.var_parent = tk.StringVar(value=str(Path.home() / "Documents" / "MahoProjects"))
		self.var_engine = tk.StringVar(value=str(ENGINE_ROOT))
		self.var_author = tk.StringVar(value="")
		self.var_desc = tk.StringVar(value="")
		self.var_gen_sln = tk.BooleanVar(value=True)
		self.var_open_folder = tk.BooleanVar(value=True)
		self._associate_busy = False
		self._creating = False
		self._close_after_abort = False
		self._cancel_event = threading.Event()
		self._proc_holder: list = []
		self._project_dir: Path | None = None
		self._busy_widgets: list[tk.Misc] = []
		self._plugins: list[dict] = []
		self._plugin_vars: dict[str, tk.BooleanVar] = {}
		self._plugin_widgets: dict[str, ttk.Checkbutton] = {}
		self._locked: set[str] = set()
		self._status_start = 0.0
		self._status_running = False

		self._build()
		self.protocol("WM_DELETE_WINDOW", self._on_close_request)
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
		ent_name = ttk.Entry(frm, textvariable=self.var_name)
		ent_name.grid(row=1, column=1, columnspan=2, sticky="ew", **pad)

		ttk.Label(frm, text="Parent Folder").grid(row=2, column=0, sticky="w", **pad)
		ent_parent = ttk.Entry(frm, textvariable=self.var_parent)
		ent_parent.grid(row=2, column=1, sticky="ew", **pad)
		btn_browse_parent = ttk.Button(frm, text="Browse…", command=self._browse_parent)
		btn_browse_parent.grid(row=2, column=2, sticky="e", **pad)

		ttk.Label(frm, text="Engine Root").grid(row=3, column=0, sticky="w", **pad)
		ent_engine = ttk.Entry(frm, textvariable=self.var_engine)
		ent_engine.grid(row=3, column=1, sticky="ew", **pad)
		btn_browse_engine = ttk.Button(frm, text="Browse…", command=self._browse_engine)
		btn_browse_engine.grid(row=3, column=2, sticky="e", **pad)

		ttk.Label(frm, text="Author").grid(row=4, column=0, sticky="w", **pad)
		ent_author = ttk.Entry(frm, textvariable=self.var_author)
		ent_author.grid(row=4, column=1, columnspan=2, sticky="ew", **pad)

		ttk.Label(frm, text="Description").grid(row=5, column=0, sticky="nw", **pad)
		self.txt_desc = tk.Text(frm, height=3, wrap=tk.WORD)
		self.txt_desc.grid(row=5, column=1, columnspan=2, sticky="nsew", **pad)

		ttk.Label(frm, text="Project Template").grid(row=6, column=0, sticky="w", **pad)
		self.cmb_template = ttk.Combobox(
			frm,
			state="readonly",
			values=[t["label"] for t in ENGINE_TEMPLATES],
		)
		self.cmb_template.grid(row=6, column=1, columnspan=2, sticky="ew", **pad)
		self.cmb_template.bind("<<ComboboxSelected>>", self._on_template_change)
		self.cmb_template.set(DEFAULT_TEMPLATE_LABEL)

		ttk.Label(frm, text="Plugins").grid(row=7, column=0, sticky="nw", **pad)
		plugin_frame = ttk.Frame(frm)
		plugin_frame.grid(row=7, column=1, columnspan=2, sticky="nsew", **pad)
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
		opts.grid(row=8, column=0, columnspan=3, sticky="w", **pad)
		chk_sln = ttk.Checkbutton(opts, text="Generate .sln after create", variable=self.var_gen_sln)
		chk_sln.pack(side=tk.LEFT, padx=(0, 16))
		chk_open = ttk.Checkbutton(opts, text="Open project folder", variable=self.var_open_folder)
		chk_open.pack(side=tk.LEFT)

		hint = (
			"Creates Parent/Name/ with Name.cproject (JSON, like .uproject).\n"
			"Double-click the .cproject to regenerate Name.sln beside it, then open the .sln in VS.\n"
			"Requires engine Setup.bat (local Tools/python) beforehand."
		)
		ttk.Label(frm, text=hint, foreground="#555").grid(row=9, column=0, columnspan=3, sticky="w", **pad)

		ttk.Label(frm, text="Log").grid(row=10, column=0, sticky="nw", **pad)
		log_frame = ttk.Frame(frm)
		log_frame.grid(row=10, column=1, columnspan=2, sticky="nsew", **pad)
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
		btns.grid(row=11, column=0, columnspan=3, sticky="ew", **pad)
		self.btn_associate = ttk.Button(btns, text="Re-associate .cproject", command=self._associate_async)
		self.btn_associate.pack(side=tk.LEFT)
		self.btn_clear_log = ttk.Button(btns, text="Clear Log", command=self._clear_log)
		self.btn_clear_log.pack(side=tk.LEFT, padx=(8, 0))
		self.btn_create = ttk.Button(btns, text="Create Project", command=self._create)
		self.btn_create.pack(side=tk.RIGHT, padx=(8, 0))
		# Close stays enabled during create so the user can abort.
		ttk.Button(btns, text="Close", command=self._on_close_request).pack(side=tk.RIGHT)

		status_frame = ttk.Frame(frm)
		status_frame.grid(row=12, column=0, columnspan=3, sticky="ew", **pad)
		status_frame.columnconfigure(0, weight=1)
		self.progress = ttk.Progressbar(status_frame, mode="indeterminate")
		self.progress.grid(row=0, column=0, sticky="ew")
		self.lbl_status = ttk.Label(status_frame, text="", anchor="w")
		self.lbl_status.grid(row=0, column=1, padx=(8, 0))

		self._busy_widgets = [
			ent_name,
			ent_parent,
			btn_browse_parent,
			ent_engine,
			btn_browse_engine,
			ent_author,
			self.txt_desc,
			self.cmb_template,
			chk_sln,
			chk_open,
			self.btn_associate,
			self.btn_clear_log,
			self.btn_create,
		]

		frm.columnconfigure(1, weight=1)
		frm.rowconfigure(5, weight=1)
		frm.rowconfigure(7, weight=2)
		frm.rowconfigure(10, weight=2)

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

	def _required_closure(self, required: list[str]) -> set[str]:
		"""Template required plugins plus their transitive dependencies."""
		closure = set(required)
		changed = True
		while changed:
			changed = False
			for p in self._plugins:
				if p["Name"] not in closure:
					continue
				for dep in p["Dependencies"]:
					if dep not in closure:
						closure.add(dep)
						changed = True
		return closure

	def _reload_plugins(self) -> None:
		for child in self.plugin_inner.winfo_children():
			child.destroy()
		engine = Path(self.var_engine.get().strip())
		self._plugins = list_engine_plugins(engine)
		self._plugin_vars = {}
		self._plugin_widgets = {}
		for p in self._plugins:
			var = tk.BooleanVar(value=False)
			text = p["Name"]
			if p["Description"]:
				text += f"  —  {p['Description']}"
			cb = ttk.Checkbutton(
				self.plugin_inner,
				text=text,
				variable=var,
				command=lambda n=p["Name"]: self._toggle_plugin(n),
			)
			cb.pack(anchor="w", fill=tk.X)
			self._plugin_vars[p["Name"]] = var
			self._plugin_widgets[p["Name"]] = cb
			self._busy_widgets.append(cb)
		self._apply_template()

	def _on_template_change(self, _event=None) -> None:
		self._apply_template()

	def _apply_template(self) -> None:
		label = self.cmb_template.get()
		required: list[str] = []
		for t in ENGINE_TEMPLATES:
			if t["label"] == label:
				required = t["required"]
				break
		self._locked = self._required_closure(required)
		for name, var in self._plugin_vars.items():
			cb = self._plugin_widgets[name]
			if name in self._locked:
				var.set(True)
				cb.configure(state=tk.DISABLED)
			else:
				cb.configure(state=tk.NORMAL)
				var.set(False)

	def _toggle_plugin(self, name: str) -> None:
		var = self._plugin_vars[name]
		if name in self._locked:
			var.set(True)
			return
		if var.get():
			self._check_deps(name)
		else:
			self._uncheck_dependents(name)

	def _check_deps(self, name: str) -> None:
		for dep in self._deps_of(name):
			dep_var = self._plugin_vars.get(dep)
			if dep_var is None or dep in self._locked:
				continue
			if not dep_var.get():
				dep_var.set(True)
				self._check_deps(dep)

	def _uncheck_dependents(self, name: str) -> None:
		for p in self._plugins:
			if name not in p["Dependencies"]:
				continue
			dep_var = self._plugin_vars.get(p["Name"])
			if dep_var is None or p["Name"] in self._locked:
				continue
			if dep_var.get():
				dep_var.set(False)
				self._uncheck_dependents(p["Name"])

	def _set_creating(self, busy: bool) -> None:
		self._creating = busy
		state = tk.DISABLED if busy else tk.NORMAL
		for w in self._busy_widgets:
			try:
				w.configure(state=state)
			except tk.TclError:
				pass
		# Log stays read-only (append_log toggles NORMAL briefly).
		if not busy:
			self.txt_log.configure(state=tk.DISABLED)

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
		if self._creating:
			return
		self.txt_log.configure(state=tk.NORMAL)
		self.txt_log.delete("1.0", tk.END)
		self.txt_log.configure(state=tk.DISABLED)

	def _start_status_timer(self) -> None:
		self._status_start = time.monotonic()
		self._status_running = True
		self.progress.start(12)
		self._tick_status()

	def _tick_status(self) -> None:
		if not self._status_running:
			return
		elapsed = int(time.monotonic() - self._status_start)
		self.lbl_status.configure(text=f"… working ({elapsed}s)")
		self.after(1000, self._tick_status)

	def _stop_status_timer(self) -> None:
		self._status_running = False
		self.progress.stop()
		self.lbl_status.configure(text="")

	def _browse_parent(self) -> None:
		path = filedialog.askdirectory(initialdir=self.var_parent.get() or str(Path.home()))
		if path:
			self.var_parent.set(path)

	def _browse_engine(self) -> None:
		path = filedialog.askdirectory(initialdir=self.var_engine.get() or str(ENGINE_ROOT))
		if path:
			self.var_engine.set(path)
			self._reload_plugins()

	def _kill_active_proc(self) -> None:
		if self._proc_holder:
			_kill_process(self._proc_holder[0])

	def _delete_project_dir(self, project_dir: Path | None) -> None:
		if project_dir is None or not project_dir.exists():
			return
		try:
			shutil.rmtree(project_dir, ignore_errors=False)
			self.log_line(f"[Maho] Deleted incomplete project: {project_dir}")
		except Exception as ex:  # noqa: BLE001
			self.log_line(f"[Maho] Failed to delete {project_dir}: {ex}")

	def _on_close_request(self) -> None:
		if not self._creating:
			self.destroy()
			return

		ok = messagebox.askyesno(
			"Maho",
			"Project creation is still running.\n\n"
			"Abort creation and delete the target project folder?",
			icon=messagebox.WARNING,
		)
		if not ok:
			return

		self._close_after_abort = True
		self._cancel_event.set()
		self._kill_active_proc()
		self.log_line("[Maho] Abort requested — stopping and deleting project…")

	def _finish_create_ui(self, *, aborted: bool) -> None:
		self._stop_status_timer()
		if aborted and self._close_after_abort:
			self.destroy()
			return
		self._set_creating(False)
		self._close_after_abort = False
		self._project_dir = None

	def _run_associate(self, *, show_dialog: bool) -> None:
		"""Runs on a worker thread — do not touch Tk widgets except via after/log_line."""
		try:
			install_windows_cproject_association(log=self.log_line)
			if show_dialog:
				self.after(
					0,
					lambda: messagebox.showinfo(
						"Maho",
						"Associated .cproject for the current Windows user:\n"
						"  • Double-click → generate .sln\n"
						"  • Right-click → 选择链接引擎…\n\n"
						"If Explorer still asks which app to use, close all Explorer\n"
						"windows once, or sign out/in.",
					),
				)
		except Exception as ex:  # noqa: BLE001
			err = str(ex)
			self.log_line(f"[Maho] Associate failed: {err}")
			if show_dialog:
				self.after(0, lambda e=err: messagebox.showerror("Maho", e))
		finally:
			self._associate_busy = False

	def _auto_associate_cproject_async(self) -> None:
		if sys.platform != "win32":
			return
		if self._associate_busy:
			return
		self._associate_busy = True
		self.log_line("[Maho] Registering .cproject association (background)…")
		threading.Thread(target=self._run_associate, kwargs={"show_dialog": False}, daemon=True).start()

	def _associate_async(self) -> None:
		if self._creating:
			return
		if self._associate_busy:
			self.log_line("[Maho] Association already running…")
			return
		self._associate_busy = True
		self.log_line("[Maho] Re-associating .cproject…")
		threading.Thread(target=self._run_associate, kwargs={"show_dialog": True}, daemon=True).start()

	def _create(self) -> None:
		if self._creating:
			return

		name = self.var_name.get().strip()
		parent = Path(self.var_parent.get().strip())
		engine = Path(self.var_engine.get().strip())
		author = self.var_author.get().strip()
		desc = self.txt_desc.get("1.0", tk.END).strip()
		want_sln = self.var_gen_sln.get()
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

		self._cancel_event.clear()
		self._close_after_abort = False
		self._proc_holder.clear()
		self._project_dir = None
		self._set_creating(True)
		self._start_status_timer()
		self.log_line(f"[Maho] Creating project '{name}' …")

		def work() -> None:
			project_dir: Path | None = None
			aborted = False
			try:
				if self._cancel_event.is_set():
					raise OperationCancelled("Cancelled")

				cproject = create_project(
					name,
					parent,
					engine,
					description=desc,
					author=author,
					plugins=checked_plugins,
					template=self._selected_template_key(),
				)
				project_dir = cproject.parent
				self._project_dir = project_dir
				self.log_line(f"[Maho] Wrote {cproject}")

				if self._cancel_event.is_set():
					raise OperationCancelled("Cancelled")

				sln_msg = ""
				if want_sln:
					self.log_line("[Maho] Generating .sln (cmake; may take a moment)…")
					sln = generate_from_cproject(
						cproject,
						log=self.log_line,
						cancel_event=self._cancel_event,
						proc_holder=self._proc_holder,
					)
					sln_msg = f"\nSLN: {sln}"
					self.log_line(f"[Maho] SLN: {sln}")

				if self._cancel_event.is_set():
					raise OperationCancelled("Cancelled")

				if want_open:
					open_in_file_manager(cproject.parent)
				self.log_line("[Maho] Project create finished successfully.")

				def done_ok() -> None:
					self._finish_create_ui(aborted=False)
					messagebox.showinfo("Maho", f"Project created:\n{cproject}{sln_msg}")

				self.after(0, done_ok)
			except OperationCancelled:
				aborted = True
				self.log_line("[Maho] Creation aborted by user.")
				self._delete_project_dir(project_dir or self._project_dir)
				self.after(0, lambda: self._finish_create_ui(aborted=True))
			except Exception as ex:  # noqa: BLE001
				err = str(ex)
				self.log_line(f"[ERROR] {err}")

				def done_err(e: str = err) -> None:
					self._finish_create_ui(aborted=False)
					messagebox.showerror("Maho", e)

				self.after(0, done_err)

		threading.Thread(target=work, daemon=True).start()


def main() -> int:
	app = CreateProjectApp()
	app.mainloop()
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
