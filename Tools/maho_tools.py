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
TEMPLATE_DIR = ENGINE_ROOT / "Build" / "Templates" / "GameProject"
PLUGIN_TEMPLATE_DIR = ENGINE_ROOT / "Build" / "Templates" / "Plugin"
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
		if src.suffix.lower() == ".tmpl":
			# Alternate templates (e.g. GameAppCli.cpp.tmpl) are rendered on
			# demand by _write_game_app — never copied into the project tree.
			continue
		rel = src.relative_to(TEMPLATE_DIR)
		rel_parts = [render_template_text(part, mapping) for part in rel.parts]
		dst = project_dir.joinpath(*rel_parts)
		dst.parent.mkdir(parents=True, exist_ok=True)
		if src.suffix.lower() in {".png", ".bin", ".exe"}:
			shutil.copy2(src, dst)
		else:
			text = src.read_text(encoding="utf-8")
			# .bat/.cmd must be CRLF or cmd.exe mis-parses them (goto/labels break).
			newline = "\r\n" if src.suffix.lower() in {".bat", ".cmd"} else "\n"
			dst.write_text(render_template_text(text, mapping), encoding="utf-8", newline=newline)


def _format_extensions_list(classes: list[str]) -> str:
	"""Format a Maho::FExtensions<...> list — vertical when non-empty."""
	if not classes:
		return "Maho::FExtensions<>"
	body = ",\n".join(f"\t{cls}" for cls in classes)
	return f"Maho::FExtensions<\n{body}\n>"


def build_gameapp_mapping(project_name: str, engine_root: Path, plugin_names: list[str], dev_platform: str = "Windows") -> dict[str, str]:
	"""
	Build the GameApp.cpp placeholder mapping from a plugin-name list.
	Plugins are split by Extension.Stage: tools → singleton registry, the
	rest → engine.
	"""
	ident = project_cpp_ident(project_name)
	plugin_meta = {p["Name"]: p for p in list_engine_plugins(engine_root)}

	plugin_includes: list[str] = []
	tool_includes: list[str] = []
	engine_includes: list[str] = []
	tool_classes: list[str] = []
	engine_classes: list[str] = []
	for name in sorted(plugin_names):
		if name == "AssemblyImporter":
			continue   # infrastructure — loaded by the thin launcher, not an aggregate member
		meta = plugin_meta.get(name)
		if meta is None or not meta.get("Extension"):
			continue
		header = meta["Extension"]["Header"]
		cls = meta["Extension"]["Class"]
		stage = meta["Extension"].get("Stage", "EEngineStage")
		plugin_includes.append(f"#include <{header}>")
		if stage == "EToolStage":
			tool_includes.append(f"#include <{header}>")
			tool_classes.append(cls)
		else:
			engine_includes.append(f"#include <{header}>")
			engine_classes.append(cls)

	has_parser = "CommandParser" in plugin_names
	parse_body = "\t\tMaho::CommandParser::ParseCommandLine(Argc, Argv);" if has_parser else "\t\t(void)Argc;\n\t\t(void)Argv;"

	return {
		"PROJECT_NAME": project_name,
		"PROJECT_IDENT": ident,
		"TOOLKIT_CLASS": f"F{ident}Toolkit",
		"APP_CLASS": f"F{ident}Engine",
		"PLUGIN_INCLUDES": "\n".join(plugin_includes),
		"TOOL_INCLUDES": "\n".join(tool_includes),
		"ENGINE_INCLUDES": "\n".join(engine_includes),
		"TOOL_EXTENSIONS": _format_extensions_list(tool_classes),
		"ENGINE_EXTENSIONS": _format_extensions_list(engine_classes),
		"ENTRY_POINT_INCLUDE": {
			"Windows": "EntryPointWindows.h",
			"Linux": "EntryPointLinux.h",
			"Android": "EntryPointAndroid.h",
			"IOS": "EntryPointIOS.h",
			"Xbox": "EntryPointXbox.h",
		}.get(dev_platform, "EntryPointWindows.h"),
		"PARSE_COMMAND_LINE": parse_body,
	}


