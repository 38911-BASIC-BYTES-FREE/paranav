#!/usr/bin/env python3
"""ParaNav - topographic background map generator (black & white, for the e-paper screen).

Reads OpenStreetMap extracts (.osm.pbf, e.g. from download.geofabrik.de) and renders
1-bit 256x256 tiles (Web Mercator / "slippy map" numbering) for zoom levels 10..14.
Output - copy the whole "maps" folder to the root of the SD card:

    <out>/maps/<name>/info.txt
    <out>/maps/<name>/z10.pak ... z14.pak

Example (Gdansk, radius 100 km):
    python3 make_tiles.py --pbf pomorskie-latest.osm.pbf --pbf kujawsko-pomorskie-latest.osm.pbf \
        --pbf warminsko-mazurskie-latest.osm.pbf --pbf kaliningrad-latest.osm.pbf \
        --name gdansk --title "Gdańsk 100 km" --center 54.35,18.65 --radius-km 100 --out out

Needs: Python 3.9+, pip install osmium shapely pillow numpy
Map data (c) OpenStreetMap contributors, ODbL.

What is drawn (aimed at a paramotor pilot - obstacles first):
  water and sea (horizontal lines, solid shoreline), forest (dots), built-up areas (cross-hatch),
  power lines (line with cross ticks), wind turbines, masts/towers/chimneys (+ height),
  aerodromes/airstrips + runways, main roads, railways, rivers, place names.

.pak file format (little endian), one file per zoom level:
  0  char[4] "PNMP"      4 u16 version=1     6 u8 zoom   7 u8 0
  8  u32 x0  12 u32 y0  16 u16 nx  18 u16 ny     (tile range covered by the index)
  20 u32 index offset (=32)  24 u32 data offset  28 u32 0
  index: nx*ny entries (row-major, y then x) of {u32 offset, u32 length}
         length 0 + offset 0          -> white tile (inside the area, nothing drawn)
         length 0 + offset 0xFFFFFFFF -> no data (outside the area)
  tile record: u8 mode (0 plain, 1 = rows XOR row-8), u8 0, u16 0, u32 CRC32 of the 8192-byte
               bitmap, then PackBits-compressed bitmap (32 bytes per row, MSB = left pixel, 1 = black)
Identical tiles (open sea, solid forest) are stored once.
"""
import argparse
import datetime
import hashlib
import math
import os
import struct
import sys
import time
import zlib

import numpy as np
import shapely
from PIL import Image, ImageDraw, ImageFont
from shapely.geometry import LineString, MultiLineString, Point, Polygon, box
from shapely.ops import polygonize, unary_union

TILE = 256
META = 8  # tiles per metatile side (rendered together, then cut)
MARGIN = 48  # px rendered around a metatile so symbols/labels crossing tile edges match

# ----------------------------------------------------------------------------- projection


def world_xy(lon, lat):
    """lon/lat (deg) -> Web Mercator world coords 0..1"""
    lat = max(-85.0511, min(85.0511, lat))
    x = (lon + 180.0) / 360.0
    s = math.sin(math.radians(lat))
    y = 0.5 - math.log((1 + s) / (1 - s)) / (4 * math.pi)
    return x, y


def world_lonlat(x, y):
    lon = x * 360.0 - 180.0
    lat = math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * y))))
    return lon, lat


# ----------------------------------------------------------------------------- tag classification

URBAN_LANDUSE = {"residential", "commercial", "industrial", "retail", "garages", "construction", "railway"}
OBST_MAN_MADE = {"mast", "tower", "communications_tower", "chimney", "water_tower", "flare", "crane"}
AERO = {"aerodrome", "airstrip"}


def is_wind(tags):
    return tags.get("power") == "generator" and (
        tags.get("generator:source") == "wind" or tags.get("generator:method") == "wind_turbine")


def area_class(tags):
    nat, lu = tags.get("natural"), tags.get("landuse")
    if nat == "water" or tags.get("waterway") == "riverbank" or lu in ("reservoir", "basin"):
        return "water"
    if nat == "wood" or lu == "forest":
        return "forest"
    if lu in URBAN_LANDUSE:
        return "urban"
    return None


def line_class(tags):
    hw = tags.get("highway")
    if hw in ("motorway", "trunk", "motorway_link", "trunk_link"):
        return "road1"
    if hw in ("primary", "primary_link"):
        return "road2"
    if hw in ("secondary",):
        return "road3"
    if hw in ("tertiary",):
        return "road4"
    if tags.get("railway") in ("rail", "light_rail", "narrow_gauge"):
        return "rail"
    ww = tags.get("waterway")
    if ww in ("river", "canal"):
        return "river"
    if ww == "stream":
        return "stream"
    pw = tags.get("power")
    if pw == "line":
        return "power"
    if pw == "minor_line":
        return "power_minor"
    if tags.get("aeroway") == "runway":
        return "runway"
    if tags.get("natural") == "coastline":
        return "coast"
    return None


def parse_height(tags):
    for k in ("height", "ele:top"):  # metres above ground
        v = tags.get(k)
        if v:
            try:
                return float(v.replace("m", "").replace(",", ".").strip().split()[0])
            except ValueError:
                pass
    return None


