#!/usr/bin/env python3
"""Build the response pack and the built-in set from the raw downloads in ir-src/.

  python3 tools/make_ir_pack.py            # writes ir-pack/ and Resources/internal/

Every file is converted to what the module itself uses: 48 kHz WAV, stereo (or
4-channel true stereo: L>L, R>R, L>R, R>L). Changes made to the originals:
B-format and Mid-Side files decoded to stereo, leading silence and inaudible
tails trimmed, level normalised, resampled to 48 kHz. Needs numpy, soundfile, sox.
"""
import collections, json, os, re, subprocess, sys, tempfile, unicodedata
import numpy as np
import soundfile as sf

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "ir-src")
PACK = os.path.join(ROOT, "ir-pack")
INTERNAL = os.path.join(ROOT, "Resources", "internal")
MAX_SECONDS = 23.0
PER_SPACE = 3

# the module's eight built-in responses: (file name, space, preferred format, path hint)
BUILTIN = [
    ("BOTTLEDUNGEON_48K", "Falkland Palace Bottle Dungeon", None, ""),
    ("FALKLAND_PALACE_48K", "Falkland Palace Royal Tennis Court", "stereo", ""),
    ("HAMILTON_MAUSOLEUM_48K", "Hamilton Mausoleum", "stereo", ""),
    ("LADY_CHAPEL_48K", "Lady Chapel, St Albans Cathedral", "stereo", "ortf"),
    ("MIDDLE_TUNNEL_48K", "Innocent Railway Tunnel", "bformat-wxyz", "middle_tunnel_1way"),
    ("R1_OMNI_48K", "R1 Nuclear Reactor Hall", "mono", ""),
    ("TERRYS_WAREHOUSE_48K", "Terry's Factory Warehouse", "stereo", ""),
    ("YORK_MINSTER_48K", "York Minster", "stereo", ""),
]
SKIP_SPACES = {"Clifford's Tower, York"}          # B-format channel order could not be confirmed
SHORT = {  # tidy folder-friendly names for the long ones
    "The Shrine and Parish Church of All Saints North Street": "All Saints North Street",
    "St. Margaret's Church – National Centre for Early Music": "St Margarets Church",
    "Saint Lawrence Church Molenbeek-Wersbeek Belgium": "Saint Lawrence Church",
    "D/L/028 Hendrix Hall Derwent, University of York": "Hendrix Hall",
    "P/L/001 Physics, University of York": "Physics Lecture Hall",
    "Arthur Sykes Rymer Auditorium, University of York.": "Rymer Auditorium",
    "Genesis 6 Studio – Live Room Drum Set Up": "Genesis 6 Drum Room",
    "Tvísöngur Sound Sculpture, Iceland (Model)": "Tvisongur Sound Sculpture",
    "The Dixon Studio Theatre – University of York": "Dixon Studio Theatre",
    "Abies Grandis Forest, Wheldrake Wood": "Wheldrake Wood Forest",
    "T2 Hangar, Yorkshire Air Museum": "Air Museum Hangar",
}


def clean(name):
    name = SHORT.get(name, name)
    name = unicodedata.normalize("NFKD", name).encode("ascii", "ignore").decode()
    name = re.sub(r",? University of York\.?", "", name)
    name = re.sub(r"\(.*?\)", "", name)
    name = re.sub(r"[^A-Za-z0-9 '\-]", " ", name).replace("'", "")
    return re.sub(r"\s+", " ", name).strip()


def to_stereo(x, fmt, notes):
    """Returns (n, 1|2) float array."""
    if x.ndim == 1:
        x = x[:, None]
    if fmt == "bformat-wxyz" and x.shape[1] >= 4:
        w, xx, y = x[:, 0], x[:, 1], x[:, 2]
        if "ACN" in notes.upper() and "W,Y,Z,X" in notes.upper():
            y, xx = x[:, 1], x[:, 3]
        # two virtual cardioid-ish microphones at +/-55 degrees (FuMa: W is -3 dB, +Y is left)
        a = np.radians(55.0)
        l = 0.5 * (np.sqrt(2.0) * w + np.cos(a) * xx + np.sin(a) * y)
        r = 0.5 * (np.sqrt(2.0) * w + np.cos(a) * xx - np.sin(a) * y)
        return np.stack([l, r], axis=1), "B-format decoded to stereo"
    if fmt == "other" and x.shape[1] >= 2:      # Mid-Side
        return np.stack([x[:, 0] + x[:, 1], x[:, 0] - x[:, 1]], axis=1), "Mid-Side decoded to stereo"
    if x.shape[1] >= 2:
        return x[:, :2], ""
    return x[:, :1], ""


def trim(x, sr):
    env = np.max(np.abs(x), axis=1)
    peak = env.max()
    if peak <= 0:
        return None
    start = max(0, int(np.argmax(env > peak * 10 ** (-30 / 20))) - int(0.002 * sr))
    above = np.nonzero(env > peak * 10 ** (-80 / 20))[0]
    end = min(len(x), int(above[-1]) + int(0.05 * sr), start + int(MAX_SECONDS * sr))
    x = x[start:end].copy()
    fade = min(len(x), int(0.03 * sr))
    x[-fade:] *= np.linspace(1.0, 0.0, fade)[:, None]
    return x * (0.891 / np.max(np.abs(x)))


def write(x, sr, dest, flac=False):
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        t = os.path.join(tmp, "in.wav")
        sf.write(t, x, sr, subtype="FLOAT")
        cmd = ["sox", "-G", t, "-b", "24", dest, "rate", "-v", "48000"]
        subprocess.run(cmd, check=True, stderr=subprocess.DEVNULL)
    return sf.info(dest)


