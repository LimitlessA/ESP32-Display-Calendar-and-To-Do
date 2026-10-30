# Keeps the firmware inside its 1.9 MB update slot: everything is built for
# size (-Os) except the drawing code, LVGL and LovyanGFX, which stay at -O2 so
# the screen stays fast. Used by platformio.ini (extra_scripts).
Import("env")

FAST = ("lvgl", "LovyanGFX")


def small(env, node):
    if any(name in node.get_path() for name in FAST):
        return node
    flags = ["-Os" if f == "-O2" else f for f in env["CCFLAGS"]]
    return env.Object(node, CCFLAGS=flags)


env.AddBuildMiddleware(small)