def _write_game_app(project_dir: Path, mapping: dict[str, str], app_type: str) -> Path:
	"""
	Render the aggregate modules (Toolkit/Engine DLLs) + thin Main.cpp.
	IDE → toolkit + engine aggregates + thin launcher (MainLoop).
	CLI  → toolkit aggregate + thin launcher (Init only, no loop).
	"""
	src_dir = project_dir / "Source"
	src_dir.mkdir(parents=True, exist_ok=True)

	def render(src_name: str) -> str:
		text = (TEMPLATE_DIR / "Source" / src_name).read_text(encoding="utf-8")
		return render_template_text(text, mapping)

	ident = mapping["PROJECT_IDENT"]

	# Toolkit aggregate (both IDE and CLI).
	(src_dir / f"{ident}Toolkit.cpp").write_text(
		render("ToolkitModule.cpp.tmpl"), encoding="utf-8", newline="\n"
	)

	if app_type == "CLI":
		project_name = mapping["PROJECT_NAME"]
		main_text = (
			"#include <Maho.h>\n"
			"#include <AssemblyImporter.h>\n\n"
			"int main(int Argc, char** Argv)\n"
			"{\n"
			"\t(void)Argc;\n\t(void)Argv;\n"
			"\tMaho::InstallFatalHandlers();\n\n"
			"\tauto& Importer = Maho::AssemblyImporter::FAssemblyImporter::Get();\n"
			f"\tif (!Importer.ImportToolkit(\"{project_name}Toolkit.dll\"))\n\t{{\n\t\treturn 1;\n\t}}\n"
			"\tImporter.ExecuteStage(Maho::EEngineStage::Shutdown);\n"
			"\treturn 0;\n"
			"}\n"
		)
	else:
		# Engine aggregate.
		(src_dir / f"{ident}Engine.cpp").write_text(
			render("EngineModule.cpp.tmpl"), encoding="utf-8", newline="\n"
		)
		main_text = render("Main.cpp")

	dst = src_dir / "Main.cpp"
	dst.write_text(main_text, encoding="utf-8", newline="\n")
	return dst


def codegen_plugin_dependencies(engine_root: Path, out_dir: Path) -> list[Path]:
	"""
	Regenerate <Name>.gen.h for every plugin (with source) into out_dir — the
	project's Intermediate/Generated. Plugin headers only `#include <Name>.gen.h>`;
	the file itself is generated per-project and never committed.
	"""
	plugins_dir = (engine_root / "Maho" / "Plugins").resolve()
	if not plugins_dir.is_dir():
		return []

	meta: dict[str, dict[str, str]] = {}
	has_source: dict[str, bool] = {}
	for cplugin_path in discover_cplugin_files([plugins_dir]):
		data = read_cplugin(cplugin_path)
		name = cplugin_path.parent.name
		mod = data.get("Modules", [{}])[0]
		ext = mod.get("Extension") or {}
		meta[name] = {
			"Class": ext.get("Class", ""),
			"Stage": ext.get("Stage", "EEngineStage"),
			"Header": ext.get("Header", f"{name}.h"),
			"CPlugin": str(cplugin_path),
		}
		public_dir = cplugin_path.parent / "Source" / name / "Public"
		has_source[name] = public_dir.is_dir() and any(p.suffix == ".h" for p in public_dir.glob("*.h"))

	out_dir = out_dir.resolve()
	out_dir.mkdir(parents=True, exist_ok=True)

	generated: list[Path] = []
	for name, info in meta.items():
		if not has_source[name]:
			continue   # empty scaffolding — no class to attach deps to

		cplugin_path = Path(info["CPlugin"])
		mod_data = read_cplugin(cplugin_path).get("Modules", [{}])[0]
		deps = mod_data.get("Dependencies", []) or []
		inherits = mod_data.get("Inherits", []) or []
		if isinstance(inherits, str):
			inherits = [inherits]

		cls = info["Class"]
		class_short = cls.split("::")[-1]
		ns_parts = cls.split("::")[:-1]
		stage = info["Stage"]

		dep_classes = [meta[d]["Class"] for d in deps if d in meta and has_source[d]]
		dep_headers = [meta[d]["Header"] for d in deps if d in meta and has_source[d]]

		# Inherits: pull in each parent's .gen.h (FDependsPack struct) + header
		# (class declaration) so TResolveDependsPack<FParent> resolves correctly.
		parent_classes: list[str] = []
		inherits_includes: list[str] = []
		for parent in inherits:
			if parent in meta and has_source[parent]:
				parent_classes.append(meta[parent]["Class"])
				inherits_includes.append(f"{parent}.gen.h")
				inherits_includes.append(meta[parent]["Header"])

		dep_includes = "\n".join(f"#include <{h}>" for h in (inherits_includes + dep_headers))

		if parent_classes:
			parent_packs = ",\n".join(
				f"\t\ttypename TResolveDependsPack<{pc}>::Type" for pc in parent_classes
			)
			if dep_classes:
				dep_list = ",\n".join(f"\t\t\t\t{dc}" for dc in dep_classes)
				pack_body = (
					"\tusing FDependsPack = typename TPackConcat<\n"
					f"{parent_packs},\n"
					"\t\tTDependsPack<\n"
					f"\t\t\tTDependsOn<{stage}::Init, TTypeList<\n"
					f"{dep_list}\n"
					"\t\t\t>>\n"
					"\t\t>\n"
					"\t>::Type;\n"
				)
			else:
				pack_body = (
					"\tusing FDependsPack = typename TPackConcat<\n"
					f"{parent_packs}\n"
					"\t>::Type;\n"
				)
		elif dep_classes:
			dep_list = ",\n".join(f"\t\t\t{dc}" for dc in dep_classes)
			pack_body = (
				"\tusing FDependsPack = TDependsPack<\n"
				f"\t\tTDependsOn<{stage}::Init, TTypeList<\n"
				f"{dep_list}\n"
				"\t\t>>\n"
				"\t>;\n"
			)
		else:
			pack_body = "\tusing FDependsPack = TDependsPack<>;\n"

		opens = "\n".join(f"namespace {p}\n{{" for p in ns_parts)
		closes = "\n".join(f"}} // namespace {p}" for p in reversed(ns_parts))

		text = (
			f"// Generated from {name}.cplugin Dependencies — do not edit by hand.\n"
			"#pragma once\n"
			"#include <Engine.h>\n"
			f"{dep_includes}\n\n"
			f"{opens}\n\n"
			f"/** Scheduler-level dependency declaration, synced from .cplugin. */\n"
			f"struct {class_short}Dependencies\n"
			"{\n"
			f"{pack_body}"
			"};\n\n"
			f"{closes}\n"
		)

		out = out_dir / f"{name}.gen.h"
		out.write_text(text, encoding="utf-8", newline="\n")
		generated.append(out)

	return generated


