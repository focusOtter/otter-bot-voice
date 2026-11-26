#!/usr/bin/env python3
"""Draws docs/wiring.svg: ESP32-S3 DevKitC left header (J1) to the amp, mic and speaker."""
from pathlib import Path

W, H = 1250, 880
ROW = 30
J1 = ["3V3", "3V3", "RST", "4", "5", "6", "7", "15", "16", "17", "18", "8", "3", "46",
      "9", "10", "11", "12", "13", "14", "5V", "G"]
BX, BY = 90, 70  # board top-left
PIN_X = BX + 200

def pin_y(name, nth=0):
    idx = [i for i, p in enumerate(J1) if p == name][nth]
    return BY + 50 + idx * ROW

AMP = ("MAX98357A amp", 760, 80, ["LRC", "BCLK", "DIN", "GAIN", "SD", "GND", "VIN"])
MIC = ("INMP441 mic", 760, 400, ["L/R", "WS", "SCK", "SD", "VDD", "GND"])

def mod_pin_y(mod, name):
    _, _, y, pins = mod
    return y + 50 + pins.index(name) * ROW

WIRES = [  # (board pin, nth, module, module pin, color, label)
    ("5V", 0, AMP, "VIN", "#d62728", "5V"),
    ("G", 0, AMP, "GND", "#222222", "GND"),
    ("5", 0, AMP, "BCLK", "#1f77b4", "GPIO5"),
    ("6", 0, AMP, "LRC", "#2ca02c", "GPIO6"),
    ("7", 0, AMP, "DIN", "#ff7f0e", "GPIO7"),
    ("3V3", 0, MIC, "VDD", "#e377c2", "3V3"),
    ("G", 0, MIC, "GND", "#222222", "GND"),
    ("15", 0, MIC, "SCK", "#9467bd", "GPIO15"),
    ("16", 0, MIC, "WS", "#17becf", "GPIO16"),
    ("17", 0, MIC, "SD", "#8c564b", "GPIO17"),
]

out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" '
       'font-family="Helvetica, Arial, sans-serif">',
       f'<rect width="{W}" height="{H}" fill="#fbfaf7"/>',
       '<text x="30" y="40" font-size="24" font-weight="bold" fill="#333">Otter bot voice bench test: wiring</text>']

# board
bh = 50 + len(J1) * ROW
out.append(f'<rect x="{BX}" y="{BY}" width="230" height="{bh}" rx="12" fill="#1d3b2a" stroke="#0e2015"/>')
out.append(f'<text x="{BX + 20}" y="{BY + 30}" font-size="15" fill="#fff" font-weight="bold">ESP32-S3 DevKitC</text>')
out.append(f'<text x="{BX + 20}" y="{BY + bh - 12}" font-size="12" fill="#cfe">left header J1 · BOOT = talk</text>')
for i, p in enumerate(J1):
    y = BY + 50 + i * ROW
    used = any(w[0] == p for w in WIRES)
    label = p if not p.isdigit() else f"GPIO{p}"
    out.append(f'<circle cx="{PIN_X}" cy="{y}" r="7" fill="{"#f5c542" if used else "#777"}" stroke="#000"/>')
    out.append(f'<text x="{PIN_X - 15}" y="{y + 5}" font-size="14" text-anchor="end" fill="{"#fff" if used else "#9ab"}">{label}</text>')

def module(mod, extra):
    name, x, y, pins = mod
    h = 50 + len(pins) * ROW
    out.append(f'<rect x="{x}" y="{y}" width="210" height="{h}" rx="10" fill="#26408b" stroke="#132050"/>')
    out.append(f'<text x="{x + 40}" y="{y + 30}" font-size="16" fill="#fff" font-weight="bold">{name}</text>')
    for p in pins:
        py = mod_pin_y(mod, p)
        out.append(f'<circle cx="{x}" cy="{py}" r="7" fill="#f5c542" stroke="#000"/>')
        out.append(f'<text x="{x + 16}" y="{py + 5}" font-size="14" fill="#fff">{p}</text>')
        if p in extra:
            out.append(f'<text x="{x + 80}" y="{py + 5}" font-size="12" fill="#dde">{extra[p]}</text>')

module(AMP, {"GAIN": "leave open (9 dB)", "SD": "leave open (on)"})
module(MIC, {"L/R": "jumper to GND"})

for i, (bp, nth, mod, mp, color, label) in enumerate(WIRES):
    y1, y2 = pin_y(bp, nth), mod_pin_y(mod, mp)
    x1, x2 = PIN_X + 7, mod[1] - 7
    mid = 420 + i * 28
    out.append(f'<path d="M{x1},{y1} H{mid} V{y2} H{x2}" fill="none" stroke="{color}" stroke-width="4" '
               'stroke-linejoin="round" opacity="0.9"/>')

# mic L/R to GND jumper
lr, gnd = mod_pin_y(MIC, "L/R"), mod_pin_y(MIC, "GND")
mx = MIC[1] - 7
out.append(f'<path d="M{mx},{lr} h-22 V{gnd} h22" fill="none" stroke="#222" stroke-width="3" stroke-dasharray="6 4"/>')

# speaker
sx, sy = 1130, 150
out.append(f'<rect x="{AMP[1] + 210}" y="{sy - 40}" width="14" height="60" fill="#555"/>')
out.append(f'<text x="{AMP[1] + 232}" y="{sy - 32}" font-size="13" fill="#333">+</text>')
out.append(f'<text x="{AMP[1] + 232}" y="{sy + 22}" font-size="13" fill="#333">-</text>')
out.append(f'<path d="M{AMP[1] + 224},{sy - 25} H{sx - 20}" stroke="#d62728" stroke-width="4"/>')
out.append(f'<path d="M{AMP[1] + 224},{sy + 5} H{sx - 20}" stroke="#222" stroke-width="4"/>')
out.append(f'<circle cx="{sx + 20}" cy="{sy - 10}" r="42" fill="#333" stroke="#111"/>')
out.append(f'<circle cx="{sx + 20}" cy="{sy - 10}" r="18" fill="#666"/>')
out.append(f'<text x="{sx - 25}" y="{sy + 60}" font-size="13" fill="#333">40 mm 4 Ω 3 W</text>')

notes = ["USB-C into the UART port · power is USB only",
         "Snap the antenna onto the IPEX jack first",
         "Never wire GPIO35-37 (PSRAM) or 0/3/45/46"]
for i, n in enumerate(notes):
    out.append(f'<text x="760" y="{720 + i * 24}" font-size="15" fill="#444">• {n}</text>')
out.append("</svg>")
Path(__file__).resolve().parent.parent.joinpath("docs/wiring.svg").write_text("\n".join(out))
print("wrote docs/wiring.svg")
