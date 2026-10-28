"""Draws Keycard's own textures for the Veldane Grand Harbourfront: CCTV wall feeds, the LP office
shift board, the front office system, the incident report and clip export screens, the fire panel,
the LP whiteboard, harbour art prints, the exit sign, the corridor carpet, the wallpaper, the kitchen quarry tile, the
do-not-disturb hanger, the typical guest floor evacuation plan and the
night skylines round the harbour.

Everything is invented (Docs/FictionalNames.md); nothing on a screen hints at a case's rubric.
Output: Data/Assets/generated/*.png, imported by Scripts/ImportAssets.py. Deterministic: the same
output on every run. Needs Pillow; uses Inter (Resources/Fonts) when present, else DejaVu Sans.
"""
import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "Data", "Assets", "generated")
FONT_DIR = os.path.join(ROOT, "Resources", "Fonts")

BG = (12, 16, 22)
PANEL = (24, 30, 39)
LINE = (46, 54, 66)
TEXT = (228, 232, 236)
MUTED = (140, 150, 162)
BLUE = (75, 156, 196)
AMBER = (227, 163, 59)
RED = (217, 83, 79)
GREEN = (92, 196, 138)
BRASS = (196, 160, 96)


def font(size, weight="Regular"):
    inter = os.path.join(FONT_DIR, f"Inter-{weight}.ttf")
    if os.path.exists(inter):
        return ImageFont.truetype(inter, size)
    bold = weight in ("SemiBold", "Bold")
    for path in ("/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf" % ("-Bold" if bold else ""),
                 "C:/Windows/Fonts/%s" % ("segoeuib.ttf" if bold else "segoeui.ttf")):
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def mono(size):
    for path in ("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", "C:/Windows/Fonts/consola.ttf"):
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return font(size)


def title_bar(d, w, title, right="Veldane LP"):
    d.rectangle([0, 0, w, 44], fill=(9, 12, 17))
    d.text((18, 11), title, font=font(19, "SemiBold"), fill=TEXT)
    d.text((w - 18 - d.textlength(right, font=font(14)), 15), right, font=font(14), fill=MUTED)


# ---------------------------------------------------------------- CCTV
def camera_scene(w, h, kind, seed):
    """A grainy night camera view in grey: corridor, lobby, bar or loading bay, drawn in perspective."""
    rnd = random.Random(seed)
    im = Image.new("L", (w, h), 28)
    d = ImageDraw.Draw(im)
    cx, cy = w * (0.5 + rnd.uniform(-0.08, 0.08)), h * 0.42
    if kind == "corridor":
        # Floor, walls and ceiling converging; doors down both sides; downlight pools.
        d.polygon([(0, h), (w, h), (cx + 40, cy + 18), (cx - 40, cy + 18)], fill=58)
        d.polygon([(0, 0), (0, h), (cx - 40, cy + 18), (cx - 40, cy - 30)], fill=44)
        d.polygon([(w, 0), (w, h), (cx + 40, cy + 18), (cx + 40, cy - 30)], fill=40)
        d.polygon([(0, 0), (w, 0), (cx + 40, cy - 30), (cx - 40, cy - 30)], fill=34)
        for i in range(6):
            t = 0.12 + i * 0.14
            for side in (-1, 1):
                x = cx + side * (40 + (w * 0.5 - 40) * (1 - t))
                top, bot = cy - 30 - (cy - 30) * (1 - t) * 0.4, cy + 18 + (h - cy - 18) * (1 - t)
                dw = 26 * (1 - t) + 4
                d.rectangle([min(x, x - side * dw), top + (bot - top) * 0.25, max(x, x - side * dw), bot], fill=70)
            r = 30 * (1 - t) + 4
            d.ellipse([cx - r * 2, cy + 18 + (h - cy) * (1 - t) * 0.9 - r, cx + r * 2, cy + 18 + (h - cy) * (1 - t) * 0.9 + r], fill=86)
    elif kind == "lobby":
        d.rectangle([0, int(h * 0.55), w, h], fill=72)
        d.rectangle([int(w * 0.1), int(h * 0.28), int(w * 0.62), int(h * 0.56)], fill=50)  # desk wall
        d.rectangle([int(w * 0.12), int(h * 0.48), int(w * 0.6), int(h * 0.6)], fill=96)  # counter
        d.ellipse([int(w * 0.7), int(h * 0.05), int(w * 0.86), int(h * 0.16)], fill=120)  # chandelier
        for x in (0.68, 0.8, 0.9):
            d.rectangle([int(w * x), int(h * 0.6), int(w * x) + 30, int(h * 0.7)], fill=60)
    elif kind == "bar":
        d.rectangle([0, int(h * 0.58), w, h], fill=60)
        for i in range(4):
            x = int(w * (0.12 + i * 0.22))
            d.ellipse([x, int(h * 0.62), x + 70, int(h * 0.7)], fill=96)
            d.rectangle([x + 32, int(h * 0.68), x + 38, int(h * 0.86)], fill=80)
        d.rectangle([int(w * 0.8), int(h * 0.3), w, int(h * 0.62)], fill=78)
    else:  # loading bay
        d.rectangle([0, int(h * 0.6), w, h], fill=64)
        d.rectangle([int(w * 0.15), int(h * 0.18), int(w * 0.6), int(h * 0.6)], fill=48)
        for y in range(int(h * 0.2), int(h * 0.58), 10):
            d.line([int(w * 0.15), y, int(w * 0.6), y], fill=56)
        d.rectangle([int(w * 0.68), int(h * 0.44), int(w * 0.84), int(h * 0.62)], fill=88)
    im = im.filter(ImageFilter.GaussianBlur(1.2))
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = max(0, min(255, px[x, y] + rnd.randint(-9, 9)))
    return im.convert("RGB")