# ───────────────────────────────────────────────────────────────────────
# Plugin health: validate (pre-compile check) + fix (regenerate missing files).
# ───────────────────────────────────────────────────────────────────────

def _api_header_text(name: str) -> str:
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


def validate_plugins(
	engine_root: Path,
	enabled: set[str] | None = None,
) -> list[tuple[str, str]]:
	"""
	Check every plugin (or only the enabled set) for missing headers that would
	break compilation. Returns [(severity, message)] — severity is 'error' or
	'warning'. Empty list = all healthy.
	"""
	plugins_dir = (engine_root / "Maho" / "Plugins").resolve()
	basic_dir = (engine_root / "Maho" / "Source" / "Public" / "Basic").resolve()
	if not plugins_dir.is_dir():
		return [("error", f"Plugins dir not found: {plugins_dir}")]

	all_cplugins = discover_cplugin_files([plugins_dir, basic_dir])
	all_names = {p.parent.name for p in all_cplugins}

	problems: list[tuple[str, str]] = []
	for cplugin_path in all_cplugins:
		data = read_cplugin(cplugin_path)
		name = cplugin_path.parent.name
		if enabled is not None and name not in enabled:
			continue
		mod = data.get("Modules", [{}])[0]
		ext = mod.get("Extension") or {}
		header = ext.get("Header", f"{name}.h")
		public_dir = cplugin_path.parent / "Source" / name / "Public"
		private_dir = cplugin_path.parent / "Source" / name / "Private"

		# Skip empty scaffolding — no implementation (.cpp) means nothing to compile.
		if not private_dir.is_dir() or not any(p.suffix == ".cpp" for p in private_dir.glob("*.cpp")):
			continue

		if not (public_dir / header).is_file():
			problems.append(("error", f"{name}: Extension.Header '{header}' missing"))
		if not (public_dir / f"{name}Api.h").is_file():
			problems.append(("error", f"{name}: {name}Api.h missing (export macro)"))

		for dep in mod.get("Dependencies", []) or []:
			if dep not in all_names:
				problems.append(("error", f"{name}: dependency plugin '{dep}' not found"))

	return problems


