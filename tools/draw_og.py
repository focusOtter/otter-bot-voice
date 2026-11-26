#!/usr/bin/env python3
"""Renders docs/og-image.png (1280x640) for the repo's social preview."""
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, FancyBboxPatch, Arc

fig = plt.figure(figsize=(12.8, 6.4), dpi=100)
ax = fig.add_axes([0, 0, 1, 1]); ax.set_xlim(0, 1280); ax.set_ylim(0, 640); ax.axis("off")
fig.patch.set_facecolor("#0f2a3d")
ax.add_patch(FancyBboxPatch((60, 60), 1160, 520, boxstyle="round,pad=0,rounding_size=30", fc="#143a54", ec="#2f6f95", lw=3))
ax.text(110, 470, "otter-bot-voice", fontsize=54, color="white", weight="bold")
ax.text(112, 400, "Talk to Devin from an ESP32-S3 breadboard", fontsize=26, color="#cfe8f7")
ax.text(112, 345, "MAX98357A amp · INMP441 mic · push-to-talk · ElevenLabs", fontsize=19, color="#8fc3e0")
for i, t in enumerate(["tone", "mic level", "loopback", "chat"]):
    ax.add_patch(FancyBboxPatch((112 + i * 165, 200), 150, 60, boxstyle="round,pad=0,rounding_size=14", fc="#f5c542", ec="none"))
    ax.text(187 + i * 165, 222, t, ha="center", fontsize=19, color="#0f2a3d", weight="bold")
# otter face
cx, cy = 1030, 330
ax.add_patch(Circle((cx - 118, cy + 40), 24, fc="#8a5a3b")); ax.add_patch(Circle((cx + 118, cy + 40), 24, fc="#8a5a3b"))
ax.add_patch(Circle((cx, cy), 135, fc="#a06a45"))
ax.add_patch(Circle((cx, cy - 40), 80, fc="#e8cfb0"))
ax.add_patch(Circle((cx - 50, cy + 35), 14, fc="#1b1b1b")); ax.add_patch(Circle((cx + 50, cy + 35), 14, fc="#1b1b1b"))
ax.add_patch(Circle((cx, cy - 15), 18, fc="#3b2416"))
ax.add_patch(Arc((cx, cy - 45), 60, 40, theta1=200, theta2=340, color="#3b2416", lw=4))
for dy in (-20, -35, -50):
    ax.plot([cx - 40, cx - 115], [cy - 25, cy + dy], color="#3b2416", lw=2)
    ax.plot([cx + 40, cx + 115], [cy - 25, cy + dy], color="#3b2416", lw=2)
for r in (165, 195, 225):
    ax.add_patch(Arc((cx, cy), r * 2, r * 2, theta1=-30, theta2=30, color="#f5c542", lw=5, alpha=0.9))
out = Path(__file__).resolve().parent.parent / "docs/og-image.png"
fig.savefig(out, facecolor=fig.get_facecolor()); print("wrote", out)