def cctv_wall(cams, seed):
    """One wall monitor: a 2x2 grid of camera feeds with the camera id, place and clock burned in."""
    w, h = 1024, 576
    im = Image.new("RGB", (w, h), (0, 0, 0))
    d = ImageDraw.Draw(im)
    tw, th = w // 2, h // 2
    for i, (cam, place, kind, clock) in enumerate(cams):
        x, y = (i % 2) * tw, (i // 2) * th
        im.paste(camera_scene(tw - 4, th - 4, kind, seed + i), (x + 2, y + 2))
        d.rectangle([x + 2, y + 2, x + tw - 2, y + 24], fill=(0, 0, 0))
        d.text((x + 8, y + 5), f"{cam}  {place}", font=mono(14), fill=(230, 230, 230))
        d.text((x + tw - 92, y + 5), clock, font=mono(14), fill=(230, 230, 230))
        d.ellipse([x + tw - 108, y + 9, x + tw - 100, y + 17], fill=(200, 40, 40))  # recording
    return im


# ---------------------------------------------------------------- LP office and front office screens
def shift_board():
    w, h = 1024, 576
    im = Image.new("RGB", (w, h), BG)
    d = ImageDraw.Draw(im)
    title_bar(d, w, "Shift board  -  Loss Prevention, night shift")
    d.text((24, 62), "Veldane Grand Harbourfront", font=font(22, "SemiBold"), fill=TEXT)
    d.text((24, 94), "Policy every shift: observe, report and de-escalate.", font=font(16), fill=BLUE)
    rows = [("22:00", "Handover from late shift", "done"), ("23:00", "Patrol tour 1: B1, G, guest floors", "done"),
            ("00:00", "Fire panel check", "done"), ("01:00", "Street doors locked, card access only", "done"),
            ("02:00", "Patrol tour 2", "due"), ("03:00", "Lift 2 contractor inspection (escort)", "due"),
            ("05:30", "Street doors open", "due"), ("06:00", "Handover to day shift", "due")]
    d.rounded_rectangle([24, 132, 620, 552], 8, fill=PANEL)
    d.text((40, 144), "Tonight", font=font(17, "Medium"), fill=TEXT)
    for i, (t, what, state) in enumerate(rows):
        y = 180 + i * 44
        d.text((40, y), t, font=mono(17), fill=MUTED)
        d.text((120, y), what, font=font(17), fill=TEXT)
        d.text((540, y), state, font=font(15, "Medium"), fill=GREEN if state == "done" else AMBER)
    d.rounded_rectangle([636, 132, 1000, 552], 8, fill=PANEL)
    d.text((652, 144), "On duty", font=font(17, "Medium"), fill=TEXT)
    for i, (role, name) in enumerate([("LP Officer", "You"), ("LP Officer", "I. Marlowe"), ("Night manager", "On the desk"),
                                       ("Night auditor", "Front office"), ("Engineering", "On call")]):
        d.text((652, 182 + i * 50), role, font=font(14), fill=MUTED)
        d.text((652, 200 + i * 50), name, font=font(17), fill=TEXT)
    d.text((652, 470), "Radio channel 2   Body-worn camera on", font=font(14), fill=BLUE)
    d.text((652, 494), "Log every call. Facts, not opinions.", font=font(14), fill=MUTED)
    return im


def front_office():
    w, h = 1024, 576
    im = Image.new("RGB", (w, h), (238, 240, 242))
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, w, 40], fill=(32, 44, 60))
    d.text((16, 10), "PORT AVENER  Front Office  -  Room rack", font=font(17, "SemiBold"), fill=(240, 242, 244))
    d.text((w - 200, 12), "Night audit in progress", font=font(13), fill=(200, 210, 220))
    rnd = random.Random(41)
    states = [("OCC", (200, 220, 240)), ("VAC", (220, 240, 220)), ("DND", (250, 228, 190)), ("OOO", (230, 230, 230))]
    for floor in range(6):
        y = 60 + floor * 84
        f = 14 - floor
        d.text((16, y + 26), f"{f:02d}", font=font(18, "SemiBold"), fill=(60, 70, 84))
        for r in range(20):
            x = 64 + r * 47
            s = states[0] if rnd.random() < 0.72 else states[1] if rnd.random() < 0.8 else states[rnd.randint(2, 3)]
            d.rectangle([x, y, x + 42, y + 72], fill=s[1], outline=(190, 196, 204))
            d.text((x + 4, y + 6), f"{f}{r + 1:02d}", font=font(13, "Medium"), fill=(40, 48, 60))
            d.text((x + 4, y + 48), s[0], font=font(12), fill=(80, 90, 104))
    return im


