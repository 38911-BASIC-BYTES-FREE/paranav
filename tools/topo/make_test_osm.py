#!/usr/bin/env python3
"""Writes a small SYNTHETIC .osm.pbf (fictional features) for testing make_tiles.py
without internet access. Not real geography - the names are made up.

    python3 make_test_osm.py out.osm.pbf [lat lon]
"""
import math
import sys

import osmium
from osmium.osm.mutable import Node, Relation, Way

LAT0, LON0 = 52.09, 20.85
if len(sys.argv) >= 4:
    LAT0, LON0 = float(sys.argv[2]), float(sys.argv[3])

KM_LAT = 1 / 111.2
KM_LON = 1 / (111.2 * math.cos(math.radians(LAT0)))

nodes, ways, rels = [], [], []
_nid, _wid = [1000], [1000]


def ll(dx_km, dy_km):
    return (LON0 + dx_km * KM_LON, LAT0 + dy_km * KM_LAT)


def node(dx, dy, tags=None):
    _nid[0] += 1
    nodes.append(Node(id=_nid[0], location=ll(dx, dy), tags=tags or {}, version=1))
    return _nid[0]


def way(pts, tags=None, closed=False):
    ids = [node(x, y) for x, y in pts]
    if closed:
        ids.append(ids[0])
    _wid[0] += 1
    ways.append(Way(id=_wid[0], nodes=ids, tags=tags or {}, version=1))
    return _wid[0]


def circle(cx, cy, r, n=24, wob=0.0, ccw=True):
    pts = []
    for i in range(n):
        a = 2 * math.pi * i / n
        rr = r * (1 + wob * math.sin(3 * a))
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    return pts if ccw else pts[::-1]


# --- coastline: sea to the NORTH of y ~ +18 km. OSM rule: land on the LEFT of the way direction,
# so the line runs east -> west. Split in three ways to test merging.
coast = [(60 - i * 2.0, 18 + 2.5 * math.sin(i * 0.5) + (4 if 25 < i < 32 else 0)) for i in range(61)]
way(coast[0:21], {"natural": "coastline"})
way(coast[20:41], {"natural": "coastline"})
way(coast[40:61], {"natural": "coastline"})
# island in the sea: land inside -> counter-clockwise ring (land on the left)
way(circle(5, 30, 3, 16, 0.2, ccw=True), {"natural": "coastline"}, closed=True)

# lake (closed way) + lake with an island (multipolygon)
way(circle(-12, -8, 2.5, 20, 0.15), {"natural": "water", "name": "Example Lake"}, closed=True)
o = way(circle(14, -12, 4, 24, 0.1), {}, closed=True)
i = way(circle(14.5, -12, 1.2, 12), {}, closed=True)
_wid[0] += 1
rels.append(Relation(id=1, members=[("w", o, "outer"), ("w", i, "inner")],
                     tags={"type": "multipolygon", "natural": "water"}, version=1))

# forests
way(circle(-5, 6, 5, 24, 0.25), {"landuse": "forest"}, closed=True)
way(circle(10, 2, 3, 18, 0.2), {"natural": "wood"}, closed=True)
fo = way([(-20, -20), (-6, -21), (-4, -12), (-18, -10)], {}, closed=True)
fi = way([(-14, -17), (-10, -17), (-10, -14), (-14, -14)], {}, closed=True)
rels.append(Relation(id=2, members=[("w", fo, "outer"), ("w", fi, "inner")],
                     tags={"type": "multipolygon", "landuse": "forest"}, version=1))

# towns: built-up areas + place nodes
for (x, y, r, name, kind) in [(0, 0, 2.2, "Test Town", "town"), (-14, 8, 1.0, "First Village", "village"),
                              (18, 10, 1.2, "Second Village", "village"), (-25, -2, 4, "Big City", "city")]:
    way(circle(x, y, r, 16, 0.2), {"landuse": "residential"}, closed=True)
    node(x, y, {"place": kind, "name": name})
way(circle(2.5, -2.5, 0.8, 8), {"landuse": "industrial"}, closed=True)
node(6, -5, {"place": "hamlet", "name": "Hamlet"})

# river into the sea, stream
way([(-30 + i * 1.5, -25 + i * 1.8 + math.sin(i) * 0.8) for i in range(28)], {"waterway": "river", "name": "River"})
way([(8 + i * 0.6, -4 + i * 0.5) for i in range(12)], {"waterway": "stream"})

# roads, railway
way([(-40, -1), (-20, -0.5), (0, 0), (20, 1), (40, 3)], {"highway": "primary"})
way([(0, 0), (-3, 10), (-6, 22)], {"highway": "secondary"})
way([(0, 0), (5, -10), (8, -30)], {"highway": "tertiary"})
way([(-40, -30), (0, -26), (40, -24)], {"highway": "motorway"})
way([(-40, 4), (-10, 3), (10, 5), (40, 9)], {"railway": "rail"})

# power lines
pl = [(-35, -15), (-15, -6), (5, -3), (25, 12), (35, 17)]
way(pl, {"power": "line", "voltage": "110000"})
way([(0, 0), (4, 6), (12, 9), (18, 10)], {"power": "minor_line"})

# obstacles
for k in range(6):
    node(22 + k * 0.7, -6 - k * 0.4, {"power": "generator", "generator:source": "wind", "height": "150"})
node(-3, 3, {"man_made": "mast", "height": "120"})
node(1.5, -1.0, {"man_made": "chimney", "height": "60"})
node(-8, -3, {"man_made": "tower", "tower:type": "communication"})

# aerodrome (area) + runway + airstrip node
way([(12, -20), (17, -20), (17, -17.5), (12, -17.5)],
    {"aeroway": "aerodrome", "name": "Test Aerodrome", "icao": "EPXX"}, closed=True)
way([(12.5, -19), (16.5, -18.5)], {"aeroway": "runway", "width": "30"})
node(-22, 14, {"aeroway": "airstrip", "name": "Łąka Airstrip"})

with osmium.SimpleWriter(sys.argv[1]) as w:
    for n in nodes:
        w.add_node(n)
    for wy in ways:
        w.add_way(wy)
    for r in rels:
        w.add_relation(r)
print(f"{sys.argv[1]}: {len(nodes)} nodes, {len(ways)} ways, {len(rels)} relations")
