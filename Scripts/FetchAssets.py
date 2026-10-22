"""Downloads the third-party art assets listed in Data/AssetManifest.json and verifies their SHA-256.

Only redistributable licences are allowed in the manifest (CC0 from Poly Haven / ambientCG, SIL OFL fonts).
Raw downloads go to Data/Assets/raw/ (not committed); Scripts/ImportAssets.py turns them into
Content/Keycard/ assets inside the editor.

  python Scripts/FetchAssets.py             download + verify everything in the manifest
  python Scripts/FetchAssets.py --resolve   rebuild the manifest from WANTED (queries the Poly Haven API,
                                            downloads, records sha256), then write Docs/AssetCredits.md
"""
import argparse
import hashlib
import io
import json
import os
import sys
import urllib.error
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, "Data", "AssetManifest.json")
RAW = os.path.join(ROOT, "Data", "Assets", "raw")
FONTS = os.path.join(ROOT, "Resources", "Fonts")
CREDITS = os.path.join(ROOT, "Docs", "AssetCredits.md")
UA = {"User-Agent": "Keycard-asset-fetch/1.0"}

# (kind, slot, Poly Haven ids to try in order, role in the game). --resolve keeps the first id that exists, so a
# renamed or withdrawn asset never stops the fetch; ImportAssets.py finds each asset by its slot.
WANTED = [
    ("texture", "parquet", ["herringbone_parquet"], "Parquet: LP office, manager's office"),
    ("texture", "marble", ["marble_01", "white_marble_03"], "Lobby floor; dark-tinted for the front desk and bar tops"),
    ("texture", "tiles", ["white_bathroom_tiles", "bathroom_tiles", "square_tiles_02"], "Guest bathrooms, staff toilets"),
    ("texture", "concrete_floor", ["concrete_floor_02", "smooth_concrete_floor"], "B1 back of house, loading dock"),
    ("texture", "concrete_wall", ["concrete_wall_008", "concrete_wall_006", "painted_concrete"], "Stair shafts, B1 walls, facade"),
    ("texture", "asphalt", ["asphalt_02", "asphalt_track"], "Street and garage ramp"),
    ("texture", "paving", ["grey_cartago_02", "patterned_paving", "cobblestone_floor_08"], "Quay Promenade paving"),
    ("texture", "wool", ["poly_wool_herringbone"], "Carpets, upholstery, acoustic panels"),
    ("texture", "linen", ["fabric_pattern_07", "cotton_jersey", "denim_fabric"], "Bed linen and towels"),
    ("texture", "leather", ["fabric_leather_02", "fabric_leather_01", "leather_red_02"], "Bar stools, lobby chairs"),
    ("texture", "plaster", ["plastered_wall_04", "plastered_wall_02"], "Interior walls"),
    ("texture", "oak", ["oak_veneer_01"], "Light oak: desks, guest-room doors"),
    ("texture", "walnut", ["black_walnut_veneer_01"], "Walnut: front desk, manager's desk"),
    ("model", "Chandelier_03", ["Chandelier_03"], "Lobby chandelier"),
    ("model", "bar_chair_round_01", ["bar_chair_round_01"], "Lobby bar stools"),
    ("model", "modern_coffee_table_01", ["modern_coffee_table_01"], "Lobby lounge table"),
    ("model", "sofa_02", ["sofa_02"], "Lobby lounge and guest-room sofas"),
    ("model", "modern_arm_chair_01", ["modern_arm_chair_01"], "Task and lounge chairs"),
    ("model", "dining_chair_02", ["dining_chair_02"], "Guest-room desk chairs, LP office"),
    ("model", "potted_plant_02", ["potted_plant_02"], "Large plants"),
    ("model", "potted_plant_04", ["potted_plant_04"], "Desk plants"),
    ("hdri", "night_sky", ["shanghai_bund", "dikhololo_night", "satara_night"], "Night harbour city through the windows"),
]
# Real-world brand marks found in downloaded textures (Docs/AssetCredits.md "Logo and brand check"):
# each box is painted over with the average colour of a 6 px ring around it. None found in the hotel set yet.
EDITS = {}
EDITED = os.path.join(ROOT, "Data", "Assets", "edited")
FONT_ZIP = ("font", "inter", "https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip",
            ["Inter-Regular.ttf", "Inter-Medium.ttf", "Inter-SemiBold.ttf", "Inter-Bold.ttf"])