def point_class(tags):
    """-> (class, name) for point features (also used for area centroids)"""
    if is_wind(tags):
        return "wind"
    if tags.get("man_made") in OBST_MAN_MADE:
        if tags.get("man_made") == "tower" and tags.get("tower:type") in ("observation", "bell_tower") and \
                not parse_height(tags):
            return "mast"
        return "mast"
    if tags.get("aeroway") in AERO:
        return tags.get("aeroway")
    if tags.get("place") in ("city", "town", "village", "hamlet") and tags.get("name"):
        return "place"
    return None


# ----------------------------------------------------------------------------- OSM reading


class Data:
    def __init__(self):
        self.polys = {"water": [], "forest": [], "urban": [], "aerodrome": []}
        self.lines = {k: [] for k in ("road1", "road2", "road3", "road4", "rail", "river", "stream", "power",
                                      "power_minor", "runway", "coast")}
        self.points = []  # dicts: cls, x, y, name, height, kind
        self.seen = set()

    def stats(self):
        s = {k: len(v) for k, v in self.polys.items()}
        s.update({k: len(v) for k, v in self.lines.items()})
        s["points"] = len(self.points)
        return s


def read_pbf(path, data, bbox_world):
    import osmium
    from osmium.filter import KeyFilter

    bx0, by0, bx1, by1 = bbox_world
    keys = ("natural", "landuse", "waterway", "power", "aeroway", "highway", "railway", "man_made", "place")
    fp = osmium.FileProcessor(path).with_locations().with_areas().with_filter(KeyFilter(*keys))

    def inside(xs, ys):
        return not (max(xs) < bx0 or min(xs) > bx1 or max(ys) < by0 or min(ys) > by1)

    def ring_xy(ring):
        pts = [world_xy(n.lon, n.lat) for n in ring if n.location.valid()]
        return pts

    n_obj = 0
    for o in fp:
        n_obj += 1
        t = o.type_str()
        tags = o.tags
        if t == "n":
            pc = point_class(tags)
            if not pc:
                continue
            key = ("n", o.id)
            if key in data.seen:
                continue
            data.seen.add(key)
            if not o.location.valid():
                continue
            x, y = world_xy(o.location.lon, o.location.lat)
            if not inside([x], [y]):
                continue
            add_point(data, pc, x, y, tags)
        elif t == "w":
            lc = line_class(tags)
            if not lc:
                continue
            key = ("w", o.id)
            if key in data.seen:
                continue
            data.seen.add(key)
            pts = ring_xy(o.nodes)
            if len(pts) < 2:
                continue
            xs, ys = zip(*pts)
            if not inside(xs, ys):
                continue
            w = None
            try:
                w = float(tags.get("width", "").replace("m", "")) if lc == "runway" and tags.get("width") else None
            except ValueError:
                w = None
            data.lines[lc].append((LineString(pts), {"width": w, "voltage": tags.get("voltage")}))
        elif t == "a":
            ac = area_class(tags)
            pc = point_class(tags)
            aero_area = tags.get("aeroway") == "aerodrome"
            if not ac and not pc and not aero_area:
                continue
            key = ("a", o.orig_id(), o.from_way())
            if key in data.seen:
                continue
            data.seen.add(key)
            polys = []
            for outer in o.outer_rings():
                ext = ring_xy(outer)
                if len(ext) < 4:
                    continue
                holes = [ring_xy(inner) for inner in o.inner_rings(outer)]
                holes = [h for h in holes if len(h) >= 4]
                try:
                    p = Polygon(ext, holes)
                    if not p.is_valid:
                        p = p.buffer(0)
                except Exception:
                    continue
                if p.is_empty:
                    continue
                polys.append(p)
            if not polys:
                continue
            geom = unary_union(polys) if len(polys) > 1 else polys[0]
            minx, miny, maxx, maxy = geom.bounds
            if not inside([minx, maxx], [miny, maxy]):
                continue
            if ac:
                for g in getattr(geom, "geoms", [geom]):
                    if isinstance(g, Polygon):
                        data.polys[ac].append(g)
            if aero_area:
                for g in getattr(geom, "geoms", [geom]):
                    if isinstance(g, Polygon):
                        data.polys["aerodrome"].append(g)
            if pc:
                c = geom.representative_point()
                add_point(data, pc, c.x, c.y, tags)
    return n_obj


def add_point(data, pc, x, y, tags):
    name = tags.get("name") or ""
    icao = tags.get("icao") or ""
    data.points.append({"cls": pc, "x": x, "y": y, "name": name, "icao": icao,
                        "kind": tags.get("place") or tags.get("aeroway") or tags.get("man_made") or "",
                        "height": parse_height(tags)})


# ----------------------------------------------------------------------------- coastline -> sea polygons


