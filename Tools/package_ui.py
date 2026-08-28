# Run via Tools/maho_pythonw.bat / launch_package.vbs — engine Tools/python only.
"""
Maho package UI — pick platform / config and ship to Packaged/<Platform>/.

Logs go to the UI (no console). Launched by:
  - Engine: Tools/package.bat → launch_package.vbs → pythonw
  - Game project: package.bat → invoke_engine.ps1 → same VBS
"""

from __future__ import annotations

import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from maho_tools import (  # noqa: E402
	OperationCancelled,
	generate_from_cproject,
	open_in_file_manager,
	read_cproject,
	resolve_engine_directory,
	run_package,
)

# UI platform id → whether packaging is implemented today
_PLATFORMS = [
	("Win64", True),
	("Linux", False),
	("Android", False),
	("iOS", False),
	("Xbox", False),
]

_CONFIGS = ("Release", "Debug", "RelWithDebInfo", "MinSizeRel")


def _discover_cproject(project_dir: Path) -> Path | None:
	hits = sorted(project_dir.glob("*.cproject"))
	return hits[0] if hits else None


def _default_cproject_from_argv(argv: list[str]) -> Path | None:
	if len(argv) >= 2:
		p = Path(argv[1]).expanduser().resolve()
		if p.is_file() and p.suffix.lower() == ".cproject":
			return p
		if p.is_dir():
			return _discover_cproject(p)
	# Engine workspace: no default .cproject
	return None


