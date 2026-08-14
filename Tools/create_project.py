# Run via Tools/maho_python.bat (or Tools/*.bat) — engine Tools/python only.
"""Maho new-project UI (CreateProject.bat). Logs go to the UI, not a console window."""

from __future__ import annotations

import shutil
import sys
import threading
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
	open_in_file_manager,
)


class CreateProjectApp(tk.Tk):
	def __init__(self) -> None:
		super().__init__()
		self.title("Maho — New Project")
		self.geometry("720x620")
		self.minsize(640, 520)
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

		opts = ttk.Frame(frm)
		opts.grid(row=6, column=0, columnspan=3, sticky="w", **pad)
		chk_sln = ttk.Checkbutton(opts, text="Generate .sln after create", variable=self.var_gen_sln)
		chk_sln.pack(side=tk.LEFT, padx=(0, 16))
		chk_open = ttk.Checkbutton(opts, text="Open project folder", variable=self.var_open_folder)
		chk_open.pack(side=tk.LEFT)

		hint = (
			"Creates Parent/Name/ with Name.cproject (JSON, like .uproject).\n"
			"Double-click the .cproject to regenerate Name.sln beside it, then open the .sln in VS.\n"
			"Requires engine Setup.bat (local Tools/python) beforehand."
		)
		ttk.Label(frm, text=hint, foreground="#555").grid(row=7, column=0, columnspan=3, sticky="w", **pad)

		ttk.Label(frm, text="Log").grid(row=8, column=0, sticky="nw", **pad)
		log_frame = ttk.Frame(frm)
		log_frame.grid(row=8, column=1, columnspan=2, sticky="nsew", **pad)
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
		btns.grid(row=9, column=0, columnspan=3, sticky="ew", **pad)
		self.btn_associate = ttk.Button(btns, text="Re-associate .cproject", command=self._associate_async)
		self.btn_associate.pack(side=tk.LEFT)
		self.btn_clear_log = ttk.Button(btns, text="Clear Log", command=self._clear_log)
		self.btn_clear_log.pack(side=tk.LEFT, padx=(8, 0))
		self.btn_create = ttk.Button(btns, text="Create Project", command=self._create)
		self.btn_create.pack(side=tk.RIGHT, padx=(8, 0))
		# Close stays enabled during create so the user can abort.
		ttk.Button(btns, text="Close", command=self._on_close_request).pack(side=tk.RIGHT)

		self._busy_widgets = [
			ent_name,
			ent_parent,
			btn_browse_parent,
			ent_engine,
			btn_browse_engine,
			ent_author,
			self.txt_desc,
			chk_sln,
			chk_open,
			self.btn_associate,
			self.btn_clear_log,
			self.btn_create,
		]

		frm.columnconfigure(1, weight=1)
		frm.rowconfigure(5, weight=1)
		frm.rowconfigure(8, weight=2)

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

	def _browse_parent(self) -> None:
		path = filedialog.askdirectory(initialdir=self.var_parent.get() or str(Path.home()))
		if path:
			self.var_parent.set(path)

	def _browse_engine(self) -> None:
		path = filedialog.askdirectory(initialdir=self.var_engine.get() or str(ENGINE_ROOT))
		if path:
			self.var_engine.set(path)

	def _kill_active_proc(self) -> None:
		if self._proc_holder:
			proc = self._proc_holder[0]
			try:
				proc.kill()
			except OSError:
				pass

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
		self.log_line(f"[Maho] Creating project '{name}' …")

		def work() -> None:
			project_dir: Path | None = None
			aborted = False
			try:
				if self._cancel_event.is_set():
					raise OperationCancelled("Cancelled")

				cproject = create_project(name, parent, engine, description=desc, author=author)
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
