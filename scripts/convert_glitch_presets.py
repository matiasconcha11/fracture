#!/usr/bin/env python3
"""Converts Glitch 2 preset banks (.g2bnk) into Fracture factory presets.

Usage: scripts/convert_glitch_presets.py <folder with designer subfolders of .g2bnk files>
Writes Source/GlitchLibrary.inc (included by PresetManager.cpp) and prints a summary.

What carries over: each scene's sequencer pattern and its timing (beats x divisions per beat).
What doesn't: the Glitch effect modules' own settings (their parameter layout isn't documented),
so converted presets use Fracture's lane settings. Glitch can stack effects on the same step;
Fracture plays one, so the most recently started effect wins.
"""

import os
import re
import sys
import xml.etree.ElementTree as ET

# Glitch 2 sequencer rows -> Fracture lane letters (see Patterns.cpp).
ROW_TO_LETTER = {
    0: "?",  # Randomizer -> Random
    1: "M",  # Modulator -> Ring Mod
    2: "T",  # Tape Stop
    3: "R",  # Retrigger -> Repeat
    4: "V",  # Reverser -> Reverse
    5: "X",  # Stretcher -> Stretch
    6: "C",  # Lofi -> Crush
    7: "D",  # Distortion -> Distort
    8: "G",  # Gater -> Gate
    9: "E",  # Delay -> Echo
    10: "S", # Shuffler -> Shuffle
}

# Divisions per beat -> index into Fracture's step lengths ("1/8", "1/16", "1/32", "1/8T", "1/16T").
DIVISION_TO_STEP = {2: 0, 4: 1, 8: 2, 3: 3, 6: 4}

STEPS = 16
PATTERNS = 4


def bank_label(designer, filename):
    """'Supremeja Bank Delay SJA.g2bnk' -> 'Delay'; 'Supremeja Combi Bank 1 SJA' -> 'Combi 1'."""
    name = os.path.splitext(filename)[0]
    for word in designer.split() + ["Sink", "SJA", "GJ"]:
        name = re.sub(rf"\b{re.escape(word)}\b", "", name)
    name = re.sub(r"\bBank\b", "", name)
    name = " ".join(name.split())
    return name or "Bank"


def blocks(row_text, steps):
    """[(start, length)] for '*' followed by '+' runs."""
    return [(m.start(), len(m.group())) for m in re.finditer(r"\*\+*", row_text[:steps])]


def convert_scene(sequencer):
    beats = int(sequencer.findtext("NumBeats", "4"))
    divisions = int(sequencer.findtext("NumStepsPerBeat", "4"))
    steps = beats * divisions

    if divisions not in DIVISION_TO_STEP:
        return None, "unsupported division"

    if steps > STEPS and (steps % STEPS != 0 or steps > STEPS * PATTERNS):
        return None, "too long"

    rows = {}
    for child in sequencer:
        m = re.match(r"Sequence(\d+)$", child.tag)
        if m and int(m.group(1)) in ROW_TO_LETTER:
            rows[int(m.group(1))] = child.text or ""

    # For each step, the block that covers it and started most recently (lower row breaks ties).
    covering = [None] * steps
    for row, text in sorted(rows.items()):
        for start, length in blocks(text, steps):
            for i in range(start, min(start + length, steps)):
                current = covering[i]
                if current is None or start > current[1]:
                    covering[i] = (row, start)

    if all(c is None for c in covering):
        return None, "empty"

    text = ""
    for i, block in enumerate(covering):
        if block is None:
            text += "."
        elif i % STEPS != 0 and covering[i - 1] == block:
            text += "+"
        else:
            text += ROW_TO_LETTER[block[0]]

    chunks = [text[i:i + STEPS] for i in range(0, len(text), STEPS)]
    patterns = chunks + [""] * (PATTERNS - len(chunks))
    length = min(steps, STEPS)
    chain = len(chunks) > 1
    return (patterns, DIVISION_TO_STEP[divisions], length, chain), None


def cpp_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main(folder, output):
    entries, seen, skipped = [], set(), {}

    for designer in sorted(os.listdir(folder)):
        path = os.path.join(folder, designer)
        if not os.path.isdir(path):
            continue

        banks = sorted(f for f in os.listdir(path) if f.endswith(".g2bnk") and not f.startswith("._"))

        for bank in banks:
            category = f"Glitch 2 Library/{designer}" + (f"/{bank_label(designer, bank)}" if len(banks) > 1 else "")
            root = ET.parse(os.path.join(path, bank)).getroot()

            for program in root:
                if not program.tag.startswith("Program"):
                    continue

                name = (program.findtext("Name") or "Untitled").strip()
                converted = []

                for scene in program:
                    sequencer = scene.find("Sequencer") if scene.tag.startswith("Scene") else None
                    if sequencer is None:
                        continue

                    result, reason = convert_scene(sequencer)
                    if result is None:
                        skipped[reason] = skipped.get(reason, 0) + 1
                    else:
                        converted.append(result)

                for i, (patterns, step, length, chain) in enumerate(converted):
                    key = (tuple(patterns), step, length)
                    if key in seen:
                        skipped["duplicate"] = skipped.get("duplicate", 0) + 1
                        continue

                    seen.add(key)
                    label = name if len(converted) == 1 else f"{name} {i + 1}"
                    entries.append((category, label, patterns, step, length, chain))

    with open(output, "w") as f:
        f.write("// Generated by scripts/convert_glitch_presets.py from the Glitch 2 factory banks.\n")
        f.write("// Pattern designs by Illformed, Alex Shore, Rick Christy (Soundgasm Design), Gotthard Ortner\n")
        f.write("// (Supremeja Music) and WaveShaper/Sink: see CREDITS.md. Do not edit by hand.\n")
        f.write("// { category, name, { pattern A, B, C, D }, step length index, pattern length, chain }\n")
        for category, label, patterns, step, length, chain in entries:
            pats = ", ".join(cpp_string(p) for p in patterns)
            f.write(f"{{ {cpp_string(category)}, {cpp_string(label)}, {{ {pats} }}, {step}, {length}, {'true' if chain else 'false'} }},\n")

    print(f"{len(entries)} presets written to {output}")
    print("skipped:", skipped)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)

    here = os.path.dirname(os.path.abspath(__file__))
    main(sys.argv[1], os.path.join(here, "..", "Source", "GlitchLibrary.inc"))
