# Shared helpers for create_project.py / generateProject.py / package.py / clean.py
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import threading
from pathlib import Path
from typing import Any


# Tools/maho_tools.py → repo root is parent of Tools/
ENGINE_ROOT = Path(__file__).resolve().parents[1]
TEMPLATE_DIR = ENGINE_ROOT / "Build" / "Templates" / "GameProject"
CPROJECT_VERSION = 1
ENGINE_PYTHON_DIR = (ENGINE_ROOT / "Tools" / "python").resolve()


def ensure_engine_python() -> None:
	"""
	Require the Maho-managed interpreter (setup.bat), not an arbitrary system Python.
	Real files live under %LOCALAPPDATA%\\Maho\\python\\tooling\\; Tools\\python is a junction.
	Set MAHO_ALLOW_SYSTEM_PYTHON=1 only for emergency debugging.
	"""
	if os.environ.get("MAHO_ALLOW_SYSTEM_PYTHON") == "1":
		return

	exe = Path(sys.executable).resolve()
	allowed_roots: list[Path] = []
	if ENGINE_PYTHON_DIR.is_dir():
		allowed_roots.append(ENGINE_PYTHON_DIR.resolve())
	local_app = os.environ.get("LOCALAPPDATA")
	if local_app:
		allowed_roots.append((Path(local_app) / "Maho" / "python").resolve())

	for root in allowed_roots:
		try:
			exe.relative_to(root)
			return
		except ValueError:
			continue

	msg = (
		"This Maho tool must run with the engine-local Python, not a system install.\n\n"
		f"Expected under:\n  {ENGINE_PYTHON_DIR}\n"
		f"  (junction → %LOCALAPPDATA%\\Maho\\python\\tooling)\n\n"
		f"Current interpreter:\n  {exe}\n\n"
		"Fix:\n"
		"  1) Run setup.bat in the Maho engine root\n"
		"  2) Launch via *.bat / Tools\\maho_python.bat / Tools\\maho_pythonw.bat\n"
	)
	try:
		sys.stderr.write("[ERROR] " + msg.replace("\n", "\n[ERROR] ") + "\n")
		sys.stderr.flush()
	except Exception:
		pass

	# pythonw has no console — show a dialog so the failure is visible.
	if sys.platform == "win32":
		try:
			import ctypes

			ctypes.windll.user32.MessageBoxW(0, msg, "Maho — wrong Python", 0x10)
		except Exception:
			pass

	raise SystemExit(2)


# Enforce on every import of maho_tools (all tool entry scripts go through here,
# except reflect_codegen which imports this module for the same check).
ensure_engine_python()


def is_valid_project_name(name: str) -> bool:
	# Folder / display name. Hyphen allowed; C++ idents use project_cpp_ident().
	return bool(re.fullmatch(r"[A-Za-z][A-Za-z0-9_-]*", name or ""))


def project_cpp_ident(name: str) -> str:
	"""Map project name to a C++ identifier (hyphen → underscore)."""
	return (name or "").replace("-", "_")


def find_cmake() -> str:
	exe = shutil.which("cmake")
	if not exe:
		raise RuntimeError("cmake not found in PATH. Install CMake and restart the terminal.")
	# Prefer a real .exe — launching cmake.bat from pythonw can flash a console.
	path = Path(exe)
	if path.suffix.lower() in {".bat", ".cmd"}:
		sibling = path.with_suffix(".exe")
		if sibling.is_file():
			return str(sibling)
	return str(path.resolve()) if path.exists() else exe


def _subprocess_no_window_kwargs() -> dict[str, Any]:
	"""Avoid flashing a console when spawning tools from pythonw / GUI."""
	if sys.platform != "win32":
		return {}
	startupinfo = subprocess.STARTUPINFO()
	startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
	startupinfo.wShowWindow = subprocess.SW_HIDE
	return {
		"creationflags": subprocess.CREATE_NO_WINDOW,  # type: ignore[attr-defined]
		"startupinfo": startupinfo,
	}


def open_in_file_manager(path: Path) -> None:
	"""Open a folder/file in the OS file manager without a console flash."""
	path = path.resolve()
	if sys.platform == "win32":
		# explorer.exe is a GUI subsystem binary; still pass no-window flags for safety.
		subprocess.Popen(["explorer", str(path)], **_subprocess_no_window_kwargs())
	elif sys.platform == "darwin":
		subprocess.Popen(["open", str(path)])
	else:
		subprocess.Popen(["xdg-open", str(path)])


class OperationCancelled(Exception):
	"""Raised when a long-running tool command is aborted by the user."""


def run_command(
	cmd: list[str],
	*,
	log: Any = print,
	cancel_event: threading.Event | None = None,
	proc_holder: list[Any] | None = None,
) -> None:
	"""
	Run a process, stream combined stdout/stderr line-by-line to log/print,
	and never attach a new console window on Windows.

	If cancel_event is set (or the process is killed from outside), raises OperationCancelled.
	proc_holder, when provided, receives the live Popen so the UI can kill it on abort.
	"""
	if cancel_event is not None and cancel_event.is_set():
		raise OperationCancelled("Cancelled before start")

	log("[Maho] $ " + " ".join(cmd))
	proc = subprocess.Popen(
		cmd,
		stdout=subprocess.PIPE,
		stderr=subprocess.STDOUT,
		text=True,
		encoding="utf-8",
		errors="replace",
		bufsize=1,
		**_subprocess_no_window_kwargs(),
	)
	if proc_holder is not None:
		proc_holder.clear()
		proc_holder.append(proc)

	assert proc.stdout is not None
	try:
		for line in proc.stdout:
			if cancel_event is not None and cancel_event.is_set():
				_kill_process(proc)
				raise OperationCancelled("Cancelled")
			text = line.rstrip("\r\n")
			if text:
				log(text)
		rc = proc.wait()
		if cancel_event is not None and cancel_event.is_set():
			raise OperationCancelled("Cancelled")
		if rc != 0:
			raise RuntimeError(f"Command failed (exit {rc}): {' '.join(cmd)}")
	finally:
		if proc_holder is not None and proc_holder and proc_holder[0] is proc:
			proc_holder.clear()


