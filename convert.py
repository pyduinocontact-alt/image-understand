from PIL import Image
import sys
import os

def png_to_ico(input_path, output_path=None, sizes=(16, 32, 48, 64, 128, 256)):
    if not os.path.exists(input_path):
        print("❌ File not found:", input_path)
        return

    if output_path is None:
        output_path = os.path.splitext(input_path)[0] + ".ico"

    img = Image.open(input_path).convert("RGBA")

    # Generate multiple sizes
    icon_sizes = [(size, size) for size in sizes]

    img.save(output_path, format='ICO', sizes=icon_sizes)

    print(f"✅ ICO file created: {output_path}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python png_to_ico.py input.png [output.ico]")
    else:
        input_file = sys.argv[1]
        output_file = sys.argv[2] if len(sys.argv) > 2 else None
        png_to_ico(input_file, output_file)
