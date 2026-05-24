import io
import os
import sys

threshold = 128
USAGE = "Usage: python scripts/convert_logo.py input.png|input.svg [output_name] [width] [height]"


def svg_to_png_bytes(svg_path, width, height):
    import cairosvg

    with open(svg_path, "rb") as f:
        svg_data = f.read()
    return cairosvg.svg2png(bytestring=svg_data, output_width=width, output_height=height)


def load_image(path, width, height):
    from PIL import Image

    ext = os.path.splitext(path)[1].lower()
    if ext == ".svg":
        png_bytes = svg_to_png_bytes(path, width, height)
        img = Image.open(io.BytesIO(png_bytes))
    else:
        img = Image.open(path)
        img = img.convert("RGBA")
        img = img.resize((width, height), Image.LANCZOS)
        background = Image.new("RGBA", img.size, (255, 255, 255, 255))
        background.paste(img, mask=img.split()[3])
        img = background
    return img


def image_to_c_array(img, array_name, source_label):
    img = img.convert("L")
    width, height = img.size
    pixels = list(img.getdata())
    packed = []
    for y in range(height):
        for x in range(0, width, 8):
            byte = 0
            for b in range(8):
                if x + b < width:
                    v = pixels[y * width + x + b]
                    bit = 1 if v >= threshold else 0
                    byte |= bit << (7 - b)
            packed.append(byte)

    c = "#pragma once\n#include <cstdint>\n\n"
    c += f"// '{source_label}', {width}x{height}px\n"
    c += f"static const uint8_t {array_name}[] = {{\n"
    for i in range(0, len(packed), 16):
        chunk = ", ".join(f"0x{v:02X}" for v in packed[i:i + 16])
        suffix = "," if i + 16 < len(packed) else ""
        c += f"    {chunk}{suffix}\n"
    c += "};\n"
    c += f'\nstatic_assert(sizeof({array_name}) == {len(packed)}, "{array_name} must be exactly {width}x{height} / 8 bytes");\n'
    return c


def main():
    if any(arg in ("-h", "--help") for arg in sys.argv[1:]):
        print(USAGE)
        sys.exit(0)

    if len(sys.argv) not in (2, 3, 4, 5):
        print(USAGE)
        sys.exit(1)

    input_path = sys.argv[1]
    output_name = sys.argv[2] if len(sys.argv) >= 3 else "Logo120"
    width = int(sys.argv[3]) if len(sys.argv) >= 4 else 120
    height = int(sys.argv[4]) if len(sys.argv) >= 5 else 120

    img = load_image(input_path, width, height)
    source_label = os.path.splitext(os.path.basename(input_path))[0]
    c_array = image_to_c_array(img, output_name, source_label)

    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    output_dir = os.path.join(project_root, "src", "images")
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, f"{output_name}.h")
    with open(output_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(c_array)
    print(f"Wrote {output_path}")


if __name__ == "__main__":
    main()