FONT_LICENCE = "https://raw.githubusercontent.com/google/fonts/main/ofl/inter/OFL.txt"


def fetch(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120) as r:
        return r.read()


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def polyhaven_files(asset_id, kind):
    """Returns [(url, relative path)] for the 1k JPG maps (2k HDR for HDRIs) of an asset."""
    files = json.loads(fetch("https://api.polyhaven.com/files/" + asset_id))
    out = []
    if kind == "hdri":
        f = files["hdri"]["2k"]["hdr"]
        return [(f["url"], f"{asset_id}/{asset_id}_2k.hdr")]
    if kind == "model":
        f = files["fbx"]["1k"]["fbx"]
        out.append((f["url"], f"{asset_id}/{asset_id}_1k.fbx"))
    for key, res in files.items():
        # diffuse, DirectX normal, packed AO/Rough/Metal and (leaf cards) opacity, per material part.
        if key in ("Diffuse", "nor_dx", "arm", "alpha") or key.endswith(("_diff", "_nor_dx", "_arm", "_alpha")):
            one = res.get("1k", {})
            f = one.get("jpg") or one.get("png")
            if f:
                out.append((f["url"], f"{asset_id}/{os.path.basename(f['url'])}"))
    return out


def first_existing(candidates):
    for asset_id in candidates:
        try:
            return asset_id, json.loads(fetch("https://api.polyhaven.com/info/" + asset_id))
        except urllib.error.HTTPError as e:
            print(f"  {asset_id}: HTTP {e.code}, trying the next candidate")
    return None, None


def resolve():
    assets = []
    for kind, slot, candidates, role in WANTED:
        asset_id, info = first_existing(candidates)
        if not asset_id:
            print(f"MISSING {slot}: none of {candidates} exist; ImportAssets.py falls back to a flat tint")
            continue
        entry = {
            "id": asset_id, "slot": slot, "kind": kind, "role": role, "source": "Poly Haven",
            "page": f"https://polyhaven.com/a/{asset_id}", "licence": "CC0 1.0",
            "authors": sorted((info.get("authors") or {}).keys()), "name": info.get("name", asset_id), "files": [],
        }
        for url, rel in polyhaven_files(asset_id, kind):
            data = fetch(url)
            entry["files"].append({"url": url, "path": f"polyhaven/{rel}", "sha256": sha256(data), "size": len(data)})
            save(os.path.join(RAW, "polyhaven", rel), data)
        print(f"resolved {asset_id}: {len(entry['files'])} files")
        assets.append(entry)
    data = fetch(FONT_ZIP[2])
    assets.append({"id": FONT_ZIP[1], "kind": "font", "role": "UI typeface", "source": "rsms/inter (Rasmus Andersson)",
                   "page": "https://rsms.me/inter/", "licence": "SIL Open Font License 1.1", "authors": ["Rasmus Andersson"],
                   "name": "Inter 4.1", "extract": FONT_ZIP[3],
                   "files": [{"url": FONT_ZIP[2], "path": "fonts/Inter-4.1.zip", "sha256": sha256(data), "size": len(data)}]})
    save(os.path.join(RAW, "fonts", "Inter-4.1.zip"), data)
    lic = fetch(FONT_LICENCE)
    assets[-1]["files"].append({"url": FONT_LICENCE, "path": "fonts/OFL.txt", "sha256": sha256(lic), "size": len(lic)})
    save(os.path.join(RAW, "fonts", "OFL.txt"), lic)
    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"version": 1, "assets": assets}, f, indent=1)
        f.write("\n")
    print("wrote", MANIFEST)