def _kill_process(proc: subprocess.Popen[Any]) -> None:
	try:
		proc.kill()
	except OSError:
		pass
	try:
		proc.wait(timeout=5)
	except Exception:
		pass


def read_cproject(path: Path) -> dict[str, Any]:
	with path.open("r", encoding="utf-8") as f:
		data = json.load(f)
	if "ProjectName" not in data:
		raise ValueError(f"Invalid .cproject (missing ProjectName): {path}")
	return data


def write_cproject(path: Path, data: dict[str, Any]) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	with path.open("w", encoding="utf-8", newline="\n") as f:
		json.dump(data, f, indent=2, ensure_ascii=False)
		f.write("\n")


def resolve_engine_directory(cproject_path: Path, data: dict[str, Any]) -> Path:
	raw = data.get("EngineDirectory") or str(ENGINE_ROOT)
	engine = Path(raw)
	if not engine.is_absolute():
		engine = (cproject_path.parent / engine).resolve()
	else:
		engine = engine.resolve()
	if not (engine / "Maho").is_dir():
		raise FileNotFoundError(f"Maho engine not found under: {engine}")
	return engine


def engine_path_for_cproject(engine_root: Path, project_dir: Path) -> str:
	try:
		rel = os.path.relpath(engine_root, project_dir)
		return rel.replace("\\", "/")
	except ValueError:
		return str(engine_root.resolve()).replace("\\", "/")


def set_cproject_engine(cproject_path: Path, engine_root: Path) -> str:
	"""
	Rewrite EngineDirectory in a .cproject (relative when possible).
	Returns the path string written into the JSON.
	"""
	cproject_path = cproject_path.expanduser().resolve()
	engine_root = engine_root.expanduser().resolve()
	if cproject_path.suffix.lower() != ".cproject" or not cproject_path.is_file():
		raise FileNotFoundError(f"Not a .cproject file: {cproject_path}")
	if not (engine_root / "Maho").is_dir():
		raise FileNotFoundError(f"Maho engine not found under: {engine_root}")

	data = read_cproject(cproject_path)
	stored = engine_path_for_cproject(engine_root, cproject_path.parent)
	data["EngineDirectory"] = stored
	data["EngineAssociation"] = data.get("EngineAssociation") or "Maho"
	write_cproject(cproject_path, data)
	return stored


def render_template_text(text: str, mapping: dict[str, str]) -> str:
	out = text
	for key, value in mapping.items():
		out = out.replace("{{" + key + "}}", value)
	return out


def copy_template(project_dir: Path, mapping: dict[str, str]) -> None:
	if not TEMPLATE_DIR.is_dir():
		raise FileNotFoundError(f"Template missing: {TEMPLATE_DIR}")

	for src in TEMPLATE_DIR.rglob("*"):
		if src.is_dir():
			continue
		rel = src.relative_to(TEMPLATE_DIR)
		rel_parts = [render_template_text(part, mapping) for part in rel.parts]
		dst = project_dir.joinpath(*rel_parts)
		dst.parent.mkdir(parents=True, exist_ok=True)
		if src.suffix.lower() in {".png", ".bin", ".exe"}:
			shutil.copy2(src, dst)
		else:
			text = src.read_text(encoding="utf-8")
			dst.write_text(render_template_text(text, mapping), encoding="utf-8", newline="\n")


def create_project(
	project_name: str,
	parent_dir: Path,
	engine_root: Path,
	description: str = "",
	author: str = "",
) -> Path:
	if not is_valid_project_name(project_name):
		raise ValueError(
			"Project name must start with a letter and contain only A-Z, a-z, 0-9, _, -"
		)

	parent_dir = parent_dir.expanduser().resolve()
	engine_root = engine_root.expanduser().resolve()
	project_dir = parent_dir / project_name
	if project_dir.exists() and any(project_dir.iterdir()):
		raise FileExistsError(f"Target folder is not empty: {project_dir}")

	project_dir.mkdir(parents=True, exist_ok=True)
	ident = project_cpp_ident(project_name)
	mapping = {
		"PROJECT_NAME": project_name,
		"PROJECT_IDENT": ident,
		"APP_CLASS": f"F{ident}App",
		"DESCRIPTION": description,
		"AUTHOR": author,
	}
	copy_template(project_dir, mapping)

	cproject = {
		"FileVersion": CPROJECT_VERSION,
		"EngineAssociation": "Maho",
		"EngineDirectory": engine_path_for_cproject(engine_root, project_dir),
		"ProjectName": project_name,
		"Description": description,
		"Author": author,
		"Modules": [
			{
				"Name": project_name,
				"Type": "Runtime",
			}
		],
		"Plugins": default_engine_plugin_entries(engine_root),
	}
	cproject_path = project_dir / f"{project_name}.cproject"
	write_cproject(cproject_path, cproject)
	generate_game_app_cpp(cproject_path)
	return cproject_path


def _rewrite_sln_paths(sln_text: str) -> str:
	def repl(match: re.Match[str]) -> str:
		prefix = match.group(1)
		path = match.group(2)
		suffix = match.group(3)
		norm = path.replace("/", "\\")
		if os.path.isabs(path) or norm.lower().startswith("intermediate\\"):
			return match.group(0)
		return f'{prefix}Intermediate\\{norm}{suffix}'

	return re.sub(
		r'(Project\("[^"]+"\)\s*=\s*"[^"]+",\s*")([^"]+)(")',
		repl,
		sln_text,
	)


# VS / CMake noise that should not appear in the sibling .sln tree.
_SLN_STRIP_PROJECT_NAMES = {
	"ALL_BUILD",
	"ZERO_CHECK",
	"INSTALL",
	"PACKAGE",
	"RUN_TESTS",
	"Nightly",
	"NightlyMemoryCheck",
	"Experimental",
	"Continuous",
	"CMakePredefinedTargets",
	"Packaging",
}


