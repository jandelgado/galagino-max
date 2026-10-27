import re
import sys

Import("env")

_LAYOUT_NAME = re.compile(r"^[a-z][a-z0-9_]*$")


# custom_arena_layout selects a row of [arena] (A B bytes): the Arena block
# sizes the firmware builds with and romconv plans against
def _arena_defines(env):
    config = env.GetProjectConfig()
    name = env.GetProjectOption("custom_arena_layout", "")
    known = ", ".join(k for k, _ in config.items("arena")) if config.has_section("arena") else ""
    if not _LAYOUT_NAME.match(name) or not config.has_option("arena", name):
        sys.exit("extraScript: custom_arena_layout '%s' not in [arena]: %s" % (name, known))

    row = config.get("arena", name)
    tokens = row.split()
    sizes = [int(t) for t in tokens] if len(tokens) == 2 and all(t.isdigit() for t in tokens) else None
    if sizes is None or sizes[0] <= 0:
        sys.exit("extraScript: [arena] %s = %s: expected 'A B' bytes, A > 0, B >= 0" % (name, row))

    # a _remote layout plans for the smaller boot heap of an ASSETS_REMOTE
    # build and gets bundles; any other pairing builds something that
    # cannot run or cannot download
    flags = env.GetProjectOption("build_flags", [])
    text = flags if isinstance(flags, str) else " ".join(flags)
    remote = "ASSETS_REMOTE" in text
    if name.endswith("_remote") != remote:
        sys.exit("extraScript: layout '%s' is for %s builds but ASSETS_REMOTE is %s"
                 % (name, "remote" if name.endswith("_remote") else "local", "set" if remote else "not set"))

    return ["ARENA_LAYOUT_" + name.upper(), ("ARENA_A_SIZE", sizes[0]), ("ARENA_B_SIZE", sizes[1])]


# firmware file name; the native test runner looks for the default program name
if env.get("PIOPLATFORM") != "native":
    env.Replace(PROGNAME="%s_%s_%s" % (env.GetProjectOption("custom_prog_name"), env.GetProjectOption("custom_prog_version"), env.GetBuildType()))
env.Append(CPPDEFINES=[("PROG_NAME", env.StringifyMacro(env.GetProjectOption("custom_prog_name")))])
env.Append(CPPDEFINES=[("PROG_VERSION", env.StringifyMacro(env.GetProjectOption("custom_prog_version")))])
env.Append(CPPDEFINES=_arena_defines(env))