def report_screen():
    w, h = 1024, 576
    im = Image.new("RGB", (w, h), (244, 245, 247))
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, w, 44], fill=(28, 36, 48))
    d.text((18, 11), "Incident report  -  Veldane LP", font=font(19, "SemiBold"), fill=(236, 238, 240))
    fields = ["Report number", "Date and time reported", "Location", "Reported by", "Persons involved",
              "What happened (facts only)", "Actions taken", "Evidence and chain of custody", "Notified"]
    for i, f in enumerate(fields):
        y = 64 + i * 56
        d.text((24, y), f, font=font(15, "Medium"), fill=(60, 70, 84))
        d.rectangle([260, y - 4, 1000, y + 30], fill=(255, 255, 255), outline=(200, 206, 214))
    d.text((268, 70), "IR-", font=mono(16), fill=(120, 128, 140))
    d.rectangle([272, 128, 274, 150], fill=(40, 48, 60))  # caret
    return im


def export_screen():
    w, h = 1024, 576
    im = Image.new("RGB", (w, h), BG)
    d = ImageDraw.Draw(im)
    title_bar(d, w, "Clip export", "NVR  30-day retention")
    im.paste(camera_scene(560, 315, "corridor", 77), (24, 64))
    d.rectangle([24, 392, 584, 406], fill=PANEL)
    d.rectangle([180, 392, 330, 406], fill=BLUE)
    for i, (k, v) in enumerate([("Camera", "select"), ("From", "--:--:--"), ("To", "--:--:--"), ("Format", "Native + player"),
                                ("Hash", "SHA-256 on export"), ("Exported by", "LP officer on duty"), ("Media", "Sealed USB, evidence bag")]):
        d.text((612, 72 + i * 50), k, font=font(14), fill=MUTED)
        d.text((612, 90 + i * 50), v, font=font(17), fill=TEXT)
    d.rounded_rectangle([612, 460, 1000, 510], 8, fill=BLUE)
    d.text((700, 474), "Export and log custody", font=font(17, "SemiBold"), fill=(8, 12, 18))
    return im


def fire_panel():
    w, h = 512, 320
    im = Image.new("RGB", (w, h), (20, 60, 40))
    d = ImageDraw.Draw(im)
    d.rectangle([12, 12, w - 12, 140], fill=(150, 200, 90))
    d.text((28, 26), "SYSTEM NORMAL", font=mono(36), fill=(20, 40, 10))
    d.text((28, 82), "ZONES 1-48 NORMAL   00 FAULTS", font=mono(20), fill=(20, 40, 10))
    for i, (label, col) in enumerate([("FIRE", (90, 20, 20)), ("FAULT", (90, 70, 20)), ("SUPPLY", (30, 200, 60)), ("DISABLED", (90, 70, 20))]):
        x = 24 + i * 120
        d.ellipse([x, 168, x + 28, 196], fill=col)
        d.text((x, 204), label, font=font(16, "SemiBold"), fill=(230, 236, 230))
    d.text((24, 258), "Do not silence without investigating.", font=font(17), fill=(230, 236, 230))
    return im


def whiteboard():
    w, h = 1024, 640
    im = Image.new("RGB", (w, h), (246, 247, 245))
    d = ImageDraw.Draw(im)
    blue, red, black = (40, 70, 160), (190, 40, 40), (40, 40, 44)
    d.text((40, 30), "NIGHTS - THIS WEEK", font=font(40, "Bold"), fill=black)
    notes = [("Lift 2 inspection Thu 03:00 - escort the contractor, log in and out", blue),
             ("Key audit: master keys signed back by 06:00 every night", blue),
             ("Body-worn camera ON before you knock", red),
             ("Lost property: log it, bag it, safe. Never leave it on the desk", black),
             ("Smoke detector covered = fire risk, not a complaint. Report it.", red),
             ("CCTV: export, hash, bag, log. Footage overwrites after 30 days", blue)]
    for i, (n, c) in enumerate(notes):
        d.text((48, 118 + i * 74), "- " + n, font=font(27), fill=c)
    # Circled "TOOLBOX" tag, top right, clear of the first note.
    d.ellipse([872, 8, 1016, 106], outline=red, width=6)
    d.text((944, 57), "TOOLBOX", font=font(18, "Bold"), fill=red, anchor="mm")
    return im


