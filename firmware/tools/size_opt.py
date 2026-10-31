# Keeps the firmware inside its 1.9 MB update slot: everything is built for
# size (-Os) except LVGL, which stays at -O2 so the screen stays fast.
# (LovyanGFX only runs touch and the backlight now.) Used by platformio.ini.
Import("env")

FAST = ("lvgl",)


def small(env, node):
    if any(name in node.get_path() for name in FAST):
        return node
    flags = ["-Os" if f == "-O2" else f for f in env["CCFLAGS"]]
    # One node, not a list: other build middleware (ESP-IDF's) expects that.
    return env.Object(node, CCFLAGS=flags)[0]


env.AddBuildMiddleware(small)