def save(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def download(manifest):
    bad = 0
    for a in manifest["assets"]:
        for f in a["files"]:
            path = os.path.join(RAW, f["path"])
            data = open(path, "rb").read() if os.path.exists(path) else None
            if data is None or sha256(data) != f["sha256"]:
                data = fetch(f["url"])
                if sha256(data) != f["sha256"]:
                    print("SHA-256 MISMATCH", f["url"])
                    bad += 1
                    continue
                save(path, data)
        print("ok", a["id"])
    return bad


def install_fonts(manifest):
    for a in manifest["assets"]:
        if a["kind"] != "font":
            continue
        os.makedirs(FONTS, exist_ok=True)
        z = zipfile.ZipFile(io.BytesIO(open(os.path.join(RAW, "fonts", "Inter-4.1.zip"), "rb").read()))
        for wanted in a["extract"]:
            member = next(n for n in z.namelist() if n.endswith("/" + wanted) or n == wanted)
            save(os.path.join(FONTS, wanted), z.read(member))
        save(os.path.join(FONTS, "OFL.txt"), open(os.path.join(RAW, "fonts", "OFL.txt"), "rb").read())
        print("fonts installed in", FONTS)


def apply_edits():
    if not EDITS:
        return
    from PIL import Image  # pip install pillow
    for rel, boxes in EDITS.items():
        img = Image.open(os.path.join(RAW, rel)).convert("RGB")
        px = img.load()
        for (x0, y0, x1, y1) in boxes:
            ring = [px[x, y] for x in range(x0 - 6, x1 + 6) for y in range(y0 - 6, y1 + 6)
                    if not (x0 <= x < x1 and y0 <= y < y1)]
            avg = tuple(sum(c[i] for c in ring) // len(ring) for i in range(3))
            for x in range(x0, x1):
                for y in range(y0, y1):
                    px[x, y] = avg
        out = os.path.join(EDITED, rel)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        img.save(out, quality=95)
        print("edited", rel)


def write_credits_include(manifest):
    """The in-game credits read the asset list from this generated table (one source: the manifest)."""
    path = os.path.join(ROOT, "Source", "Keycard", "Private", "Generated", "KcAssetCredits.inl")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    def esc(t):
        return t.replace("\\", "\\\\").replace('"', '\\"')
    lines = ["// Generated by Scripts/FetchAssets.py from Data/AssetManifest.json. Do not edit by hand.",
             "// { name, role, source, authors, licence }"]
    for a in manifest["assets"]:
        lines.append('{TEXT("%s"), TEXT("%s"), TEXT("%s"), TEXT("%s"), TEXT("%s")},' % (
            esc(a["name"]), esc(a["role"]), esc(a["source"]), esc(", ".join(a["authors"])), esc(a["licence"])))
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote", path)


BEGIN, END = "<!-- generated:assets begin -->", "<!-- generated:assets end -->"


def write_credits(manifest):
    """Rewrites only the generated block of Docs/AssetCredits.md; the hand-written rows (cast, voices, audio) stay."""
    rows = ["| Asset | Source | Licence | Use | Notes |", "|---|---|---|---|---|"]
    for a in manifest["assets"]:
        rows.append(f"| [{a['name']}]({a['page']}) by {', '.join(a['authors'])} | {a['source']} | {a['licence']} | {a['role']} | "
                    "Logo check: see below |")
    block = "\n".join([BEGIN, "Generated from Data/AssetManifest.json by `Scripts/FetchAssets.py --resolve`.", ""] + rows + [END])
    text = open(CREDITS, encoding="utf-8").read() if os.path.exists(CREDITS) else "# Asset credits\n"
    if BEGIN in text and END in text:
        text = text[:text.index(BEGIN)] + block + text[text.index(END) + len(END):]
    else:
        text = text.rstrip("\n") + "\n\n## Art assets\n" + block + "\n"
    with open(CREDITS, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote", CREDITS)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--resolve", action="store_true")
    args = p.parse_args()
    if args.resolve:
        resolve()
    manifest = json.load(open(MANIFEST, encoding="utf-8"))
    bad = download(manifest)
    install_fonts(manifest)
    apply_edits()
    write_credits_include(manifest)
    if args.resolve:
        write_credits(manifest)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