def build_sea(coast_lines, bbox_world):
    """Turns natural=coastline ways (land on the LEFT of the way direction) into sea polygons
    inside bbox_world, using polygonize + a side test on every face."""
    if not coast_lines:
        return []
    bx0, by0, bx1, by1 = bbox_world
    frame = box(bx0, by0, bx1, by1)
    clipped = []
    for ln, _ in coast_lines:
        g = ln.intersection(frame)
        for part in getattr(g, "geoms", [g]):
            if isinstance(part, LineString) and len(part.coords) >= 2:
                clipped.append(part)
    if not clipped:
        return []
    noded = unary_union(clipped + [frame.exterior])
    faces = list(polygonize(noded))
    # directed coastline segments for the side test
    segs = []
    for part in clipped:
        c = np.asarray(part.coords)
        for i in range(len(c) - 1):
            segs.append((c[i], c[i + 1]))
    seg_tree = shapely.STRtree([LineString([a, b]) for a, b in segs])
    sea = []
    ambiguous = 0
    for f in faces:
        f = shapely.geometry.polygon.orient(f, 1.0)  # exterior CCW in (x,y) math sense
        ext = np.asarray(f.exterior.coords)
        water = land = 0.0
        for i in range(len(ext) - 1):
            a, b = ext[i], ext[i + 1]
            m = (a + b) / 2
            L = float(np.hypot(*(b - a)))
            if L == 0:
                continue
            cand = seg_tree.query(Point(m).buffer(L * 1e-6 + 1e-12))
            best = None
            for ci in cand:
                sa, sb = segs[ci]
                d = LineString([sa, sb]).distance(Point(m))
                if d < 1e-10 and (best is None or d < best[0]):
                    best = (d, sb - sa)
            if best is None:
                continue  # frame edge
            fdir = (b - a) / L
            cdir = best[1] / (np.hypot(*best[1]) or 1)
            # CCW exterior (y axis pointing DOWN in world coords => CCW math == CW on screen).
            # In world coords (y down) "left of direction" = (dy, -dx) ... handled by comparing
            # directions: face edge running the same way as the coastline means the face lies on the
            # same side as the face interior of a CCW ring, i.e. on the LEFT in math coords.
            same = float(np.dot(fdir, cdir)) > 0
            # math-left with y down == screen-right. Land is on the left of the coastline direction
            # as seen on a map (y up, north up) -> screen-left. So same direction => face on the
            # screen-right of the coastline => water.
            if same:
                water += L
            else:
                land += L
        if water == 0 and land == 0:
            continue
        if water > 0 and land > 0 and min(water, land) > 0.2 * max(water, land):
            ambiguous += 1
            continue
        if water > land:
            sea.append(f)
    if ambiguous:
        print(f"  UWAGA: {ambiguous} obszar(y) brzegu morza niejednoznaczne (przerwana linia brzegowa?) - "
              f"pominięte. Dodaj sąsiedni plik .pbf, żeby linia brzegowa była ciągła.")
    return sea


# ----------------------------------------------------------------------------- rendering helpers


def find_font(bold=False):
    # same family as the device fonts (DejaVu Sans Condensed Bold) - readable on e-paper
    names = ["DejaVuSansCondensed-Bold.ttf", "DejaVuSans-Bold.ttf",
             "arialbd.ttf" if bold else "arial.ttf", "LiberationSans-Bold.ttf" if bold else "LiberationSans-Regular.ttf"]
    dirs = [os.path.dirname(os.path.abspath(__file__)), "/usr/share/fonts/truetype/dejavu",
            "/usr/share/fonts/truetype/liberation", "/usr/share/fonts/TTF", "/Library/Fonts",
            "/System/Library/Fonts/Supplemental", "C:/Windows/Fonts"]
    for d in dirs:
        for n in names:
            p = os.path.join(d, n)
            if os.path.exists(p):
                return p
    return None


class Fonts:
    def __init__(self):
        self.reg = find_font(False)
        self.bold = find_font(True) or self.reg
        self.cache = {}
        if not self.reg:
            print("UWAGA: nie znaleziono czcionki TTF (DejaVuSans) - napisy będą bez polskich znaków")

    def get(self, size, bold=False):
        k = (size, bold)
        if k not in self.cache:
            p = self.bold if bold else self.reg
            self.cache[k] = ImageFont.truetype(p, size) if p else ImageFont.load_default()
        return self.cache[k]


_PAT = {}


def pattern(kind, ox, oy, w, h):
    k = (kind, ox % 8, oy % 8, w, h)
    if k not in _PAT:
        _PAT[k] = _pattern(kind, ox, oy, w, h)
    return _PAT[k]


def _pattern(kind, ox, oy, w, h):
    """bool array: pattern pixels set (black). Aligned to global pixel coords -> seamless tiles.
    All periods divide 8 so the XOR-8 tile mode compresses filled areas to almost nothing."""
    X = (np.arange(w) + ox) % 8
    Y = (np.arange(h) + oy) % 8
    Xg, Yg = np.meshgrid(X, Y)
    if kind == "water":
        return (Yg == 0) & (Xg != 7)
    if kind == "forest":  # 2x2 dots on a staggered 8 px grid
        return ((Xg < 2) & (Yg < 2)) | ((Xg >= 4) & (Xg < 6) & (Yg >= 4) & (Yg < 6))
    if kind == "urban":  # cross-hatch
        return ((Xg + Yg) % 8 == 0) | ((Xg - Yg) % 8 == 0)
    raise ValueError(kind)


