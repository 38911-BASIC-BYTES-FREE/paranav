# Usage: python3 gen_wiring_svg.py <out_dir>   (writes gy63_wiring_pl.svg / gy63_wiring_en.svg)
# Generates the GY-63 (MS5611) <-> LilyGO T5 E-Paper S3 Pro QWIIC wiring diagram (PL and EN SVG).
# Usage: python3 gen_wiring_svg.py <out_dir>
import os
import sys

T = {
    "pl": {
        "title": "Podłączenie barometru GY-63 (MS5611) do LilyGO T5 E-Paper S3 Pro",
        "sub": "przewód JST-SH 4 pin (QWIIC / STEMMA QT), raster 1,0 mm – wtyczka do płytki, drugi koniec przylutowany do modułu",
        "board": "LilyGO T5 E-Paper S3 Pro",
        "board2": "(tył płytki)",
        "socket": "gniazdo QWIIC",
        "socket2": "JST-SH 4 pin",
        "plug": "wtyczka JST-SH",
        "cut": "koniec odcięty,",
        "cut2": "zdjęta izolacja ~2 mm,",
        "cut3": "pobielony cyną",
        "module": "moduł GY-63",
        "nc": "nie podłączaj",
        "ps_note": "PS: na module podciągnięty do VCC = tryb I²C",
        "csb_note": "CSB: adres 0x77 / 0x76 – firmware wykrywa oba",
        "cap": "opcjonalnie: 10 µF + 100 nF",
        "cap_lines": ["opcjonalnie", "10 µF + 100 nF", "VCC – GND"],
        "cap2": "między VCC a GND, przy czujniku",
        "pin": "pin",
        "notes": [
            "• Kolory to standard QWIIC – tani przewód może mieć inne: sprawdź miernikiem, który pin wtyczki to który przewód.",
            "• Pin 1 gniazda (GND) jest oznaczony na płytce; wtyczka wchodzi tylko w jednej orientacji.",
            "• Przewód krótki (5–10 cm) i z dala od taśmy wyświetlacza – pełne odświeżenie e-papieru zakłóca długi kabel.",
            "• Zasilanie 3,3 V z gniazda QWIIC – nie podłączaj 5 V. Czujnik przykryj pianką (jest wrażliwy na światło).",
        ],
    },
    "en": {
        "title": "Connecting the GY-63 (MS5611) barometer to the LilyGO T5 E-Paper S3 Pro",
        "sub": "JST-SH 4-pin cable (QWIIC / STEMMA QT), 1.0 mm pitch – plug into the board, other end soldered to the module",
        "board": "LilyGO T5 E-Paper S3 Pro",
        "board2": "(back of the board)",
        "socket": "QWIIC socket",
        "socket2": "JST-SH 4 pin",
        "plug": "JST-SH plug",
        "cut": "end cut off,",
        "cut2": "~2 mm stripped,",
        "cut3": "tinned",
        "module": "GY-63 module",
        "nc": "leave unconnected",
        "ps_note": "PS: pulled up to VCC on the module = I²C mode",
        "csb_note": "CSB: address 0x77 / 0x76 – firmware detects both",
        "cap": "optional: 10 µF + 100 nF",
        "cap_lines": ["optional", "10 µF + 100 nF", "VCC – GND"],
        "cap2": "between VCC and GND, at the sensor",
        "pin": "pin",
        "notes": [
            "• The colours are the QWIIC standard – a cheap cable may differ: check with a meter which plug pin is which wire.",
            "• Socket pin 1 (GND) is marked on the board; the plug fits only one way.",
            "• Keep the cable short (5–10 cm) and away from the display ribbon – a full e-paper refresh disturbs a long cable.",
            "• 3.3 V supply from the QWIIC socket – never connect 5 V. Cover the sensor with foam (it is sensitive to light).",
        ],
    },
}

