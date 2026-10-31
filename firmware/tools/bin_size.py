# Fails the build if firmware.bin is larger than one update slot. PlatformIO's
# own check adds up the ELF sections, which misses the padding ESP-IDF puts
# between segments when the program runs from PSRAM; an image that does not
# fit leaves the display unable to boot.
Import("env")

import os


def check(source, target, env):
    size = os.path.getsize(target[0].get_abspath())
    limit = int(env.BoardConfig().get("upload.maximum_size"))
    print("firmware.bin: %d of %d bytes (%d free)" % (size, limit, limit - size))
    if size > limit:
        env.Exit(1)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check)
