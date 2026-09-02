"""Parse TFT_eSPI's font data out of the library's own C sources.

Parsing rather than hand-transcribing means the glyphs in the rendered images
are byte-identical to the ones the watch draws. Only the drawing primitives
are reimplemented in Python; the type itself is the real thing.
"""
import re
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def _clean(text):
    """Strip comments and resolve the #ifdef branches inside the arrays.

    Both matter. The trailing `// char 32 - 39` comments would otherwise be
    parsed as data, and the width tables carry an #ifdef whose two branches
    would both be read, giving 104 values instead of 96. TFT_eSPI builds with
    TFT_ESPI_GRAVE_IS_DEGREE and TFT_ESPI_FONT2_DOLLAR defined, so those are
    the branches to keep.
    """
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    kept, skip_stack = [], []
    for line in text.splitlines():
        st = line.strip()
        if st.startswith("#ifdef") or st.startswith("#ifndef"):
            defined = ("GRAVE_IS_DEGREE" in st) or ("DOLLAR" in st)
            skip_stack.append(not defined if st.startswith("#ifdef") else defined)
            continue
        if st.startswith("#else"):
            if skip_stack:
                skip_stack[-1] = not skip_stack[-1]
            continue
        if st.startswith("#endif"):
            if skip_stack:
                skip_stack.pop()
            continue
        if st.startswith("#"):
            continue
        if not any(skip_stack):
            kept.append(re.sub(r"//.*$", "", line))
    return "\n".join(kept)


def _nums(block):
    return [int(x, 16) if x.lower().startswith("0x") else int(x)
            for x in re.findall(r"0x[0-9A-Fa-f]+|\b\d+\b", block)]


def _load(cfile, hfile, wname, stem, hname):
    src = _clean(open(os.path.join(HERE, cfile)).read())
    hdr = open(os.path.join(HERE, hfile)).read()

    height = int(re.search(rf"#define\s+{hname}\s+(\d+)", hdr).group(1))

    m = re.search(rf"{wname}\s*\[\s*96\s*\]\s*=\s*\{{(.*?)\}}\s*;", src, re.S)
    widths = _nums(m.group(1))
    assert len(widths) == 96, f"{wname}: parsed {len(widths)} widths, want 96"

    # Glyph arrays are named chr_f16_20 / chr_f32_20 - the suffix is the
    # character code in hex with no 0x prefix.
    glyphs = {}
    for code_hex, body in re.findall(
            rf"{stem}_([0-9A-Fa-f]{{2}})\s*\[[^\]]*\]\s*=\s*\{{(.*?)\}}\s*;", src, re.S):
        glyphs[int(code_hex, 16)] = _nums(body)

    chars = [glyphs.get(32 + i, []) for i in range(96)]
    return {"height": height, "widths": widths, "chars": chars,
            "missing": [32 + i for i, c in enumerate(chars) if not c]}


def font16():
    return _load("Font16.c", "Font16.h", "widtbl_f16", "chr_f16", "chr_hgt_f16")


def font32():
    return _load("Font32rle.c", "Font32rle.h", "widtbl_f32", "chr_f32", "chr_hgt_f32")


def glcd():
    src = _clean(open(os.path.join(HERE, "glcdfont.c")).read())
    m = re.search(r"\{(.*)\}", src, re.S)
    return _nums(m.group(1))[:256 * 5]


if __name__ == "__main__":
    for name, f in (("font 2", font16()), ("font 4", font32())):
        have = sum(1 for c in f["chars"] if c)
        print(f"  {name}: height {f['height']:2}, {have}/96 glyphs, "
              f"widths[0:8] {f['widths'][:8]}, missing {f['missing'] or 'none'}")
    print(f"  font 1: {len(glcd())} bytes")