class PackageApp(tk.Tk):
	def __init__(self, initial_cproject: Path | None = None) -> None:
		super().__init__()
		self.title("Maho — Package")
		self.geometry("720x640")
		self.minsize(600, 520)

		self.var_cproject = tk.StringVar(value=str(initial_cproject) if initial_cproject else "")
		self.var_platform = tk.StringVar(value="Win64")
		self.var_config = tk.StringVar(value="Release")
		self.var_regen = tk.BooleanVar(value=False)
		self.var_open = tk.BooleanVar(value=True)
		self._busy = False
		self._close_after_abort = False
		self._cancel_event = threading.Event()
		self._proc_holder: list = []
		self._busy_widgets: list[tk.Misc] = []

		self._build()
		self.protocol("WM_DELETE_WINDOW", self._on_close_request)
		self._refresh_summary()
		self.log_line("[Maho] UI ready.")

	def _build(self) -> None:
		pad = {"padx": 12, "pady": 6}
		frm = ttk.Frame(self, padding=12)
		frm.pack(fill=tk.BOTH, expand=True)

		ttk.Label(frm, text="Package game / workspace", font=("Segoe UI", 12, "bold")).grid(
			row=0, column=0, columnspan=3, sticky="w", **pad
		)

		ttk.Label(frm, text=".cproject").grid(row=1, column=0, sticky="w", **pad)
		ent_cproject = ttk.Entry(frm, textvariable=self.var_cproject)
		ent_cproject.grid(row=1, column=1, sticky="ew", **pad)
		btn_browse = ttk.Button(frm, text="Browse…", command=self._browse_cproject)
		btn_browse.grid(row=1, column=2, sticky="e", **pad)

		ttk.Label(frm, text="(Leave empty to package the engine workspace)", foreground="#666").grid(
			row=2, column=1, columnspan=2, sticky="w", padx=12
		)

		ttk.Label(frm, text="Platform").grid(row=3, column=0, sticky="w", **pad)
		plat_box = ttk.Combobox(
			frm,
			textvariable=self.var_platform,
			values=[name for name, _ in _PLATFORMS],
			state="readonly",
			width=24,
		)
		plat_box.grid(row=3, column=1, sticky="w", **pad)
		plat_box.bind("<<ComboboxSelected>>", lambda _e: self._refresh_summary())

		ttk.Label(frm, text="Configuration").grid(row=4, column=0, sticky="w", **pad)
		cfg = ttk.Combobox(frm, textvariable=self.var_config, values=_CONFIGS, state="readonly", width=24)
		cfg.grid(row=4, column=1, sticky="w", **pad)
		cfg.bind("<<ComboboxSelected>>", lambda _e: self._refresh_summary())

		opts = ttk.Frame(frm)
		opts.grid(row=5, column=0, columnspan=3, sticky="w", **pad)
		chk_regen = ttk.Checkbutton(opts, text="Regenerate project files before package", variable=self.var_regen)
		chk_regen.pack(side=tk.LEFT, padx=(0, 16))
		chk_open = ttk.Checkbutton(opts, text="Open Packaged folder when done", variable=self.var_open)
		chk_open.pack(side=tk.LEFT)

		ttk.Label(frm, text="Summary").grid(row=6, column=0, sticky="nw", **pad)
		self.txt_summary = tk.Text(frm, height=6, wrap=tk.WORD, state=tk.DISABLED)
		self.txt_summary.grid(row=6, column=1, columnspan=2, sticky="nsew", **pad)

		ttk.Label(frm, text="Log").grid(row=7, column=0, sticky="nw", **pad)
		log_frame = ttk.Frame(frm)
		log_frame.grid(row=7, column=1, columnspan=2, sticky="nsew", **pad)
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

		self.status = ttk.Label(frm, text="Ready", foreground="#336633")
		self.status.grid(row=8, column=0, columnspan=3, sticky="w", **pad)

		btns = ttk.Frame(frm)
		btns.grid(row=9, column=0, columnspan=3, sticky="ew", **pad)
		self.btn_clear_log = ttk.Button(btns, text="Clear Log", command=self._clear_log)
		self.btn_clear_log.pack(side=tk.LEFT)
		self.btn_run = ttk.Button(btns, text="Package", command=self._start_package)
		self.btn_run.pack(side=tk.RIGHT, padx=(8, 0))
		# Close stays enabled during package so the user can abort.
		ttk.Button(btns, text="Close", command=self._on_close_request).pack(side=tk.RIGHT)

		self._busy_widgets = [
			ent_cproject,
			btn_browse,
			plat_box,
			cfg,
			chk_regen,
			chk_open,
			self.btn_clear_log,
			self.btn_run,
		]

		frm.columnconfigure(1, weight=1)
		frm.rowconfigure(6, weight=1)
		frm.rowconfigure(7, weight=2)

		self.var_cproject.trace_add("write", lambda *_: self._refresh_summary())
		self.var_regen.trace_add("write", lambda *_: self._refresh_summary())

	def _browse_cproject(self) -> None:
		if self._busy:
			return
		path = filedialog.askopenfilename(
			title="Select .cproject",
			filetypes=[("Maho Project", "*.cproject"), ("All", "*.*")],
			initialdir=str(ENGINE_ROOT),
		)
		if path:
			self.var_cproject.set(path)

	def _set_summary(self, content: str) -> None:
		self.txt_summary.configure(state=tk.NORMAL)
		self.txt_summary.delete("1.0", tk.END)
		self.txt_summary.insert("1.0", content)
		self.txt_summary.configure(state=tk.DISABLED)

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
		if self._busy:
			return
		self.txt_log.configure(state=tk.NORMAL)
		self.txt_log.delete("1.0", tk.END)
		self.txt_log.configure(state=tk.DISABLED)

	def _refresh_summary(self) -> None:
		cproject_str = self.var_cproject.get().strip()
		platform = self.var_platform.get()
		config = self.var_config.get()
		enabled = dict(_PLATFORMS).get(platform, False)

		lines: list[str] = []
		if cproject_str:
			cp = Path(cproject_str)
			lines.append("Mode      : Game project")
			lines.append(f".cproject  : {cp}")
			try:
				data = read_cproject(cp)
				engine = resolve_engine_directory(cp, data)
				project_dir = cp.parent
				lines.append(f"Project   : {data.get('ProjectName', cp.stem)}")
				lines.append(f"Engine    : {engine}")
				lines.append(f"Output    : {project_dir / 'Packaged' / platform / config}")
			except Exception as ex:  # noqa: BLE001
				lines.append(f"Error     : {ex}")
		else:
			lines.append("Mode      : Engine workspace")
			lines.append(f"Engine    : {ENGINE_ROOT}")
			lines.append(f"Output    : {ENGINE_ROOT / 'Packaged' / platform / config}")

		lines.append(f"Platform  : {platform}" + ("" if enabled else "  (not implemented yet)"))
		lines.append(f"Config    : {config}")
		lines.append(f"Regenerate: {'yes' if self.var_regen.get() else 'no'}")
		self._set_summary("\n".join(lines))

	def _set_busy(self, busy: bool, msg: str = "") -> None:
		self._busy = busy
		state = tk.DISABLED if busy else tk.NORMAL
		for w in self._busy_widgets:
			try:
				# Combobox uses "readonly" when idle so users cannot type free text.
				if isinstance(w, ttk.Combobox):
					w.configure(state="disabled" if busy else "readonly")
				else:
					w.configure(state=state)
			except tk.TclError:
				pass
		self.status.configure(text=msg or ("Working…" if busy else "Ready"))

	def _kill_active_proc(self) -> None:
		if self._proc_holder:
			proc = self._proc_holder[0]
			try:
				proc.kill()
			except OSError:
				pass

	def _on_close_request(self) -> None:
		if not self._busy:
			self.destroy()
			return

		ok = messagebox.askyesno(
			"Maho",
			"Packaging is still running.\n\n"
			"Cancel the package operation?",
			icon=messagebox.WARNING,
		)
		if not ok:
			return

		self._close_after_abort = True
		self._cancel_event.set()
		self._kill_active_proc()
		self.log_line("[Maho] Abort requested — stopping package…")
		self.status.configure(text="Cancelling…")

	def _finish_package_ui(self, *, aborted: bool, status: str = "Ready") -> None:
		if aborted and self._close_after_abort:
			self.destroy()
			return
		self._set_busy(False, status)
		self._close_after_abort = False

	def _start_package(self) -> None:
		if self._busy:
			return

		platform = self.var_platform.get()
		if not dict(_PLATFORMS).get(platform, False):
			messagebox.showerror("Maho", f"Platform '{platform}' is not implemented yet.\nUse Win64.")
			return

		cproject_str = self.var_cproject.get().strip()
		cproject: Path | None = None
		if cproject_str:
			cproject = Path(cproject_str).expanduser().resolve()
			if not cproject.is_file():
				messagebox.showerror("Maho", f".cproject not found:\n{cproject}")
				return

		config = self.var_config.get()
		regen = self.var_regen.get()
		open_folder = self.var_open.get()

		self._cancel_event.clear()
		self._close_after_abort = False
		self._proc_holder.clear()
		self._set_busy(True, "Packaging…")
		self.log_line("[Maho] Packaging started…")

		threading.Thread(
			target=self._run_package_worker,
			args=(cproject, platform, config, regen, open_folder),
			daemon=True,
		).start()

	def _run_package_worker(
		self,
		cproject: Path | None,
		platform: str,
		config: str,
		regen: bool,
		open_folder: bool,
	) -> None:
		try:
			if self._cancel_event.is_set():
				raise OperationCancelled("Cancelled")

			if cproject is not None:
				if regen or not (cproject.parent / "Intermediate" / "CMakeCache.txt").is_file():
					self.log_line("[Maho] Generating project files…")
					generate_from_cproject(
						cproject,
						log=self.log_line,
						cancel_event=self._cancel_event,
						proc_holder=self._proc_holder,
					)
				project_dir = cproject.parent
				label = read_cproject(cproject).get("ProjectName", cproject.stem)
			else:
				self.log_line("[ERROR] A .cproject is required (engine is a catalog).")
				return

			if self._cancel_event.is_set():
				raise OperationCancelled("Cancelled")

			# Platform folder name must match MahoDirectories (Win64 today).
			out_dir = project_dir / "Packaged" / platform / config
			run_package(
				project_dir,
				config=config,
				platform=platform,
				log=self.log_line,
				cancel_event=self._cancel_event,
				proc_holder=self._proc_holder,
			)
			self.log_line(f"[Maho] Package finished for {label}.")

			def done_ok() -> None:
				self._finish_package_ui(aborted=False, status=f"Done → {out_dir}")
				messagebox.showinfo("Maho", f"Package finished for {label}\n\n{out_dir}")
				if open_folder and out_dir.is_dir():
					open_in_file_manager(out_dir)

			self.after(0, done_ok)
		except OperationCancelled:
			self.log_line("[Maho] Packaging aborted by user.")
			self.after(0, lambda: self._finish_package_ui(aborted=True, status="Cancelled"))
		except Exception as ex:  # noqa: BLE001
			err = str(ex)
			self.log_line(f"[ERROR] {err}")

			def done_err(e: str = err) -> None:
				self._finish_package_ui(aborted=False, status="Failed")
				messagebox.showerror("Maho", e)

			self.after(0, done_err)


def main(argv: list[str]) -> int:
	initial = _default_cproject_from_argv(argv)
	app = PackageApp(initial_cproject=initial)
	app.mainloop()
	return 0


if __name__ == "__main__":
	raise SystemExit(main(sys.argv))
