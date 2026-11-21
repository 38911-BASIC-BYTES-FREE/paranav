# Usage: python3 render_stl_preview.py <model.stl> <out.png> [pl|en]
# Painter's-algorithm render of a binary STL (per-triangle depth sort, flat shading) - clean preview for the README
import struct, sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection

src, out = sys.argv[1], sys.argv[2]
lang = sys.argv[3] if len(sys.argv) > 3 else "pl"
L = {"pl": ["od strony płyty (na zewnątrz)", "od środka – komora na GY-63", "ścianka z okienkami", "Plecki z komorą na barometr GY-63"],
     "en": ["plate side (outside)", "inside – GY-63 compartment", "wall with the vent windows", "Back cover with a GY-63 barometer compartment"]}[lang]
d = open(src, "rb").read()
n = struct.unpack("<I", d[80:84])[0]
t = np.frombuffer(d[84:84 + 50 * n], dtype=np.dtype([("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")]))
V = t["v"].reshape(-1, 3, 3).astype(float)
V -= V.reshape(-1, 3).min(axis=0)
C = V.reshape(-1, 3).max(axis=0) / 2
V -= C

def rot(elev, azim):
    e, a = np.radians(elev), np.radians(azim)
    Rz = np.array([[np.cos(a), -np.sin(a), 0], [np.sin(a), np.cos(a), 0], [0, 0, 1]])
    Rx = np.array([[1, 0, 0], [0, np.cos(e), -np.sin(e)], [0, np.sin(e), np.cos(e)]])
    return Rx @ Rz

def draw(ax, elev, azim, title):
    R = rot(elev, azim)
    P = V @ R.T                      # x right, y up(ish), z towards viewer after rotation about x
    # camera looks along -y' : use (x, z) as screen, y as depth
    scr = P[:, :, [0, 2]]
    depth = P[:, :, 1].mean(axis=1)
    Nrm = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
    Nrm /= np.linalg.norm(Nrm, axis=1, keepdims=True) + 1e-12
    L = np.array([-0.35, -0.8, 0.5]); L /= np.linalg.norm(L)
    sh = 0.35 + 0.65 * np.abs(Nrm @ L)
    order = np.argsort(-depth)       # far first
    base = np.array([0.30, 0.52, 0.78])
    cols = np.clip(base[None, :] * sh[:, None] + 0.12, 0, 1)
    pc = PolyCollection(scr[order], facecolors=cols[order], edgecolors=cols[order] * 0.85, linewidths=0.3)
    ax.add_collection(pc)
    ax.autoscale_view(); ax.set_aspect("equal"); ax.axis("off")
    ax.set_title(title, fontsize=12)

fig, axs = plt.subplots(1, 3, figsize=(15, 5.4), dpi=110)
draw(axs[0], 25, 30, L[0])
draw(axs[1], -40, 25, L[1])
draw(axs[2], -15, 100, L[2])
mx = (C * 2)
fig.suptitle(f"{L[3]}  –  {mx[0]:.1f} × {mx[1]:.1f} × {mx[2]:.1f} mm", fontsize=13)
plt.tight_layout()
plt.savefig(out, facecolor="white")