def along(coords, step, phase=0.0):
    """Points every `step` px along a polyline: list of (x, y, ux, uy) (unit direction)."""
    out = []
    c = np.asarray(coords, dtype=float)
    if len(c) < 2:
        return out
    d = np.hypot(np.diff(c[:, 0]), np.diff(c[:, 1]))
    pos = phase
    acc = 0.0
    for i in range(len(d)):
        L = d[i]
        if L <= 0:
            continue
        while pos <= acc + L:
            t = (pos - acc) / L
            p = c[i] + (c[i + 1] - c[i]) * t
            u = (c[i + 1] - c[i]) / L
            out.append((p[0], p[1], u[0], u[1]))
            pos += step
        acc += L
    return out


# per zoom style --------------------------------------------------------------

def style(z):
    """line widths / visibility per zoom"""
    s = {
        "road1": 5 if z >= 13 else (4 if z >= 11 else 3),
        "road2": 3 if z >= 11 else 2,
        "road3": (3 if z >= 13 else 2) if z >= 11 else 0,
        "road4": (2 if z >= 13 else 1) if z >= 12 else 0,
        "rail": 1 if z >= 11 else 0,
        "river": 2 if z >= 11 else 1,
        "stream": 1 if z >= 13 else 0,
        "power": 2 if z >= 11 else 1,
        "power_minor": (2 if z >= 14 else 1) if z >= 12 else 0,
        "power_tick": 18 if z >= 13 else 24,
        "runway": z >= 12,
        "wind": z >= 10,
        "mast": z >= 11,
        "height_labels": z >= 13,
        "village": z >= 12,
        "hamlet": z >= 14,
        "town": z >= 10,
        "aero_label": z >= 10,
        "min_area_px": 6.0 if z <= 11 else 3.0,
    }
    return s


# ----------------------------------------------------------------------------- labels


