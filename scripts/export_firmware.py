# Post-build hook: drop a ready-to-flash image into firmware/<board>/<face>.bin
#
# The faces live in exactly one place (src/faces/) and are compiled for both
# chips, so a face can never drift between boards. What IS per-board is the
# binary, and that is what lands here - one merged image per face per chip,
# flashable in a single step at offset 0.
#
# Wired in from [base] in platformio.ini, so any `pio run` keeps the folders
# current; there is nothing to remember to run by hand.
Import("env")  # noqa: F821
import os
import sys
import json
import subprocess
import datetime

BOARDS = {"s3": "esp32s3", "c3": "esp32c3"}


def split_env(pioenv):
    """c3_casio -> ('c3', 'casio');  casio -> ('s3', 'casio')"""
    for prefix, _ in BOARDS.items():
        if pioenv.startswith(prefix + "_"):
            return prefix, pioenv[len(prefix) + 1:]
    return "s3", pioenv


def after_build(source, target, env):
    pioenv = env.subst("$PIOENV")
    board, face = split_env(pioenv)
    chip = BOARDS[board]

    build_dir = env.subst("$BUILD_DIR")
    proj_dir = env.subst("$PROJECT_DIR")
    out_dir = os.path.join(proj_dir, "firmware", board)
    os.makedirs(out_dir, exist_ok=True)

    platform = env.PioPlatform()
    esptool = os.path.join(platform.get_package_dir("tool-esptoolpy"), "esptool.py")
    boot_app0 = os.path.join(platform.get_package_dir("framework-arduinoespressif32"),
                             "tools", "partitions", "boot_app0.bin")

    app = os.path.join(build_dir, "firmware.bin")
    parts = [
        ("0x0",     os.path.join(build_dir, "bootloader.bin")),
        ("0x8000",  os.path.join(build_dir, "partitions.bin")),
        ("0xe000",  boot_app0),
        ("0x10000", app),
    ]
    for _, path in parts:
        if not os.path.isfile(path):
            print("export_firmware: missing %s, skipping" % path)
            return

    out = os.path.join(out_dir, face + ".bin")
    cmd = [sys.executable, esptool, "--chip", chip, "merge_bin", "-o", out,
           "--flash_mode", "dio", "--flash_freq", "80m", "--flash_size", "4MB"]
    for addr, path in parts:
        cmd += [addr, path]

    try:
        subprocess.run(cmd, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
        print("export_firmware: merge_bin failed (%s)" % e)
        return

    # Keep a small manifest per board so it is obvious what each image is and
    # when it was built, without having to read the binary.
    man_path = os.path.join(out_dir, "manifest.json")
    man = {}
    if os.path.isfile(man_path):
        try:
            with open(man_path) as f:
                man = json.load(f)
        except ValueError:
            man = {}
    man.setdefault("board", board)
    man.setdefault("chip", chip)
    man["flash_offset"] = "0x0"
    man.setdefault("images", {})[face] = {
        "file": face + ".bin",
        "bytes": os.path.getsize(out),
        "app_bytes": os.path.getsize(app),
        "built": datetime.datetime.now().isoformat(timespec="seconds"),
    }
    with open(man_path, "w") as f:
        json.dump(man, f, indent=2, sort_keys=True)
        f.write("\n")

    print("export_firmware: firmware/%s/%s.bin  (%.0f KB)"
          % (board, face, os.path.getsize(out) / 1024.0))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", after_build)  # noqa: F821