def _strip_sln_noise_projects(sln_text: str) -> str:
	"""Remove CMakePredefinedTargets / Packaging folders and related projects from a .sln."""
	# Collect GUIDs for projects we want to drop (by display name).
	drop_guids: set[str] = set()
	project_re = re.compile(
		r'Project\("\{[^}]+\}"\)\s*=\s*"([^"]+)",\s*"[^"]*",\s*"(\{[^}]+\})"',
		re.IGNORECASE,
	)
	for match in project_re.finditer(sln_text):
		name = match.group(1)
		guid = match.group(2).upper()
		if name in _SLN_STRIP_PROJECT_NAMES:
			drop_guids.add(guid)

	if not drop_guids:
		return sln_text

	# Drop whole Project ... EndProject blocks whose project GUID is in drop_guids.
	block_re = re.compile(
		r'Project\("\{[^}]+\}"\)\s*=\s*"[^"]+",\s*"[^"]*",\s*"(\{[^}]+\})"\s*\r?\n'
		r'.*?EndProject\s*\r?\n?',
		re.IGNORECASE | re.DOTALL,
	)

	def keep_block(match: re.Match[str]) -> str:
		guid = match.group(1).upper()
		return "" if guid in drop_guids else match.group(0)

	text = block_re.sub(keep_block, sln_text)

	# Drop NestedProjects / ProjectConfigurationPlatforms lines that mention dropped GUIDs.
	out_lines: list[str] = []
	for line in text.splitlines(keepends=True):
		upper = line.upper()
		if any(g in upper for g in drop_guids):
			# Keep section headers / EndGlobalSection lines intact.
			stripped = line.strip()
			if stripped.startswith("GlobalSection(") or stripped.startswith("EndGlobalSection"):
				out_lines.append(line)
			continue
		out_lines.append(line)

	return "".join(out_lines)


def emit_sibling_sln(intermediate_dir: Path, project_dir: Path, project_name: str) -> Path:
	candidates = sorted(intermediate_dir.glob("*.sln"))
	if not candidates:
		raise FileNotFoundError(f"No .sln generated under {intermediate_dir}")

	src = None
	for c in candidates:
		if c.stem.lower() == project_name.lower():
			src = c
			break
	if src is None:
		src = candidates[0]

	text = src.read_text(encoding="utf-8", errors="replace")
	text = _rewrite_sln_paths(text)
	text = _strip_sln_noise_projects(text)
	dst = project_dir / f"{project_name}.sln"
	dst.write_text(text, encoding="utf-8", newline="\n")
	return dst


def run_cmake_generate(
	source_dir: Path,
	binary_dir: Path,
	engine_root: Path | None = None,
	*,
	cproject: Path | None = None,
	log: Any = print,
	cancel_event: threading.Event | None = None,
	proc_holder: list[Any] | None = None,
) -> None:
	cmake = find_cmake()
	cmd = [
		cmake,
		"-S",
		str(source_dir),
		"-B",
		str(binary_dir),
		"-G",
		"Visual Studio 17 2022",
		"-A",
		"x64",
	]
	if engine_root is not None:
		cmd.append(f"-DMAHO_ENGINE_ROOT={engine_root}")
	if cproject is not None:
		cmd.append(f"-DMAHO_CPROJECT={cproject}")
	run_command(cmd, log=log, cancel_event=cancel_event, proc_holder=proc_holder)


def generate_from_cproject(
	cproject_path: Path,
	*,
	log: Any = print,
	cancel_event: threading.Event | None = None,
	proc_holder: list[Any] | None = None,
) -> Path:
	cproject_path = cproject_path.resolve()
	data = read_cproject(cproject_path)
	project_dir = cproject_path.parent
	project_name = str(data["ProjectName"])
	engine_root = resolve_engine_directory(cproject_path, data)
	intermediate = project_dir / "Intermediate"

	log(f"[Maho] Project : {project_name}")
	log(f"[Maho] Dir     : {project_dir}")
	log(f"[Maho] Engine  : {engine_root}")

	generate_game_app_cpp(cproject_path, log=log)

	run_cmake_generate(
		project_dir,
		intermediate,
		engine_root=engine_root,
		cproject=cproject_path,
		log=log,
		cancel_event=cancel_event,
		proc_holder=proc_holder,
	)
	if cancel_event is not None and cancel_event.is_set():
		raise OperationCancelled("Cancelled")
	sln = emit_sibling_sln(intermediate, project_dir, project_name)
	log(f"[Maho] Solution: {sln}")
	return sln


def generate_engine_workspace(
	engine_root: Path | None = None,
	*,
	log: Any = print,
	cancel_event: threading.Event | None = None,
	proc_holder: list[Any] | None = None,
) -> Path:
	engine_root = (engine_root or ENGINE_ROOT).resolve()
	source_dir = engine_root / "Build"
	if not (source_dir / "CMakeLists.txt").is_file():
		raise FileNotFoundError(f"Engine CMake entry missing: {source_dir / 'CMakeLists.txt'}")
	intermediate = engine_root / "Intermediate"
	run_cmake_generate(
		source_dir,
		intermediate,
		engine_root=None,
		log=log,
		cancel_event=cancel_event,
		proc_holder=proc_holder,
	)
	if cancel_event is not None and cancel_event.is_set():
		raise OperationCancelled("Cancelled")
	sln = emit_sibling_sln(intermediate, engine_root, "MahoWorkspace")
	log(f"[Maho] Workspace solution: {sln}")
	return sln


