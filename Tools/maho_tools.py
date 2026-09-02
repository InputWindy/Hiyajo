# Shared helpers for create_project.py / generateProject.py / package.py
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
CPROJECT_VERSION = 1
ENGINE_PYTHON_DIR = (ENGINE_ROOT / "Tools" / "python").resolve()


def ensure_engine_python() -> None:
	"""
	Require the Maho-managed interpreter (Setup.bat), not an arbitrary system Python.
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
		"  1) Run Setup.bat in the Maho engine root\n"
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


# Enforce on every import of maho_tools (all tool entry scripts go through here).
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


# Newest → oldest Visual Studio generators (the one installed wins).
_VS_GENERATORS = [
	("Visual Studio 18 2026", "2026"),
	("Visual Studio 17 2022", "2022"),
	("Visual Studio 16 2019", "2019"),
]


def find_vs_generator() -> str:
	"""
	Pick the newest installed Visual Studio generator via vswhere.exe.
	Falls back to the newest known generator name when vswhere is absent
	(cmake will then error cleanly if none match).
	"""
	vswhere = (
		Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
		/ "Microsoft Visual Studio"
		/ "Installer"
		/ "vswhere.exe"
	)
	if vswhere.is_file():
		try:
			out = subprocess.run(
				[str(vswhere), "-latest", "-products", "*", "-property", "catalog_productLineVersion"],
				capture_output=True,
				text=True,
				check=False,
				**_subprocess_no_window_kwargs(),
			).stdout.strip()
			if out:
				# e.g. "2022" → generator "Visual Studio 17 2022".
				for gen_name, year in _VS_GENERATORS:
					if out.startswith(year):
						return gen_name
		except Exception:
			pass
	# No vswhere/probe — return the newest CMake name (let cmake report).
	return _VS_GENERATORS[0][0]


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


# git emits progress with \n (not \r) when its stderr is not a TTY (piped).
# Recognise those lines so they can be redrawn in place instead of spamming.
_PROGRESS_LINE_RE = re.compile(
	r"^(Receiving objects|Resolving deltas|remote: Compressing objects|remote: Counting objects|remote: Enumerating objects):")


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
		# Read char-by-char. \n = real line (log it); \r = in-place progress
		# update (git clone "Receiving objects: X%") — overwrite the SAME
		# console line via print(..., end=""), padded to clear stale chars.
		buf = ""
		on_progress = False
		while True:
			ch = proc.stdout.read(1)
			if not ch:
				break
			if cancel_event is not None and cancel_event.is_set():
				_kill_process(proc)
				raise OperationCancelled("Cancelled")
			if ch == "\n":
				if _PROGRESS_LINE_RE.match(buf) and "done." not in buf:
					# in-place progress (git uses \n when stderr isn't a TTY)
					print("\r" + buf.ljust(80), end="", flush=True)
					on_progress = True
				else:
					if on_progress:
						print()
						on_progress = False
					if buf:
						log(buf)
				buf = ""
			else:
				buf += ch
		if on_progress:
			print()
		if buf:
			log(buf)
		rc = proc.wait()
		if cancel_event is not None and cancel_event.is_set():
			raise OperationCancelled("Cancelled")
		if rc != 0:
			raise RuntimeError(f"Command failed (exit {rc}): {' '.join(cmd)}")
	finally:
		if proc_holder is not None and proc_holder and proc_holder[0] is proc:
			proc_holder.clear()


def _kill_process(proc: subprocess.Popen[Any]) -> None:
	if proc.poll() is not None:
		return
	if sys.platform == "win32":
		# Kill the whole tree — a lone proc.kill() leaves child git/clone processes
		# holding the stdout pipe, so callers block on read() forever.
		try:
			subprocess.run(
				["taskkill", "/F", "/T", "/PID", str(proc.pid)],
				**_subprocess_no_window_kwargs(),
				check=False,
			)
		except OSError:
			pass
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
	if not (engine / "Source").is_dir():
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
	if not (engine_root / "Source").is_dir():
		raise FileNotFoundError(f"Maho engine not found under: {engine_root}")

	data = read_cproject(cproject_path)
	stored = engine_path_for_cproject(engine_root, cproject_path.parent)
	data["EngineDirectory"] = stored
	data["EngineAssociation"] = data.get("EngineAssociation") or "Maho"
	write_cproject(cproject_path, data)
	return stored


def inheritance_problems(
	engine_root: Path,
	new_name: str,
	parent_names: list[str],
) -> list[str]:
	"""
	Check circular / redundant inheritance if a plugin `new_name` inherits
	`parent_names`. Returns problem messages (empty = OK).

	- Cycle: a parent (transitively) inherits `new_name` → new plugin would
	  close a loop.
	- Redundant: one selected parent already inherits another selected parent
	  (diamond) → duplicate base, drop the redundant direct selection.
	"""
	plugins_dir = (engine_root / "Maho" / "Plugins").resolve()
	graph: dict[str, set[str]] = {}
	for cplugin_path in discover_cplugin_files([plugins_dir]):
		data = read_cplugin(cplugin_path)
		name = cplugin_path.parent.name
		inherits = (data).get("Inherits", []) or []
		if isinstance(inherits, str):
			inherits = [inherits]
		graph[name] = set(inherits)

	# Transitive ancestors of `node` (everything node inherits, directly or not).
	def ancestors_of(node: str) -> set[str]:
		seen: set[str] = set()
		stack: list[str] = list(graph.get(node, ()))
		while stack:
			cur = stack.pop()
			if cur in seen:
				continue
			seen.add(cur)
			stack.extend(graph.get(cur, ()))
		return seen

	problems: list[str] = []
	for parent in parent_names:
		if new_name in ancestors_of(parent):
			problems.append(f"循环继承：{parent} 已（传递）继承 {new_name}")
	for p in parent_names:
		for q in parent_names:
			if p != q and q in ancestors_of(p):
				problems.append(f"冗余继承：{p} 已继承 {q}，无需再直接勾选 {q}")

	# De-duplicate, keep order.
	seen: set[str] = set()
	uniq: list[str] = []
	for msg in problems:
		if msg not in seen:
			seen.add(msg)
			uniq.append(msg)
	return uniq


MAIN_CPP = """// ═══════════════════════════════════════════════════════════════════════
//  Maho 项目入口（code-gen，无需改动）
//
//  Source/ 文件夹里应该只有本文件。一切项目逻辑都是插件：
//    - 项目默认插件：Extension/<ProjectName>/
//    - 手动创建的插件：Extension/<其他插件名>/
//
//  入口只负责：安装（加载）默认插件 DLL → CreateLayer → Main 执行。
// ═══════════════════════════════════════════════════════════════════════
#if defined(_WIN32)
#	include <EntryPointWindows.h>
#elif defined(__ANDROID__)
#	include <EntryPointAndroid.h>
#elif defined(__APPLE__)
#	include <EntryPointIOS.h>
#elif defined(__linux__)
#	include <EntryPointLinux.h>
#endif
"""

CMAKELISTS = """# Generated by Maho CreateProject.
cmake_minimum_required(VERSION 3.20)
project({name} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC: source files are UTF-8 (comments contain em-dash etc.).
if(MSVC)
	add_compile_options(/utf-8)
endif()

# Enable VS solution folders (FOLDER property groups targets under virtual dirs).
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# The .cproject double-click drives regeneration; stop CMake from embedding the
# top-level CMakeLists.txt into every plugin vcxproj (ZERO_CHECK auto-rerun).
set(CMAKE_SUPPRESS_REGENERATION ON)

set(ENGINE_DIR "${{CMAKE_CURRENT_SOURCE_DIR}}/{engine_rel}")

# Third-party helpers (FetchContent reuse + mirror/proxy + Threads).
include("${{ENGINE_DIR}}/Build/CMake/MahoDependencies.cmake")

# Engine core headers — listed in the entry target only (the DLL tree stays clean).
file(GLOB MAHO_HEADERS CONFIGURE_DEPENDS "${{ENGINE_DIR}}/Source/Public/**/*.h")

# Engine core private sources — only the entry needs them (Maho::Main uses
# FAssembly + Fatal); the plugin DLL only uses the header-only core.
file(GLOB MAHO_PRIVATE CONFIGURE_DEPENDS "${{ENGINE_DIR}}/Source/Private/**/*.cpp")

{plugin_targets}
# The project root — the entry plugin (under Plugins/, Public header + Private impl).
add_library({name} SHARED
	Plugins/{name}/Public/{name}.h
	Plugins/{name}/Private/{name}.cpp
{host_aux}
)
{host_aux_props}
target_include_directories({name} PUBLIC
	"${{ENGINE_DIR}}/Source/Public"
	"${{CMAKE_CURRENT_SOURCE_DIR}}/Plugins/{name}/Public"
{plugin_dirs}
)
# Export the extern "C" CreateLayer() bridge — the host's single entry.
set_target_properties({name} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions({name} PRIVATE MAHO_{NAME_UPPER}_MODULE_EXPORTS)
target_link_libraries({name} PUBLIC Maho)
add_dependencies({name} {plugin_link_names})
target_link_libraries({name} PUBLIC {plugin_link_names})
# Plugin DLLs co-locate with Maho.dll + EntryPoint so relative DLL deps resolve.
set_property(TARGET {name} PROPERTY RUNTIME_OUTPUT_DIRECTORY "${{CMAKE_BINARY_DIR}}/Binaries/$<CONFIG>")

# Cycle check — runs before every in-IDE build (host + entry depend on it).
# Prefer the engine-local venv (Setup.bat); fall back to any system python.
find_program(MAHO_PYTHON_EXECUTABLE
	NAMES python.exe python python3
	HINTS "${{ENGINE_DIR}}/Tools/python" "${{ENGINE_DIR}}/Tools/python/Scripts"
	NO_DEFAULT_PATH
)
if(NOT MAHO_PYTHON_EXECUTABLE)
	find_program(MAHO_PYTHON_EXECUTABLE NAMES python python3)
endif()
if(NOT MAHO_PYTHON_EXECUTABLE)
	message(WARNING "Maho: no python found — skipping plugin checks")
else()
	add_custom_target(MahoCheckCycle
		COMMAND "${{MAHO_PYTHON_EXECUTABLE}}" "${{ENGINE_DIR}}/Tools/check_plugin_cycle.py" "${{CMAKE_CURRENT_SOURCE_DIR}}/{name}.cproject"
		BYPRODUCTS "${{CMAKE_CURRENT_SOURCE_DIR}}/Intermediate/_cycle_check.stamp"
		VERBATIM
	)
		set_target_properties(MahoCheckCycle PROPERTIES FOLDER "ThirdParty")
		add_dependencies({name} MahoCheckCycle)

	# Scan MAHO_EXTEND_DEPS globally and (re)generate each class's dependency
	# macros into its declaring file's sibling .gen.h BEFORE any source that
	# includes them compiles.
	add_custom_target(MahoScanDeps
		COMMAND "${{MAHO_PYTHON_EXECUTABLE}}" "${{ENGINE_DIR}}/Tools/scan_deps.py"
			--src "${{ENGINE_DIR}}/Source"
			--src "${{ENGINE_DIR}}/Plugins"
			--src "${{CMAKE_CURRENT_SOURCE_DIR}}/Plugins"
			--out "${{CMAKE_CURRENT_SOURCE_DIR}}/Intermediate/Generated/_deps.json"
		BYPRODUCTS "${{CMAKE_CURRENT_SOURCE_DIR}}/Intermediate/Generated/_deps.json"
		VERBATIM
	)
		set_target_properties(MahoScanDeps PROPERTIES FOLDER "ThirdParty")
		add_dependencies({name} MahoScanDeps)
	endif()

# The entry — code-gen boilerplate (never edited), loads {name}.dll.
# The engine source lives in the Maho library (sln folder Maho/); the exe
# imports the Maho.dll the library builds. Maho.dll is the process-unique copy
# of engine code + the shared third-party set.
add_library(Maho SHARED
	${{MAHO_PRIVATE}}
	${{MAHO_HEADERS}}
)
# activate MAHO_API → dllexport here; consumers (plugins/EntryPoint) get dllimport.
target_compile_definitions(Maho PRIVATE MAHO_BUILD_SHARED MAHO_EXPORTS)
# export every engine symbol pragmatically (fast path for stage 1; MAHO_API
# classes refine the surface later).
set_target_properties(Maho PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
# show engine source as its on-disk tree (Source/Public, Source/Private, ...)
source_group(TREE "${{ENGINE_DIR}}/Source" FILES ${{MAHO_HEADERS}} ${{MAHO_PRIVATE}})
target_include_directories(Maho PUBLIC "${{ENGINE_DIR}}/Source/Public")
# The engine third-parties are the shared infra (glm, nlohmann/json, CLI11,
# zstd …); PUBLIC so every Consumer (entry plugin + EntryPoint + sub-plugins)
# can just #include them directly.
target_link_libraries(Maho PUBLIC glm::glm nlohmann_json::nlohmann_json CLI11::CLI11 libzstd_static)
set_target_properties(Maho PROPERTIES FOLDER "Maho")

add_executable(EntryPoint WIN32 Intermediate/Main.cpp)
target_compile_definitions(EntryPoint PRIVATE MAHO_ENGINE_NAME="{name}.dll")
target_link_libraries(EntryPoint PRIVATE Maho)

# Copy the project's Config/ directory next to the binary (DefaultEngine.ini etc.
# are loaded at runtime relative to the working directory). Folded into
# EntryPoint's POST_BUILD so no standalone CopyConfig project appears in the
# solution tree - the copy runs whenever EntryPoint (re)links.
if(EXISTS "${{CMAKE_CURRENT_SOURCE_DIR}}/Config")
	add_custom_command(TARGET EntryPoint POST_BUILD
		COMMAND ${{CMAKE_COMMAND}} -E copy_directory
			"${{CMAKE_CURRENT_SOURCE_DIR}}/Config"
			"${{CMAKE_BINARY_DIR}}/Binaries/$<CONFIG>/Config"
		COMMENT "Copying Config/ to runtime dir"
		VERBATIM
	)
endif()

# One Binaries/<Config> dir for the exe + Maho.dll + every plugin DLL, so at
# runtime they all resolve each other (both Maho.dll and the plugin DLLs must
# sit next to the EntryPoint exe). Multi-config (MSVC): per-config suffix.
set(_MAHO_BIN "${{CMAKE_BINARY_DIR}}/Binaries/")
foreach(_Cfg Debug Release RelWithDebInfo MinSizeRel)
	string(TOUPPER "${{_Cfg}}" _CfgUp)
	set_target_properties(Maho PROPERTIES "RUNTIME_OUTPUT_DIRECTORY_${{_CfgUp}}" "${{_MAHO_BIN}}/${{_Cfg}}")
	set_target_properties(EntryPoint PROPERTIES "RUNTIME_OUTPUT_DIRECTORY_${{_CfgUp}}" "${{_MAHO_BIN}}/${{_Cfg}}")
endforeach()
set_property(DIRECTORY "${{CMAKE_CURRENT_SOURCE_DIR}}" PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${{_MAHO_BIN}}/$<CONFIG>")
# The sln startup project is EntryPoint (the app host), not ZERO_CHECK.
set_property(DIRECTORY "${{CMAKE_CURRENT_SOURCE_DIR}}" PROPERTY VS_STARTUP_PROJECT EntryPoint)
add_dependencies(EntryPoint {name} {plugin_all_names} MahoCheckCycle)

# Solution folders: EntryPoint at the ROOT; Maho (engine), Project, ThirdParty.
# The project root sits at Project/ itself; project plugin DLLs under
# Project/Plugins/. Engine plugins under Maho/Plugins/ — each grouped by the
# filesystem tree (shown via source_group as Public/Private on disk).
set_target_properties({name} PROPERTIES FOLDER "Project")
source_group(TREE "${{CMAKE_CURRENT_SOURCE_DIR}}/Plugins/{name}" FILES
	Plugins/{name}/Public/{name}.h
	Plugins/{name}/Private/{name}.cpp
)
# EntryPoint stays at the root (no FOLDER property).
{plugin_folders}
"""

PACKAGE_BAT = """@echo off
setlocal
cd /d "%~dp0"

rem Package UI — pick platform / config, ship to Packaged/<Platform>/<Config>/.
rem Forwards to the engine's package_ui.py (same GUI as Tools/package.bat).

set "PYW={engine_rel}/Tools/python/pythonw.exe"
if not exist "%PYW%" set "PYW={engine_rel}/Tools/python/Scripts/pythonw.exe"
if not exist "%PYW%" set "PYW={engine_rel}/Tools/python/python.exe"
if not exist "%PYW%" set "PYW={engine_rel}/Tools/python/Scripts/python.exe"

if not exist "%PYW%" (
	echo [ERROR] Engine-local Python missing. Run Setup.bat in the engine root:
	echo         {engine_rel}
	pause
	exit /b 1
)

start "" "%PYW%" "{engine_rel}/Tools/package_ui.py" "%~dp0{name}.cproject"
exit /b 0
"""

CREATE_PLUGIN_BAT = """@echo off
setlocal
cd /d "%~dp0"

rem CreatePlugin.bat — open the new-plugin UI (creates at the project root).

set "PYW={engine_rel}/Tools/python/pythonw.exe"
if not exist "%PYW%" set "PYW={engine_rel}/Tools/python/Scripts/pythonw.exe"
if not exist "%PYW%" set "PYW={engine_rel}/Tools/python/python.exe"
if not exist "%PYW%" set "PYW={engine_rel}/Tools/python/Scripts/python.exe"

if not exist "%PYW%" (
	echo [ERROR] Engine-local Python missing. Run Setup.bat in the engine root:
	echo         {engine_rel}
	pause
	exit /b 1
)

start "" "%PYW%" "{engine_rel}/Tools/create_plugin_ui.py" "%CD%\\Source"
exit /b 0
"""


def _all_plugin_infos(
	engine_root: Path, project_dir: Path | None = None
) -> dict[str, dict[str, Any]]:
	"""Map plugin Name → {Dependencies, public_dir, private_dir} across engine
	+ project plugins. Engine plugins resolve via ${ENGINE_DIR}, project
	plugins via ${CMAKE_CURRENT_SOURCE_DIR}."""
	infos: dict[str, dict[str, Any]] = {}

	def _register(cplugin: Path, prefix: str, cmake_dir: str, group: str) -> None:
		data = read_cplugin(cplugin)
		name = data.get("Name") or cplugin.parent.name
		# Public header file name — defaults to the plugin's Name; a plugin whose
		# name would collide with a system header (Math vs <math.h>) overrides it
		# (e.g. "Header": "MathTool").
		header = data.get("Header") or name
		deps: list[str] = []
		for dep in data.get("Dependencies", []) or []:
			if dep not in deps:
				deps.append(dep)

		# The plugin's own directory basename is the stable prefix for its
		# .cmake/.cplugin/settings.json; a plugin whose implementation file
		# differs from the directory name (e.g. ImGui → ImGuiSystem.cpp) overrides
		# the module file with a "Module" field.
		dir_name = Path(cmake_dir).name
		mod_file = data.get("Module") or dir_name
		aux: list[str] = []
		plugin_dir = cplugin.parent
		for fn in (
			cplugin.name,
			f"{dir_name}.cmake",
			"settings.json",
			f"{name}Doc.md",
			f"{name}API.html",
		):
			if (plugin_dir / fn).is_file():
				aux.append(f"{prefix}/{cmake_dir}/{fn}")
		infos[name] = {
			"Dependencies": deps,
			"public_dir": f"{prefix}/{cmake_dir}/Public",
			"private_dir": f"{prefix}/{cmake_dir}/Private",
			"cmake_file": f"{prefix}/{cmake_dir}/{dir_name}.cmake",
			"group": group,
			"header": header,
			"mod_file": mod_file,
			# Kind → class-name suffix (F{Name}Tool / F{Name}Layer / F{Name}Engine).
			"kind": group.split("/")[0].lower() if group else "tool",
			"aux_files": aux,
		}

	# Engine plugins: Plugins/<group>/<name>/.
	engine_ext = engine_root / "Plugins"
	for cplugin in discover_cplugin_files([engine_ext]):
		rel = cplugin.parent.relative_to(engine_ext).as_posix()
		group = "/".join(rel.split("/")[:-1]) if "/" in rel else ""
		_register(cplugin, "${ENGINE_DIR}", f"Plugins/{rel}", group)

	if project_dir is not None:
		# Project plugins: Plugins/<group>/.../<name>/<name>.cplugin (the entry
		# plugin sits at Plugins/<name>; feature plugins may nest under groups).
		proj_src = project_dir / "Plugins"
		for cplugin in discover_cplugin_files([proj_src]):
			rel = cplugin.parent.relative_to(proj_src).as_posix()
			group = "/".join(rel.split("/")[:-1]) if "/" in rel else ""
			_register(cplugin, "${CMAKE_CURRENT_SOURCE_DIR}", f"Plugins/{rel}", group)
		# Third-party plugins: Extension/<name>/.
		proj_ext = project_dir / "Extension"
		for cplugin in discover_cplugin_files([proj_ext]):
			rel = cplugin.parent.relative_to(proj_ext).as_posix()
			_register(cplugin, "${CMAKE_CURRENT_SOURCE_DIR}", f"Extension/{rel}", "")

	return infos


def _resolve_plugin_chain(
	engine_root: Path, selected: list[str], project_dir: Path | None = None
) -> list[str]:
	"""Selected plugins + their transitive deps, topo order (deps first).

	Raises ValueError on a dependency cycle (A → B → A, transitively).
	"""
	by_name = _all_plugin_infos(engine_root, project_dir)
	order: list[str] = []
	# 0 = unvisited, 1 = in-progress (on the current DFS stack), 2 = done.
	state: dict[str, int] = {}
	stack: list[str] = []

	def visit(name: str) -> None:
		st = state.get(name, 0)
		if st == 2:
			return
		if st == 1:
			# Find where the cycle starts on the current stack and render A → B → … → A.
			start = stack.index(name)
			cycle = stack[start:] + [name]
			chain = " → ".join(cycle)
			raise ValueError(
				f"FATAL: plugin dependency cycle: {chain}"
			)
		state[name] = 1
		stack.append(name)
		info = by_name.get(name) or {}
		for dep in info.get("Dependencies", []) or []:
			if dep in by_name:
				visit(dep)
		stack.pop()
		state[name] = 2
		order.append(name)

	for name in selected:
		visit(name)
	return order


def _plugin_include_dirs(
	engine_root: Path, names: list[str], project_dir: Path | None = None
) -> str:
	"""CMake include-dir lines for each resolved plugin (Source/Public)."""
	infos = _all_plugin_infos(engine_root, project_dir)
	lines: list[str] = []
	for name in names:
		info = infos.get(name)
		if info:
			lines.append(f'\t"{info["public_dir"]}"')
	return "\n".join(lines)


def _plugin_targets(
	engine_root: Path, names: list[str], project_name: str, project_dir: Path | None = None
) -> tuple[str, list[str], list[str], str]:
	"""CMake add_library blocks for each dependency plugin (a loadable DLL target).

	Returns (targets_block, link_names, all_names, folders_block):
	  - link_names: engine plugins ONLY — link into the host/entry DLL (their
	    headers are compile-time-included by the host).
	  - all_names: every plugin — build targets + EntryPoint add_dependencies
	    (project feature plugins are loaded at RUNTIME via FAssembly, so the
	    host does NOT link them).
	  - folders_block groups each dep target under the project's solution folder.
	"""
	infos = _all_plugin_infos(engine_root, project_dir)
	name_set = set(names)

	targets: list[str] = []
	link_names: list[str] = []
	all_names: list[str] = []
	folders: list[str] = []
	for name in names:
		info = infos.get(name)
		if not info:
			continue
		all_names.append(name)
		is_engine = info["public_dir"].startswith("${ENGINE_DIR}")
		if is_engine:
			link_names.append(name)
		aux_files = info.get("aux_files", [])
		aux_sources = "\n".join(f"\t{af}" for af in aux_files)
		aux_props = (
			f"set_source_files_properties({aux_sources} PROPERTIES HEADER_FILE_ONLY ON)\n"
			if aux_files
			else ""
		)
		# A project-side plugin depends on the project's entry plugin → also
		# include the entry plugin's Public/ (the parent interfaces it implements).
		entry_public = f"\"${{CMAKE_CURRENT_SOURCE_DIR}}/Plugins/{project_name}/Public\""
		# Dependency plugins' Public/ include dirs (engine + project plugins).
		dep_public_dirs = "".join(
			f'\t"{infos[d]["public_dir"]}"\n'
			for d in info.get("Dependencies", [])
			if d in infos and d != name
		)
		cmake_include = (
			f'set(_MOD_PLUGIN_DIR "{info["public_dir"].rsplit("/", 1)[0]}")\n'
			f'include("{info["cmake_file"]}")\n'
			f'unset(_MOD_PLUGIN_DIR)\n'
		)
		targets.append(
			f"add_library({name} SHARED\n"
			f'\t{info["public_dir"]}/{info.get("header", name)}.h\n'
			f'\t{info["private_dir"]}/{info["mod_file"]}.cpp\n'
			f"{aux_sources}\n"
			f")\n"
			f"{aux_props}"
			f"target_include_directories({name} PUBLIC\n"
			f'\t"${{ENGINE_DIR}}/Source/Public"\n'
			f'\t"{info["public_dir"]}"\n'
			f"\t{entry_public}\n"
			f"{dep_public_dirs}"
			f")\n"
			f"set_target_properties({name} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)\n"
			f"target_compile_definitions({name} PRIVATE MAHO_{name.upper()}_MODULE_EXPORTS)\n"
			f"target_link_libraries({name} PUBLIC Maho)\n"
			f"set_property(TARGET {name} PROPERTY RUNTIME_OUTPUT_DIRECTORY \"${{CMAKE_BINARY_DIR}}/Binaries/$<CONFIG>\")\n"
			f"{cmake_include}"
		)
		# Plugin → its in-chain deps (host + disabled plugins excluded). Link,
		# not just add_dependencies — PUBLIC include dirs + third-party targets
		# (nlohmann_json, glm, …) must propagate to dependents.
		deps_in_chain = [
			d for d in info.get("Dependencies", []) if d in name_set and d != name
		]
		if deps_in_chain:
			targets.append(
				f"target_link_libraries({name} PUBLIC {' '.join(deps_in_chain)})\n"
			)
		group = info["group"]
		# sln folder tree: EntryPoint at root, then Maho / Project / ThirdParty.
		# Engine plugins → Maho/Plugins/..., project plugins → Project/Plugins/...
		# (grouped by the filesystem hierarchy under each Plugins root).
		base = "Maho/Plugins" if is_engine else "Project/Plugins"
		folder = f"{base}/{group}" if group else base
		folders.append(
			f"set_target_properties({name} PROPERTIES FOLDER \"{folder}\")\n"
		)
		# show the plugin's files as the on-disk tree (Public/Private, ...)
		plugin_root = info["public_dir"].rsplit("/", 1)[0]
		plugin_h = f'{info["public_dir"]}/{info.get("header", name)}.h'
		plugin_cpp = f'{info["private_dir"]}/{info["mod_file"]}.cpp'
		targets.append(
			f'source_group(TREE "{plugin_root}" FILES\n'
			f'\t"{plugin_h}"\n'
			f'\t"{plugin_cpp}"\n'
			f")\n"
		)
	return "\n".join(targets), link_names, all_names, "\n".join(folders)


def codegen_plugin_extensions(cproject_path: Path) -> Path:
	"""Generate <Name>.gen.h in Intermediate/Generated/ — the plugin includes +
	the extension macro (injected into the host's TExtensionList).

	Code injection: the generated header lives in Intermediate/, the user's
	workspace files are never rewritten by code-gen.
	"""
	cproject_path = cproject_path.resolve()
	data = read_cproject(cproject_path)
	name = str(data["ProjectName"])
	project_dir = cproject_path.parent

	# The dependency plugins = .cproject Plugins minus the default plugin (the
	# project itself, named the same as the project).
	plugins = [p["Name"] for p in data.get("Plugins", []) if p.get("Enabled", True)]
	plugins = [p for p in plugins if p != name]

	# Class-name suffix from each plugin's kind directory (Tool/Layer/Engine).
	# Class = F{Name}{Suffix}; the project's own root is a Layer: F{Name}Layer.
	infos = _all_plugin_infos(resolve_engine_directory(cproject_path, data), project_dir)
	_suffix = {"tool": "Tool", "layer": "Layer", "engine": "Engine"}

	def _class_of(p: str) -> str:
		if p == name:
			return f"{name}::F{name}Layer"
		kind = infos[p]["kind"] if p in infos else "tool"
		return f"{p}::F{p}{_suffix.get(kind, 'Tool')}"

	# Forward-declare every dependency plugin (their full headers would drag the
	# project header back in → include cycle). The host cpp includes the real
	# headers when it actually drives them.
	fwd_decls = "\n".join(
		f"namespace Maho {{ namespace {p} {{ class {_class_of(p).split('::')[1]}; }} }}"
		for p in plugins
	)
	# Full includes — guarded so the host cpp can pull them in deliberately.
	# Use each plugin's Header (Math → MathTool.h) unless it collides with a
	# system header of the same name.
	full_includes = "\n".join(
		f"#include <{infos[p]['header']}.h>" if p in infos else f"#include <{p}.h>"
		for p in plugins
	)
	text = (
		"// Generated by Maho code-gen — the project's plugin extensions.\n"
		"// Do not edit — regenerated from the .cproject before every build.\n"
		"#pragma once\n\n"
		f"{fwd_decls}\n\n"
	)
	if full_includes:
		text += (
			"\n// Full plugin headers — the host cpp defines "
			f"{name.upper()}_INCLUDE_PLUGINS to pull them in.\n"
			f"#ifdef {name.upper()}_INCLUDE_PLUGINS\n"
			f"{full_includes}\n"
			f"#endif\n"
		)
	out_dir = project_dir / "Intermediate" / "Generated"
	out_dir.mkdir(parents=True, exist_ok=True)
	gen_h = out_dir / f"{name}.gen.h"
	gen_h.write_text(text, encoding="utf-8", newline="\n")
	return gen_h


def _write_cmake_lists(
	project_dir: Path,
	project_name: str,
	engine_root: Path,
	cproject_data: dict[str, Any],
) -> None:
	"""Regenerate CMakeLists.txt from the .cproject (plugin targets + include dirs).

	Called both at create time and on every generate (so project-side
	create-plugin picks up new plugins into the sln).
	"""
	selected = [p["Name"] for p in cproject_data.get("Plugins", []) if p.get("Enabled", True)]
	chain = _resolve_plugin_chain(
		engine_root, [p for p in selected if p != project_name], project_dir
	)
	# The project's own plugin is the host (added separately) — never a dep target.
	chain = [p for p in chain if p != project_name]
	engine_rel = engine_path_for_cproject(engine_root, project_dir)
	# Host DLL include dirs: engine plugins only (compile-time headers). Project
	# feature plugins are runtime-loaded — their Public/ is NOT in the host.
	engine_chain = [
		p for p in chain
		if _all_plugin_infos(engine_root, project_dir).get(p, {}).get("public_dir", "").startswith("${ENGINE_DIR}")
	]
	plugin_dirs = _plugin_include_dirs(engine_root, engine_chain, project_dir)
	plugin_targets, plugin_link_names, plugin_all_names, plugin_folders = _plugin_targets(
		engine_root, chain, project_name, project_dir
	)

	# Host aux files (the project's own plugin manifest/docs) shown in the IDE.
	host_dir = project_dir / "Plugins" / project_name
	host_aux: list[str] = []
	for fn in (
		f"{project_name}.cplugin",
		f"{project_name}.cmake",
		"settings.json",
		f"{project_name}.md",
		"API.md",
	):
		if (host_dir / fn).is_file():
			host_aux.append(f"Plugins/{project_name}/{fn}")
	host_aux_block = "\n".join(f"\t{p}" for p in host_aux)
	host_aux_props = (
		f'set_source_files_properties({", ".join(host_aux)} PROPERTIES HEADER_FILE_ONLY ON)\n'
		if host_aux
		else ""
	)

	(project_dir / "CMakeLists.txt").write_text(
		CMAKELISTS.format(
			name=project_name,
			NAME_UPPER=project_name.upper(),
			engine_rel=engine_rel,
			plugin_dirs=plugin_dirs,
			plugin_targets=plugin_targets,
			plugin_link_names=" ".join(plugin_link_names),
			plugin_all_names=" ".join(plugin_all_names),
			plugin_folders=plugin_folders,
			host_aux=host_aux_block,
			host_aux_props=host_aux_props,
		),
		encoding="utf-8", newline="\n",
	)


def create_project(
	project_name: str,
	parent_dir: Path,
	engine_root: Path,
	description: str = "",
	author: str = "",
	plugins: list[str] | None = None,
	template: str = "",
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

	if plugins is None:
		# Empty project by default — the user hand-edits the .cproject Plugins
		# to mount engine/feature plugins. No auto-selection.
		selected = []
	else:
		selected = list(plugins)

	engine_rel = engine_path_for_cproject(engine_root, project_dir)

	# .cproject (records the user's selection).
	cproject = {
		"FileVersion": CPROJECT_VERSION,
		"ProjectName": project_name,
		"EngineDirectory": engine_rel,
		"Plugins": [{"Name": name, "Enabled": True} for name in selected],
	}
	cproject_path = project_dir / f"{project_name}.cproject"
	write_cproject(cproject_path, cproject)

	# The project root = the entry plugin (an FEngineBase). First scaffold the
	# bare plugin (Api.h + .cplugin + .cmake + docs), then overwrite the class
	# skeleton with the engine template.
	create_plugin(
		project_name, engine_root, description=description,
		plugins_dir=project_dir / "Plugins", children=selected,
	)
	export = project_name.upper()
	entry_h = project_dir / "Plugins" / project_name / "Public" / f"{project_name}.h"
	entry_cpp = project_dir / "Plugins" / project_name / "Private" / f"{project_name}.cpp"
	entry_h.write_text(
		f"#pragma once\n\n"
		f'#include "{project_name}Api.h"\n'
		f"#include <Maho.h>\n\n"
		f"namespace Maho\n{{\n\n"
		f"// {project_name} - the application root (an FEngineBase).\n"
		f"// Implement PreMain/PostMain to install layers; the engine drives the loop.\n"
		f"class F{project_name} : public FEngineBase\n"
		f"{{\n"
		f"MAHO_DECLARE_ENGINE(F{project_name}, \"{project_name}.dll\");\n"
		f"\n"
		f"public:\n"
		f"\tvoid PreMain() override;\n"
		f"\tvoid PostMain() override;\n"
		f"}};\n\n"
		f"}} // namespace Maho\n",
		encoding="utf-8", newline="\n",
	)
	entry_cpp.write_text(
		f'#include "{project_name}.h"\n\n'
		f"namespace Maho\n{{\n\n"
		f"void F{project_name}::PreMain()\n"
		f"{{\n"
		f"\t// Install(&SomeLayer);\n"
		f"\t// Install(\"SomeLayer.dll\");\n"
		f"}}\n\n"
		f"void F{project_name}::PostMain()\n"
		f"{{\n"
		f"}}\n\n"
		f"}} // namespace Maho\n\n"
		f"// The C export the host (EntryPoint) looks up BY SYMBOL NAME.\n"
		f'extern "C" MAHO_{export}_API Maho::FEngineBase* CreateEngine()\n'
		f"{{\n"
		f"\treturn Maho::F{project_name}::CreateEngine();\n"
		f"}}\n",
		encoding="utf-8", newline="\n",
	)

	# Intermediate/Main.cpp — the code-gen entry (never edited): load the root DLL.
	intermediate_dir = project_dir / "Intermediate"
	intermediate_dir.mkdir(exist_ok=True)
	(intermediate_dir / "Main.cpp").write_text(MAIN_CPP, encoding="utf-8", newline="\n")

	# Config/ — copy the engine's default config (DefaultEngine.ini) so the
	# project has runtime config (the Config plugin + the CopyConfig build
	# target expect it). Fall back to writing a minimal one if the engine
	# template is absent.
	config_dir = project_dir / "Config"
	engine_config = engine_root / "Config"
	if engine_config.is_dir():
		shutil.copytree(engine_config, config_dir, dirs_exist_ok=True)
	else:
		config_dir.mkdir(exist_ok=True)
		(config_dir / "DefaultEngine.ini").write_text(
			"[ConsoleVariables]\nr.Window.Width=100\nr.Window.Height=100\n",
			encoding="utf-8", newline="\n",
		)

	# CMakeLists.txt (core + plugin DLL targets + include dirs) + package.bat.
	_write_cmake_lists(project_dir, project_name, engine_root, cproject)
	(project_dir / "package.bat").write_text(
		PACKAGE_BAT.format(engine_rel=engine_rel, name=project_name),
		encoding="utf-8", newline="\r\n",
	)
	(project_dir / "CreatePlugin.bat").write_text(
		CREATE_PLUGIN_BAT.format(engine_rel=engine_rel), encoding="utf-8", newline="\r\n",
	)

	return cproject_path


def create_plugin(
	plugin_name: str,
	engine_root: Path,
	description: str = "",
	stage: str = "",  # ignored — stage is plugin-defined in the new model
	inherits: list[str] | None = None,  # ignored — no multi-inherit
	plugins_dir: Path | None = None,
	children: list[str] | None = None,  # plugin names mounted as this layer's children
) -> Path:
	"""
	Scaffold a new self-contained plugin under plugins_dir (default
	<engine>/Extension). Generates a bare FLayer<> skeleton (no stages mounted) +
	.cplugin + .cmake + settings.json + AGENTS.md + docs. The user hand-writes
	the stage interfaces they want by editing the FLayer<...> template list.
	"""
	if not is_valid_project_name(plugin_name):
		raise ValueError(
			"Plugin name must start with a letter and contain only A-Z, a-z, 0-9, _, -"
		)

	plugins_dir = (plugins_dir or engine_root / "Extension").resolve()
	dst = plugins_dir / plugin_name
	if dst.exists():
		raise FileExistsError(f"Plugin already exists: {dst}")

	deps = [d for d in (inherits or []) if d and d != plugin_name]

	# Project-side plugins (created under a project's Source/) automatically
	# depend on the project's entry plugin (the anchor): the child includes the
	# parent's Public/ interfaces and implements them in its own Private/.
	# Engine-side plugins (under <engine>/Extension/) have no anchor.
	for candidate_cproject in list(plugins_dir.glob("*.cproject")) + list(plugins_dir.parent.glob("*.cproject")):
		project_data = read_cproject(candidate_cproject)
		anchor = str(project_data.get("ProjectName", ""))
		if anchor and anchor != plugin_name and anchor not in deps:
			deps.insert(0, anchor)
		break

	export = plugin_name.upper()

	dst.mkdir(parents=True, exist_ok=True)

	# .cplugin (module-level build manifest).
	cplugin = {
		"FileVersion": 1,
		"Name": plugin_name,
		"Description": description,
		"Dependencies": deps,
	}
	(dst / f"{plugin_name}.cplugin").write_text(
		json.dumps(cplugin, indent=2, ensure_ascii=False) + "\n",
		encoding="utf-8", newline="\n",
	)

	# .cmake — third-party deps only (the lib target is built by codegen).
	(dst / f"{plugin_name}.cmake").write_text(
		f"# {plugin_name} plugin: third-party dependencies.\n"
		f"# The DLL target is built by codegen; this file only pulls FetchContent\n"
		f"# deps and links them into the {plugin_name} target.\n"
		f"#\n"
		f"# Example:\n"
		f"#   include(FetchContent)\n"
		f"#   maho_git_repository_url(_URL https://github.com/example/repo.git)\n"
		f"#   maho_fetchcontent_populate_or_reuse(repo ${{_URL}} v1.0 path/to/marker)\n"
		f"#   maho_add_thirdparty_subdirectory(${{repo_SOURCE_DIR}} ${{repo_BINARY_DIR}})\n"
		f"#   target_link_libraries({plugin_name} PUBLIC repo::repo)\n",
		encoding="utf-8", newline="\n",
	)

	# settings.json
	(dst / "settings.json").write_text(
		'{\n\t"mirrors": {}\n}\n', encoding="utf-8", newline="\n",
	)

	# .gitignore
	(dst / ".gitignore").write_text(
		"Intermediate/\nBinaries/\nSaved/\nPackaged/\n", encoding="utf-8", newline="\n",
	)

	# AGENTS.md
	(dst / "AGENTS.md").write_text(
		f"# {plugin_name} — Agent 入口\n\n"
		f"所有 AI Agent 进本插件前先读本文件。\n\n"
		f"## 设计约束（强约束）\n\n"
		f"- {description or 'TODO: 插件职责边界'}\n"
		f"- 依赖只走 `.cplugin` `Dependencies`，include `<Name.h>`，不跨目录相对 include。\n"
		f"- 遵循根 [AGENTS.md](../../../../AGENTS.md)。\n",
		encoding="utf-8", newline="\n",
	)

	# Docs (Markdown — the repo's doc format; no API.html).
	(dst / f"{plugin_name}.md").write_text(
		f"# {plugin_name}\n\n{description or '待补'}\n\n"
		f"## Code files\n"
		f"- [Public/{plugin_name}.h](Public/{plugin_name}.h) — 层声明（挂载 stage 接口）\n"
		f"- [Private/{plugin_name}.cpp](Private/{plugin_name}.cpp) — 实现\n\n"
		f"## Related docs\n"
		f"- [API.md](API.md) - API documentation\n",
		encoding="utf-8", newline="\n",
	)
	(dst / "API.md").write_text(
		f"# {plugin_name} — API 文档\n\n"
		f"{description or '空插件骨架——挂载 stage 接口后补全。'}\n\n"
		f"## F{plugin_name} <class : FLayer<...>>\n\n"
		f"插件骨架。把要实现的 stage 接口（IInit/ITick/...）填进 `FLayer<...>` 模板列表并覆写，\n"
		f"然后在 `.cplugin` 的 `Dependencies` 手填依赖插件。\n\n"
		f"- [{plugin_name}.md]({plugin_name}.md) — 概念\n",
		encoding="utf-8", newline="\n",
	)

	# Public (declaration) + Private (impl) — UE-style, no intermediate Source/.
	public = dst / "Public"
	private = dst / "Private"
	public.mkdir(parents=True, exist_ok=True)
	private.mkdir(parents=True, exist_ok=True)
	(public / f"{plugin_name}Api.h").write_text(
		"#pragma once\n\n#include <Core/Export.h>\n\n"
		f"#ifdef MAHO_{export}_MODULE_EXPORTS\n"
		f"#	define MAHO_{export}_API MAHO_EXPORT\n"
		f"#else\n"
		f"#	define MAHO_{export}_API MAHO_IMPORT\n"
		f"#endif\n",
		encoding="utf-8", newline="\n",
	)
	def _child_header(child: str) -> str:
		# Resolve each child's include name — a plugin's .cplugin "Header" may
		# override the default <Name>.h (e.g. RHI → RHI/RHIServer).
		for cplugin in discover_cplugin_files([engine_root / "Plugins"]):
			data = read_cplugin(cplugin)
			if (data.get("Name") or cplugin.parent.name) == child:
				return data.get("Header") or child
		return child

	child_includes = "".join(f'#include <{_child_header(c)}.h>\n' for c in (children or []))

	# ── class skeleton: a bare layer (no stages mounted) ─────────────────────
	header_body = (
		f"#pragma once\n\n"
		f'#include "{plugin_name}Api.h"\n'
		f"#include <Maho.h>\n"
		f"#include <Engine/Layer.h>\n"
		f"{child_includes}\n"
		f"namespace Maho\n{{\n\n"
		f"// {plugin_name} - an engine layer. Add the stage interfaces you implement\n"
		f"// to the FLayer<...> template list, e.g. FLayer<IInit, IShutdown> or\n"
		f"// FLayer<IBeginFrame, ITick, IEndFrame, IExit>. Each mounted stage must be\n"
		f"// overridden in this class.\n"
		f"class F{plugin_name} : public FLayer<>\n"
		f"{{\n"
		f"MAHO_DECLARE_LAYER(F{plugin_name}, \"{plugin_name}.dll\");\n"
		f"}};\n\n"
		f"}} // namespace Maho\n"
	)
	impl_body = (
		f'#include "{plugin_name}.h"\n\n'
		f"namespace Maho\n{{\n\n"
		f"// {plugin_name} - implementation. Override your mounted stages here.\n\n"
		f"}} // namespace Maho\n\n"
		f"// The C export the host looks up BY SYMBOL NAME for dynamic install.\n"
		f'extern "C" MAHO_{export}_API Maho::FLayerBase* CreateLayer()\n'
		f"{{\n"
		f"\treturn Maho::F{plugin_name}::CreateLayer();\n"
		f"}}\n"
	)

	(public / f"{plugin_name}.h").write_text(header_body, encoding="utf-8", newline="\n")
	(private / f"{plugin_name}.cpp").write_text(impl_body, encoding="utf-8", newline="\n")

	# Auto-register into the project's .cproject (when the plugin lives under
	# the project's Plugins/), then regenerate CMakeLists so the new plugin
	# reaches the sln / build without a manual generate step.
	for cproject in list(plugins_dir.glob("*.cproject")) + list(plugins_dir.parent.glob("*.cproject")):
		data = read_cproject(cproject)
		entries = data.get("Plugins", [])
		if not any(entry.get("Name") == plugin_name for entry in entries):
			entries.append({"Name": plugin_name, "Enabled": True})
			data["Plugins"] = entries
			write_cproject(cproject, data)
			print(f"[Maho] Registered {plugin_name} → {cproject.name}")
		# regenerate the project's CMakeLists (contains the new plugin target)
		project_name = str(data.get("ProjectName", ""))
		if project_name:
			engine_root = resolve_engine_directory(cproject, data)
			_write_cmake_lists(cproject.parent, project_name, engine_root, data)
			print(f"[Maho] Regenerated CMakeLists for {project_name}")
		break

	return dst


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
	generator = find_vs_generator()
	log(f"[Maho] Generator: {generator}")
	cmd = [
		cmake,
		"-S",
		str(source_dir),
		"-B",
		str(binary_dir),
		"-G",
		generator,
		"-A",
		"x64",
	]
	if engine_root is not None:
		cmd.append(f"-DMAHO_ENGINE_ROOT={engine_root}")
	if cproject is not None:
		cmd.append(f"-DMAHO_CPROJECT={cproject}")
	run_command(cmd, log=log, cancel_event=cancel_event, proc_holder=proc_holder)


def _pid_alive(pid: int) -> bool:
	"""Best-effort liveness check for a stale lock owner."""
	if pid <= 0:
		return False
	if sys.platform != "win32":
		try:
			os.kill(pid, 0)
			return True
		except OSError:
			return False
	try:
		import ctypes

		PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
		handle = ctypes.windll.kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
		if not handle:
			return False
		ctypes.windll.kernel32.CloseHandle(handle)
		return True
	except Exception:
		return True


class _GenerateLock:
	"""Prevent two cmake generates from racing on the same project Intermediate."""

	def __init__(self, intermediate: Path) -> None:
		self._path = intermediate / ".generate.lock"

	def acquire(self) -> None:
		self._path.parent.mkdir(parents=True, exist_ok=True)
		if self._path.exists():
			try:
				owner = int(self._path.read_text(encoding="utf-8").strip() or "0")
			except (OSError, ValueError):
				owner = 0
			if _pid_alive(owner):
				raise RuntimeError(
					f"Another generate is already running for this project "
					f"(lock: {self._path}).\nWait for it to finish, or delete the lock file "
					f"if it is stale."
				)
		self._path.write_text(str(os.getpid()), encoding="utf-8")

	def release(self) -> None:
		try:
			self._path.unlink(missing_ok=True)
		except OSError:
			pass


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

	# Code injection — regenerate the .gen.h (plugin extensions) from .cproject.
	gen_h = codegen_plugin_extensions(cproject_path)
	log(f"[Maho] Gen     : {gen_h.relative_to(project_dir)}")

	# Regenerate CMakeLists so project-side create-plugin targets reach the sln --
	# but only when the project has none yet. An existing CMakeLists.txt is
	# hand-maintained here (the generator's dependency inference is incomplete and
	# would overwrite manual include/link fixes), so double-click regeneration
	# keeps it and just re-runs CMake on it.
	if not (project_dir / "CMakeLists.txt").is_file():
		_write_cmake_lists(project_dir, project_name, engine_root, data)

	lock = _GenerateLock(intermediate)
	lock.acquire()
	try:
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
	finally:
		lock.release()


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

	packaged = project_dir / "Packaged" / platform / config
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

	# Copy the build outputs (exe + plugin dlls + Config dir) into Packaged/,
	# preserving the sln-FOLDER-mirrored subdirectory tree -- plugin DLLs build
	# into Binaries/<Config>/<FOLDER>/ and must stay in their own subfolder so
	# same-named modules from different groups (e.g. a project Scene.dll vs a
	# game Scene.dll) never collide when flattened.
	bin_dir = intermediate / "Binaries" / config
	copied = 0
	for ext in ("*.exe", "*.dll"):
		for src in bin_dir.rglob(ext):
			rel = src.relative_to(bin_dir)
			dst = packaged / rel
			dst.parent.mkdir(parents=True, exist_ok=True)
			shutil.copy2(src, dst)
			copied += 1
	# Config/ (DefaultEngine.ini + platform overrides) is staged into the
	# runtime dir by the CopyConfig target; ship it alongside the binaries.
	config_dir = bin_dir / "Config"
	if config_dir.is_dir():
		shutil.copytree(config_dir, packaged / "Config", dirs_exist_ok=True)
	if copied == 0:
		raise RuntimeError(f"No build outputs found under {bin_dir}")

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

	# Register the app so Windows trusts the association (avoids the
	# "choose a program" dialog on Win10/11's UserChoice hash check).
	with winreg.CreateKeyEx(
		winreg.HKEY_CURRENT_USER,
		rf"Software\Classes\{prog_id}\Capabilities\FileAssociations",
	) as key:
		winreg.SetValueEx(key, ".cproject", 0, winreg.REG_SZ, prog_id)
	with winreg.CreateKeyEx(
		winreg.HKEY_CURRENT_USER,
		r"Software\RegisteredApplications",
	) as key:
		winreg.SetValueEx(
			key, prog_id, 0, winreg.REG_SZ,
			rf"Software\Classes\{prog_id}\Capabilities",
		)

	# Explorer context menu — a catalog ("Maho") holding both commands.
	# Remove the legacy top-level SwitchEngine key first.
	_winreg_delete_tree(winreg.HKEY_CURRENT_USER, rf"Software\Classes\{prog_id}\shell\SwitchEngine")
	catalog_dir = rf"Software\Classes\{prog_id}\shell\MahoTools"
	with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, catalog_dir) as key:
		winreg.SetValueEx(key, None, 0, winreg.REG_SZ, "Maho")
		winreg.SetValueEx(key, "MUIVerb", 0, winreg.REG_SZ, "Maho")
		winreg.SetValueEx(key, "SubCommands", 0, winreg.REG_SZ, "")

	# Sub-command 1: Switch Engine.
	switcheng_dir = rf"Software\Classes\{prog_id}\shell\MahoTools\shell\SwitchEngine"
	with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, switcheng_dir) as key:
		winreg.SetValueEx(key, None, 0, winreg.REG_SZ, "选择链接引擎(&E)…")
		winreg.SetValueEx(key, "MUIVerb", 0, winreg.REG_SZ, "选择链接引擎(&E)…")
	with winreg.CreateKeyEx(
		winreg.HKEY_CURRENT_USER, rf"{switcheng_dir}\command"
	) as key:
		winreg.SetValueEx(key, None, 0, winreg.REG_SZ, switch_command)

	# Sub-command 2: Update docs.
	update_docs_vbs = ENGINE_ROOT / "Tools" / "launch_update_docs.vbs"
	if update_docs_vbs.is_file():
		update_command = f"\"{wscript}\" //nologo \"{update_docs_vbs}\" \"%1\""
		updocs_dir = rf"Software\Classes\{prog_id}\shell\MahoTools\shell\UpdateDocs"
		with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, updocs_dir) as key:
			winreg.SetValueEx(key, None, 0, winreg.REG_SZ, "更新项目文档(&D)")
			winreg.SetValueEx(key, "MUIVerb", 0, winreg.REG_SZ, "更新项目文档(&D)")
		with winreg.CreateKeyEx(
			winreg.HKEY_CURRENT_USER, rf"{updocs_dir}\command"
		) as key:
			winreg.SetValueEx(key, None, 0, winreg.REG_SZ, update_command)

	log("[Maho] Associated .cproject → launch_generate_project.vbs (current user)")
	log("[Maho] Context menu: Maho → 选择链接引擎 / 更新项目文档")
	log(f"[Maho] Open: {open_command}")
	log("[Maho] Removed legacy Catty.CProject association if present.")


def install_linux_cproject_association(*, log: Any = print) -> None:
	"""Register the .cproject file association via XDG .desktop + shared-mime-info."""
	if sys.platform == "win32":
		raise RuntimeError("Linux file association called on Windows.")

	generate_sh = ENGINE_ROOT / "Tools" / "generateProject.sh"
	if not generate_sh.is_file():
		raise FileNotFoundError(f"Missing {generate_sh}")

	data_home = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share"))
	config_home = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
	apps_dir = data_home / "applications"
	mime_pkg_dir = data_home / "mime" / "packages"
	apps_dir.mkdir(parents=True, exist_ok=True)
	mime_pkg_dir.mkdir(parents=True, exist_ok=True)

	# 1) .desktop entry: double-click opens generateProject.sh with the file.
	desktop = apps_dir / "maho-cproject.desktop"
	desktop.write_text(
		"[Desktop Entry]\n"
		"Type=Application\n"
		"Name=Generate Maho Project\n"
		"Comment=Generate build files from a Maho .cproject\n"
		f'Exec="{generate_sh}" %f\n'
		"MimeType=application/x-maho-cproject;\n"
		"Terminal=false\n"
		"NoDisplay=true\n",
		encoding="utf-8",
		newline="\n",
	)

	# 2) shared-mime-info package: map *.cproject to our mime type.
	mime_xml = mime_pkg_dir / "maho-cproject.xml"
	mime_xml.write_text(
		'<?xml version="1.0" encoding="UTF-8"?>\n'
		'<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">\n'
		'\t<mime-type type="application/x-maho-cproject">\n'
		'\t\t<comment>Maho Project</comment>\n'
		'\t\t<glob pattern="*.cproject"/>\n'
		'\t</mime-type>\n'
		'</mime-info>\n',
		encoding="utf-8",
		newline="\n",
	)

	# 3) Refresh the mime database (best-effort).
	update_mime = shutil.which("update-mime-database")
	if update_mime:
		subprocess.run([update_mime, str(data_home / "mime")], check=False)
	update_desktop = shutil.which("update-desktop-database")
	if update_desktop:
		subprocess.run([update_desktop, str(apps_dir)], check=False)

	# 4) Default application in mimeapps.list.
	apps_list = config_home / "mimeapps.list"
	lines = apps_list.read_text(encoding="utf-8").splitlines() if apps_list.is_file() else []
	section = "[Default Applications]"
	entry = "application/x-maho-cproject=maho-cproject.desktop;"
	out: list[str] = []
	in_default = False
	wrote = False
	for line in lines:
		if line.strip() == section:
			in_default = True
			out.append(line)
			continue
		if in_default and line.strip().startswith("["):
			in_default = False
		if in_default and line.startswith("application/x-maho-cproject="):
			out.append(entry)
			wrote = True
			continue
		out.append(line)
	if section not in {l.strip() for l in out}:
		out.append(section)
	if not wrote:
		out.append(entry)
	apps_list.write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")

	log("[Maho] Associated .cproject → generateProject.sh (current user)")
	log(f"[Maho] Desktop: {desktop}")
	log(f"[Maho] Mime:    {mime_xml}")


def install_cproject_association(*, log: Any = print) -> None:
	"""Register the .cproject file association for the current platform."""
	if sys.platform == "win32":
		install_windows_cproject_association(log=log)
	else:
		install_linux_cproject_association(log=log)


def install_windows_cplugin_association(*, log: Any = print) -> None:
	if sys.platform != "win32":
		raise RuntimeError("File association is only implemented for Windows.")

	import winreg

	fix_vbs = ENGINE_ROOT / "Tools" / "launch_fix_plugin.vbs"
	if not fix_vbs.is_file():
		raise FileNotFoundError(f"Missing {fix_vbs}")

	prog_id = "Maho.CPlugin"
	wscript = str(Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "wscript.exe")
	open_command = f"\"{wscript}\" //nologo \"{fix_vbs}\" \"%1\""

	# Clear Explorer "chosen app" state for .cplugin.
	_winreg_delete_tree(
		winreg.HKEY_CURRENT_USER,
		r"Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.cplugin\UserChoice",
	)
	try:
		with winreg.CreateKeyEx(
			winreg.HKEY_CURRENT_USER,
			r"Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.cplugin\OpenWithProgids",
		) as key:
			winreg.SetValueEx(key, prog_id, 0, winreg.REG_NONE, b"")
	except OSError as ex:
		log(f"[Maho] OpenWithProgids cleanup warning: {ex}")

	with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, r"Software\Classes\.cplugin") as key:
		winreg.SetValueEx(key, None, 0, winreg.REG_SZ, prog_id)

	with winreg.CreateKeyEx(
		winreg.HKEY_CURRENT_USER,
		r"Software\Classes\.cplugin\OpenWithProgids",
	) as key:
		winreg.SetValueEx(key, prog_id, 0, winreg.REG_NONE, b"")

	with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, rf"Software\Classes\{prog_id}") as key:
		winreg.SetValueEx(key, None, 0, winreg.REG_SZ, "Maho Plugin")

	with winreg.CreateKeyEx(
		winreg.HKEY_CURRENT_USER,
		rf"Software\Classes\{prog_id}\shell\open\command",
	) as key:
		winreg.SetValueEx(key, None, 0, winreg.REG_SZ, open_command)

	log("[Maho] Associated .cplugin → launch_fix_plugin.vbs (current user)")
	log(f"[Maho] Open: {open_command}")


def install_linux_cplugin_association(*, log: Any = print) -> None:
	if sys.platform == "win32":
		raise RuntimeError("Linux file association called on Windows.")

	fix_sh = ENGINE_ROOT / "Tools" / "fix_plugin.sh"
	if not fix_sh.is_file():
		raise FileNotFoundError(f"Missing {fix_sh}")

	data_home = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share"))
	config_home = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
	apps_dir = data_home / "applications"
	mime_pkg_dir = data_home / "mime" / "packages"
	apps_dir.mkdir(parents=True, exist_ok=True)
	mime_pkg_dir.mkdir(parents=True, exist_ok=True)

	desktop = apps_dir / "maho-cplugin.desktop"
	desktop.write_text(
		"[Desktop Entry]\n"
		"Type=Application\n"
		"Name=Fix Maho Plugin\n"
		"Comment=Auto-fix missing headers in a Maho .cplugin\n"
		f'Exec="{fix_sh}" %f\n'
		"MimeType=application/x-maho-cplugin;\n"
		"Terminal=false\n"
		"NoDisplay=true\n",
		encoding="utf-8",
		newline="\n",
	)

	mime_xml = mime_pkg_dir / "maho-cplugin.xml"
	mime_xml.write_text(
		'<?xml version="1.0" encoding="UTF-8"?>\n'
		'<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">\n'
		'\t<mime-type type="application/x-maho-cplugin">\n'
		'\t\t<comment>Maho Plugin</comment>\n'
		'\t\t<glob pattern="*.cplugin"/>\n'
		'\t</mime-type>\n'
		'</mime-info>\n',
		encoding="utf-8",
		newline="\n",
	)

	update_mime = shutil.which("update-mime-database")
	if update_mime:
		subprocess.run([update_mime, str(data_home / "mime")], check=False)
	update_desktop = shutil.which("update-desktop-database")
	if update_desktop:
		subprocess.run([update_desktop, str(apps_dir)], check=False)

	apps_list = config_home / "mimeapps.list"
	lines = apps_list.read_text(encoding="utf-8").splitlines() if apps_list.is_file() else []
	section = "[Default Applications]"
	entry = "application/x-maho-cplugin=maho-cplugin.desktop;"
	out: list[str] = []
	in_default = False
	wrote = False
	for line in lines:
		if line.strip() == section:
			in_default = True
			out.append(line)
			continue
		if in_default and line.strip().startswith("["):
			in_default = False
		if in_default and line.startswith("application/x-maho-cplugin="):
			out.append(entry)
			wrote = True
			continue
		out.append(line)
	if section not in {l.strip() for l in out}:
		out.append(section)
	if not wrote:
		out.append(entry)
	apps_list.write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")

	log("[Maho] Associated .cplugin → fix_plugin.sh (current user)")
	log(f"[Maho] Desktop: {desktop}")
	log(f"[Maho] Mime:    {mime_xml}")


def install_cplugin_association(*, log: Any = print) -> None:
	"""Register the .cplugin file association for the current platform."""
	if sys.platform == "win32":
		install_windows_cplugin_association(log=log)
	else:
		install_linux_cplugin_association(log=log)




# ---------------------------------------------------------------------------
# .cplugin scan (module DAG for multi-DLL builds)
# ---------------------------------------------------------------------------

CPLUGIN_FILE_VERSION = 1
DEFAULT_ENGINE_PLUGINS_DIR = ENGINE_ROOT / "Maho" / "Plugins"


def discover_cplugin_files(plugin_roots: list[Path]) -> list[Path]:
	"""Find *.cplugin recursively under each root (flat or nested group dirs)."""
	found: list[Path] = []
	seen: set[Path] = set()
	for root in plugin_roots:
		root = root.resolve()
		if not root.is_dir():
			continue
		for cplugin in sorted(root.rglob("*.cplugin")):
			rel = cplugin.relative_to(root)
			if any(part.startswith(".") for part in rel.parts):
				continue
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
	name = data.get("Name")
	if not isinstance(name, str) or not name.strip():
		raise ValueError(f"Invalid .cplugin (missing Name): {path}")
	deps = data.get("Dependencies")
	if deps is None:
		data["Dependencies"] = []
	elif not isinstance(deps, list):
		raise ValueError(f"Invalid .cplugin (Dependencies must be an array): {path}")
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

	# Inherits implies a build dependency — the child C++-inherits the parent,
	# so it links the parent's DLL (and gets its PUBLIC include dirs).
	inherits_raw = raw.get("Inherits", [])
	if inherits_raw is None:
		inherits_raw = []
	if isinstance(inherits_raw, str):
		inherits_raw = [inherits_raw]
	for inh in inherits_raw:
		if isinstance(inh, str) and inh.strip() and inh.strip() not in deps:
			deps.append(inh.strip())

	extension = None
	ext_raw = raw.get("Extension")
	if ext_raw is not None:
		if not isinstance(ext_raw, dict):
			raise ValueError(f"Module '{name}' Extension must be an object in {cplugin_path}")
		cls = ext_raw.get("Class")
		header = ext_raw.get("Header")
		priority = ext_raw.get("Priority")
		stage = ext_raw.get("Stage", "EEngineStage")
		if not isinstance(cls, str) or not cls.strip():
			raise ValueError(f"Module '{name}' Extension.Class required in {cplugin_path}")
		if not isinstance(header, str) or not header.strip():
			raise ValueError(f"Module '{name}' Extension.Header required in {cplugin_path}")
		if not isinstance(priority, str) or priority.strip() not in ("System", "Layer", "Overlay"):
			raise ValueError(
				f"Module '{name}' Extension.Priority must be System|Layer|Overlay in {cplugin_path}"
			)
		if not isinstance(stage, str) or stage.strip() not in ("EToolStage", "EEngineStage"):
			raise ValueError(
				f"Module '{name}' Extension.Stage must be EToolStage|EEngineStage in {cplugin_path}"
			)
		extension = {
			"Class": cls.strip(),
			"Header": header.strip(),
			"Priority": priority.strip(),
			"Stage": stage.strip(),
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
	When the field is present (even empty), it is an EXPLICIT selection: listed
	entries use their Enabled flag, unlisted plugins are disabled.
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


def list_engine_plugins(engine_root: Path | None = None) -> list[dict[str, Any]]:
	"""
	Enumerate engine plugins for the CreateProject UI.

	Engine plugins live under <engine>/Plugins/ (Common = service plugins,
	Gameplay = layer plugins). Returns [{Name, Dependencies, FriendlyName,
	Description, EnabledByDefault, Extension}] in stable name order. Extension is
	the first module's {Class, Header, Priority} (None when the module declares no
	extension).
	"""
	root = (engine_root or ENGINE_ROOT).resolve() / "Plugins"
	if not root.is_dir():
		return []
	out: list[dict[str, Any]] = []
	for cplugin_path in discover_cplugin_files([root]):
		data = read_cplugin(cplugin_path)
		name = data.get("Name") or cplugin_path.parent.name
		deps: list[str] = []
		for dep in data.get("Dependencies", []) or []:
			if dep not in deps:
				deps.append(dep)
		extension = None
		ext = data.get("Extension")
		if ext:
			extension = {
				"Class": ext.get("Class", ""),
				"Header": ext.get("Header", ""),
			}
		out.append(
			{
				"Name": name,
				"Dependencies": deps,
				"FriendlyName": name,
				"Description": data.get("Description", ""),
				"EnabledByDefault": True,
				"Extension": extension,
				"Group": list(cplugin_path.parent.relative_to(root).parts[:-1]),
				# Kind: the top-level category dir — Tool / Layer / Engine.
				"Kind": (
					cplugin_path.parent.relative_to(root).parts[0].lower()
					if cplugin_path.parent.relative_to(root).parts
					else "tool"
				),
			}
		)
	out.sort(key=lambda p: p["Name"])
	return out


def list_engine_templates(engine_root: Path | None = None) -> list[str]:
	"""Enumerate Engine plugin templates under Extension/Engine/ (names only)."""
	root = (engine_root or ENGINE_ROOT).resolve() / "Extension" / "Engine"
	if not root.is_dir():
		return []
	return sorted(p.parent.name for p in discover_cplugin_files([root]))


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

	# Pre-pass: resolve transitive enablement — enabling a plugin also enables
	# its Dependencies + Inherits parents (recursively).
	_name_to_deps: dict[str, set[str]] = {}
	_name_to_default: dict[str, bool] = {}
	for cplugin_path in cplugin_files:
		data = read_cplugin(cplugin_path)
		plugin_name = cplugin_path.parent.name
		_name_to_default[plugin_name] = bool(data.get("EnabledByDefault", True))
		deps: set[str] = set()
		for raw in [data]:
			norm = _normalize_module_entry(raw, cplugin_path=cplugin_path)
			deps.update(norm["Dependencies"])
		_name_to_deps[plugin_name] = deps

	if enabled_overrides is not None:
		enabled_set = {n for n, on in enabled_overrides.items() if on}
	else:
		enabled_set = {n for n, d in _name_to_default.items() if d}

	changed = True
	while changed:
		changed = False
		for n in list(enabled_set):
			for dep in _name_to_deps.get(n, ()):
				if dep in _name_to_deps and dep not in enabled_set:
					enabled_set.add(dep)
					changed = True

	for cplugin_path in cplugin_files:
		data = read_cplugin(cplugin_path)
		default_enabled = data.get("EnabledByDefault", True)
		if default_enabled is None:
			default_enabled = True
		plugin_dir = cplugin_path.parent
		plugin_name = plugin_dir.name
		enabled = plugin_name in enabled_set
		if not include_disabled and not enabled:
			continue

		friendly = data.get("FriendlyName", plugin_name)

		plugin_modules: list[dict[str, Any]] = []
		for raw in [data]:
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
	"""Engine Maho/Plugins + Basic (infra) + game Project/Plugins when present."""
	cproject_path = cproject_path.resolve()
	data = read_cproject(cproject_path)
	engine_root = resolve_engine_directory(cproject_path, data)
	project_dir = cproject_path.parent
	roots = [
		engine_root / "Maho" / "Plugins",
		engine_root / "Maho" / "Basic",
	]
	for candidate in (project_dir / "Plugins", project_dir / "Project" / "Plugins"):
		if candidate.is_dir():
			roots.append(candidate)
			break
	return roots


# ───────────────────────────────────────────────────────────────────────
# Doc structure generator — <Name>Doc.md + <Name>API.html.
# ───────────────────────────────────────────────────────────────────────

_GENERATED_MARK = "mahogen"
_DOC_SKIP_DIRS = {
	"Content", "Binaries", "Intermediate", "Cached", "Saved",
	"ThirdParty", "Resources", ".git", ".vs",
}
_CODE_SUFFIXES = {".h", ".hpp", ".cpp", ".cxx", ".inl"}


def _merge_md(existing: str, generated: str) -> str:
	"""Keep hand-written prose below the generated block.

	The generated header carries `<!-- mahogen -->` on its first line. Any
	hand-written prose appended AFTER the generated body (a `<!-- mahogen -->
	`-separated "手写区") survives a refresh; the generated block above is
	replaced wholesale.
	"""
	marker = f"<!-- {_GENERATED_MARK} -->"
	sep = f"<!-- {_GENERATED_MARK} end -->"
	if sep in existing:
		hand_written = existing[existing.index(sep) + len(sep):]
		return generated + sep + hand_written
	# Legacy: single marker, everything after the first line is generated.
	return generated


def update_docs(root: Path) -> None:
	"""Generate <Name>Doc.md + <Name>API.html at every dir that has code files.

	- Only dirs containing code files (own or transitively) get docs.
	- A doc lists the dir's OWN code files; sub-levels are jump links.
	- Private-side .cpp files are documented the same way.
	- Never overwrites hand-written docs (no `mahogen` marker).
	"""

	def _skip(d: Path) -> bool:
		return d.name in _DOC_SKIP_DIRS or d.name.startswith(".")

	def _own_files(d: Path) -> list[Path]:
		return sorted(
			(p for p in d.iterdir() if p.is_file() and p.suffix.lower() in _CODE_SUFFIXES),
			key=lambda p: p.name.lower(),
		)

	def _has_code(d: Path) -> bool:
		if _own_files(d):
			return True
		return any(_has_code(c) for c in d.iterdir() if c.is_dir() and not _skip(c))

	def _render_md(d: Path, own: list[Path], subs: list[Path]) -> str:
		lines = [f"<!-- {_GENERATED_MARK} -->", f"# {d.name}", ""]
		if own:
			lines += ["## 代码文件", ""]
			for f in own:
				lines.append(f"- [{f.name}]({f.name})")
			lines.append("")
		if subs:
			lines += ["## 子层级", ""]
			for c in subs:
				lines.append(f"- [{c.name}]({c.name}/{c.name}Doc.md)")
			lines.append("")
		return "\n".join(lines)

	def _render_api(d: Path, own: list[Path], subs: list[Path]) -> str:
		file_items = "\n".join(f'\t<li><a href="{f.name}">{f.name}</a></li>' for f in own)
		sub_items = "\n".join(
			f'\t<li><a href="{c.name}/{c.name}API.html">{c.name}</a></li>' for c in subs
		)
		body = f"<h1>{d.name} — API</h1>\n"
		if own:
			body += f"<h2>代码文件</h2>\n<ul>\n{file_items}\n</ul>\n"
		if subs:
			body += f"<h2>子层级</h2>\n<ul>\n{sub_items}\n</ul>\n"
		return (
			f"<!DOCTYPE html>\n<!-- {_GENERATED_MARK} -->\n"
			'<html lang="zh">\n<head>\n<meta charset="UTF-8">\n'
			f"<title>{d.name} — API</title>\n"
			"<style>"
			":root{--bg:#14181f;--panel:#1b2130;--text:#d8e1f0;--muted:#8b96a8;--accent:#5b8dd9;--border:#2c3444;}"
			"body{margin:0;padding:32px 40px;background:var(--bg);color:var(--text);font-family:'Segoe UI',sans-serif;max-width:1040px;line-height:1.7;}"
			"h1{font-size:26px;border-bottom:2px solid var(--border);padding-bottom:12px;}"
			"h2{font-size:22px;margin-top:32px;color:var(--accent);}"
			"a{color:var(--accent);text-decoration:none;}"
			"</style>\n</head>\n<body>\n"
			+ body
			+ "</body>\n</html>\n"
		)

	created = refreshed = skipped = 0
	dirs = [root] + sorted(
		(p for p in root.rglob("*") if p.is_dir() and not _skip(p)),
		key=lambda p: str(p).lower(),
	)
	for d in dirs:
		own = _own_files(d)
		subs = sorted(
			(c for c in d.iterdir() if c.is_dir() and not _skip(c) and _has_code(c)),
			key=lambda p: p.name.lower(),
		)
		if not own and not subs:
			continue  # empty dir — no doc

		doc_md = d / f"{d.name}Doc.md"
		api_html = d / f"{d.name}API.html"
		for target, render in (
			(doc_md, lambda: _render_md(d, own, subs)),
			(api_html, lambda: _render_api(d, own, subs)),
		):
			if target.exists():
				existing = target.read_text(encoding="utf-8")
				if _GENERATED_MARK not in existing:
					skipped += 1
					continue
				if target == doc_md:
					target.write_text(_merge_md(existing, render()), encoding="utf-8", newline="\n")
				else:
					target.write_text(render(), encoding="utf-8", newline="\n")
				refreshed += 1
			else:
				target.write_text(render(), encoding="utf-8", newline="\n")
				created += 1
	print(f"[Maho] Docs: {created} created, {refreshed} refreshed, {skipped} skipped.")


# ───────────────────────────────────────────────────────────────────────
# .cplugin double-click: validate + fix.
# ───────────────────────────────────────────────────────────────────────

def _plugin_api_header_text(name: str) -> str:
	"""<Name>Api.h body — the plugin's MAHO_<NAME>_API export macro."""
	export = name.upper()
	return (
		"#pragma once\n\n"
		"#include <Core/Export.h>\n\n"
		f"#ifdef MAHO_{export}_MODULE_EXPORTS\n"
		f"#	define MAHO_{export}_API MAHO_EXPORT\n"
		"#else\n"
		f"#	define MAHO_{export}_API MAHO_IMPORT\n"
		"#endif\n"
	)


def validate_plugin(cplugin_path: Path) -> list[tuple[str, str]]:
	"""Check one plugin's health. Returns [(severity, message)], 'error' or 'warning'."""
	cplugin_path = cplugin_path.resolve()
	data = read_cplugin(cplugin_path)
	name = data.get("Name") or cplugin_path.parent.name

	problems: list[tuple[str, str]] = []

	# Dependency must exist somewhere in the engine's Extension/ tree.
	all_names = {
		p.get("Name") or p.parent.name
		for p in list_engine_plugins(ENGINE_ROOT)
	}
	for dep in data.get("Dependencies", []) or []:
		if dep not in all_names:
			problems.append(("error", f"Dependency '{dep}' not found in the engine catalog"))

	# Api.h should exist (the export macro header).
	public_dir = cplugin_path.parent / "Public"
	api = public_dir / f"{name}Api.h"
	if not api.is_file():
		problems.append(("warning", f"Missing {name}Api.h (will be generated)"))

	return problems


def fix_plugin(cplugin_path: Path) -> list[str]:
	"""Auto-fix one plugin: regenerate missing Api.h + docs. Returns messages."""
	cplugin_path = cplugin_path.resolve()
	data = read_cplugin(cplugin_path)
	name = data.get("Name") or cplugin_path.parent.name
	base = cplugin_path.parent

	messages: list[str] = []

	# Api.h
	public_dir = base / "Public"
	public_dir.mkdir(parents=True, exist_ok=True)
	api = public_dir / f"{name}Api.h"
	if not api.is_file():
		api.write_text(_plugin_api_header_text(name), encoding="utf-8", newline="\n")
		messages.append(f"FIXED {api.relative_to(base)}")

	# Docs (only if missing).
	plugin_md = base / f"{name}Doc.md"
	if not plugin_md.is_file():
		plugin_md.write_text(f"# {name}\n\n待补。\n", encoding="utf-8", newline="\n")
		messages.append(f"FIXED {plugin_md.relative_to(base)}")

	plugin_api = base / f"{name}API.html"
	if not plugin_api.is_file():
		plugin_api.write_text(
			f"<!DOCTYPE html>\n<html lang=\"zh\">\n<head>\n<meta charset=\"UTF-8\">\n"
			f"<title>{name} — API</title>\n"
			f"<style>body{{background:#14181f;color:#d8e1f0;font-family:'Segoe UI',sans-serif;padding:32px}}</style>\n"
			f"</head>\n<body>\n<h1>{name} — API</h1>\n<p>占位。</p>\n</body>\n</html>\n",
			encoding="utf-8", newline="\n",
		)
		messages.append(f"FIXED {plugin_api.relative_to(base)}")

	return messages


def check_and_fix_plugin(cplugin_path: Path) -> int:
	"""
	Double-click .cplugin: validate then fix. Prints messages; returns exit code
	(0 = healthy/fixed, 1 = has unfixable errors).
	"""
	cplugin_path = cplugin_path.resolve()
	data = read_cplugin(cplugin_path)
	name = data.get("Name") or cplugin_path.parent.name

	print(f"[Maho] Plugin: {name}")

	problems = validate_plugin(cplugin_path)
	hard_errors = 0
	for severity, msg in problems:
		tag = "ERROR" if severity == "error" else "WARN"
		print(f"[Maho][{tag}] {msg}")
		if severity == "error":
			hard_errors += 1
	if hard_errors:
		print("[Maho] Fix manually: fix .cplugin Dependencies, then double-click again.")
		return 1

	messages = fix_plugin(cplugin_path)
	for m in messages:
		print(f"[Maho] {m}")
	print("[Maho] Plugin healthy." if not messages else "[Maho] Plugin fixed.")
	return 0