def harbour_art(seed):
    """Abstract harbour prints for the guest rooms and corridors: water, cranes and a low sun."""
    rnd = random.Random(seed)
    w, h = 512, 640
    sky = [(rnd.randint(170, 230), rnd.randint(150, 200), rnd.randint(120, 170)) for _ in range(2)]
    im = Image.new("RGB", (w, h), sky[0])
    d = ImageDraw.Draw(im)
    for y in range(h):
        t = y / h
        d.line([0, y, w, y], fill=tuple(int(sky[0][i] * (1 - t) + sky[1][i] * t) for i in range(3)))
    sun = (rnd.randint(80, 420), rnd.randint(120, 260))
    d.ellipse([sun[0] - 50, sun[1] - 50, sun[0] + 50, sun[1] + 50], fill=(240, 200, 140))
    water = int(h * rnd.uniform(0.6, 0.7))
    d.rectangle([0, water, w, h], fill=(40, 70, 90))
    for i in range(12):
        y = water + 8 + i * 18
        d.line([rnd.randint(0, 100), y, rnd.randint(300, w), y], fill=(90, 120, 140), width=2)
    for i in range(rnd.randint(2, 4)):
        x = rnd.randint(40, w - 80)
        top = rnd.randint(140, 260)
        d.line([x, water, x, top], fill=(30, 36, 44), width=7)
        d.line([x - 40, top, x + 110, top], fill=(30, 36, 44), width=6)
        d.line([x + 90, top, x + 90, top + 60], fill=(30, 36, 44), width=2)
    return im.filter(ImageFilter.GaussianBlur(0.6))


def exit_sign():
    """Green running-figure exit sign (ISO 7010 style pictogram) with an arrow."""
    w, h = 512, 192
    im = Image.new("RGB", (w, h), (20, 150, 70))
    d = ImageDraw.Draw(im)
    white = (245, 250, 245)
    d.rectangle([24, 20, 172, 172], outline=white, width=8)
    d.rectangle([110, 36, 160, 156], fill=white)  # door
    d.ellipse([60, 40, 84, 64], fill=white)  # head
    for seg in [((70, 68), (82, 112)), ((82, 112), (60, 150)), ((82, 112), (104, 140)), ((74, 80), (52, 96)), ((74, 80), (100, 88))]:
        d.line([seg[0], seg[1]], fill=white, width=10)
    big = font(80, "Bold")
    d.text((196, 96), "EXIT", font=big, fill=white, anchor="lm")
    ax = 196 + d.textlength("EXIT", font=big) + 24  # arrow starts after the word, never over it
    d.polygon([(ax + 76, 96), (ax + 36, 58), (ax + 36, 80), (ax, 80), (ax, 112), (ax + 36, 112), (ax + 36, 134)], fill=white)
    return im


def corridor_carpet():
    """One repeat of the floor 14 corridor carpet: deep blue ground, brass lozenges, a border line."""
    w = h = 512
    im = Image.new("RGB", (w, h), (24, 34, 58))
    d = ImageDraw.Draw(im)
    for y in range(0, h, 128):
        for x in range(0, w, 128):
            cx, cy = x + 64, y + 64
            d.polygon([(cx, cy - 40), (cx + 40, cy), (cx, cy + 40), (cx - 40, cy)], outline=(150, 122, 70), width=3)
            d.ellipse([cx - 6, cy - 6, cx + 6, cy + 6], fill=(120, 100, 60))
    rnd = random.Random(5)
    px = im.load()
    for y in range(h):
        for x in range(w):
            n = rnd.randint(-6, 6)
            r, g, b = px[x, y]
            px[x, y] = (max(0, r + n), max(0, g + n), max(0, b + n))
    return im


def wallpaper():
    """Guest corridor wallpaper repeat: warm grey with a fine vertical weave."""
    w = h = 512
    im = Image.new("RGB", (w, h), (176, 166, 150))
    d = ImageDraw.Draw(im)
    rnd = random.Random(9)
    for x in range(0, w, 4):
        c = 166 + rnd.randint(-8, 8)
        d.line([x, 0, x, h], fill=(c, c - 8, c - 20), width=2)
    # Slubs: short lighter threads at random heights (a grid of lines here read as tiles in walk 12).
    for _ in range(900):
        x, y = rnd.randrange(w), rnd.randrange(h)
        c = 176 + rnd.randint(0, 10)
        d.line([x, y, x, (y + rnd.randint(6, 20)) % h], fill=(c, c - 8, c - 20), width=1)
    return im