def run_package(
	project_dir: Path,
	config: str = "Release",
	platform: str = "Win64",
	*,
	log: Any = print,
	cancel_event: threading.Event | None = None,
	proc_holder: list[Any] | None = None,
) -> None:
	cmake = find_cmake()
	project_dir = project_dir.resolve()
	intermediate = project_dir / "Intermediate"
	if not (intermediate / "CMakeCache.txt").is_file():
		raise RuntimeError("Project not generated yet. Run generateProject on the .cproject first.")

	if cancel_event is not None and cancel_event.is_set():
		raise OperationCancelled("Cancelled")

	packaged = project_dir / "Packaged" / platform
	if packaged.exists():
		shutil.rmtree(packaged, ignore_errors=True)
	packaged.mkdir(parents=True, exist_ok=True)

	run_command(
		[cmake, "--build", str(intermediate), "--config", config],
		log=log,
		cancel_event=cancel_event,
		proc_holder=proc_holder,
	)
	if cancel_event is not None and cancel_event.is_set():
		raise OperationCancelled("Cancelled")
	run_command(
		[
			cmake,
			"--install",
			str(intermediate),
			"--prefix",
			str(packaged),
			"--config",
			config,
			"--component",
			"Runtime",
		],
		log=log,
		cancel_event=cancel_event,
		proc_holder=proc_holder,
	)
	if cancel_event is not None and cancel_event.is_set():
		raise OperationCancelled("Cancelled")
	log(f"[Maho] Packaged → {packaged}")


def _winreg_delete_tree(root: Any, path: str) -> None:
	"""Delete a registry key and all subkeys (HKCU Classes cleanup)."""
	import winreg

	try:
		with winreg.OpenKey(root, path, 0, winreg.KEY_READ | winreg.KEY_WRITE) as key:
			while True:
				try:
					sub = winreg.EnumKey(key, 0)
				except OSError:
					break
				_winreg_delete_tree(root, f"{path}\\{sub}")
		winreg.DeleteKey(root, path)
	except FileNotFoundError:
		pass
	except OSError:
		pass


def install_windows_cproject_association(*, log: Any = print) -> None:
	if sys.platform != "win32":
		raise RuntimeError("File association is only implemented for Windows.")

	import winreg

	# Open via wscript.exe — Windows often refuses to make .bat the silent default
	# and keeps showing "Select an app" (especially after Catty→Maho renames).
	generate_vbs = ENGINE_ROOT / "Tools" / "launch_generate_project.vbs"
	switch_vbs = ENGINE_ROOT / "Tools" / "launch_switch_engine.vbs"
	if not generate_vbs.is_file():
		raise FileNotFoundError(f"Missing {generate_vbs}")
	if not switch_vbs.is_file():
		raise FileNotFoundError(f"Missing {switch_vbs}")

	prog_id = "Maho.CProject"
	wscript = str(Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "wscript.exe")
	open_command = f"\"{wscript}\" //nologo \"{generate_vbs}\" \"%1\""
	switch_command = f"\"{wscript}\" //nologo \"{switch_vbs}\" \"%1\""

	# Drop legacy Catty ProgID (points at deleted Desktop\\Catty paths).
	_winreg_delete_tree(winreg.HKEY_CURRENT_USER, r"Software\Classes\Catty.CProject")

	# Clear Explorer "chosen app" / multi-ProgID chooser state for .cproject.
	_winreg_delete_tree(
		winreg.HKEY_CURRENT_USER,
		r"Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.cproject\UserChoice",
	)
	try:
		with winreg.CreateKeyEx(
			winreg.HKEY_CURRENT_USER,
			r"Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.cproject\OpenWithProgids",
		) as key:
			# Snapshot names first — EnumValue(0) + delete can skip entries.
			names: list[str] = []
			i = 0
			while True:
				try:
					name, _, _ = winreg.EnumValue(key, i)
					names.append(name)
					i += 1
				except OSError:
					break
			for name in names:
				try:
					winreg.DeleteValue(key, name)
				except OSError:
					pass
			# Legacy ProgID leftover from Catty rename.
			for stale in ("Catty.CProject", "catty.cproject"):
				try:
					winreg.DeleteValue(key, stale)
				except OSError:
					pass
			winreg.SetValueEx(key, prog_id, 0, winreg.REG_NONE, b"")
	except OSError as ex:
		log(f"[Maho] OpenWithProgids cleanup warning: {ex}")

	# Use winreg (no reg.exe) so pythonw GUIs do not flash 3 console windows.
	with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, r"Software\Classes\.cproject") as key:
		winreg.SetValueEx(key, None, 0, winreg.REG_SZ, prog_id)

	with winreg.CreateKeyEx(
		winreg.HKEY_CURRENT_USER,
		r"Software\Classes\.cproject\OpenWithProgids",
	) as key:
		winreg.SetValueEx(key, prog_id, 0, winreg.REG_NONE, b"")

	with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, rf"Software\Classes\{prog_id}") as key:
		winreg.SetValueEx(key, None, 0, winreg.REG_SZ, "Maho Project")

	with winreg.CreateKeyEx(
		winreg.HKEY_CURRENT_USER,
		rf"Software\Classes\{prog_id}\shell\open\command",
	) as key:
		winreg.SetValueEx(key, None, 0, winreg.REG_SZ, open_command)

	# Explorer context menu: pick EngineDirectory (like UE "Switch Unreal Engine version").
	with winreg.CreateKeyEx(
		winreg.HKEY_CURRENT_USER,
		rf"Software\Classes\{prog_id}\shell\SwitchEngine",
	) as key:
		winreg.SetValueEx(key, None, 0, winreg.REG_SZ, "选择链接引擎(&E)…")
		winreg.SetValueEx(key, "MUIVerb", 0, winreg.REG_SZ, "选择链接引擎(&E)…")

	with winreg.CreateKeyEx(
		winreg.HKEY_CURRENT_USER,
		rf"Software\Classes\{prog_id}\shell\SwitchEngine\command",
	) as key:
		winreg.SetValueEx(key, None, 0, winreg.REG_SZ, switch_command)

	log("[Maho] Associated .cproject → launch_generate_project.vbs (current user)")
	log("[Maho] Context menu: 选择链接引擎 → launch_switch_engine.vbs")
	log(f"[Maho] Open: {open_command}")
	log("[Maho] Removed legacy Catty.CProject association if present.")


# Entire trees wiped by clean (including any README/.gitkeep inside).
_WIPE_DIR_NAMES = (
	"Intermediate",
	"Binaries",
	"Packaged",
	"Cached",
	"Saved",
	"out",
	"cmake-build-debug",
	"cmake-build-release",
	".vs",
)

# Extra wipe paths relative to the clean root (engine tree).
_WIPE_RELATIVE_PATHS = (
	Path("Maho") / "Source" / "Generated",
)

