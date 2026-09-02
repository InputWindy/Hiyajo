import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

from maho_tools import create_plugin, ENGINE_ROOT  # noqa: E402

import argparse

p = argparse.ArgumentParser()
p.add_argument("--plugins-dir", type=Path, required=True)
args = p.parse_args()

plugins_dir = args.plugins_dir.resolve()
print("ENGINE_ROOT:", ENGINE_ROOT)
print("plugins_dir :", plugins_dir)

path = create_plugin(
    "UI",
    ENGINE_ROOT,
    description="UI engine layer: hosts the Dear ImGui context (CPU side) and "
    "drives it from the engine stages (IInit/ITick/IShutdown); the UI render "
    "feature draws the produced draw data over the scene before present",
    plugins_dir=plugins_dir,
)
print("CREATED:", path)