def ballroom_carpet():
    """Harbour Ballroom carpet repeat: deep claret ground, a gold and teal medallion on a scroll lattice."""
    w = h = 512
    im = Image.new("RGB", (w, h), (74, 18, 26))
    d = ImageDraw.Draw(im)
    gold, teal, dark = (168, 128, 62), (38, 82, 88), (52, 12, 18)
    for cx, cy in [(128, 128), (384, 384)]:
        d.ellipse([cx - 92, cy - 92, cx + 92, cy + 92], outline=gold, width=4)
        for k in range(8):
            a = k * math.pi / 4
            px, py = cx + math.cos(a) * 62, cy + math.sin(a) * 62
            d.ellipse([px - 16, py - 16, px + 16, py + 16], fill=teal, outline=gold, width=2)
        d.ellipse([cx - 30, cy - 30, cx + 30, cy + 30], fill=dark, outline=gold, width=3)
    for cx, cy in [(384, 128), (128, 384)]:
        d.polygon([(cx, cy - 40), (cx + 40, cy), (cx, cy + 40), (cx - 40, cy)], fill=dark, outline=gold)
        d.ellipse([cx - 8, cy - 8, cx + 8, cy + 8], fill=gold)
    rnd = random.Random(31)
    px = im.load()
    for y in range(h):
        for x in range(w):
            n = rnd.randint(-5, 5)
            r, g, b = px[x, y]
            px[x, y] = (max(0, r + n), max(0, g + n), max(0, b + n))
    return im


def quarry_tile():
    """Kitchen floor repeat: 4 x 4 plain 15 cm quarry tiles, light warm grey, each a shade apart, in dark grout."""
    w = h = 512
    grout = (92, 88, 82)
    im = Image.new("RGB", (w, h), grout)
    d = ImageDraw.Draw(im)
    rnd = random.Random(17)
    for ty in range(4):
        for tx in range(4):
            c = rnd.randint(-7, 7)
            d.rectangle([tx * 128 + 3, ty * 128 + 3, tx * 128 + 124, ty * 128 + 124], fill=(188 + c, 180 + c, 168 + c))
    px = im.load()
    for y in range(h):
        for x in range(w):
            n = rnd.randint(-5, 5)
            r, g, b = px[x, y]
            px[x, y] = (max(0, r + n), max(0, g + n), max(0, b + n))
    return im


def wall_tile():
    """Bathroom walls and the kitchen's tiled walls: 6 x 6 glazed white 10 cm tiles in pale grout, each a shade apart.
    (Poly Haven's white tile sets 404'd and the fallback, square_tiles_02, rendered near black on walls.)"""
    w = h = 516
    grout = (178, 178, 172)
    im = Image.new("RGB", (w, h), grout)
    d = ImageDraw.Draw(im)
    rnd = random.Random(23)
    for ty in range(6):
        for tx in range(6):
            c = rnd.randint(-5, 5)
            d.rectangle([tx * 86 + 2, ty * 86 + 2, tx * 86 + 83, ty * 86 + 83], fill=(236 + c, 238 + c, 236 + c))
    return im.resize((512, 512))


def dnd_tag():
    w, h = 256, 640
    im = Image.new("RGB", (w, h), (120, 30, 40))
    d = ImageDraw.Draw(im)
    d.ellipse([78, 30, 178, 130], fill=(250, 250, 250))
    d.ellipse([98, 50, 158, 110], fill=(120, 30, 40))
    for i, line in enumerate(["PLEASE", "DO NOT", "DISTURB"]):
        tw = d.textlength(line, font=font(40, "Bold"))
        d.text(((w - tw) / 2, 220 + i * 60), line, font=font(40, "Bold"), fill=(245, 240, 232))
    d.text((40, 560), "Veldane Grand", font=font(26), fill=BRASS)
    return im