_DELETE_NAME_GLOBS = (
	"*.sln",
	"*.vcxproj",
	"*.vcxproj.filters",
	"*.vcxproj.user",
	"CMakeUserPresets.json",
	"compile_commands.json",
)


def _rm_tree(path: Path) -> bool:
	if not path.exists():
		return False
	try:
		if path.is_file() or path.is_symlink():
			path.unlink(missing_ok=True)
		else:
			shutil.rmtree(path, ignore_errors=True)
			if path.exists():
				# Windows file locks: best-effort second pass via cmd
				if sys.platform == "win32":
					subprocess.call(
						["cmd", "/c", "rmdir", "/s", "/q", str(path)],
						shell=False,
						**_subprocess_no_window_kwargs(),
					)
		return not path.exists()
	except OSError:
		return False


def _iter_pycache(root: Path):
	if not root.is_dir():
		return
	for p in root.rglob("__pycache__"):
		yield p
	for p in root.rglob("*.pyc"):
		yield p


def collect_clean_targets(project_dir: Path) -> list[Path]:
	"""Paths that are safe to delete (generated / local only)."""
	project_dir = project_dir.resolve()
	targets: list[Path] = []

	for name in _WIPE_DIR_NAMES:
		# Never delete tracked Build/ tooling (Windows case-insensitive).
		if name.lower() == "build":
			continue
		p = project_dir / name
		if p.exists():
			targets.append(p)

	for rel in _WIPE_RELATIVE_PATHS:
		p = project_dir / rel
		if p.exists():
			targets.append(p)

	for pattern in _DELETE_NAME_GLOBS:
		for p in project_dir.glob(pattern):
			targets.append(p)

	for p in _iter_pycache(project_dir / "Tools"):
		targets.append(p)

	# De-dupe while preserving order
	seen: set[Path] = set()
	unique: list[Path] = []
	for t in targets:
		rp = t.resolve() if t.exists() else t
		if rp in seen:
			continue
		seen.add(rp)
		unique.append(t)
	return unique


def clean_project_tree(project_dir: Path, *, dry_run: bool = False) -> list[Path]:
	"""
	Fully remove generated/temp trees under project_dir (no README placeholders left behind).
	Does not touch Maho/Test0/Build/Tools/Doc/source.
	"""
	project_dir = project_dir.resolve()
	targets = collect_clean_targets(project_dir)
	if dry_run:
		return targets

	removed: list[Path] = []
	for t in targets:
		ok = _rm_tree(t)
		if ok or not t.exists():
			removed.append(t)
		else:
			print(f"[WARN] Still locked (skipped): {t}")

	return removed


# ---------------------------------------------------------------------------
# .cplugin scan (module DAG for multi-DLL builds)
# ---------------------------------------------------------------------------

CPLUGIN_FILE_VERSION = 1
DEFAULT_ENGINE_PLUGINS_DIR = ENGINE_ROOT / "Maho" / "Plugins"


def discover_cplugin_files(plugin_roots: list[Path]) -> list[Path]:
	"""Find *.cplugin under each root (one level of plugin folders, or loose files)."""
	found: list[Path] = []
	seen: set[Path] = set()
	for root in plugin_roots:
		root = root.resolve()
		if not root.is_dir():
			continue
		# Prefer <PluginName>/<PluginName>.cplugin
		for child in sorted(root.iterdir()):
			if not child.is_dir():
				continue
			if child.name.startswith("."):
				continue
			candidate = child / f"{child.name}.cplugin"
			if candidate.is_file():
				resolved = candidate.resolve()
				if resolved not in seen:
					seen.add(resolved)
					found.append(resolved)
				continue
			# Fallback: any .cplugin directly in the plugin folder
			for cplugin in sorted(child.glob("*.cplugin")):
				resolved = cplugin.resolve()
				if resolved not in seen:
					seen.add(resolved)
					found.append(resolved)
		# Loose .cplugin at root (discouraged, still accepted)
		for cplugin in sorted(root.glob("*.cplugin")):
			resolved = cplugin.resolve()
			if resolved not in seen:
				seen.add(resolved)
				found.append(resolved)
	return found


def read_cplugin(path: Path) -> dict[str, Any]:
	path = path.resolve()
	with path.open("r", encoding="utf-8-sig") as f:
		data = json.load(f)
	if not isinstance(data, dict):
		raise ValueError(f"Invalid .cplugin (root must be object): {path}")
	version = data.get("FileVersion")
	if version is None:
		raise ValueError(f"Invalid .cplugin (missing FileVersion): {path}")
	if int(version) != CPLUGIN_FILE_VERSION:
		raise ValueError(
			f"Unsupported .cplugin FileVersion {version} (expected {CPLUGIN_FILE_VERSION}): {path}"
		)
	modules = data.get("Modules")
	if not isinstance(modules, list) or not modules:
		raise ValueError(f"Invalid .cplugin (Modules must be a non-empty array): {path}")
	return data


def _normalize_module_entry(raw: Any, *, cplugin_path: Path) -> dict[str, Any]:
	if not isinstance(raw, dict):
		raise ValueError(f"Invalid module entry in {cplugin_path}")
	name = raw.get("Name")
	if not isinstance(name, str) or not name.strip():
		raise ValueError(f"Module missing Name in {cplugin_path}")
	name = name.strip()
	module_type = raw.get("Type", "Runtime")
	if not isinstance(module_type, str) or not module_type.strip():
		module_type = "Runtime"
	deps_raw = raw.get("Dependencies", [])
	if deps_raw is None:
		deps_raw = []
	# Optional / legacy: extension order is TDependsOn, not .cplugin Dependencies.
	if not isinstance(deps_raw, list):
		raise ValueError(f"Module '{name}' Dependencies must be an array in {cplugin_path}")
	deps: list[str] = []
	for dep in deps_raw:
		if not isinstance(dep, str) or not dep.strip():
			raise ValueError(f"Module '{name}' has invalid dependency in {cplugin_path}")
		deps.append(dep.strip())

	extension = None
	ext_raw = raw.get("Extension")
	if ext_raw is not None:
		if not isinstance(ext_raw, dict):
			raise ValueError(f"Module '{name}' Extension must be an object in {cplugin_path}")
		cls = ext_raw.get("Class")
		header = ext_raw.get("Header")
		priority = ext_raw.get("Priority")
		if not isinstance(cls, str) or not cls.strip():
			raise ValueError(f"Module '{name}' Extension.Class required in {cplugin_path}")
		if not isinstance(header, str) or not header.strip():
			raise ValueError(f"Module '{name}' Extension.Header required in {cplugin_path}")
		if not isinstance(priority, str) or priority.strip() not in ("System", "Layer", "Overlay"):
			raise ValueError(
				f"Module '{name}' Extension.Priority must be System|Layer|Overlay in {cplugin_path}"
			)
		extension = {
			"Class": cls.strip(),
			"Header": header.strip(),
			"Priority": priority.strip(),
		}

	return {
		"Name": name,
		"Type": module_type.strip(),
		"Dependencies": deps,
		"Extension": extension,
	}


