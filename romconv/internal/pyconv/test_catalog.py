#!/usr/bin/env python3
# Checks catalog.py: games, bundle dirs per game, generated catalog.h text.
import re
from catalog import GAMES, CATALOG, EXCLUDED, BASES, bundle_dirs, catalog_h

# 1. catalog: every machine game except the broken ones
assert len(GAMES) == 110, len(GAMES)
assert "motorace" in GAMES and "motorace" not in CATALOG
assert CATALOG == GAMES - EXCLUDED and "1943" in CATALOG and "gng" in CATALOG
assert all(b in GAMES for bases in BASES.values() for b in bases)
print("1. catalog: OK")

# 2. bundle dirs: own dir, then base dirs whose assets the class constructs
assert bundle_dirs("amidar") == ["amidar", "turtles", "scramble"]
assert bundle_dirs("mspacman") == ["mspacman", "pacman"]
assert bundle_dirs("gng") == ["gng"]
print("2. bundle_dirs: OK")

# 3. catalog.h: GAME_ compiled in, BUILTIN_ assets in the app
text = catalog_h()
assert "#if defined(ENABLE_GNG) || defined(ASSETS_REMOTE)\n#define GAME_GNG\n#endif" in text
assert "#if defined(ENABLE_MOTORACE)\n#define GAME_MOTORACE\n#endif" in text
m = re.search(r"#if (.*)\n#define BUILTIN_PACMAN\n", text)
for g in ("pacman", "mspacman", "pengo", "alibaba", "crush", "eyes", "lizwiz", "mrtnt", "theglob"):
    assert f"defined(ENABLE_{g.upper()})" in m.group(1), (g, m.group(1))
assert "#if defined(ENABLE_GNG)\n#define BUILTIN_GNG\n#endif" in text
m = re.search(r"#if (.*)\n#define BUILTIN_SCRAMBLE\n", text)
assert "defined(ENABLE_AMIDAR)" in m.group(1) and "defined(ENABLE_SUPERCOBRA)" in m.group(1)
assert "#define BUILTIN_NAMCO54\n" in text and "#define BUILTIN_WILLIAMS\n" in text
print("3. catalog.h text: OK")
