import sys, tempfile
sys.path.insert(0, r"c:\Users\luchunyi01\Desktop\书架\Hiyajo\Tools")
from pathlib import Path
from maho_tools import create_plugin

eng = Path(r"c:\Users\luchunyi01\Desktop\书架\Hiyajo")
# create a couple engine plugins (they get ignored by git, but exist for the test)
p1 = eng / "Plugins" / "Rendering" / "SSAO"
create_plugin("SSAO", eng, plugins_dir=p1.parent) if not (p1 / "SSAO.cplugin").exists() else None
p2 = eng / "Plugins" / "Net"
create_plugin("Net", eng, plugins_dir=p2.parent) if not (p2 / "Net.cplugin").exists() else None
print("engine plugins ready")
