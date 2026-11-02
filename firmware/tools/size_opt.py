# Keeps the firmware inside its 1.9 MB update slot: everything is built for
# size (-Os) except LVGL's drawing code (src/draw, src/misc for memcpy/area
# helpers), which stays at -O2 so the screen stays fast. LVGL's widgets,
# layouts and event handling run rarely and are built for size too.
# (LovyanGFX only runs touch and the backlight now.) Used by platformio.ini.
Import("env")

FAST = ("lvgl/src/draw/", "lvgl/src/misc/")


def small(env, node):
    path = node.get_path().replace("\\", "/")
    if any(name in path for name in FAST):
        return node
    flags = ["-Os" if f == "-O2" else f for f in env["CCFLAGS"]]
    # One node, not a list: other build middleware (ESP-IDF's) expects that.
    return env.Object(node, CCFLAGS=flags)[0]


env.AddBuildMiddleware(small)