def floor_plan():
    """Typical guest floor (2-14) evacuation plan for the back offices: corridor, rooms, both stairs, lifts, assembly point.
    It hangs on B1 and G, so it carries no you-are-here mark."""
    w, h = 1024, 512
    im = Image.new("RGB", (w, h), (248, 248, 244))
    d = ImageDraw.Draw(im)
    d.text((24, 16), "TYPICAL GUEST FLOOR (2-14)  -  EVACUATION PLAN", font=font(28, "Bold"), fill=(30, 30, 34))
    d.text((24, 52), "In a fire: do not use the lifts. Go to the nearest stair. Assembly point: Quay Promenade.", font=font(16), fill=(60, 60, 64))
    x0, y0, x1, y1 = 40, 110, 984, 470
    d.rectangle([x0, y0, x1, y1], outline=(30, 30, 34), width=4)
    cy = (y0 + y1) // 2
    d.rectangle([x0, cy - 22, x1, cy + 22], fill=(230, 236, 240))
    for i in range(10):
        x = x0 + i * (x1 - x0) / 10
        d.line([x, y0, x, cy - 22], fill=(30, 30, 34), width=2)
        d.line([x, cy + 22, x, y1], fill=(30, 30, 34), width=2)
        d.text((x + 8, y0 + 8), f"x{i + 1:02d}", font=font(14), fill=(60, 60, 64))
        d.text((x + 8, y1 - 26), f"x{20 - i:02d}", font=font(14), fill=(60, 60, 64))
    for sx, label in [(x0 + 430, "STAIR A"), (x1 - 40, "STAIR B")]:
        d.rectangle([sx - 30, cy - 70, sx + 30, cy - 22], fill=(20, 150, 70))
        d.text((sx - 28, cy - 64), label, font=font(12, "Bold"), fill=(255, 255, 255))
    # Escape-route arrows along the corridor, each pointing to the nearer stair.
    green = (20, 150, 70)
    split = (x0 + 430 + x1 - 40) / 2
    for ax in (x0 + 110, x0 + 290, x0 + 600, x0 + 760, x1 - 120):
        sgn = 1 if ax < x0 + 430 or ax > split else -1
        tip = ax + sgn * 46
        d.line([ax, cy, tip - sgn * 14, cy], fill=green, width=6)
        d.polygon([(tip, cy), (tip - sgn * 16, cy - 11), (tip - sgn * 16, cy + 11)], fill=green)
    # The lift lobby, marked on a white card below the corridor (room numbers: x = the floor).
    hx = x0 + 500
    d.rectangle([hx - 26, cy - 20, hx + 26, cy + 20], fill=(255, 255, 255), outline=(30, 30, 34), width=3)
    d.line([hx, cy - 20, hx, cy + 20], fill=(30, 30, 34), width=2)
    label = font(14, "Bold")
    tw = d.textlength("LIFTS", font=label)
    d.rectangle([hx - tw / 2 - 6, cy + 28, hx + tw / 2 + 6, cy + 50], fill=(255, 255, 255), outline=(30, 30, 34), width=2)
    d.text((hx, cy + 39), "LIFTS", font=label, fill=(30, 30, 34), anchor="mm")
    d.text((x0, y1 + 12), "Room numbers: x = floor (e.g. 1406). In a fire alarm the lifts return to G.", font=font(14), fill=(60, 60, 64))
    return im


def _tower(d, rng, x0, x1, top, base, px_x, px_y, shade, share, warm, cool, dim, podium_m=0.0, glass=None):
    """One building from x0 to x1 (px), roof at top, ground at base: its own storey height, bay, window shape
    and lit share, so no two buildings line up their rows (walks 12: straight lit bands ran across every pane)."""
    d.rectangle([x0, int(top), x1, base], fill=shade + (255,))
    storey = rng.uniform(3.0, 4.4) * px_y
    bay = rng.uniform(2.2, 4.2) * px_x
    style = rng.random()
    if style < 0.2:      # ribbon windows: long horizontal strips
        ww, wh = bay * rng.uniform(0.9, 1.0), storey * rng.uniform(0.35, 0.5)
    elif style < 0.4:    # tall narrow slots
        ww, wh = bay * rng.uniform(0.25, 0.35), storey * rng.uniform(0.6, 0.75)
    else:
        ww, wh = bay * rng.uniform(0.4, 0.6), storey * rng.uniform(0.4, 0.6)
    ww, wh = max(1, int(ww)), max(1, int(wh))
    share *= rng.uniform(0.3, 1.8)
    tint = warm if rng.random() < 0.6 else cool
    # Ground storey: taller, and on the near panels a shop or lobby glow.
    yy = base - max(podium_m * px_y, storey * rng.uniform(1.0, 1.5))
    if podium_m > 0.0 and rng.random() < 0.7:
        g = rng.choice([(210, 164, 108, 255), (190, 180, 156, 255), (150, 172, 210, 255), (214, 136, 86, 255)])
        sx0 = int(x0 + rng.uniform(0.05, 0.2) * (x1 - x0))
        sx1 = int(x1 - rng.uniform(0.05, 0.2) * (x1 - x0))
        d.rectangle([sx0, int(base - podium_m * 0.75 * px_y), sx1, base - max(1, int(0.3 * px_y))], fill=g)
        d.rectangle([sx0 - 2, int(base - podium_m * 0.85 * px_y), sx1 + 2, int(base - podium_m * 0.75 * px_y)], fill=(18, 16, 14, 255))
    xoff = rng.uniform(0.2, 0.8) * bay
    while yy - wh > top + storey * 0.4:
        r_floor = rng.random()
        floor_share = share * (0.1 if r_floor < 0.3 else (2.5 if r_floor > 0.9 else 1.0))
        xx = x0 + xoff
        run = 0
        while xx + ww < x1 - bay * 0.2:
            r = rng.random()
            # Lit windows come in runs (one office lit, the next dark).
            if run > 0 or r < floor_share * 0.5:
                run = run - 1 if run > 0 else rng.randint(0, 5)
                d.rectangle([int(xx), int(yy - wh), int(xx) + ww - 1, int(yy)], fill=tint if rng.random() < 0.85 else (warm if tint is cool else cool))
            elif r < floor_share * 0.5 + 0.05:
                d.rectangle([int(xx), int(yy - wh), int(xx) + ww - 1, int(yy)], fill=dim)
            elif glass:
                d.rectangle([int(xx), int(yy - wh), int(xx) + ww - 1, int(yy)], fill=glass)
            xx += bay
        yy -= storey


