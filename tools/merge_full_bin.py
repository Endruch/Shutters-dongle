Import("env")

from os.path import join


def merge(source, target, env):
    images = []
    for offset, path in env.get("FLASH_EXTRA_IMAGES", []):
        images += [offset, '"%s"' % env.subst(path)]
    images += [env.subst("$ESP32_APP_OFFSET"), '"%s"' % target[0].get_abspath()]
    esptool = join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
    out = join(env.subst("$BUILD_DIR"), "firmware-full.bin")
    env.Execute(
        " ".join(
            [
                '"$PYTHONEXE"',
                '"%s"' % esptool,
                "--chip",
                env.BoardConfig().get("build.mcu", "esp32"),
                "merge_bin",
                "-o",
                '"%s"' % out,
                "--flash_mode",
                "${__get_board_flash_mode(__env__)}",
                "--flash_freq",
                "${__get_board_f_image(__env__)}",
                "--flash_size",
                env.BoardConfig().get("upload.flash_size", "4MB"),
            ]
            + images
        )
    )


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge)