# QWIIC pin order on the JST-SH connector, with the standard wire colours
WIRES = [  # (jst pin, signal, colour, label colour, GY-63 pad)
    (1, "GND", "#222222", "#222222", "GND"),
    (2, "3V3", "#d62728", "#c0141b", "VCC"),
    (3, "SDA", "#1f6fd1", "#1a5fb4", "SDA"),
    (4, "SCL", "#e8c21a", "#9a7d00", "SCL"),
]
GY_PADS = ["VCC", "GND", "SCL", "SDA", "CSB", "SDO", "PS"]
GPIO = {"SDA": "GPIO39", "SCL": "GPIO40", "3V3": "3,3 V", "GND": "GND"}


def svg(lang):
    G = dict(GPIO)
    if lang == "en":
        G["3V3"] = "3.3 V"
    t = T[lang]
    W, H = 1100, 690
    o = []
    a = o.append
    a(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
      'font-family="DejaVu Sans, Liberation Sans, Arial, sans-serif">')
    a(f'<rect width="{W}" height="{H}" fill="#ffffff"/>')
    a(f'<text x="{W/2}" y="34" font-size="21" font-weight="bold" text-anchor="middle" fill="#111">{t["title"]}</text>')
    a(f'<text x="{W/2}" y="58" font-size="13" text-anchor="middle" fill="#555">{t["sub"]}</text>')

    # ---- board (left)
    bx, by, bw, bh = 30, 95, 330, 340
    a(f'<rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="14" fill="#2f6b3a" stroke="#1c4424" stroke-width="2"/>')
    a(f'<text x="{bx+bw/2}" y="{by+32}" font-size="15" font-weight="bold" text-anchor="middle" fill="#fff">{t["board"]}</text>')
    a(f'<text x="{bx+bw/2}" y="{by+52}" font-size="12" text-anchor="middle" fill="#d6ead9">{t["board2"]}</text>')
    # a few decorative chips
    a(f'<rect x="{bx+25}" y="{by+262}" width="64" height="58" rx="4" fill="#1d1d1d"/>')
    a(f'<text x="{bx+57}" y="{by+296}" font-size="10" text-anchor="middle" fill="#bbb">ESP32-S3</text>')

    # QWIIC socket on the right edge of the board
    pitch = 34
    sy0 = 205
    sx = bx + bw - 62
    a(f'<rect x="{sx}" y="{sy0-22}" width="62" height="{pitch*4+10}" rx="3" fill="#f2f2f2" stroke="#666" stroke-width="1.5"/>')
    a(f'<text x="{sx-8}" y="{sy0-30}" font-size="12" text-anchor="end" fill="#fff">{t["socket"]}</text>')
    a(f'<text x="{sx-8}" y="{sy0-16}" font-size="11" text-anchor="end" fill="#d6ead9">{t["socket2"]}</text>')
    pin_y = [sy0 + i * pitch for i in range(4)]
    for (pn, sig, col, lcol, pad), y in zip(WIRES, pin_y):
        a(f'<rect x="{sx+40}" y="{y-5}" width="22" height="10" fill="#c9a227"/>')
        a(f'<text x="{sx+6}" y="{y+5}" font-size="13" font-weight="bold" fill="#222">{pn}</text>')
        a(f'<text x="{sx-8}" y="{y+5}" font-size="12" text-anchor="end" fill="#fff">{G[sig] if sig in ("GND",) else (sig + " · " + G[sig])}</text>')
    a(f'<polygon points="{sx+20},{pin_y[0]-4} {sx+28},{pin_y[0]} {sx+20},{pin_y[0]+4}" fill="#c0141b"/>')  # pin-1 mark

    # ---- plug + cable (middle)
    px = sx + 70
    a(f'<rect x="{px}" y="{sy0-18}" width="46" height="{pitch*3+36}" rx="4" fill="#fbfbfb" stroke="#555" stroke-width="1.5"/>')
    a(f'<text x="{px}" y="{sy0+pitch*3+58}" font-size="12" fill="#444">{t["plug"]}</text>')

    # GY-63 module (right)
    mx, my, mw, mh = 760, 130, 150, 300
    gy_pitch = 38
    gy0 = my + 40
    pad_y = {p: gy0 + i * gy_pitch for i, p in enumerate(GY_PADS)}
    a(f'<rect x="{mx}" y="{my}" width="{mw}" height="{mh}" rx="8" fill="#1f4fa8" stroke="#123270" stroke-width="2"/>')
    a(f'<text x="{mx+mw/2+12}" y="{my-12}" font-size="16" font-weight="bold" text-anchor="middle" fill="#123270">{t["module"]}</text>')
    # MS5611 chip
    a(f'<rect x="{mx+70}" y="{my+105}" width="56" height="44" rx="3" fill="#d9d9d9" stroke="#777"/>')
    a(f'<circle cx="{mx+98}" cy="{my+127}" r="9" fill="#9a9a9a"/>')
    a(f'<text x="{mx+98}" y="{my+168}" font-size="11" text-anchor="middle" fill="#e6ecff">MS5611</text>')
    a(f'<rect x="{mx+78}" y="{my+205}" width="34" height="22" rx="2" fill="#111"/>')
    a(f'<text x="{mx+95}" y="{my+243}" font-size="10" text-anchor="middle" fill="#c8d4f5">LDO 3,3 V</text>' if lang == "pl"
      else f'<text x="{mx+95}" y="{my+243}" font-size="10" text-anchor="middle" fill="#c8d4f5">3.3 V LDO</text>')
    for p, y in pad_y.items():
        a(f'<circle cx="{mx+22}" cy="{y}" r="9" fill="#d8b24a" stroke="#8a6d16" stroke-width="1.5"/>')
        a(f'<circle cx="{mx+22}" cy="{y}" r="3.5" fill="#1f4fa8"/>')
        a(f'<text x="{mx+38}" y="{y+5}" font-size="13" font-weight="bold" fill="#fff">{p}</text>')

    # wires: plug -> GY-63 pads (bezier), with stripped/soldered ends
    for (pn, sig, col, lcol, pad), y in zip(WIRES, pin_y):
        x0 = px + 46
        x1 = mx + 22
        y1 = pad_y[pad]
        a(f'<path d="M{px+8},{y} L{x0},{y} C{x0+190},{y} {x1-200},{y1} {x1-26},{y1}" fill="none" stroke="{col}" '
          'stroke-width="7" stroke-linecap="round"/>')
        a(f'<line x1="{x1-26}" y1="{y1}" x2="{x1-2}" y2="{y1}" stroke="#b8b8b8" stroke-width="3"/>')  # bare tinned wire
        a(f'<circle cx="{x1}" cy="{y1}" r="6" fill="#c8c8c8" stroke="#888"/>')  # solder joint
        a(f'<text x="{x0+14}" y="{y-9}" font-size="12" font-weight="bold" fill="{lcol}">{sig}</text>')
    # label for the cut end
    a(f'<text x="{mx-30}" y="{my-10}" font-size="11" text-anchor="end" fill="#444">{t["cut"]} {t["cut2"]} {t["cut3"]} →</text>')

    # unconnected pads
    for p in ("CSB", "SDO", "PS"):
        y = pad_y[p]
        a(f'<line x1="{mx-6}" y1="{y-6}" x2="{mx+6}" y2="{y+6}" stroke="#c0141b" stroke-width="2.5"/>')
        a(f'<line x1="{mx-6}" y1="{y+6}" x2="{mx+6}" y2="{y-6}" stroke="#c0141b" stroke-width="2.5"/>')
    a(f'<text x="{mx-14}" y="{pad_y["SDO"]+5}" font-size="12" text-anchor="end" fill="#c0141b">{t["nc"]}</text>')

    # capacitor (optional) between VCC and GND, to the right of the module
    cx = mx + mw + 30
    yv, yg = pad_y["VCC"], pad_y["GND"]
    a(f'<path d="M{mx+mw-10},{yv} H{cx}" stroke="#d62728" stroke-width="2" stroke-dasharray="5 4" fill="none"/>')
    a(f'<path d="M{mx+mw-10},{yg} H{cx}" stroke="#222" stroke-width="2" stroke-dasharray="5 4" fill="none"/>')
    a(f'<line x1="{cx}" y1="{yv}" x2="{cx}" y2="{(yv+yg)/2-5}" stroke="#555" stroke-width="2"/>')
    a(f'<line x1="{cx}" y1="{(yv+yg)/2+5}" x2="{cx}" y2="{yg}" stroke="#555" stroke-width="2"/>')
    a(f'<line x1="{cx-12}" y1="{(yv+yg)/2-5}" x2="{cx+12}" y2="{(yv+yg)/2-5}" stroke="#555" stroke-width="3"/>')
    a(f'<line x1="{cx-12}" y1="{(yv+yg)/2+5}" x2="{cx+12}" y2="{(yv+yg)/2+5}" stroke="#555" stroke-width="3"/>')
    ym = (yv + yg) / 2
    for k, part in enumerate(t["cap_lines"]):
        a(f'<text x="{cx+20}" y="{ym-12+k*14}" font-size="11" fill="#555">{part}</text>')

    # notes about PS / CSB
    a(f'<text x="{mx+mw/2}" y="{my+mh+22}" font-size="11" text-anchor="middle" fill="#123270">{t["ps_note"]}</text>')
    a(f'<text x="{mx+mw/2}" y="{my+mh+37}" font-size="11" text-anchor="middle" fill="#123270">{t["csb_note"]}</text>')

    # connection table
    ty = 520
    a(f'<rect x="30" y="{ty}" width="{W-60}" height="150" rx="8" fill="#f6f7f9" stroke="#d5d8de"/>')
    hdr = ["JST-SH " + t["pin"], "", "T5 S3 Pro", "GY-63"]
    xs = [55, 150, 300, 470]
    a(f'<text x="{xs[0]}" y="{ty+24}" font-size="12" font-weight="bold" fill="#333">{hdr[0]}</text>')
    a(f'<text x="{xs[2]}" y="{ty+24}" font-size="12" font-weight="bold" fill="#333">{hdr[2]}</text>')
    a(f'<text x="{xs[3]}" y="{ty+24}" font-size="12" font-weight="bold" fill="#333">{hdr[3]}</text>')
    for i, (pn, sig, col, lcol, pad) in enumerate(WIRES):
        y = ty + 46 + i * 20
        a(f'<text x="{xs[0]+20}" y="{y}" font-size="12" fill="#222">{pn}</text>')
        a(f'<rect x="{xs[1]-40}" y="{y-9}" width="60" height="9" rx="4" fill="{col}"/>')
        a(f'<text x="{xs[2]}" y="{y}" font-size="12" fill="#222">{sig if sig == "GND" else sig + " (" + G[sig] + ")"}</text>')
        a(f'<text x="{xs[3]}" y="{y}" font-size="12" font-weight="bold" fill="#222">{pad}</text>')
    # notes on the right part of the box
    nx = 600
    a(f'<line x1="{nx-20}" y1="{ty+12}" x2="{nx-20}" y2="{ty+138}" stroke="#d5d8de"/>')
    import textwrap
    yy = ty + 24
    for line in t["notes"]:
        for k, part in enumerate(textwrap.wrap(line, 66)):
            a(f'<text x="{nx + (0 if k == 0 else 10)}" y="{yy}" font-size="11" fill="#333">{part}</text>')
            yy += 15
        yy += 3
    a('</svg>')
    return "\n".join(o)


if __name__ == "__main__":
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for lang in ("pl", "en"):
        with open(os.path.join(out, f"gy63_wiring_{lang}.svg"), "w", encoding="utf-8") as f:
            f.write(svg(lang))
    print("ok")