def skyline(seed, panel_w_m, panel_h_m, min_m, max_m, tower_chance, lit=0.2, h=1024, podium_m=0.0):
    """The city round the harbour at night for one panel (KcHotel BuildLighting) panel_w_m by panel_h_m:
    near-black blocks with scattered lit windows and red aviation lights, transparent above the roofs.
    Every building has its own outline (setbacks, crowns, masts), storey height and window pattern.
    Pixels are not square (4096 px across, h px up), so every size is converted from metres on each axis."""
    w = 4096
    px_x = w / float(panel_w_m)
    px_y = h / float(panel_h_m)
    rng = random.Random(seed)
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    warm, cool, dim = (255, 196, 128, 255), (196, 216, 255, 255), (110, 92, 74, 255)
    for layer, share in enumerate([lit * 0.5, lit]):  # back row, then front
        x = rng.uniform(-30, 0) * px_x
        while x < w:
            bw = rng.uniform(14, 55) * px_x
            tall = rng.random() < tower_chance
            bh_m = rng.uniform(max_m * 0.55, max_m) if tall else rng.uniform(min_m, min_m + (max_m - min_m) * 0.4)
            if layer == 0:
                bh_m *= rng.uniform(1.05, 1.35)
            bh_m = min(bh_m, panel_h_m - 4.0)
            v = rng.randint(-2, 3)
            shade = (4 + v, 5 + v, 8 + v) if layer == 0 else (8 + v, 9 + v, 13 + v)
            top = h - bh_m * px_y
            x0, x1 = int(x), int(x + bw)
            _tower(d, rng, x0, x1, top, h, px_x, px_y, shade, share, warm, cool, dim, podium_m if layer == 1 else 0.0)
            # The roof: a setback, a stepped crown, a slanted top, a mast, or plant on a flat roof.
            roof = rng.random()
            cx = (x0 + x1) // 2
            mast_top = top
            if tall and roof < 0.3:
                sw = (x1 - x0) * rng.uniform(0.45, 0.7)
                sh = rng.uniform(6, 18) * px_y
                sx0 = int(cx - sw / 2 + rng.uniform(-0.15, 0.15) * (x1 - x0))
                _tower(d, rng, sx0, int(sx0 + sw), top - sh, int(top), px_x, px_y, shade, share, warm, cool, dim)
                mast_top = top - sh
            elif tall and roof < 0.5:
                for k in range(1, 4):
                    inset = (x1 - x0) * 0.12 * k
                    d.rectangle([int(x0 + inset), int(top - k * 3 * px_y), int(x1 - inset), int(top)], fill=shade + (255,))
                mast_top = top - 9 * px_y
            elif roof < 0.65:
                rise = min(rng.uniform(4, 12) * px_y, (x1 - x0) * 0.3)
                if rng.random() < 0.5:
                    d.polygon([(x0, top), (x1, top), (x1, top - rise)], fill=shade + (255,))
                else:
                    d.polygon([(x0, top), (x1, top), (x0, top - rise)], fill=shade + (255,))
            elif roof < 0.85:
                pw = (x1 - x0) * rng.uniform(0.2, 0.4)
                px0 = int(x0 + rng.uniform(0.1, 0.5) * (x1 - x0))
                d.rectangle([px0, int(top - rng.uniform(2, 4) * px_y), int(px0 + pw), int(top)], fill=shade + (255,))
            if tall and roof < 0.75:
                mh = rng.uniform(5, 15) * px_y
                d.rectangle([cx, int(mast_top - mh), cx + 1, int(mast_top)], fill=shade + (255,))
                d.rectangle([cx - 1, int(mast_top - mh) - 3, cx + 2, int(mast_top - mh)], fill=(255, 40, 30, 255))
            elif tall:
                d.rectangle([cx - 1, int(mast_top) - 3, cx + 1, int(mast_top)], fill=(255, 40, 30, 255))
            # Gaps between buildings on the near panels; overlaps (depth) on the far ring.
            x += bw + (rng.uniform(1, 14) if podium_m > 0.0 else rng.uniform(-6, 10)) * px_x
    # Street or quay lights along the foot of the panel, at uneven spacing.
    xx = 0.0
    while xx < w:
        d.rectangle([int(xx), h - 3, int(xx) + 1, h - 2], fill=warm)
        xx += rng.uniform(3, 14)
    return im


