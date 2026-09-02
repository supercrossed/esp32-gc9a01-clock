# Build-time patch: TFT_eSPI 2.5.43 on ESP32-C3.
#
# The library already carries the right fix, but guards it behind #ifndef:
#
#     #ifndef REG_SPI_BASE
#       #define REG_SPI_BASE(i) DR_REG_SPI2_BASE
#     #endif
#
# That guard was written when the IDF did not define REG_SPI_BASE at all.
# Current IDF does define it, as
#
#     REG_SPI_BASE(i) = ((i)==2) ? DR_REG_SPI2_BASE : 0
#
# so the guard skips the library's fix. TFT_eSPI then sets SPI_PORT to
# SPI2_HOST - which is an *enum* whose value is 1, not 2 - and every SPI
# register write resolves to address 0x00000000. Nothing is ever clocked out:
# the backlight is on and the panel stays black. It is the same failure as the
# ESP32-S3 default, reached from the opposite direction, and it cannot be
# fixed from platformio.ini because SPI_PORT is #defined unconditionally.
#
# The C3 has exactly one general-purpose SPI (SOC_SPI_PERIPH_NUM 2 = the flash
# bus plus SPI2), so mapping any index to SPI2's base is correct. This makes
# the library's own fix unconditional.
#
# Idempotent, and only touches the C3 processor header.
Import("env")  # noqa: F821
import os

OLD = """  #ifndef REG_SPI_BASE
    #define REG_SPI_BASE(i) DR_REG_SPI2_BASE
  #endif"""

NEW = """  // PATCHED by scripts/patch_tft_c3.py - see that file for the reasoning.
  // Was #ifndef-guarded, but the IDF defines REG_SPI_BASE as
  //   ((i)==2) ? DR_REG_SPI2_BASE : 0
  // while TFT_eSPI's SPI_PORT is SPI2_HOST, whose enum value is 1 - so the
  // guarded form resolved every register write to address 0.
  #undef REG_SPI_BASE
  #define REG_SPI_BASE(i) DR_REG_SPI2_BASE"""

path = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"),  # noqa: F821
                    "TFT_eSPI", "Processors", "TFT_eSPI_ESP32_C3.h")

if not os.path.isfile(path):
    print("patch_tft_c3: TFT_eSPI not present yet, skipping (re-run the build)")
else:
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        src = f.read()
    if "PATCHED by scripts/patch_tft_c3.py" in src:
        print("patch_tft_c3: already applied")
    elif OLD in src:
        with open(path, "w", encoding="utf-8") as f:
            f.write(src.replace(OLD, NEW))
        print("patch_tft_c3: applied REG_SPI_BASE fix")
    else:
        print("patch_tft_c3: WARNING - expected block not found; "
              "TFT_eSPI may have changed, check the C3 SPI setup by hand")