def topo_sort_modules(
	modules_by_name: dict[str, dict[str, Any]],
) -> tuple[list[str], list[str]]:
	"""
	Return (startup_order, shutdown_order).
	Raises ValueError on missing dependency or cycle.
	"""
	in_degree: dict[str, int] = {name: 0 for name in modules_by_name}
	adj: dict[str, list[str]] = {name: [] for name in modules_by_name}

	for name, module in modules_by_name.items():
		for dep in module["Dependencies"]:
			if dep not in modules_by_name:
				raise ValueError(
					f"FATAL: Module '{name}' depends on missing module '{dep}'"
				)
			adj[dep].append(name)
			in_degree[name] += 1

	# Stable: among zero-degree nodes, preserve declaration order via sorted ready by first-seen index
	order_index = {name: i for i, name in enumerate(modules_by_name.keys())}
	ready = sorted(
		[name for name, deg in in_degree.items() if deg == 0],
		key=lambda n: order_index[n],
	)
	startup: list[str] = []
	while ready:
		name = ready.pop(0)
		startup.append(name)
		next_ready: list[str] = []
		for nxt in adj[name]:
			in_degree[nxt] -= 1
			if in_degree[nxt] == 0:
				next_ready.append(nxt)
		next_ready.sort(key=lambda n: order_index[n])
		ready.extend(next_ready)
		ready.sort(key=lambda n: order_index[n])

	if len(startup) != len(modules_by_name):
		remaining = [n for n, d in in_degree.items() if d > 0]
		raise ValueError(
			"FATAL: Module dependency cycle involving: "
			+ ", ".join(sorted(remaining))
		)

	shutdown = list(reversed(startup))
	return startup, shutdown


def parse_cproject_plugin_overrides(data: dict[str, Any]) -> dict[str, bool] | None:
	"""
	Read .cproject Plugins[] overrides (UE .uproject style).
	Returns None when the field is absent (fall back to each .cplugin EnabledByDefault).
	Listed entries: {"Name": "...", "Enabled": true|false} or bare "Name" (Enabled=true).
	Unlisted plugins still use EnabledByDefault.
	"""
	raw = data.get("Plugins")
	if raw is None:
		return None
	if not isinstance(raw, list):
		raise ValueError(".cproject Plugins must be an array")
	overrides: dict[str, bool] = {}
	for entry in raw:
		if isinstance(entry, str):
			name = entry.strip()
			if not name:
				raise ValueError(".cproject Plugins entry is an empty string")
			overrides[name] = True
			continue
		if not isinstance(entry, dict):
			raise ValueError(".cproject Plugins entries must be objects or strings")
		name = str(entry.get("Name", "")).strip()
		if not name:
			raise ValueError(".cproject Plugins entry missing Name")
		overrides[name] = bool(entry.get("Enabled", True))
	return overrides


def default_engine_plugin_entries(engine_root: Path | None = None) -> list[dict[str, Any]]:
	"""Seed .cproject Plugins from EnabledByDefault engine plugins (stable name order)."""
	root = (engine_root or ENGINE_ROOT).resolve() / "Maho" / "Plugins"
	if not root.is_dir():
		return []
	names: list[str] = []
	for cplugin_path in discover_cplugin_files([root]):
		data = read_cplugin(cplugin_path)
		if not bool(data.get("EnabledByDefault", True)):
			continue
		names.append(cplugin_path.parent.name)
	names.sort()
	return [{"Name": name, "Enabled": True} for name in names]


def scan_plugin_modules(
	plugin_roots: list[Path],
	*,
	include_disabled: bool = False,
	enabled_overrides: dict[str, bool] | None = None,
) -> dict[str, Any]:
	"""
	Scan .cplugin manifests and resolve a global module dependency DAG.

	Returns a JSON-serializable dict with Plugins, Modules, BuildOrder, ShutdownOrder.
	Raises ValueError on duplicate names, missing deps, or dependency cycles (FATAL).
	"""
	cplugin_files = discover_cplugin_files(plugin_roots)
	plugins_out: list[dict[str, Any]] = []
	modules_by_name: dict[str, dict[str, Any]] = {}

	for cplugin_path in cplugin_files:
		data = read_cplugin(cplugin_path)
		default_enabled = data.get("EnabledByDefault", True)
		if default_enabled is None:
			default_enabled = True
		plugin_dir = cplugin_path.parent
		plugin_name = plugin_dir.name
		if enabled_overrides is not None and plugin_name in enabled_overrides:
			enabled = enabled_overrides[plugin_name]
		else:
			enabled = bool(default_enabled)
		if not include_disabled and not enabled:
			continue

		friendly = data.get("FriendlyName", plugin_name)

		plugin_modules: list[dict[str, Any]] = []
		for raw in data["Modules"]:
			normalized = _normalize_module_entry(raw, cplugin_path=cplugin_path)
			name = normalized["Name"]
			if name in modules_by_name:
				other = modules_by_name[name]["Cplugin"]
				raise ValueError(
					f"Duplicate module name '{name}' in:\n  {cplugin_path}\n  and\n  {other}"
				)
			entry = {
				"Name": name,
				"Type": normalized["Type"],
				"Dependencies": list(normalized["Dependencies"]),
				"Extension": normalized.get("Extension"),
				"Plugin": plugin_name,
				"PluginPath": str(plugin_dir.resolve()),
				"Cplugin": str(cplugin_path.resolve()),
				"SourceDir": str((plugin_dir / "Source" / name).resolve()),
			}
			modules_by_name[name] = entry
			plugin_mod = {
				"Name": name,
				"Type": normalized["Type"],
				"Dependencies": list(normalized["Dependencies"]),
			}
			if normalized.get("Extension") is not None:
				plugin_mod["Extension"] = dict(normalized["Extension"])
			plugin_modules.append(plugin_mod)

		plugins_out.append(
			{
				"Name": plugin_name,
				"FriendlyName": friendly,
				"Path": str(plugin_dir.resolve()),
				"Cplugin": str(cplugin_path.resolve()),
				"EnabledByDefault": bool(default_enabled),
				"Enabled": bool(enabled),
				"Modules": plugin_modules,
			}
		)

	startup, shutdown = topo_sort_modules(modules_by_name)

	return {
		"FileVersion": 1,
		"PluginRoots": [str(p.resolve()) for p in plugin_roots if p.is_dir()],
		"Plugins": plugins_out,
		"Modules": [modules_by_name[name] for name in startup],
		"BuildOrder": startup,
		"ShutdownOrder": shutdown,
	}


