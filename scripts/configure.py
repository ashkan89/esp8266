"""Create an editable, ignored configuration without copying YAML passwords."""
Import("env")
from pathlib import Path
root = Path(env.subst("$PROJECT_DIR"))
target = root / "include" / "secrets.h"
if not target.exists():
    target.write_text((root / "include" / "secrets.example.h").read_text(), encoding="utf-8")
    print("Created include/secrets.h; configure credentials before flashing.")