def _plugin_doc_info(cplugin_path: Path, data: dict[str, Any]) -> dict[str, str]:
	"""Metadata for the auto-generated .md / .html plugin docs."""
	name = cplugin_path.parent.name
	mod = data.get("Modules", [{}])[0]
	ext = mod.get("Extension") or {}
	deps = mod.get("Dependencies", []) or []
	inherits = mod.get("Inherits", []) or []
	if isinstance(inherits, str):
		inherits = [inherits]
	stage = ext.get("Stage", "EEngineStage")
	return {
		"Name": name,
		"FriendlyName": data.get("FriendlyName", name),
		"Description": data.get("Description", ""),
		"Class": ext.get("Class", f"Maho::{name}::F{name}"),
		"Header": ext.get("Header", f"{name}.h"),
		"Stage": stage,
		"StageLabel": (
			"engine extension (driven by EEngineStage)"
			if stage == "EEngineStage"
			else "pre-app toolkit (driven by EToolStage)"
		),
		"Priority": ext.get("Priority", "System"),
		"Dependencies": ", ".join(deps) or "—",
		"Inherits": ", ".join(inherits) or "—",
	}


def _plugin_md_text(info: dict[str, str]) -> str:
	title = info["Name"]
	if info["FriendlyName"] and info["FriendlyName"] != info["Name"]:
		title = f"{info['Name']} — {info['FriendlyName']}"
	return (
		f"# {title}\n\n"
		f"{info['Description']}\n\n"
		"## 扩展类\n\n"
		"| 字段 | 值 |\n"
		"|------|-----|\n"
		f"| Class | `{info['Class']}` |\n"
		f"| Header | `{info['Header']}` |\n"
		f"| Stage | `{info['Stage']}` |\n"
		f"| Priority | `{info['Priority']}` |\n"
		f"| Dependencies | {info['Dependencies']} |\n"
		f"| Inherits | {info['Inherits']} |\n\n"
		"## 说明\n\n"
		f"{info['StageLabel']}。\n\n"
		"## 用法\n\n"
		"（此处补充使用示例）\n\n"
		"## 相关文档\n\n"
		f"- [{info['Name']}.html]({info['Name']}.html) — API 文档\n"
		"- [../README.md](../README.md) — 插件总览\n"
		"- [../../Source/Public/Maho.md](../../Source/Public/Maho.md) — 引擎核心\n"
		"- [../../Source/Public/Core/Core.md](../../Source/Public/Core/Core.md) — 基础设施\n"
	)


