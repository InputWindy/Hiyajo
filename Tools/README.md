# Tools

Engine-local tooling. Root only exposes thin user-facing `.bat` launchers.

| File | Role |
|------|------|
| `maho_python.bat` | Run with `Tools/python/python.exe` or `Scripts/python.exe` (venv) |
| `maho_pythonw.bat` | Same with `pythonw.exe` (no console; for GUI tools) |
| `launch_create_project.vbs` / `launch_package.vbs` | WScript → pythonw (root or Scripts) for create / package GUIs |
| `launch_generate_project.vbs` | Double-click `.cproject` → generate `.sln` (open verb; via wscript) |
| `launch_switch_engine.vbs` / `switch_engine.py` | Explorer right-click → rewrite `.cproject` `EngineDirectory` |
| `create_project.py` | New-project UI (`createProject.bat`) |
| `generateProject.py` / `generateProject.bat` | `.cproject` / workspace → sibling `.sln` |
| `package_ui.py` / `package.bat` | Packaging UI (logs in window; abort via Close) |
| `package.py` | Headless CLI package |
| `clean.py` | Wipe generated/temp files (`clean.bat` at engine root) |
| `scan_plugins.py` / `scan_plugins.bat` | Scan `.cplugin` → module DAG / build-order JSON |
| `maho_tools.py` | Shared helpers |

## Local Python

Installed by root `setup.bat` into `%LOCALAPPDATA%\Maho\python\tooling\` (outside the repo — **survives engine folder rename**).  
`Tools/python/` is a **junction** to that directory (gitignore). Cache: `Tools/_cache/`.

Prefer `venv` from a host Python (no MSI registration). Official python.org installer is only a fallback, and always targets LocalAppData — never the engine tree.

Do not put on PATH; always use `maho_python.bat` / `maho_pythonw.bat` / `launch_*.vbs`. After cloning or renaming the engine tree, run `setup.bat` to recreate the junction if needed.

All `Tools/*.py` scripts **refuse** a system Python at startup (`maho_tools.ensure_engine_python`).

Templates remain under `Build/Templates/GameProject/`.
