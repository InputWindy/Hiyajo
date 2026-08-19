# Run via Tools/launch_switch_engine.vbs / maho_pythonw.bat — engine Tools/python only.
"""
Pick / rewrite EngineDirectory for a .cproject (Explorer context menu).

Usage:
  Tools\\maho_pythonw.bat Tools\\switch_engine.py path\\Game.cproject
"""

from __future__ import annotations

import sys
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from maho_tools import (  # noqa: E402
	generate_from_cproject,
	read_cproject,
	resolve_engine_directory,
	set_cproject_engine,
)


class SwitchEngineApp(tk.Tk):
	def __init__(self, cproject: Path) -> None:
		super().__init__()
		self.title("Maho — 选择链接引擎")
		self.resizable(True, False)
		self.minsize(520, 0)

		self._cproject = cproject
		data = read_cproject(cproject)
		self._project_name = str(data.get("ProjectName") or cproject.stem)
		current_raw = str(data.get("EngineDirectory") or "")
		try:
			current_resolved = str(resolve_engine_directory(cproject, data))
		except Exception:  # noqa: BLE001
			current_resolved = "(invalid / missing)"

		self.var_engine = tk.StringVar(value=current_resolved if current_resolved != "(invalid / missing)" else "")
		self.var_regen = tk.BooleanVar(value=True)

		pad = {"padx": 10, "pady": 6}
		frm = ttk.Frame(self, padding=12)
		frm.pack(fill=tk.BOTH, expand=True)
		frm.columnconfigure(1, weight=1)

		ttk.Label(frm, text="Project").grid(row=0, column=0, sticky="w", **pad)
		ttk.Label(frm, text=f"{self._project_name}  ({cproject.name})").grid(
			row=0, column=1, columnspan=2, sticky="w", **pad
		)

		ttk.Label(frm, text=".cproject").grid(row=1, column=0, sticky="nw", **pad)
		ttk.Label(frm, text=str(cproject), wraplength=420).grid(
			row=1, column=1, columnspan=2, sticky="w", **pad
		)

		ttk.Label(frm, text="Stored path").grid(row=2, column=0, sticky="w", **pad)
		ttk.Label(frm, text=current_raw or "(empty)", foreground="#666").grid(
			row=2, column=1, columnspan=2, sticky="w", **pad
		)

		ttk.Label(frm, text="Engine root").grid(row=3, column=0, sticky="w", **pad)
		ent = ttk.Entry(frm, textvariable=self.var_engine)
		ent.grid(row=3, column=1, sticky="ew", **pad)
		ttk.Button(frm, text="Browse…", command=self._browse).grid(row=3, column=2, sticky="e", **pad)

		ttk.Checkbutton(
			frm,
			text="Apply 后重新生成同级 .sln（generateProject）",
			variable=self.var_regen,
		).grid(row=4, column=1, columnspan=2, sticky="w", **pad)

		hint = ttk.Label(
			frm,
			text="选择含有 Maho/ 子目录的引擎根目录（例如 Desktop\\Hiyajo）。",
			foreground="#555",
		)
		hint.grid(row=5, column=1, columnspan=2, sticky="w", **pad)

		btns = ttk.Frame(frm)
		btns.grid(row=6, column=0, columnspan=3, sticky="e", pady=(12, 0))
		ttk.Button(btns, text="Cancel", command=self.destroy).pack(side=tk.RIGHT, padx=(6, 0))
		ttk.Button(btns, text="Apply", command=self._apply).pack(side=tk.RIGHT)

		self.bind("<Escape>", lambda _e: self.destroy())
		ent.focus_set()

	def _browse(self) -> None:
		initial = self.var_engine.get().strip()
		if not initial or not Path(initial).is_dir():
			initial = str(self._cproject.parent)
		path = filedialog.askdirectory(title="Select Maho engine root", initialdir=initial)
		if path:
			self.var_engine.set(path)

	def _apply(self) -> None:
		raw = self.var_engine.get().strip()
		if not raw:
			messagebox.showerror("Maho", "Engine root is required.")
			return
		engine = Path(raw).expanduser()
		try:
			stored = set_cproject_engine(self._cproject, engine)
			msg = f"EngineDirectory updated:\n{stored}"
			if self.var_regen.get():
				sln = generate_from_cproject(self._cproject)
				msg += f"\n\nRegenerated:\n{sln}"
			messagebox.showinfo("Maho", msg)
			self.destroy()
		except Exception as ex:  # noqa: BLE001
			messagebox.showerror("Maho", str(ex))


def main(argv: list[str]) -> int:
	# Ensure Tk exists before messagebox when launched via pythonw.
	root = tk.Tk()
	root.withdraw()
	if len(argv) < 2:
		messagebox.showerror("Maho", "Usage: switch_engine.py path\\Game.cproject")
		root.destroy()
		return 1
	target = Path(argv[1]).expanduser().resolve()
	if target.suffix.lower() != ".cproject" or not target.is_file():
		messagebox.showerror("Maho", f"Expected a .cproject file:\n{target}")
		root.destroy()
		return 1
	root.destroy()
	app = SwitchEngineApp(target)
	app.mainloop()
	return 0


if __name__ == "__main__":
	raise SystemExit(main(sys.argv))