def place_labels(data, z, fonts, draw_probe):
    """Global label placement for one zoom (so labels are identical on neighbouring tiles).
    Returns list of (x, y, text, font, anchor) in global pixel coords."""
    st = style(z)
    scale = TILE * (1 << z)
    cands = []
    for p in data.points:
        cls, kind = p["cls"], p["kind"]
        x, y = p["x"] * scale, p["y"] * scale
        if cls == "place":
            if kind == "city":
                cands.append((100, x, y, p["name"], fonts.get(26 if z >= 12 else 23, True), "mm", 0))
            elif kind == "town" and st["town"]:
                cands.append((80, x, y, p["name"], fonts.get(22 if z >= 12 else 20, True), "mm", 0))
            elif kind == "village" and st["village"]:
                cands.append((50, x, y, p["name"], fonts.get(19), "mm", 0))
            elif kind == "hamlet" and st["hamlet"]:
                cands.append((20, x, y, p["name"], fonts.get(17), "mm", 0))
        elif cls in AERO and st["aero_label"]:
            txt = p["icao"] if (z <= 11 and p["icao"]) else (p["name"] or p["icao"])
            if txt:
                cands.append((90, x, y, txt, fonts.get(19, True), "lm", 15))
        elif cls in ("mast", "wind") and st["height_labels"] and p["height"] and p["height"] >= 25:
            cands.append((30, x, y, f"{int(round(p['height']))}", fonts.get(16), "lm", 11))
    cands.sort(key=lambda c: -c[0])
    placed, boxes = [], {}
    G = 64

    def free(b):
        for gx in range(int(b[0] // G), int(b[2] // G) + 1):
            for gy in range(int(b[1] // G), int(b[3] // G) + 1):
                for o in boxes.get((gx, gy), ()):
                    if not (b[2] < o[0] or b[0] > o[2] or b[3] < o[1] or b[1] > o[3]):
                        return False
        return True

    def add(b):
        for gx in range(int(b[0] // G), int(b[2] // G) + 1):
            for gy in range(int(b[1] // G), int(b[3] // G) + 1):
                boxes.setdefault((gx, gy), []).append(b)

    for pri, x, y, txt, font, anchor, dx in cands:
        tries = [(dx, 0, anchor)] if anchor == "mm" else [(dx, 0, "lm"), (-dx, 0, "rm")]
        if anchor == "mm":
            tries += [(0, -14, "mm"), (0, 14, "mm")]
        for ox, oy, an in tries:
            l, t, r, b = draw_probe.textbbox((x + ox, y + oy), txt, font=font, anchor=an, stroke_width=2)
            bb = (l - 2, t - 2, r + 2, b + 2)
            if free(bb):
                add(bb)
                placed.append((x + ox, y + oy, txt, font, an, bb))
                break
    return placed


# ----------------------------------------------------------------------------- metatile render


def render_meta(data, z, mx, my, nt, trees, labels, fonts):
    """Renders the block of nt x nt tiles whose top-left tile is (mx,my) -> numpy bool array (True = black)
    of (nt*256) x (nt*256)."""
    st = style(z)
    scale = TILE * (1 << z)
    ox = mx * TILE - MARGIN
    oy = my * TILE - MARGIN
    W = H = nt * TILE + 2 * MARGIN
    # query window in world coords
    qbox = box(ox / scale, oy / scale, (ox + W) / scale, (oy + H) / scale)

    def tx(geom_coords):
        c = np.asarray(geom_coords) * scale
        c[:, 0] -= ox
        c[:, 1] -= oy
        return [tuple(p) for p in c]

    def poly_mask(cls):
        img = Image.new("L", (W, H), 0)
        d = ImageDraw.Draw(img)
        tree, geoms = trees[cls]
        if tree is None:
            return np.zeros((H, W), bool)
        idx = tree.query(qbox)
        # big first, so small polygons drawn later are not erased by holes of big ones
        idx = sorted(idx, key=lambda i: -geoms[i].area)
        for i in idx:
            g = geoms[i]
            ext = tx(g.exterior.coords)
            if len(ext) >= 3:
                d.polygon(ext, fill=255)
            for r in g.interiors:
                c = tx(r.coords)
                if len(c) >= 3:
                    d.polygon(c, fill=0)
        return np.asarray(img) > 0

    water = poly_mask("water")
    forest = poly_mask("forest")
    urban = poly_mask("urban")

    black = np.zeros((H, W), bool)
    black |= pattern("forest", ox, oy, W, H) & forest & ~water & ~urban
    black |= pattern("urban", ox, oy, W, H) & urban & ~water
    black |= pattern("water", ox, oy, W, H) & water
    # outlines: shoreline (solid), built-up edge
    def edges(m):
        e = np.zeros_like(m)
        e[1:, :] |= m[1:, :] != m[:-1, :]
        e[:, 1:] |= m[:, 1:] != m[:, :-1]
        return e & m
    black |= edges(water)
    if z >= 12:
        black |= edges(urban)

    img = Image.fromarray(np.where(black, 0, 255).astype(np.uint8), "L")
    d = ImageDraw.Draw(img)
    d.fontmode = "1"

    def lines_of(cls):
        tree, geoms = trees[cls]
        if tree is None:
            return []
        return [(geoms[i], trees[cls + ":attr"][i]) for i in tree.query(qbox)]

    def cased(coords, w, casing=2):
        if len(coords) < 2:
            return
        d.line(coords, fill=255, width=w + 2 * casing, joint="curve")

    # rivers / streams. From z12 a river is a hollow double line (two banks) so it cannot be
    # mistaken for a road (roads are solid black).
    for cls in ("stream", "river"):
        w = st[cls]
        if not w:
            continue
        for g, _ in lines_of(cls):
            c = tx(g.coords)
            if cls == "river" and z >= 12:
                d.line(c, fill=0, width=w + 4, joint="curve")
                d.line(c, fill=255, width=w, joint="curve")
            else:
                d.line(c, fill=0, width=w, joint="curve")

    # roads + railway: white casing first, then black
    road_order = ["road4", "road3", "road2", "road1"]
    for cls in road_order + ["rail"]:
        w = st[cls]
        if not w:
            continue
        for g, _ in lines_of(cls):
            cased(tx(g.coords), w, 1)
    for cls in road_order:
        w = st[cls]
        if not w:
            continue
        for g, _ in lines_of(cls):
            d.line(tx(g.coords), fill=0, width=w, joint="curve")
    if st["rail"]:
        # railway: black band with white dashes inside (classic map symbol)
        for g, _ in lines_of("rail"):
            c = tx(g.coords)
            if z >= 12:
                d.line(c, fill=0, width=4)
                pts = along(c, 8)
                for k in range(0, len(pts) - 1, 2):
                    d.line([pts[k][:2], pts[k + 1][:2]], fill=255, width=2)
            else:
                d.line(c, fill=0, width=1)

    # aerodromes: runways
    if st["runway"]:
        mpp = 40075016.686 * math.cos(math.radians(world_lonlat(0, (oy + H / 2) / scale)[1])) / scale
        for g, attr in lines_of("runway"):
            wm = attr.get("width") or 30.0
            w = max(4, int(round(wm / mpp)))
            c = tx(g.coords)
            d.line(c, fill=255, width=w + 4)
            d.line(c, fill=0, width=w)
        tree, geoms = trees["aerodrome"]
        if tree is not None and z >= 13:
            for i in tree.query(qbox):
                ext = tx(geoms[i].exterior.coords)
                for a, b in zip(ext[:-1], ext[1:]):  # dashed outline
                    L = math.hypot(b[0] - a[0], b[1] - a[1])
                    n = max(1, int(L // 6))
                    for k in range(0, n, 2):
                        p0 = (a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n)
                        p1 = (a[0] + (b[0] - a[0]) * (k + 1) / n, a[1] + (b[1] - a[1]) * (k + 1) / n)
                        d.line([p0, p1], fill=0, width=1)

    # power lines (most important obstacle) - on top of roads, with white casing and cross ticks
    for cls in ("power_minor", "power"):
        w = st[cls]
        if not w:
            continue
        step = st["power_tick"] if cls == "power" else st["power_tick"] + 8
        tl = 7 if cls == "power" else 5
        for g, _ in lines_of(cls):
            c = tx(g.coords)
            d.line(c, fill=255, width=w + 4, joint="curve")
        for g, _ in lines_of(cls):
            c = tx(g.coords)
            d.line(c, fill=0, width=w, joint="curve")
            for x, y, ux, uy in along(c, step, step / 2):
                d.line([(x - uy * tl, y + ux * tl), (x + uy * tl, y - ux * tl)], fill=0, width=w)
                if cls == "power" and z >= 12:  # pylon dot
                    d.ellipse([x - 3, y - 3, x + 3, y + 3], fill=0)

    # point symbols
    for p in data.points:
        px, py = p["x"] * scale - ox, p["y"] * scale - oy
        if px < -20 or py < -20 or px > W + 20 or py > H + 20:
            continue
        cls = p["cls"]
        if cls == "wind" and st["wind"]:
            if z <= 10:
                d.ellipse([px - 3, py - 3, px + 3, py + 3], fill=0)
            else:
                r = 10 if z >= 12 else 7
                d.ellipse([px - r - 2, py - r - 2, px + r + 2, py + r + 2], fill=255)
                for a in (90, 210, 330):
                    ar = math.radians(a)
                    d.line([(px, py), (px + r * math.cos(ar), py - r * math.sin(ar))], fill=0, width=2)
                d.ellipse([px - 3, py - 3, px + 3, py + 3], fill=0)
        elif cls == "mast" and st["mast"]:
            h = 14 if z >= 12 else 10
            d.polygon([(px - 9, py + 6), (px, py - h - 4), (px + 9, py + 6)], fill=255)
            d.line([(px - 6, py + 4), (px, py - h), (px + 6, py + 4)], fill=0, width=2)
            d.ellipse([px - 3, py - h - 3, px + 3, py - h + 3], fill=0)
        elif cls in AERO:
            r = 10 if cls == "aerodrome" else 7
            d.ellipse([px - r - 2, py - r - 2, px + r + 2, py + r + 2], fill=255)
            d.ellipse([px - r, py - r, px + r, py + r], outline=0, width=2)
            d.line([(px - r - 3, py), (px + r + 3, py)], fill=0, width=3)
        elif cls == "place" and p["kind"] in ("city", "town") and z <= 11:
            pass  # names only

    # labels (white halo)
    for (x, y, txt, font, an, bb) in labels:
        if bb[2] < ox or bb[0] > ox + W or bb[3] < oy or bb[1] > oy + H:
            continue
        d.text((x - ox, y - oy), txt, font=font, fill=0, anchor=an, stroke_width=2, stroke_fill=255)

    arr = np.asarray(img) < 128
    return arr[MARGIN:MARGIN + nt * TILE, MARGIN:MARGIN + nt * TILE]


# ----------------------------------------------------------------------------- tile coding


def packbits(buf):
    """PackBits encoding of a bytes object (vectorised run detection)."""
    a = np.frombuffer(buf, np.uint8)
    n = len(a)
    starts = np.flatnonzero(np.concatenate(([True], a[1:] != a[:-1])))
    lens = np.diff(np.concatenate((starts, [n])))
    out = bytearray()
    lit = bytearray()

    def flush():
        i = 0
        while i < len(lit):
            chunk = lit[i:i + 128]
            out.append(len(chunk) - 1)
            out.extend(chunk)
            i += 128
        lit.clear()

    for s, L in zip(starts.tolist(), lens.tolist()):
        v = int(a[s])
        if L >= 3:
            flush()
            while L > 0:
                k = min(L, 128)
                if k >= 2:
                    out.append(257 - k)
                    out.append(v)
                else:
                    lit.append(v)
                L -= k
        else:
            lit.extend(bytes([v]) * L)
    flush()
    return bytes(out)


def unpackbits_pb(data, size=8192):
    out = bytearray()
    i = 0
    while len(out) < size and i < len(data):
        n = data[i]
        i += 1
        if n < 128:
            out.extend(data[i:i + n + 1])
            i += n + 1
        elif n > 128:
            out.extend(bytes([data[i]]) * (257 - n))
            i += 1
    return bytes(out)


def encode_tile(bits):
    """bits: (256,256) bool -> record bytes (None for an all-white tile)"""
    if not bits.any():
        return None
    raw = np.packbits(bits, axis=1)  # (256, 32), MSB first
    rawb = raw.tobytes()
    crc = zlib.crc32(rawb) & 0xFFFFFFFF
    x8 = raw.copy()
    x8[8:] ^= raw[:-8]
    c0 = packbits(rawb)
    c1 = packbits(x8.tobytes())
    mode, comp = (0, c0) if len(c0) <= len(c1) else (1, c1)
    return struct.pack("<BBHI", mode, 0, 0, crc) + comp


def decode_tile(rec):
    mode, _, _, crc = struct.unpack_from("<BBHI", rec)
    raw = bytearray(unpackbits_pb(rec[8:]))
    if mode == 1:
        for r in range(8, 256):
            for b in range(32):
                raw[r * 32 + b] ^= raw[(r - 8) * 32 + b]
    assert zlib.crc32(bytes(raw)) & 0xFFFFFFFF == crc, "CRC"
    return np.unpackbits(np.frombuffer(bytes(raw), np.uint8).reshape(256, 32), axis=1).astype(bool)


# ----------------------------------------------------------------------------- main


def build(args):
    t0 = time.time()
    lat0, lon0 = [float(v) for v in args.center.split(",")]
    R = args.radius_km * 1000.0
    dlat = R / 111320.0
    dlon = R / (111320.0 * math.cos(math.radians(lat0)))
    x0, y0 = world_xy(lon0 - dlon, lat0 + dlat)
    x1, y1 = world_xy(lon0 + dlon, lat0 - dlat)
    mx = (x1 - x0) * 0.03
    bbox_world = (x0 - mx, y0 - mx, x1 + mx, y1 + mx)
    cx, cy = world_xy(lon0, lat0)
    m_per_world = 40075016.686 * math.cos(math.radians(lat0))

    data = Data()
    for p in args.pbf:
        t = time.time()
        print(f"Czytam {p} …", flush=True)
        n = read_pbf(p, data, bbox_world)
        print(f"  {n} obiektów, {time.time() - t:.0f} s", flush=True)
    print("  " + ", ".join(f"{k}={v}" for k, v in data.stats().items()), flush=True)

    sea = build_sea(data.lines["coast"], bbox_world)
    if sea:
        print(f"  morze: {len(sea)} obszar(y)")
    data.polys["water"].extend(sea)

    zooms = parse_zooms(args.zooms)
    out_dir = os.path.join(args.out, "maps", args.name)
    os.makedirs(out_dir, exist_ok=True)
    fonts = Fonts()
    probe = ImageDraw.Draw(Image.new("L", (8, 8)))
    total_bytes = 0
    for z in zooms:
        tz = time.time()
        n = 1 << z
        scale = TILE * n
        tx0, ty0 = int(bbox_world[0] * n), int(bbox_world[1] * n)
        tx1, ty1 = int(bbox_world[2] * n), int(bbox_world[3] * n)
        nx, ny = tx1 - tx0 + 1, ty1 - ty0 + 1

        def tile_in_circle(tx, ty):
            ax, ay = tx / n, ty / n
            bx, by = (tx + 1) / n, (ty + 1) / n
            qx, qy = min(max(cx, ax), bx), min(max(cy, ay), by)
            return math.hypot(qx - cx, qy - cy) * m_per_world <= R

        # simplified geometry + spatial index for this zoom
        tol = 0.4 / scale
        min_area = style(z)["min_area_px"] / (scale * scale)
        trees = {}
        for cls, geoms in data.polys.items():
            gs = [g for g in geoms if g.area >= min_area]
            if gs:
                arr = shapely.simplify(np.array(gs, dtype=object), tol, preserve_topology=True)
                arr = [g for g in arr if isinstance(g, Polygon) and not g.is_empty]
                trees[cls] = (shapely.STRtree(arr) if arr else None, arr)
            else:
                trees[cls] = (None, [])
        for cls, items in data.lines.items():
            if cls == "coast":
                continue
            if items:
                arr = shapely.simplify(np.array([g for g, _ in items], dtype=object), tol)
                trees[cls] = (shapely.STRtree(arr), list(arr))
                trees[cls + ":attr"] = [a for _, a in items]
            else:
                trees[cls] = (None, [])
                trees[cls + ":attr"] = []
        labels = place_labels(data, z, fonts, probe)

        index = [(0xFFFFFFFF, 0)] * (nx * ny)
        blobs, dedup = [], {}
        data_off = 32 + 8 * nx * ny
        cur = data_off
        stored = white = dups = 0
        nt = min(META, max(nx, ny))
        for my in range(ty0 - (ty0 % nt), ty1 + 1, nt):
            for mxx in range(tx0 - (tx0 % nt), tx1 + 1, nt):
                tiles = [(tx, ty) for ty in range(my, my + nt) for tx in range(mxx, mxx + nt)
                         if tx0 <= tx <= tx1 and ty0 <= ty <= ty1 and tile_in_circle(tx, ty)]
                if not tiles:
                    continue
                bits = render_meta(data, z, mxx, my, nt, trees, labels, fonts)
                for tx, ty in tiles:
                    sub = bits[(ty - my) * TILE:(ty - my + 1) * TILE, (tx - mxx) * TILE:(tx - mxx + 1) * TILE]
                    rec = encode_tile(sub)
                    i = (ty - ty0) * nx + (tx - tx0)
                    if rec is None:
                        index[i] = (0, 0)
                        white += 1
                        continue
                    h = hashlib.sha1(rec).digest()
                    if h in dedup:
                        index[i] = dedup[h]
                        dups += 1
                        continue
                    index[i] = (cur, len(rec))
                    dedup[h] = index[i]
                    blobs.append(rec)
                    cur += len(rec)
                    stored += 1
        path = os.path.join(out_dir, f"z{z}.pak")
        with open(path, "wb") as f:
            f.write(b"PNMP" + struct.pack("<HBB", 1, z, 0) + struct.pack("<IIHH", tx0, ty0, nx, ny) +
                    struct.pack("<III", 32, data_off, 0))
            f.write(b"".join(struct.pack("<II", o, L) for o, L in index))
            for b in blobs:
                f.write(b)
        size = os.path.getsize(path)
        total_bytes += size
        print(f"  z{z}: {stored} kafelków (+{dups} powtórzeń, {white} pustych), {size / 1e6:.1f} MB, "
              f"{time.time() - tz:.0f} s", flush=True)
        if args.preview:
            write_preview(path, os.path.join(args.out, f"podglad_{args.name}_z{z}.png"), cx, cy, args.preview_tiles)

    bb_lon0, bb_lat0 = world_lonlat(bbox_world[0], bbox_world[1])  # north-west corner
    bb_lon1, bb_lat1 = world_lonlat(bbox_world[2], bbox_world[3])  # south-east corner
    with open(os.path.join(out_dir, "info.txt"), "w", encoding="utf-8") as f:
        f.write("# ParaNav - mapa podkładowa. Dane (c) OpenStreetMap contributors, ODbL\n")
        f.write(f"format=1\nname={args.name}\ntitle={args.title or args.name}\n")
        f.write(f"center={lat0:.5f},{lon0:.5f}\nradius_km={args.radius_km:g}\n")
        f.write(f"bbox={bb_lat0:.5f},{bb_lon0:.5f},{bb_lat1:.5f},{bb_lon1:.5f}\n")
        f.write("zooms=" + ",".join(str(z) for z in zooms) + "\n")
        f.write(f"osm_date={args.osm_date or datetime.date.today().isoformat()}\n")
        f.write(f"generated={datetime.datetime.now().strftime('%Y-%m-%d %H:%M')}\n")
    print(f"Gotowe: {out_dir}  ({total_bytes / 1e6:.1f} MB, {time.time() - t0:.0f} s)")
    print("Skopiuj folder 'maps' na kartę SD (do katalogu głównego).")


def parse_zooms(s):
    if "-" in s:
        a, b = s.split("-")
        return list(range(int(a), int(b) + 1))
    return [int(v) for v in s.split(",")]


def write_preview(pak, png, cx, cy, ntiles):
    """Decodes ntiles x ntiles tiles around the centre into a PNG (to check the style)."""
    with open(pak, "rb") as f:
        blob = f.read()
    z = blob[6]
    tx0, ty0, nx, ny = struct.unpack_from("<IIHH", blob, 8)
    n = 1 << z
    ctx, cty = int(cx * n), int(cy * n)
    k = ntiles
    img = np.ones((k * TILE, k * TILE), bool)  # True = white
    for j in range(k):
        for i in range(k):
            tx, ty = ctx - k // 2 + i, cty - k // 2 + j
            if not (tx0 <= tx < tx0 + nx and ty0 <= ty < ty0 + ny):
                img[j * TILE:(j + 1) * TILE, i * TILE:(i + 1) * TILE] = (np.indices((TILE, TILE)).sum(0) % 16) != 0
                continue
            o, L = struct.unpack_from("<II", blob, 32 + 8 * ((ty - ty0) * nx + (tx - tx0)))
            if L == 0:
                if o == 0xFFFFFFFF:
                    img[j * TILE:(j + 1) * TILE, i * TILE:(i + 1) * TILE] = (np.indices((TILE, TILE)).sum(0) % 16) != 0
                continue
            img[j * TILE:(j + 1) * TILE, i * TILE:(i + 1) * TILE] = ~decode_tile(blob[o:o + L])
    Image.fromarray(np.where(img, 255, 0).astype(np.uint8)).save(png)
    print(f"    podgląd: {png}")


def main():
    ap = argparse.ArgumentParser(description="ParaNav: mapa podkładowa z OpenStreetMap (czarno-biała, e-papier)")
    ap.add_argument("--pbf", action="append", required=True, help="plik .osm.pbf (można podać kilka razy)")
    ap.add_argument("--name", required=True, help="krótka nazwa obszaru = nazwa folderu (a-z, 0-9, _)")
    ap.add_argument("--title", default="", help="opis wyświetlany na urządzeniu")
    ap.add_argument("--center", required=True, help="środek: szer,dł  np. 54.35,18.65")
    ap.add_argument("--radius-km", type=float, default=100.0)
    ap.add_argument("--zooms", default="10-14", help="np. 10-14 albo 10,12,14")
    ap.add_argument("--out", default="out")
    ap.add_argument("--osm-date", default="", help="data danych OSM do info.txt")
    ap.add_argument("--preview", action="store_true", help="zapisz podglądy PNG wokół środka")
    ap.add_argument("--preview-tiles", type=int, default=4)
    args = ap.parse_args()
    if not all(c.isalnum() or c == "_" for c in args.name) or len(args.name) > 24:
        sys.exit("--name: tylko litery a-z, cyfry i _ (max 24 znaki)")
    build(args)


if __name__ == "__main__":
    main()