def street_frontage(seed, panel_w_m, panel_h_m, h=1024):
    """The buildings across the street from the hotel, seen from the lobby: four to seven storeys side by side,
    lit shopfronts and lobbies at the pavement, a dark cornice line, windows above."""
    w = 4096
    px_x = w / float(panel_w_m)
    px_y = h / float(panel_h_m)
    rng = random.Random(seed)
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    warm, cool, dim = (255, 200, 140, 255), (200, 218, 255, 255), (96, 82, 66, 255)
    x = 0.0
    while x < w:
        bw = rng.uniform(9, 24) * px_x
        storeys = rng.randint(4, 7)
        bh_m = min(panel_h_m - 1.0, 5.0 + storeys * rng.uniform(3.1, 3.6))
        v = rng.randint(0, 10)
        shade = rng.choice([(22 + v, 18 + v, 16 + v), (16 + v, 17 + v, 19 + v), (26 + v, 22 + v, 18 + v)])
        x0, x1 = int(x), int(x + bw)
        top = h - bh_m * px_y
        _tower(d, rng, x0, x1, top, h, px_x, px_y, shade, 0.7, warm, cool, dim, podium_m=5.0,
               glass=(max(0, shade[0] - 12), max(0, shade[1] - 10), max(0, shade[2] - 6), 255))
        d.rectangle([x0, int(top), x1, int(top + 0.6 * px_y)], fill=(8, 8, 10, 255))  # cornice
        d.line([x1, int(top), x1, h], fill=(6, 6, 8, 255), width=2)                    # party wall line
        x += bw
    return im


def main():
    os.makedirs(OUT, exist_ok=True)
    items = {
        "T_ScreenCctv1": cctv_wall([("14-03", "FL14 CORR W", "corridor", "00:41:07"), ("14-02", "FL14 CORR E", "corridor", "00:41:07"),
                                    ("14-01", "FL14 LIFT LOBBY", "corridor", "00:41:07"), ("L-01", "STREET DOORS", "lobby", "00:41:07")], 101),
        "T_ScreenCctv2": cctv_wall([("L-02", "FRONT DESK", "lobby", "00:41:07"), ("L-07", "LOBBY BAR", "bar", "00:41:07"),
                                    ("L-09", "STAFF ENTRANCE", "corridor", "00:41:07"), ("B1-02", "RECEIVING", "bay", "00:41:07")], 202),
        "T_ScreenCctv3": cctv_wall([("B1-01", "B1 CORRIDOR", "corridor", "00:41:07"), ("G-04", "GARAGE RAMP", "bay", "00:41:07"),
                                    ("09-02", "FL09 CORR", "corridor", "00:41:07"), ("05-01", "FL05 LIFT LOBBY", "corridor", "00:41:07")], 303),
        "T_ScreenCctv4": cctv_wall([("L-04", "LOBBY LOUNGE", "lobby", "00:41:07"), ("L-05", "BELL STAND", "lobby", "00:41:07"),
                                    ("R-01", "ROOF PLANT", "bay", "00:41:07"), ("12-03", "FL12 CORR", "corridor", "00:41:07")], 404),
        "T_ScreenShiftBoard": shift_board(), "T_ScreenFrontOffice": front_office(), "T_ScreenReport": report_screen(),
        "T_ScreenExport": export_screen(), "T_ScreenPanel": fire_panel(), "T_Whiteboard": whiteboard(),
        "T_ArtHarbourA": harbour_art(11), "T_ArtHarbourB": harbour_art(29), "T_ArtHarbourC": harbour_art(53),
        "T_ExitSign": exit_sign(), "T_CorridorCarpet": corridor_carpet(), "T_Wallpaper": wallpaper(), "T_QuarryTile": quarry_tile(),
        "T_DndTag": dnd_tag(), "T_BallroomCarpet": ballroom_carpet(), "T_FloorPlan": floor_plan(), "T_WallTile": wall_tile(),
    }
    for name, (seed, pw, ph, lo, hi, towers, lit) in SKYLINES.items():
        items[name] = skyline(seed, pw, ph, lo, hi, towers, lit, 1024 if ph > 60 else 512, 0.0 if ph > 60 else 5.0)
    for name, (seed, pw, ph) in FRONTAGES.items():
        items[name] = street_frontage(seed, pw, ph)
    for name, im in items.items():
        im.save(os.path.join(OUT, name + ".png"))
        print("wrote", name)


if __name__ == "__main__":
    main()
