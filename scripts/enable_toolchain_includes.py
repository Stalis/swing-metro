Import("env")

from pathlib import Path
from SCons.Script import COMMAND_LINE_TARGETS
import subprocess
import sys

# Force inclusion of toolchain paths in the compilation database.
env.Replace(COMPILATIONDB_INCLUDE_TOOLCHAIN=True)

if "compiledb" not in COMMAND_LINE_TARGETS and "clean" not in COMMAND_LINE_TARGETS:
    project_dir = Path(env.subst("$PROJECT_DIR"))
    generator = project_dir / "scripts" / "generate_template_instantiations.py"
    result = subprocess.run([sys.executable, str(generator), str(project_dir)], check=False)
    if result.returncode:
        env.Exit(result.returncode)