def _plugin_html_text(info: dict[str, str]) -> str:
	return f"""<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>{info['Name']} — API 文档</title>
<style>
	:root {{
		--bg: #14181f;
		--panel: #1b2130;
		--code-bg: #0d1117;
		--text: #d8e1f0;
		--muted: #8b96a8;
		--accent: #5b8dd9;
		--border: #2c3444;
		--border-strong: #3d4a61;
		--kw: #ff7b72;
		--type: #79c0ff;
		--field: #e6c07b;
	}}
	* {{ box-sizing: border-box; }}
	body {{
		margin: 0;
		padding: 32px 40px;
		background: var(--bg);
		color: var(--text);
		font-family: "Segoe UI", "Microsoft YaHei", sans-serif;
		line-height: 1.7;
		max-width: 1040px;
	}}
	h1 {{ font-size: 26px; border-bottom: 2px solid var(--border-strong); padding-bottom: 12px; }}
	h2 {{ font-size: 22px; margin-top: 44px; color: var(--accent); border-bottom: 1px solid var(--border); padding-bottom: 8px; }}
	a {{ color: var(--accent); text-decoration: none; }}
	a:hover {{ text-decoration: underline; }}
	code {{
		font-family: "Cascadia Code", Consolas, monospace;
		background: var(--code-bg);
		padding: 2px 6px;
		border-radius: 4px;
		font-size: 13px;
	}}
	.kw {{ color: var(--kw); }}
	.type {{ color: var(--type); }}
	.field {{ color: var(--field); }}
	.muted {{ color: var(--muted); }}
	.class-panel {{
		background: var(--panel);
		border: 1px solid var(--border);
		border-left: 4px solid var(--accent);
		border-radius: 8px;
		padding: 20px 24px;
		margin: 20px 0;
	}}
	.class-name {{
		font-family: "Cascadia Code", Consolas, monospace;
		font-size: 18px;
		color: var(--accent);
		margin: 0 0 4px 0;
	}}
	.class-kind {{
		font-family: "Cascadia Code", Consolas, monospace;
		font-size: 12px;
		color: var(--muted);
		border: 1px solid var(--border);
		border-radius: 4px;
		padding: 1px 6px;
		margin-left: 8px;
		vertical-align: middle;
	}}
	.class-desc {{ margin: 8px 0 0 0; color: var(--text); }}
	.api-table {{ width: 100%; border-collapse: collapse; margin-top: 12px; }}
	.api-table th, .api-table td {{
		border: 1px solid var(--border);
		padding: 8px 10px;
		text-align: left;
		vertical-align: top;
	}}
	.api-table th {{ background: var(--code-bg); color: var(--muted); font-weight: normal; }}
	.sig {{ font-family: "Cascadia Code", Consolas, monospace; font-size: 13px; }}
	.group {{
		text-transform: uppercase;
		letter-spacing: 1px;
		font-size: 13px;
		color: var(--muted);
		margin: 20px 0 8px 0;
		border-bottom: 1px dashed var(--border);
		padding-bottom: 4px;
	}}
</style>
</head>
<body>
<h1>{info['Name']} — API 文档</h1>
<p class="muted">{info['Description']}</p>

<section class="class-panel">
	<h2 class="class-name">{info['Class']}<span class="class-kind">class</span></h2>
	<p class="class-desc">{info['StageLabel']}。</p>
	<p class="class-desc">
		继承 <code>TExtension&lt;{info['Stage']}, {info['Class'].split('::')[-1]}&gt;</code>
		+ <code>{info['Class'].split('::')[-1]}Dependencies</code>（依赖声明）。
	</p>

	<h3 class="group">接口</h3>
	<table class="api-table">
		<tr><th style="width:48%">签名</th><th>说明</th></tr>
		<tr>
			<td class="sig"><code><span class="kw">bool</span> ExecuteStage(<span class="type">{info['Stage']}</span> Stage)</code></td>
			<td>每阶段行为，调度器按依赖序调用。</td>
		</tr>
		<tr>
			<td class="sig"><code><span class="kw">static</span> {info['Class'].split('::')[-1]}&amp; Get()</code></td>
			<td>单例访问（<code>TSingleton</code>）。</td>
		</tr>
	</table>

	<h3 class="group">成员变量</h3>
	<table class="api-table">
		<tr><th style="width:28%">字段</th><th style="width:28%">类型</th><th>说明</th></tr>
		<tr><td colspan="3" class="muted">（此处补充成员变量）</td></tr>
	</table>
</section>

<h2>元数据</h2>
<table class="api-table">
	<tr><th style="width:24%">字段</th><th>值</th></tr>
	<tr><td>Header</td><td><code>{info['Header']}</code></td></tr>
	<tr><td>Stage</td><td><code>{info['Stage']}</code></td></tr>
	<tr><td>Priority</td><td><code>{info['Priority']}</code></td></tr>
	<tr><td>Dependencies</td><td>{info['Dependencies']}</td></tr>
	<tr><td>Inherits</td><td>{info['Inherits']}</td></tr>
</table>

<h2>相关文档</h2>
<p>
	<a href="{info['Name']}.md">{info['Name']}.md</a> — 概念文档 ·
	<a href="../README.md">插件总览</a> ·
	<a href="../../Source/Public/Maho.md">引擎核心</a>
</p>
</body>
</html>
"""


