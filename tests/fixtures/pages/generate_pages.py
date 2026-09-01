"""Deterministic synthetic manga page generator for integration tests.

Generates 3 synthetic 800x1200 manga pages using Pillow with fixed seeds:
- page_001.png: 3-panel layout with speech bubbles, vertical/horizontal text placeholder bars, and screentone texture.
- page_002.png: 4-panel dynamic layout with dialogue boxes and hatch shading.
- page_003.png: 2-panel dramatic layout with large text regions and background patterns.

All pages are completely synthetic (geometric shapes, black ink borders, grey placeholder bars,
and deterministic stippling). No external or copyrighted manga materials are used.

Usage:
    python tests/fixtures/pages/generate_pages.py
"""

from pathlib import Path
import random
from PIL import Image, ImageDraw

OUT_DIR = Path(__file__).parent
PAGE_WIDTH = 800
PAGE_HEIGHT = 1200


def draw_screentone_block(draw: ImageDraw.ImageDraw, box: tuple[int, int, int, int], step: int = 6, dot_radius: int = 1) -> None:
    x0, y0, x1, y1 = box
    for y in range(y0, y1, step):
        for x in range(x0, x1, step):
            draw.ellipse((x - dot_radius, y - dot_radius, x + dot_radius, y + dot_radius), fill=(180, 180, 180))


def draw_text_placeholder_lines(
    draw: ImageDraw.ImageDraw,
    box: tuple[int, int, int, int],
    vertical: bool = True,
    line_count: int = 3,
    line_thickness: int = 6,
    color: tuple[int, int, int] = (100, 100, 100),
) -> None:
    x0, y0, x1, y1 = box
    if vertical:
        width = x1 - x0
        spacing = width // (line_count + 1)
        for i in range(1, line_count + 1):
            lx = x1 - i * spacing
            draw.line([(lx, y0 + 10), (lx, y1 - 10)], fill=color, width=line_thickness)
    else:
        height = y1 - y0
        spacing = height // (line_count + 1)
        for i in range(1, line_count + 1):
            ly = y0 + i * spacing
            draw.line([(x0 + 10, ly), (x1 - 10, ly)], fill=color, width=line_thickness)


def draw_speech_bubble(
    draw: ImageDraw.ImageDraw,
    box: tuple[int, int, int, int],
    vertical_text: bool = True,
    line_count: int = 3,
) -> None:
    # White rounded rectangle with black border
    draw.rounded_rectangle(box, radius=16, fill=(255, 255, 255), outline=(0, 0, 0), width=3)
    # Inner text placeholder bars
    x0, y0, x1, y1 = box
    inner_box = (x0 + 8, y0 + 8, x1 - 8, y1 - 8)
    draw_text_placeholder_lines(draw, inner_box, vertical=vertical_text, line_count=line_count)


def generate_page_1() -> Image.Image:
    rng = random.Random(1001)
    img = Image.new("RGB", (PAGE_WIDTH, PAGE_HEIGHT), (255, 255, 255))
    draw = ImageDraw.Draw(img)

    # Outer border / margins
    margin = 40
    # Panel 1: Top horizontal panel
    p1 = (margin, margin, PAGE_WIDTH - margin, 420)
    draw.rectangle(p1, outline=(0, 0, 0), width=4, fill=(255, 255, 255))
    draw_screentone_block(draw, (p1[0] + 10, p1[1] + 10, p1[0] + 250, p1[3] - 10), step=8)
    # Panel 1 speech bubbles
    draw_speech_bubble(draw, (PAGE_WIDTH - margin - 160, 60, PAGE_WIDTH - margin - 40, 240), vertical_text=True, line_count=3)
    draw_speech_bubble(draw, (p1[0] + 50, 200, p1[0] + 220, 380), vertical_text=True, line_count=4)

    # Panel 2: Bottom-left vertical panel
    p2 = (margin, 440, 380, PAGE_HEIGHT - margin)
    draw.rectangle(p2, outline=(0, 0, 0), width=4, fill=(255, 255, 255))
    draw_screentone_block(draw, (p2[0] + 20, p2[1] + 200, p2[2] - 20, p2[3] - 40), step=10)
    draw_speech_bubble(draw, (p2[0] + 40, p2[1] + 40, p2[2] - 40, p2[1] + 220), vertical_text=True, line_count=3)

    # Panel 3: Bottom-right panel
    p3 = (400, 440, PAGE_WIDTH - margin, PAGE_HEIGHT - margin)
    draw.rectangle(p3, outline=(0, 0, 0), width=4, fill=(255, 255, 255))
    draw_screentone_block(draw, (p3[0] + 20, p3[1] + 20, p3[2] - 180, p3[3] - 20), step=6)
    draw_speech_bubble(draw, (p3[2] - 170, p3[1] + 60, p3[2] - 30, p3[1] + 320), vertical_text=True, line_count=4)
    draw_speech_bubble(draw, (p3[0] + 50, p3[3] - 260, p3[0] + 230, p3[3] - 60), vertical_text=False, line_count=4)

    # Add deterministic noise accents
    for _ in range(30):
        nx = rng.randint(margin + 5, PAGE_WIDTH - margin - 5)
        ny = rng.randint(margin + 5, PAGE_HEIGHT - margin - 5)
        draw.point((nx, ny), fill=(50, 50, 50))

    return img


