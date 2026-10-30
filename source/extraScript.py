import os
import re
import sys

Import("env")

_SDKCONFIG_DEFAULTS = re.compile(r'-DSDKCONFIG_DEFAULTS="([^"]*)"')

sys.path.insert(0, os.path.join(env.subst("$PROJECT_DIR"), "tools"))
import firmware_config  # noqa: E402


def _config():
    try:
        return firmware_config.load()
    except firmware_config.ConfigError as e:
        sys.exit("extraScript: %s" % e)


# firmware.toml: the [envs] row of this env (platform, board, mode, layout)
# as defines; GAMES and DEFINES from the environment for dev builds
def _config_defines(env, cfg):
    name = env.subst("$PIOENV")
    games, extra = firmware_config.dev_overrides(os.environ)
    try:
        defines = firmware_config.env_defines(cfg, name, games, extra)
    except firmware_config.ConfigError as e:
        sys.exit("extraScript: %s (env %s)" % (e, name))

    return [tuple(d.split("=", 1)) if "=" in d else d for d in defines]


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
# on the layout, not the mode, so azdelivery-max and dev builds (GAMES=) get the
# same memory map as the swept local build.
def _sdkconfig_defaults(env, cfg):
    name = firmware_config.env(cfg, env.subst("$PIOENV"))["layout"]
    extra = "sdkconfig.defaults." + name
    project = env.subst("$PROJECT_DIR")
    own = _read_sdkconfig(os.path.join(project, extra))

    # A layout without its own file wants the base values of every key
    # another layout's file sets: an env moved off esp32_local keeps its
    # cut stacks otherwise
    want = own
    if not own:
        base = _read_sdkconfig(os.path.join(project, "sdkconfig.defaults"))
        layouts = list(cfg["arena"])
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
_cfg = _config()
env.Append(CPPDEFINES=_config_defines(env, _cfg))
if env.get("PIOPLATFORM") != "native":
    _sdkconfig_defaults(env, _cfg)