def load(entry):
    x, sr = sf.read(os.path.join(SRC, entry["path"]), dtype="float64", always_2d=True)
    return x, sr


def main():
    manifest = json.load(open(os.path.join(SRC, "manifest.json")))
    openair = collections.OrderedDict()
    hybrid = collections.OrderedDict()
    for e in manifest:
        (openair if e["source"].startswith("OpenAIR") else hybrid).setdefault(e["space"], []).append(e)

    credits = []

    def credit(rel, e, changes):
        credits.append((rel, e["space"], e["licence"], e["attribution"], e["page_url"], changes))

    def convert(e, dest, flac=False):
        x, sr = load(e)
        x, change = to_stereo(x, e["format"], e.get("notes", ""))
        x = trim(x, sr)
        if x is None:
            return None
        info = write(x, sr, dest, flac)
        changes = [c for c in [change, "trimmed", "normalised", "resampled to 48 kHz" if sr != 48000 else ""] if c]
        return info, ", ".join(changes)

    # built-in set
    for f in os.listdir(INTERNAL) if os.path.isdir(INTERNAL) else []:
        if f.endswith(".flac"):
            os.remove(os.path.join(INTERNAL, f))
    for name, space, fmt, hint in BUILTIN:
        cands = [e for e in openair[space] if (fmt is None or e["format"] == fmt) and hint in e["path"].lower()
                 and "binaural" not in e["path"].lower()]
        cands.sort(key=lambda e: (e["samplerate"] != 48000, e["path"]))
        e = cands[0]
        info, changes = convert(e, os.path.join(INTERNAL, name + ".flac"), True)
        credit("Internal/" + name, e, changes)
        print(f"internal {name:24s} {info.frames / 48000:6.2f}s {info.channels}ch  <- {e['path']}")

    # pack
    subprocess.run(["rm", "-rf", PACK], check=True)
    order = {"stereo": 0, "bformat-wxyz": 1, "other": 2, "mono": 3}
    count = 0
    for space, entries in openair.items():
        if space in SKIP_SPACES:
            continue
        entries = [e for e in entries if "binaural" not in e["path"].lower() and "clip" not in e.get("notes", "").lower()
                   and not ("holy-trinity" in e["path"] and e["format"] != "mono")]
        if not entries:
            continue
        best = min(order[e["format"]] for e in entries)
        picks = [e for e in entries if order[e["format"]] == best]
        if any(e["samplerate"] == 48000 for e in picks) and any(e["samplerate"] == 44100 for e in picks):
            picks = [e for e in picks if e["samplerate"] == 48000]
        picks = sorted(picks, key=lambda e: e["path"])[:PER_SPACE]
        base = clean(space)
        for i, e in enumerate(picks):
            fname = base + (f" {i + 1}" if len(picks) > 1 else "") + ".wav"
            rel = os.path.join(e["category"], fname)
            res = convert(e, os.path.join(PACK, rel))
            if res is None:
                continue
            credit(rel, e, res[1])
            count += 1

    # HybridReverb2 rooms: four mono files -> one true-stereo response (L>L, R>R, L>R, R>L)
    groups = collections.OrderedDict()
    for space, entries in hybrid.items():
        for e in entries:
            groups.setdefault(os.path.dirname(e["path"]), {})[os.path.basename(e["path"]).split(".")[0]] = e
    for d, g in groups.items():
        if not all(k in g for k in ("left_fl", "left_fr", "right_fl", "right_fr")):
            continue
        parts = d.split("/")
        room, family, dist = parts[-3], parts[-2], parts[-1]
        if family != "music":
            continue
        chans = [load(g[k])[0][:, 0] for k in ("left_fl", "right_fr", "left_fr", "right_fl")]
        n = max(len(c) for c in chans)
        x = np.stack([np.pad(c, (0, n - len(c))) for c in chans], axis=1)
        x = trim(x, 48000)
        rel = os.path.join("True Stereo Rooms", f"{room.replace('_', ' ').title()} {dist}.wav")
        write(x, g["left_fl"]["samplerate"], os.path.join(PACK, rel))
        e = dict(g["left_fl"]); e["space"] = f"{room} ({dist})"
        credit(rel, e, "four mono responses combined into one true-stereo file, trimmed, normalised")
        count += 1

    with open(os.path.join(PACK, "CREDITS.txt"), "w") as f:
        f.write("ConvoSolution response pack\n===========================\n\n"
                "These impulse responses come from open databases and are redistributed under their\n"
                "Creative Commons licences. They were modified as listed for each file. Files from the\n"
                "HybridReverb2 database (folder 'True Stereo Rooms') are CC BY-SA 4.0, so those converted\n"
                "files carry the same licence; everything else is CC BY 4.0.\n"
                "  CC BY 4.0     https://creativecommons.org/licenses/by/4.0/\n"
                "  CC BY-SA 4.0  https://creativecommons.org/licenses/by-sa/4.0/\n\n")
        for rel, space, lic, attr, url, changes in credits:
            f.write(f"{rel}\n  {space} - {lic}\n  Credit: {attr}\n  Source: {url}\n  Changes: {changes}\n\n")
    subprocess.run(["cp", os.path.join(PACK, "CREDITS.txt"), os.path.join(ROOT, "Resources", "IR-CREDITS.txt")], check=True)
    size = sum(os.path.getsize(os.path.join(r, n)) for r, _, fs in os.walk(PACK) for n in fs)
    print(f"pack: {count} responses, {size / 1e6:.0f} MB")


if __name__ == "__main__":
    sys.exit(main())
