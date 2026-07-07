# force_tempfile.py -- Windows command-line-length workaround.
#
# Windows CreateProcess caps a command line at 32767 chars. The heltec_v4 TFT
# touch build (MeshCore core + LVGL + ~30 libs + ~150 -D flags) produces a
# ~31.9 KB g++ command line; g++ then spawns cc1plus with the full compiler
# path + those flags, tipping it over 32767 -> CreateProcess fails with the
# misleading "No such file or directory". Routing the compile/assemble commands
# through @response-files keeps the args off the command line.
#
# No-op on non-Windows (the arg limit there is ~2 MB, so the native build is
# unaffected). Safe to upstream as a Windows-compat fix.
import sys

Import("env")  # noqa: F821  (injected by PlatformIO/SCons)

if sys.platform == "win32":
    env.Replace(MAXLINELENGTH=8192)
    for _name in ("CCCOM", "CXXCOM", "ASCOM", "ASPPCOM"):
        _cmd = env.get(_name)
        if _cmd and "TEMPFILE" not in _cmd:
            env[_name] = "${TEMPFILE('%s','$%sSTR')}" % (_cmd, _name)
    print("[force_tempfile] @response-file wrapping enabled for compile/assemble (win32)")
