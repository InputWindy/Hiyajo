import sys, shutil
sys.path.insert(0, r"c:\Users\luchunyi01\Desktop\书架\Hiyajo\Tools")
from pathlib import Path
from maho_tools import create_project

eng = Path(r"c:\Users\luchunyi01\Desktop\书架\Hiyajo")
root = Path(r"c:\Users\luchunyi01\Desktop\书架\Hiyajo\Tests\CoreTest\_pik")
shutil.rmtree(root, ignore_errors=True)
p = create_project("PIK", root, eng, description="pack", plugins=["Math", "Json", "Unicode", "Name", "Paths", "Config"])
print(root)