def fix_plugin(cplugin_path: Path, base: Path | None = None) -> list[str]:
	"""
	Auto-fix one plugin: regenerate missing Api.h, and generate starter docs
	(<Name>.md + <Name>.html) at the plugin root. .gen.h is generated per-project
	into Intermediate at .cproject sync — not stored in Public/.
	Returns human-readable messages ('FIXED ...' / 'UNFIXABLE ...').
	"""
	base = (base or cplugin_path.parent).resolve()
	cplugin_path = cplugin_path.resolve()
	data = read_cplugin(cplugin_path)
	name = cplugin_path.parent.name
	mod = data.get("Modules", [{}])[0]
	ext = mod.get("Extension") or {}
	header = ext.get("Header", f"{name}.h")

	public_dir = cplugin_path.parent / "Source" / name / "Public"
	public_dir.mkdir(parents=True, exist_ok=True)

	messages: list[str] = []

	api = public_dir / f"{name}Api.h"
	if not api.is_file():
		api.write_text(_api_header_text(name), encoding="utf-8", newline="\n")
		messages.append(f"FIXED {api.relative_to(base)}")

	main = public_dir / header
	private_dir = cplugin_path.parent / "Source" / name / "Private"
	has_impl = private_dir.is_dir() and any(p.suffix == ".cpp" for p in private_dir.glob("*.cpp"))
	if has_impl and not main.is_file():
		messages.append(f"UNFIXABLE {name}: Extension.Header '{header}' missing — content unknown")

	# Starter docs at the plugin root — generated only when missing, never
	# overwriting a hand-edited doc.
	info = _plugin_doc_info(cplugin_path, data)
	md = cplugin_path.parent / f"{name}.md"
	if not md.is_file():
		md.write_text(_plugin_md_text(info), encoding="utf-8", newline="\n")
		messages.append(f"FIXED {md.relative_to(base)}")

	html = cplugin_path.parent / f"{name}.html"
	if not html.is_file():
		html.write_text(_plugin_html_text(info), encoding="utf-8", newline="\n")
		messages.append(f"FIXED {html.relative_to(base)}")

	return messages


def fix_plugins(
	engine_root: Path | None = None,
	plugin_roots: list[Path] | None = None,
) -> list[str]:
	"""
	Batch-fix all plugins under plugin_roots (default: the engine's Maho/Plugins).
	.gen.h is generated per-project at .cproject sync — not touched here.
	"""
	engine_root = (engine_root or ENGINE_ROOT).resolve()
	roots = [Path(r).resolve() for r in (plugin_roots or [engine_root / "Maho" / "Plugins"])]
	if not roots:
		return ["ERROR no plugin roots"]

	messages: list[str] = []
	for cplugin_path in discover_cplugin_files(roots):
		messages.extend(fix_plugin(cplugin_path))
	return messages


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
		inherits = (data.get("Modules", [{}])[0]).get("Inherits", []) or []
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


def codegen_game_app(cproject_path: Path) -> Path:
	"""
	Regenerate Source/GameApp.cpp from the .cproject's current plugin selection.
	Run before CMake configure so the FExtensions assembly matches the build.
	"""
	cproject_path = cproject_path.resolve()
	data = read_cproject(cproject_path)
	project_dir = cproject_path.parent
	project_name = str(data["ProjectName"])
	engine_root = resolve_engine_directory(cproject_path, data)

	overrides = parse_cproject_plugin_overrides(data)
	if overrides is None:
		plugin_names = [p["Name"] for p in list_engine_plugins(engine_root) if p["EnabledByDefault"]]
	else:
		plugin_names = [name for name, enabled in overrides.items() if enabled]

	dev_platform = str(data.get("DevPlatform", "Windows"))
	mapping = build_gameapp_mapping(project_name, engine_root, plugin_names, dev_platform=dev_platform)

	app_type = str(data.get("AppType", "IDE"))
	return _write_game_app(project_dir, mapping, app_type)


