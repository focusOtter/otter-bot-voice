#!/usr/bin/env python3
"""Writes docs/architecture.excalidraw (open it at excalidraw.com) and renders docs/architecture.svg via kroki.io."""
import json, random, urllib.request
from pathlib import Path

els = []
def _id():
    return "%016x" % random.getrandbits(64)
def base(kind, x, y, w, h, **kw):
    e = dict(id=_id(), type=kind, x=x, y=y, width=w, height=h, angle=0, strokeColor="#1e1e1e",
             backgroundColor="transparent", fillStyle="solid", strokeWidth=2, strokeStyle="solid",
             roughness=1, opacity=100, groupIds=[], seed=random.randint(1, 2**31), version=1,
             versionNonce=random.randint(1, 2**31), isDeleted=False, boundElements=None,
             updated=1, link=None, locked=False)
    e.update(kw)
    els.append(e)
    return e
def text(x, y, s, size=18, w=None):
    lines = s.split("\n")
    w = w or max(len(l) for l in lines) * size * 0.55
    return base("text", x, y, w, len(lines) * size * 1.25, text=s, fontSize=size, fontFamily=1,
                textAlign="left", verticalAlign="top", baseline=size, containerId=None,
                originalText=s, lineHeight=1.25)
def box(x, y, w, h, label, color):
    base("rectangle", x, y, w, h, backgroundColor=color, roundness={"type": 3})
    text(x + 14, y + 12, label, 17)
def arrow(x1, y1, x2, y2, label=None, ly=-28):
    base("arrow", x1, y1, x2 - x1, y2 - y1, points=[[0, 0], [x2 - x1, y2 - y1]],
         startArrowhead=None, endArrowhead="arrow", roundness={"type": 2})
    if label:
        text(min(x1, x2) + 8, min(y1, y2) + ly, label, 14)

text(40, 20, "otter-bot-voice: push-to-talk with an AI that answers as Devin", 24)
box(40, 100, 230, 110, "INMP441 mic\nI2S in, 16 kHz", "#d0ebff")
box(40, 260, 230, 110, "MAX98357A amp\n+ 40 mm 4 Ω speaker", "#d0ebff")
box(360, 160, 270, 150, "ESP32-S3 N16R8\nBOOT = push-to-talk\nPSRAM audio buffers\nWi-Fi + TLS WebSocket", "#b2f2bb")
box(800, 140, 300, 190, "ElevenLabs\nConversational AI agent\n\"Devin\" persona\nspeech-to-text, LLM,\nFlash TTS voice", "#ffec99")
box(360, 400, 270, 90, "Your laptop\nUSB power + serial menu", "#eeeeee")
arrow(270, 155, 360, 205, "mic PCM")
arrow(360, 270, 270, 315, "reply PCM", 10)
arrow(630, 205, 800, 205, "user_audio_chunk", -26)
arrow(800, 270, 630, 270, "audio_event", 8)
arrow(495, 400, 495, 310)
text(800, 360, "wss://api.elevenlabs.io/v1/convai/conversation\n16 kHz 16-bit mono PCM, base64 in JSON", 14)

doc = {"type": "excalidraw", "version": 2, "source": "https://excalidraw.com", "elements": els,
       "appState": {"viewBackgroundColor": "#ffffff", "gridSize": None}, "files": {}}
root = Path(__file__).resolve().parent.parent / "docs"
(root / "architecture.excalidraw").write_text(json.dumps(doc, indent=1))
req = urllib.request.Request("https://kroki.io/excalidraw/svg", data=json.dumps(doc).encode(),
                             headers={"Content-Type": "text/plain", "User-Agent": "otter-bot-voice"})
(root / "architecture.svg").write_bytes(urllib.request.urlopen(req).read())
print("wrote docs/architecture.excalidraw and docs/architecture.svg")
