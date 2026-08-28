"""Generates the checked-in binary image fixtures used by the data layer tests.

QImage cannot produce images with embedded metadata (EXIF, ICC, CMYK JPEG, animated WEBP),
so the baselines are generated once with Pillow and reviewed as test baselines.

Run:  python tests/unit/data/fixtures/generate_fixtures.py
"""

from pathlib import Path

from PIL import Image, ImageCms

OUT = Path(__file__).parent


def save_rgb(name: str, size, fmt: str, **kwargs) -> Image.Image:
    img = Image.new("RGB", size, (255, 0, 0))
    for y in range(size[1]):
        for x in range(size[0]):
            img.putpixel((x, y), (x * 7 % 256, y * 11 % 256, (x + y) * 5 % 256))
    img.save(OUT / name, fmt, **kwargs)
    return img


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)

    # Plain whitelisted formats for happy-path imports.
    save_rgb("sample.png", (3, 2), "PNG")
    save_rgb("sample.jpg", (3, 2), "JPEG", quality=80)
    save_rgb("sample.webp", (3, 2), "WEBP", lossless=True)

    # Transparent PNG to exercise straight-alpha preservation.
    rgba = Image.new("RGBA", (2, 2), (10, 20, 30, 128))
    rgba.putpixel((0, 0), (255, 255, 255, 0))
    rgba.save(OUT / "transparent.png", "PNG")

    # EXIF orientation 6 (rotate 90 degrees on load); decoded size is height x width.
    exif = Image.Exif()
    exif[274] = 6
    save_rgb("exif-orientation.jpg", (2, 1), "JPEG", quality=80, exif=exif)

    # PNG with an embedded ICC profile.
    icc = ImageCms.ImageCmsProfile(ImageCms.createProfile("sRGB")).tobytes()
    save_rgb("icc-profile.png", (2, 2), "PNG", icc_profile=icc)

    # 16-bit grayscale PNG (high bit depth).
    Image.new("I;16", (2, 2), 4000).save(OUT / "gray16.png", "PNG")

    # Animated WEBP (two frames); import must keep the first frame.
    first = Image.new("RGB", (2, 2), (255, 0, 0))
    second = Image.new("RGB", (2, 2), (0, 0, 255))
    first.save(OUT / "animated.webp", "WEBP", save_all=True, append_images=[second], loop=0)

    # CMYK JPEG.
    cmyk = Image.new("CMYK", (2, 2), (0, 255, 0, 0))
    cmyk.save(OUT / "cmyk.jpg", "JPEG", quality=80)

    # PNG bytes named .jpg: content must be accepted with an extension warning.
    import io

    buffer = io.BytesIO()
    Image.new("RGB", (3, 2), (0, 255, 0)).save(buffer, "PNG")
    (OUT / "disguised.jpg").write_bytes(buffer.getvalue())

    # Text content named .png: content must be rejected even though the extension fits.
    (OUT / "not-an-image.png").write_bytes(b"this is definitely not a PNG image\n")

    # Folder expansion fixture: natural order must be page1, page2, page10.
    pages = OUT / "pages"
    pages.mkdir(exist_ok=True)
    Image.new("RGB", (1, 1), (0, 0, 0)).save(pages / "page1.png", "PNG")
    Image.new("RGB", (1, 1), (0, 0, 255)).save(pages / "page2.png", "PNG")
    Image.new("RGB", (1, 1), (0, 255, 0)).save(pages / "page10.png", "PNG")

    # Header-limit fixtures: 16385 px wide (side limit) and above the RGBA8 memory budget.
    Image.new("RGB", (16385, 1), (0, 0, 0)).save(OUT / "too-wide.png", "PNG")
    Image.new("RGB", (8193, 8192), (0, 0, 0)).save(OUT / "too-many-pixels.png", "PNG")

    for path in sorted(OUT.iterdir()):
        if path.is_file():
            print(f"{path.name}: {path.stat().st_size} bytes")


if __name__ == "__main__":
    main()