def create_project(
	project_name: str,
	parent_dir: Path,
	engine_root: Path,
	description: str = "",
	author: str = "",
	plugins: list[str] | None = None,
	template: str = "client",
	dev_platform: str = "Windows",
	app_type: str = "IDE",
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
		engine_plugins = list_engine_plugins(engine_root)
		plugin_names = [p["Name"] for p in engine_plugins if p["EnabledByDefault"]]
	else:
		plugin_names = list(plugins)

	# Infrastructure assemblies every project needs (app shapes + importer).
	for infra in ("Toolkit", "Engine", "AssemblyImporter"):
		if infra not in plugin_names:
			plugin_names.append(infra)

	mapping = build_gameapp_mapping(project_name, engine_root, plugin_names, dev_platform=dev_platform)
	mapping["DESCRIPTION"] = description
	mapping["AUTHOR"] = author
	mapping["APP_TYPE"] = app_type
	copy_template(project_dir, mapping)
	_write_game_app(project_dir, mapping, app_type)

	plugin_entries = [{"Name": name, "Enabled": True} for name in plugin_names]
	cproject = {
		"FileVersion": CPROJECT_VERSION,
		"EngineAssociation": "Maho",
		"EngineDirectory": engine_path_for_cproject(engine_root, project_dir),
		"ProjectName": project_name,
		"Description": description,
		"Author": author,
		"EngineTemplate": template,
		"DevPlatform": dev_platform,
		"AppType": app_type,
		"Modules": [
			{
				"Name": project_name,
				"Type": "Runtime",
			}
		],
		"Plugins": plugin_entries,
	}
	cproject_path = project_dir / f"{project_name}.cproject"
	write_cproject(cproject_path, cproject)
	return cproject_path


def create_plugin(
	plugin_name: str,
	engine_root: Path,
	description: str = "",
	stage: str = "EEngineStage",
	inherits: list[str] | None = None,
	plugins_dir: Path | None = None,
) -> Path:
	"""
	Scaffold a new engine plugin under plugins_dir (default <engine>/Maho/Plugins)
	from the Plugin template. EEngineStage plugins get the dynamic factory
	(CreateExtension) exported automatically. `inherits` lists parent plugin
	names (multi-inherit) — written to .cplugin Inherits so codegen merges
	their FDependsPack via TPackConcat.
	"""
	if not is_valid_project_name(plugin_name):
		raise ValueError(
			"Plugin name must start with a letter and contain only A-Z, a-z, 0-9, _, -"
		)

	plugins_dir = (plugins_dir or engine_root / "Maho" / "Plugins").resolve()
	dst = plugins_dir / plugin_name
	if dst.exists():
		raise FileExistsError(f"Plugin already exists: {dst}")

	if stage not in ("EEngineStage", "EToolStage"):
		raise ValueError("stage must be EEngineStage or EToolStage")

	inherits = [p for p in (inherits or []) if p and p != plugin_name]

	class_name = f"F{plugin_name}"
	export_name = plugin_name.upper()
	stage_label = (
		"engine extension (driven by EEngineStage)"
		if stage == "EEngineStage"
		else "pre-app toolkit (driven by EToolStage)"
	)

	# Resolve parent plugin names → fully-qualified class names (C++ bases).
	parent_classes: list[str] = []
	if inherits:
		existing = {
			p["Name"]: (p.get("Extension") or {}).get("Class", "")
			for p in list_engine_plugins(engine_root)
		}
		parent_classes = [existing[p] for p in inherits if existing.get(p)]

	inherits_bases = "".join(f"\t, public {pc}\n" for pc in parent_classes)
	get_using = f"\tusing TSingleton<{class_name}>::Get;\n" if parent_classes else ""

	# Dynamic factory — exported via the plugin's API macro. The signature is
	# stage-parameterized: IExtension<EEngineStage> or IExtension<EToolStage>.
	factory_block = (
		"\n"
		"// ── Dynamic plugin entry (runtime load/unload via FAssemblyImporter) ──\n"
		"\n"
		"namespace\n"
		"{\n"
		"\n"
		f"class {class_name}Adapter final : public Maho::IExtension<Maho::{stage}>\n"
		"{\n"
		"public:\n"
		f"\t[[nodiscard]] bool ExecuteStage(Maho::{stage} Stage) override\n"
		"\t{\n"
		f"\t\treturn Maho::{plugin_name}::{class_name}::Get().ExecuteStage(Stage);\n"
		"\t}\n"
		"};\n"
		"\n"
		"} // namespace\n"
		"\n"
		f'extern "C" MAHO_{export_name}_API Maho::IExtension<Maho::{stage}>* CreateExtension()\n'
		"{\n"
		f"\treturn new {class_name}Adapter();\n"
		"}\n"
	)

	mapping = {
		"NAME": plugin_name,
		"NAMESPACE": plugin_name,
		"CLASS": class_name,
		"STAGE": stage,
		"EXPORT_NAME": export_name,
		"DESCRIPTION": description,
		"STAGE_LABEL": stage_label,
		"FACTORY_BLOCK": factory_block,
		"INHERITS_BASES": inherits_bases,
		"GET_USING": get_using,
		"INHERITS_LINE": (
			f'"Inherits": {json.dumps(inherits)},\n'
			if inherits
			else ""
		),
	}

	for src in PLUGIN_TEMPLATE_DIR.rglob("*"):
		if src.is_dir():
			continue
		rel_parts = [render_template_text(part, mapping) for part in src.relative_to(PLUGIN_TEMPLATE_DIR).parts]
		out = dst.joinpath(*rel_parts)
		out.parent.mkdir(parents=True, exist_ok=True)
		if src.name == ".gitkeep":
			out.write_text("", encoding="utf-8", newline="\n")
		else:
			text = src.read_text(encoding="utf-8")
			out.write_text(render_template_text(text, mapping), encoding="utf-8", newline="\n")

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

	# Regenerate GameApp.cpp from the .cproject's current plugin selection.
	game_app = codegen_game_app(cproject_path)
	log(f"[Maho] GameApp : {game_app.relative_to(project_dir)}")

	# Sync .cplugin Dependencies → <Name>.gen.h into the project's Intermediate.
	codegen_plugin_dependencies(engine_root, intermediate / "Generated")

	# Pre-compile health check — fail fast on missing headers before cmake does.
	overrides = parse_cproject_plugin_overrides(data)
	if overrides is None:
		enabled_names = {p["Name"] for p in list_engine_plugins(engine_root) if p.get("EnabledByDefault", True)}
	else:
		enabled_names = {name for name, on in overrides.items() if on}
	problems = validate_plugins(engine_root, enabled=enabled_names)
	hard_errors = 0
	for severity, msg in problems:
		tag = "ERROR" if severity == "error" else "WARN"
		log(f"[Maho][{tag}] {msg}")
		if severity == "error":
			hard_errors += 1
	if hard_errors:
		raise RuntimeError(
			f"Plugin validation failed ({hard_errors} error(s)) — "
			"run fix_plugins.bat, or double-click the broken .cplugin to auto-fix."
		)

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
	intermediate = engine_root / "Maho" / "Intermediate"
	# Generate per-plugin .gen.h into the workspace Intermediate before configure.
	codegen_plugin_dependencies(engine_root, intermediate / "Generated")
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

	Returns [{Name, Dependencies, FriendlyName, Description, EnabledByDefault,
	Extension}] in stable name order. Extension is the first module's
	{Class, Header, Priority} (None when the module declares no extension).
	"""
	root = (engine_root or ENGINE_ROOT).resolve() / "Maho" / "Plugins"
	if not root.is_dir():
		return []
	out: list[dict[str, Any]] = []
	for cplugin_path in discover_cplugin_files([root]):
		data = read_cplugin(cplugin_path)
		name = cplugin_path.parent.name
		deps: list[str] = []
		extension: dict[str, str] | None = None
		for mod in data.get("Modules", []):
			for dep in mod.get("Dependencies", []) or []:
				if dep not in deps:
					deps.append(dep)
			if extension is None and mod.get("Extension"):
				extension = {
					"Class": mod["Extension"]["Class"],
					"Header": mod["Extension"]["Header"],
					"Priority": mod["Extension"]["Priority"],
					"Stage": mod["Extension"].get("Stage", "EEngineStage"),
				}
		out.append(
			{
				"Name": name,
				"Dependencies": deps,
				"FriendlyName": data.get("FriendlyName", name),
				"Description": data.get("Description", ""),
				"EnabledByDefault": bool(data.get("EnabledByDefault", True)),
				"Extension": extension,
				"Group": list(cplugin_path.parent.relative_to(root).parts[:-1]),
			}
		)
	out.sort(key=lambda p: p["Name"])
	return out


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
		if enabled_overrides is not None:
			enabled = bool(enabled_overrides.get(plugin_name, False))
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
	"""Engine Maho/Plugins + Basic (infra) + game Project/Plugins when present."""
	cproject_path = cproject_path.resolve()
	data = read_cproject(cproject_path)
	engine_root = resolve_engine_directory(cproject_path, data)
	project_dir = cproject_path.parent
	roots = [
		engine_root / "Maho" / "Plugins",
		engine_root / "Maho" / "Source" / "Public" / "Basic",
	]
	for candidate in (project_dir / "Plugins", project_dir / "Project" / "Plugins"):
		if candidate.is_dir():
			roots.append(candidate)
			break
	return roots

