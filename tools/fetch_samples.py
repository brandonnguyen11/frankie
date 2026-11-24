#!/usr/bin/env python3
"""Fetch the MVP instrument samples from VSCO 2 Community Edition (CC0).

Downloads one velocity layer per instrument from github.com/sgossner/VSCO-2-CE,
converts each file to 48 kHz mono 16-bit WAV (macOS `afconvert`), trims it to a
few seconds with a short fade, and writes assets/samples/<instrument>/<NOTE>.wav,
the layout AudioEngine loads. Run once:  python3 tools/fetch_samples.py
"""
import json, os, re, shutil, struct, subprocess, sys, tempfile, urllib.parse, wave

REPO = "sgossner/VSCO-2-CE"
RAW = f"https://raw.githubusercontent.com/{REPO}/master/"
TREE = f"https://api.github.com/repos/{REPO}/git/trees/master?recursive=1"
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "samples")

# instrument -> (folder, filename filter regex, seconds to keep)
PLAN = {
    "piano":   ("Keys/Upright Nr1/", r"UR1_[CG][2-6]_mf_RR1\.wav$", 4.0),
    "violin":  ("Strings/Solo Violin/Arco Vib/", r"_f\.wav$", 5.0),
    "cello":   ("Strings/Cello Section/susvib/", r"_v3_1\.wav$", 5.0),
    "flute":   ("Woodwinds/Flute/susvib/", r"_v1_1\.wav$", 5.0),
    "trumpet": ("Brass/Trumpet/sus/", r"_v3_rr1\.wav$", 4.0),
    "timpani": ("Percussion/Timpani/", r"Timpani[1-5]_Hit_v3_rr1_Sum\.wav$", 4.0),
}
# timpani files are numbered drums, not notes: approximate tunings, largest drum first
TIMPANI_NOTES = {"1": "D2", "2": "F2", "3": "A2", "4": "C3", "5": "E3"}
NOTE_RE = re.compile(r"(?<![A-Za-z])([A-G])(#|s|b)?(\d)(?!\d)")


def note_name(inst, fname):
    if inst == "timpani":
        return TIMPANI_NOTES[re.search(r"Timpani(\d)", fname).group(1)]
    m = NOTE_RE.search(fname.replace("_", " "))
    if not m:
        return None
    acc = {"#": "s", "s": "s", "b": "b"}.get(m.group(2) or "", "")
    return f"{m.group(1)}{acc}{m.group(3)}"


def trim(path_in, path_out, seconds):
    with wave.open(path_in, "rb") as w:
        rate, n = w.getframerate(), w.getnframes()
        keep = min(n, int(rate * seconds))
        data = w.readframes(keep)
    samples = list(struct.unpack(f"<{len(data)//2}h", data))
    # skip leading silence, fade the tail
    start = next((i for i, s in enumerate(samples) if abs(s) > 200), 0)
    start = max(0, start - int(rate * 0.003))
    samples = samples[start:]
    fade = min(len(samples), int(rate * 0.25))
    for i in range(fade):
        samples[-1 - i] = int(samples[-1 - i] * i / fade)
    with wave.open(path_out, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes(struct.pack(f"<{len(samples)}h", *samples))


def fetch(url, dst=None):
    # curl ships with macOS and uses the system trust store (python.org builds often lack CA certs)
    cmd = ["curl", "-sSfL", "--retry", "3", url] + (["-o", dst] if dst else [])
    return subprocess.run(cmd, check=True, capture_output=dst is None).stdout


def main():
    if not shutil.which("afconvert"):
        sys.exit("afconvert not found (macOS only). Convert manually or use another machine.")
    tree = json.loads(fetch(TREE))["tree"]
    files = [t["path"] for t in tree if t["type"] == "blob"]
    tmp = tempfile.mkdtemp(prefix="og_samples_")
    total = 0
    for inst, (folder, pattern, secs) in PLAN.items():
        out_dir = os.path.join(ROOT, inst)
        os.makedirs(out_dir, exist_ok=True)
        picks = [f for f in files if f.startswith(folder) and re.search(pattern, f)]
        print(f"{inst}: {len(picks)} files")
        for f in picks:
            name = note_name(inst, os.path.basename(f))
            if not name:
                continue
            dst = os.path.join(out_dir, name + ".wav")
            if os.path.exists(dst):
                continue
            src = os.path.join(tmp, "src.wav")
            conv = os.path.join(tmp, "conv.wav")
            fetch(RAW + urllib.parse.quote(f), src)
            subprocess.run(["afconvert", "-f", "WAVE", "-d", "LEI16@48000", "-c", "1", src, conv], check=True)
            trim(conv, dst, secs)
            total += os.path.getsize(dst)
            print(f"  {name}.wav")
    shutil.rmtree(tmp, ignore_errors=True)
    with open(os.path.join(ROOT, "SOURCE.txt"), "w") as fh:
        fh.write("Instrument samples: VSCO 2 Community Edition by Versilian Studios, CC0 1.0.\n"
                 "https://github.com/sgossner/VSCO-2-CE  (converted to 48 kHz mono, trimmed)\n")
    print(f"done, {total/1e6:.1f} MB written to {os.path.normpath(ROOT)}")


if __name__ == "__main__":
    main()