def resolve_plugin_roots_for_cproject(cproject_path: Path) -> list[Path]:
	"""Engine Maho/Plugins + game Project/Plugins (or Plugins/) when present."""
	cproject_path = cproject_path.resolve()
	data = read_cproject(cproject_path)
	engine_root = resolve_engine_directory(cproject_path, data)
	project_dir = cproject_path.parent
	roots = [engine_root / "Maho" / "Plugins"]
	for candidate in (project_dir / "Plugins", project_dir / "Project" / "Plugins"):
		if candidate.is_dir():
			roots.append(candidate)
			break
	return roots


def _scan_header_for_class(header_path: Path, base_pattern: str) -> list[dict[str, str]]:
	"""
	Scan a C++ header for class declarations inheriting from a base type.
	Returns list of dicts: {class, namespace}.
	"""
	content = header_path.read_text(encoding="utf-8", errors="replace")
	# Strip // comments
	content = re.sub(r"//[^\n]*", "", content)
	# Strip /* */ comments
	content = re.sub(r"/\*.*?\*/", "", content, flags=re.DOTALL)
	# Collapse whitespace
	content = re.sub(r"\s+", " ", content)

	# Find all namespaces (stack not needed — we just need the innermost)
	all_ns = re.findall(r"namespace\s+(\w+)\s*\{", content)
	# Only use the outermost namespace — inner Detail/etc blocks are separate scopes,
	# not nested under the class declarations we're looking for.
	namespaces = all_ns[:1] if all_ns else []

	# Find class declarations (stop at { or ; to avoid matching nested enums)
	class_re = re.compile(r"class\s+(\w+)\s+(?:final\s+)?:\s*(?:public\s+)?[^{;]*?" + base_pattern)
	matches: list[dict[str, str]] = []
	for m in class_re.finditer(content):
		cls_name = m.group(1)
		ns = "::".join(namespaces) if namespaces else ""
		matches.append({"class": cls_name, "namespace": ns})
	return matches


def _full_class_name(info: dict[str, str]) -> str:
	"""Return 'Maho::FEditorLayer' or 'FWorldLayer'."""
	ns = info.get("namespace", "")
	if ns:
		return f"{ns}::{info['class']}"
	return info["class"]


def scan_game_extensions(source_game_dir: Path) -> list[dict[str, str]]:
	"""
	Scan Source/ (Game/ + Editor/) for IEngineExtension / FLayer subclasses.
	Returns list of dicts: {class, namespace, header, priority}.
	Priority: Editor subdir → Overlay, else → Layer.
	"""
	if not source_game_dir.is_dir():
		return []

	exts: list[dict[str, str]] = []
	for header_path in sorted(source_game_dir.rglob("*.h")):
		rel = header_path.relative_to(source_game_dir.parent).as_posix()
		results = _scan_header_for_class(header_path, r"\b(?:IEngineExtension|FLayer)\b")
		for info in results:
			full_cls = _full_class_name(info)
			# Priority heuristic: System/ → System, Editor/Script → Overlay, else → Layer
			is_system = "System" in rel
			is_overlay = "Editor" in rel or "Script" in rel
			if is_system:
				priority = "System"
			elif is_overlay:
				priority = "Overlay"
			else:
				priority = "Layer"
			exts.append({
				"class": full_cls,
				"namespace": info["namespace"],
				"header": rel,
				"priority": priority,
			})
	return exts


def scan_render_features(source_render_dir: Path) -> list[dict[str, str]]:
	"""
	Scan Source/Render/ for IRenderFeature / TRenderFeatureBase subclasses.
	Returns list of dicts: {class, namespace, header}.
	"""
	if not source_render_dir.is_dir():
		return []

	features: list[dict[str, str]] = []
	for header_path in sorted(source_render_dir.rglob("*.h")):
		rel = header_path.relative_to(source_render_dir.parent).as_posix()
		results = _scan_header_for_class(header_path, r"\b(?:IRenderFeature|TRenderFeatureBase)\b")
		for info in results:
			full_cls = _full_class_name(info)
			features.append({
				"class": full_cls,
				"namespace": info["namespace"],
				"header": rel,
			})
	return features