def generate_page_2() -> Image.Image:
    rng = random.Random(1002)
    img = Image.new("RGB", (PAGE_WIDTH, PAGE_HEIGHT), (255, 255, 255))
    draw = ImageDraw.Draw(img)

    margin = 40
    # 4-panel grid layout (2 x 2)
    mid_x = 390
    mid_y = 600

    # Panel 1: Top-Right (manga order: TR, TL, BR, BL)
    p_tr = (mid_x + 10, margin, PAGE_WIDTH - margin, mid_y - 10)
    draw.rectangle(p_tr, outline=(0, 0, 0), width=4, fill=(255, 255, 255))
    draw_screentone_block(draw, (p_tr[0] + 10, p_tr[1] + 10, p_tr[2] - 150, p_tr[3] - 10), step=7)
    draw_speech_bubble(draw, (p_tr[2] - 140, p_tr[1] + 40, p_tr[2] - 30, p_tr[1] + 280), vertical_text=True, line_count=3)

    # Panel 2: Top-Left
    p_tl = (margin, margin, mid_x - 10, mid_y - 10)
    draw.rectangle(p_tl, outline=(0, 0, 0), width=4, fill=(255, 255, 255))
    draw_speech_bubble(draw, (p_tl[0] + 30, p_tl[1] + 50, p_tl[0] + 170, p_tl[1] + 260), vertical_text=True, line_count=3)
    draw_speech_bubble(draw, (p_tl[2] - 150, p_tl[3] - 220, p_tl[2] - 30, p_tl[3] - 40), vertical_text=False, line_count=3)

    # Panel 3: Bottom-Right
    p_br = (mid_x + 10, mid_y + 10, PAGE_WIDTH - margin, PAGE_HEIGHT - margin)
    draw.rectangle(p_br, outline=(0, 0, 0), width=4, fill=(255, 255, 255))
    draw_screentone_block(draw, (p_br[0] + 20, p_br[1] + 20, p_br[2] - 20, p_br[1] + 200), step=8)
    draw_speech_bubble(draw, (p_br[0] + 40, p_br[3] - 280, p_br[0] + 180, p_br[3] - 50), vertical_text=True, line_count=3)

    # Panel 4: Bottom-Left
    p_bl = (margin, mid_y + 10, mid_x - 10, PAGE_HEIGHT - margin)
    draw.rectangle(p_bl, outline=(0, 0, 0), width=4, fill=(255, 255, 255))
    draw_screentone_block(draw, (p_bl[0] + 150, p_bl[1] + 30, p_bl[2] - 20, p_bl[3] - 30), step=9)
    draw_speech_bubble(draw, (p_bl[0] + 30, p_bl[1] + 60, p_bl[0] + 150, p_bl[1] + 320), vertical_text=True, line_count=3)

    # Deterministic noise accents
    for _ in range(30):
        nx = rng.randint(margin + 5, PAGE_WIDTH - margin - 5)
        ny = rng.randint(margin + 5, PAGE_HEIGHT - margin - 5)
        draw.point((nx, ny), fill=(40, 40, 40))

    return img


def generate_page_3() -> Image.Image:
    rng = random.Random(1003)
    img = Image.new("RGB", (PAGE_WIDTH, PAGE_HEIGHT), (255, 255, 255))
    draw = ImageDraw.Draw(img)

    margin = 40
    # 2 large dramatic panels
    split_y = 520

    # Panel 1: Top dramatic panel
    p1 = (margin, margin, PAGE_WIDTH - margin, split_y)
    draw.rectangle(p1, outline=(0, 0, 0), width=5, fill=(255, 255, 255))
    draw_screentone_block(draw, (p1[0] + 20, p1[1] + 20, p1[2] - 20, p1[3] - 20), step=12, dot_radius=2)
    # Speech bubbles on top panel
    draw_speech_bubble(draw, (p1[2] - 180, p1[1] + 40, p1[2] - 40, p1[1] + 260), vertical_text=True, line_count=3)
    draw_speech_bubble(draw, (p1[0] + 40, p1[1] + 60, p1[0] + 220, p1[1] + 280), vertical_text=True, line_count=4)

    # Panel 2: Bottom large splash panel
    p2 = (margin, split_y + 20, PAGE_WIDTH - margin, PAGE_HEIGHT - margin)
    draw.rectangle(p2, outline=(0, 0, 0), width=5, fill=(255, 255, 255))
    draw_screentone_block(draw, (p2[0] + 40, p2[1] + 150, p2[2] - 40, p2[3] - 150), step=8)
    draw_speech_bubble(draw, (p2[2] - 220, p2[1] + 80, p2[2] - 40, p2[1] + 360), vertical_text=True, line_count=4)
    draw_speech_bubble(draw, (p2[0] + 60, p2[3] - 300, p2[0] + 260, p2[3] - 50), vertical_text=False, line_count=5)

    # Deterministic noise accents
    for _ in range(30):
        nx = rng.randint(margin + 5, PAGE_WIDTH - margin - 5)
        ny = rng.randint(margin + 5, PAGE_HEIGHT - margin - 5)
        draw.point((nx, ny), fill=(60, 60, 60))

    return img


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    pages = [
        ("page_001.png", generate_page_1()),
        ("page_002.png", generate_page_2()),
        ("page_003.png", generate_page_3()),
    ]

    for name, img in pages:
        out_path = OUT_DIR / name
        img.save(out_path, format="PNG")
        print(f"Generated {name}: {img.size[0]}x{img.size[1]} ({out_path.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
