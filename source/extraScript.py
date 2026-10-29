import os
import re
import sys

Import("env")

_LAYOUT_NAME = re.compile(r"^[a-z][a-z0-9_]*$")
_SDKCONFIG_DEFAULTS = re.compile(r'-DSDKCONFIG_DEFAULTS="([^"]*)"')


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


# KEY -> VAL of an sdkconfig file; {} when it does not exist
def _read_sdkconfig(path):
    if not os.path.isfile(path):
        return {}
    with open(path) as f:
        lines = [line.strip() for line in f]
    return dict(line.split("=", 1) for line in lines if line and not line.startswith("#") and "=" in line)


# "KEY=want (is have)" for every wanted value the generated sdkconfig lacks
# or sets differently; [] when it does not exist
def _stale_values(want, generated_path):
    have = _read_sdkconfig(generated_path)
    if not have:
        return []
    return ["%s=%s (is %s)" % (k, v, have.get(k, "unset")) for k, v in sorted(want.items()) if have.get(k) != v]


# A layout row may come with sdkconfig.defaults.<name> (the task stacks that
# make its block sizes fit): appended to the build's SDKCONFIG_DEFAULTS. Keyed
# on the layout, not the mode, so azdelivery-max and the perf envs get the
# same memory map as the swept local build.
def _sdkconfig_defaults(env):
    name = env.GetProjectOption("custom_arena_layout", "")
    extra = "sdkconfig.defaults." + name
    project = env.subst("$PROJECT_DIR")
    own = _read_sdkconfig(os.path.join(project, extra))

    # A layout without its own file wants the base values of every key
    # another layout's file sets: an env moved off esp32_local keeps its
    # cut stacks otherwise
    want = own
    if not own:
        base = _read_sdkconfig(os.path.join(project, "sdkconfig.defaults"))
        layouts = [k for k, _ in env.GetProjectConfig().items("arena")]
        keys = {k for layout in layouts for k in _read_sdkconfig(os.path.join(project, "sdkconfig.defaults." + layout))}
        want = {k: base[k] for k in keys if k in base}

    # IDF applies the defaults only when it creates sdkconfig.<env>; a
    # generated file from before the layout file (a pull, an edit) would
    # keep the old stacks and silently eat the Arena's margin
    generated = "sdkconfig." + env.subst("$PIOENV")
    stale = _stale_values(want, os.path.join(project, generated))
    if stale:
        sys.exit("extraScript: %s predates %s (%s): delete source/%s and build again"
                 % (generated, extra if own else "sdkconfig.defaults", ", ".join(stale), generated))
    if not own:
        return

    board = env.BoardConfig()
    args = board.get("build.cmake_extra_args", "")
    m = _SDKCONFIG_DEFAULTS.search(args)
    files = m.group(1).split(";") if m else ["sdkconfig.defaults"]
    option = '-DSDKCONFIG_DEFAULTS="%s"' % ";".join(files + [extra])
    board.update("build.cmake_extra_args",
                 args[:m.start()] + option + args[m.end():] if m else (args + " " + option).strip())


# firmware file name; the native test runner looks for the default program name
if env.get("PIOPLATFORM") != "native":
    env.Replace(PROGNAME="%s_%s_%s" % (env.GetProjectOption("custom_prog_name"), env.GetProjectOption("custom_prog_version"), env.GetBuildType()))
env.Append(CPPDEFINES=[("PROG_NAME", env.StringifyMacro(env.GetProjectOption("custom_prog_name")))])
env.Append(CPPDEFINES=[("PROG_VERSION", env.StringifyMacro(env.GetProjectOption("custom_prog_version")))])
env.Append(CPPDEFINES=_arena_defines(env))
if env.get("PIOPLATFORM") != "native":
    _sdkconfig_defaults(env)