def generate_game_app_cpp(cproject_path: Path, *, log: Any = print) -> Path:
	"""
	Write Source/Generated/{ProjectName}App.cpp from .cproject + enabled plugin Extension meta.
	"""
	cproject_path = cproject_path.resolve()
	data = read_cproject(cproject_path)
	project_dir = cproject_path.parent
	project_name = str(data["ProjectName"])
	ident = project_cpp_ident(project_name)
	app_class = f"F{ident}App"

	roots = resolve_plugin_roots_for_cproject(cproject_path)
	enabled_overrides = parse_cproject_plugin_overrides(data)
	scan = scan_plugin_modules(roots, enabled_overrides=enabled_overrides)

	plugin_exts: list[dict[str, str]] = []
	headers: list[str] = []
	seen_headers: set[str] = set()
	for module in scan["Modules"]:
		ext = module.get("Extension")
		if not ext:
			continue
		plugin_exts.append(ext)
		header = ext["Header"]
		if header not in seen_headers:
			seen_headers.add(header)
			headers.append(header)

		# ── Auto‑scan game extensions & render features ──
		game_exts = scan_game_extensions(project_dir / "Source")
		render_features = scan_render_features(project_dir / "Source" / "Render")

	# Build include lines (deduplicated)
	include_set: dict[str, str] = {}  # header → include form
	for ext in game_exts:
		h = ext["header"]
		include_set[h] = f'#include "{h}"'
	for feat in render_features:
		h = feat["header"]
		include_set[h] = f'#include "{h}"'
	for header in headers:
		include_set[header] = f"#include <{header}>"

	lines: list[str] = [
		"// <auto-generated> DO NOT EDIT — rewritten by generateProject / generate_game_app_cpp.",
		"// Source: .cproject Plugins + .cplugin Extension metadata.",
		"//          + auto‑scanned Source/Game/ (EngineExtensions) + Source/Render/ (RenderFeatures).",
		"",
		"#include <Maho.h>",
		"#include <EntryPoint.h>",
	]
	lines.extend(include_set.values())

	lines.extend(
		[
			"",
			"#include <memory>",
			"",
			f"class {app_class} : public Maho::FApp",
			"{",
			"protected:",
			"\tvirtual void Configure(Maho::FConfig& OutConfig) override",
			"\t{",
			f'\t\tOutConfig.ApplicationName = "{project_name}";',
			"\t\t// Relative dirs — FPaths::Initialize turns them into absolute under Project/Engine roots.",
			'\t\tOutConfig.EngineShadersDir = "Engine/Shaders";',
			'\t\tOutConfig.ProjectShadersDir = "Shaders";',
			'\t\tOutConfig.EnginePluginsDir = "Engine/Plugins";',
			'\t\tOutConfig.ProjectPluginsDir = "Plugins";',
			'\t\tOutConfig.ProjectContentDir = "Content";',
			'\t\tOutConfig.CachedDir = "Cached";',
			'\t\tOutConfig.SavedDir = "Saved";',
			'\t\tOutConfig.ProjectConfigDir = "Config";',
			'\t\tOutConfig.ProjectScriptsDir = "Scripts";',
			"\t}",
			"",
			"\tvirtual bool PreInitialize() override",
			"\t{",
			"\t\tusing Maho::EExtensionPriority;",
			"",
			"\t\tRegisterExtension<Maho::FPlatformSystem>(EExtensionPriority::System);",
			"\t\tRegisterExtension<Maho::FRenderSystem>(EExtensionPriority::System);",
			"",
		]
	)

	# ── Game‑scanned extensions ──
	editor_exts = [e for e in game_exts if "/Editor/" in e["header"]]
	script_exts = [e for e in game_exts if "/Script/" in e["header"]]
	layer_exts  = [e for e in game_exts if e not in editor_exts and e not in script_exts]

	if layer_exts:
		lines.append("\t\t// Auto‑scanned Game/ extensions (Layer)")
		for ext in layer_exts:
			cls = ext["class"]
			lines.append(f"\t\tRegisterExtension<{cls}>(EExtensionPriority::Layer);")
		lines.append("")

	# ── Script overlay (unconditional) ──
	if script_exts:
		lines.append("\t\t// Auto‑scanned Game/Script/ extensions (Overlay)")
		for ext in script_exts:
			cls = ext["class"]
			lines.append(f"\t\tRegisterExtension<{cls}>(EExtensionPriority::Overlay);")
		lines.append("")

	# ── Plugin extensions ──
	if plugin_exts:
		lines.append("\t\t// Enabled plugins (.cproject + .cplugin Extension)")
		for ext in plugin_exts:
			pri = ext["Priority"]
			cls = ext["Class"]
			lines.append(f"\t\tRegisterExtension<{cls}>(EExtensionPriority::{pri});")
		lines.append("")

	# ── Editor overlay (conditional) ──
	if editor_exts:
		lines.append("#if defined(GAME_WITH_EDITOR) && defined(MAHO_WITH_IMGUI)")
		lines.append("\t\t// Auto‑scanned Game/Editor/ extensions (Overlay)")
		for ext in editor_exts:
			cls = ext["class"]
			lines.append(f"\t\tRegisterExtension<{cls}>(EExtensionPriority::Overlay);")
		lines.append("#endif")
		lines.append("")

	lines.append("\t\treturn true;")
	lines.append("\t}")

	# ── PostInitialize: auto‑register render features ──
	if render_features:
		lines.append("")
		lines.append("\tvirtual bool PostInitialize() override")
		lines.append("\t{")
		lines.append("\t\tauto* RenderSystem = GetExtension<Maho::FRenderSystem>();")
		lines.append("\t\tif (RenderSystem)")
		lines.append("\t\t{")
		lines.append("\t\t\tauto& Server = RenderSystem->GetRenderServer();")
		for feat in render_features:
			cls = feat["class"]
			lines.append(f"\t\t\tServer.RegisterFeature<{cls}>();")
		lines.append("\t\t}")
		lines.append("\t\treturn true;")
		lines.append("\t}")

	lines.extend(
		[
			"};",
			"",
			"Maho::FApp* Maho::CreateApplication()",
			"{",
			f"\treturn new {app_class}();",
			"}",
			"",
		]
	)

	out_dir = project_dir / "Intermediate" / "Generated"
	out_dir.mkdir(parents=True, exist_ok=True)
	out_path = out_dir / f"{project_name}App.cpp"
	out_path.write_text("\n".join(lines), encoding="utf-8", newline="\n")
	log(f"[Maho] Generated App: {out_path}")
	return out_path
