#!/usr/bin/env python3
"""Builds the PatchWork icon: a pleated patchwork kilt in BeOS style.

One geometry model feeds both outputs, so they cannot drift apart:
  icon/PatchWork.svg   editable 64x64 source (importable into Icon-O-Matic)
  PatchWork.rdef       its `resource vector_icon { ... };` block, as HVIF

HVIF layout follows Haiku's src/libs/icon/flat_icon/FlatIconImporter.cpp:
"ncif" magic, then styles, paths and shapes, each list prefixed by a uint8
count. Icon space is 64x64 units. Stdlib only.

Usage: python3 icon/make_hvif.py
"""

import math
import os
import re
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# HVIF tags (FlatIconFormat.h)
STYLE_SOLID = 1
STYLE_GRADIENT = 2
STYLE_SOLID_NO_ALPHA = 3
SHAPE_PATH_SOURCE = 10
TRANSFORMER_STROKE = 23
GRADIENT_FLAG_TRANSFORM = 1 << 1
GRADIENT_FLAG_NO_ALPHA = 1 << 2
PATH_FLAG_CLOSED = 1 << 1
PATH_FLAG_USES_COMMANDS = 1 << 2
PATH_FLAG_NO_CURVES = 1 << 3
SHAPE_FLAG_HAS_TRANSFORMERS = 1 << 4
CMD_HLINE, CMD_VLINE, CMD_LINE, CMD_CURVE = 0, 1, 2, 3
JOIN_ROUND = 2  # agg::round_join
CAP_ROUND = 2   # agg::round_cap


# --------------------------------------------------------------------------
# Model: a shape is (paths, style, stroke). A path is (points, closed) where
# points are (x, y) for corners or ((x, y), (inx, iny), (outx, outy)) for
# curve points. A style is ("solid", rgba) or ("linear", p1, p2, stops) with
# stops [(offset 0..1, rgba)]. stroke is None or an integer width.

def quantize(c):
    """Snaps a coordinate to a value HVIF stores exactly (see write_coord)."""
    if abs(c - round(c)) < 1e-9 and -32 <= round(c) <= 95:
        return float(round(c))
    return round((c + 128.0) * 102.0) / 102.0 - 128.0


def rgba(hexcolor, alpha=255):
    h = hexcolor.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), alpha)


TILT = math.radians(-7)  # right side slightly higher: 3/4 view from the left


def ellipse(cx, cy, rx, ry, theta):
    """Point on a tilted ellipse; theta in (0, pi) is the front half."""
    x, y = rx * math.cos(theta), ry * math.sin(theta)
    return (cx + x * math.cos(TILT) - y * math.sin(TILT),
            cy + x * math.sin(TILT) + y * math.cos(TILT))


def lerp(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


WAIST = (31, 12, 16, 3.2)       # top rim of the waistband
BAND = (31, 18, 16.7, 3.8)      # bottom of the waistband, top of the skirt
HEM = (32, 50, 26.5, 6.5)       # bottom of the skirt
COLUMNS = 6                     # patch columns, also the pleats
ROWS = (0.0, 0.34, 0.68, 1.0)   # patch row boundaries, top to bottom
PLEAT_DIP = 1.6                 # hem drops between pleat edges
STEPS = 4                       # polyline steps per patch edge
APRON = 1.85                    # angle of the fringed apron edge

# Column boundaries left to right; equal angles foreshorten the side columns.
THETAS = [math.pi - math.pi * i / COLUMNS for i in range(COLUMNS + 1)]


def skirt(theta, t):
    """Point on the skirt surface; t=1 includes the pleated hem zigzag."""
    p = lerp(ellipse(*BAND, theta), ellipse(*HEM, theta), t)
    if t > 0:
        # 0 on pleat edges, 1 midway between: a V at every pleat
        k = (math.pi - theta) / (math.pi / COLUMNS)
        dip = 1 - abs(2 * (k - math.floor(k)) - 1)
        p = (p[0], p[1] + PLEAT_DIP * dip * t)
    return p


def arc(theta_a, theta_b, func, steps):
    return [func(theta_a + (theta_b - theta_a) * i / steps)
            for i in range(steps + 1)]


def ellipse_points(spec, theta_a, theta_b, steps):
    return arc(theta_a, theta_b, lambda th: ellipse(*spec, th), steps)


def circle(cx, cy, r):
    """Closed 4-point Bezier circle."""
    k = 0.5523 * r
    return [((cx + r, cy), (cx + r, cy - k), (cx + r, cy + k)),
            ((cx, cy + r), (cx + k, cy + r), (cx - k, cy + r)),
            ((cx - r, cy), (cx - r, cy + k), (cx - r, cy - k)),
            ((cx, cy - r), (cx - k, cy - r), (cx + k, cy - r))]


RED, GREEN, NAVY, MUSTARD = (rgba("#b01e2c"), rgba("#1f6b38"),
                             rgba("#22306e"), rgba("#e0a526"))
PATCHES = [  # rows top to bottom, columns left to right
    [RED, GREEN, MUSTARD, NAVY, RED, GREEN],
    [MUSTARD, NAVY, RED, GREEN, MUSTARD, NAVY],
    [GREEN, RED, NAVY, MUSTARD, GREEN, RED],
]
STRIPE = {RED: rgba("#f4c84a"), GREEN: rgba("#e8434b"),
          NAVY: rgba("#f4c84a"), MUSTARD: rgba("#b01e2c")}
INK = rgba("#24120a")


def build_icon():
    shapes = []

    def add(paths, style, stroke=None):
        shapes.append((paths, style, stroke))

    # Drop shadow on the ground, offset away from the top-left light.
    shadow = [(ellipse(35.5, 55.5, 27, 5.5, math.pi * 2 * i / 24))
              for i in range(24)]
    add([(shadow, True)], ("solid", rgba("#000000", 70)))

    # Waist opening: the lining seen through the top.
    opening = ellipse_points(WAIST, 0, 2 * math.pi, 24)[:-1]
    add([(opening, True)], ("solid", rgba("#1e0c08")))
    # Inside of the waistband at the back; the front band hides the rest.
    back = ellipse_points(WAIST, 2 * math.pi, math.pi, 16)
    inner = [(x, y + 2.2) for x, y in reversed(back)]
    add([(back + inner, True)], ("solid", rgba("#4a2a14")))

    # Patchwork fabric.
    patches = []
    stripes = {}
    for row in range(3):
        for col in range(COLUMNS):
            ta, tb = THETAS[col], THETAS[col + 1]
            t0, t1 = ROWS[row], ROWS[row + 1]
            top = arc(ta, tb, lambda th: skirt(th, t0), STEPS)
            bottom = arc(tb, ta, lambda th: skirt(th, t1), STEPS)
            color = PATCHES[row][col]
            patches.append(((top + bottom, True), color))
            tm, thm = (t0 + t1) / 2, (ta + tb) / 2
            stripes.setdefault(STRIPE[color], []).extend([
                (arc(ta, tb, lambda th: skirt(th, tm), STEPS), False),
                ([skirt(thm, t0 + 0.02), skirt(thm, t1 - 0.02)], False)])
    for path, color in patches:
        add([path], ("solid", color))
    for color, paths in stripes.items():
        add(paths, ("solid", color), stroke=1)

    # Pleats: the right half of every column folds away from the light.
    folds = []
    for col in range(COLUMNS):
        ta, tb = THETAS[col], THETAS[col + 1]
        tm = (ta + tb) / 2
        folds.append((arc(tm, tb, lambda th: skirt(th, 0), 2)
                      + [skirt(tb, 1), skirt(tm, 1)], True))
    add(folds, ("solid", rgba("#000000", 60)))

    # Outer apron: its fringed free edge overlaps the pleats to its right.
    add([([skirt(APRON, 0), skirt(APRON, 1), skirt(APRON - 0.1, 1),
           skirt(APRON - 0.1, 0)], True)], ("solid", rgba("#000000", 80)))
    fringe = [([skirt(APRON, t), skirt(APRON + 0.07, t + 0.03)], False)
              for t in [0.06 + 0.1 * i for i in range(10)]]
    add(fringe, ("solid", rgba("#fff0c8", 230)), stroke=1)
    add([([skirt(APRON, 0), skirt(APRON, 1)], False)], ("solid", INK),
        stroke=1)

    # Soft top-left light over the whole skirt.
    outline_skirt = (arc(math.pi, 0, lambda th: skirt(th, 0), 24)
                     + arc(0, math.pi, lambda th: skirt(th, 1), 48))
    add([(outline_skirt, True)], ("linear", (8, 18), (56, 56), [
        (0, rgba("#ffffff", 90)), (0.45, rgba("#ffffff", 0)),
        (1, rgba("#000000", 90))]))

    # Seams between patches (the column seams double as pleat edges).
    seams = [([skirt(th, 0), skirt(th, 1)], False) for th in THETAS[1:-1]]
    seams += [(arc(math.pi, 0, lambda th: skirt(th, t), 24), False)
              for t in ROWS[1:-1]]
    add(seams, ("solid", rgba("#24120a", 200)), stroke=1)

    # Leather waistband.
    band = (ellipse_points(WAIST, math.pi, 0, 16)
            + ellipse_points(BAND, 0, math.pi, 16))
    add([(band, True)], ("linear", (31, 12), (31, 22),
                         [(0, rgba("#a8703a")), (1, rgba("#5a3216"))]))
    rim = ellipse_points((WAIST[0], WAIST[1] + 1.2, WAIST[2] - 0.8, WAIST[3]),
                         0.25, math.pi - 0.25, 12)
    add([(rim, False)], ("solid", rgba("#ffe0b0", 110)), stroke=1)

    # Buckle on the band.
    bx, by = lerp(ellipse(*WAIST, 0.55), ellipse(*BAND, 0.55), 0.5)
    buckle = [(bx - 2, by - 2.5), (bx + 2, by - 3), (bx + 2, by + 2),
              (bx - 2, by + 2.5)]
    add([(buckle, True)], ("linear", (bx - 2, by - 3), (bx + 2, by + 2),
                           [(0, rgba("#fff6c8")), (1, rgba("#c08a1a"))]))
    add([(buckle, True)], ("solid", INK), stroke=1)

    # Kilt pin on the front apron.
    pin_top = skirt(2.18, 0.45)
    pin_end = skirt(2.02, 0.82)
    add([([pin_top, pin_end], False)], ("solid", INK), stroke=3)
    add([([pin_top, pin_end], False)], ("solid", rgba("#f2c43c")), stroke=1)
    add([(circle(pin_top[0], pin_top[1], 1.9), True)],
        ("linear", (pin_top[0] - 2, pin_top[1] - 2),
         (pin_top[0] + 2, pin_top[1] + 2),
         [(0, rgba("#fff4b0")), (1, rgba("#b07a10"))]))
    add([(circle(pin_top[0], pin_top[1], 1.9), True)], ("solid", INK),
        stroke=1)

    # Bold outline: inner rims, then the silhouette.
    add([(ellipse_points(BAND, 0, math.pi, 16), False)], ("solid", INK),
        stroke=1)
    silhouette = (ellipse_points(WAIST, 2 * math.pi, math.pi, 24)
                  + [skirt(math.pi, 1)]
                  + arc(math.pi, 0, lambda th: skirt(th, 1), 48)[1:])
    add([(silhouette, True)], ("solid", INK), stroke=2)

    return [([(quantize_points(pts), closed) for pts, closed in paths],
             quantize_style(style), stroke)
            for paths, style, stroke in shapes]


def quantize_points(points):
    def q(p):
        return (quantize(p[0]), quantize(p[1]))
    return [tuple(q(c) for c in p) if isinstance(p[0], tuple) else q(p)
            for p in points]


def quantize_style(style):
    if style[0] == "linear":
        _, p1, p2, stops = style
        # HVIF stores stop offsets as uint8.
        return ("linear", p1, p2, [(round(o * 255) / 255, c) for o, c in stops])
    return style


def is_curve_point(p):
    return isinstance(p[0], tuple)


def point_triplet(p):
    """(point, in, out) for any model point."""
    return p if is_curve_point(p) else (p, p, p)


# --------------------------------------------------------------------------
# HVIF encoding (inverse of FlatIconImporter / PathCommandQueue)

def encode_coord(c):
    if c == int(c) and -32 <= c <= 95:
        return bytes([int(c) + 32])
    value = int(round((c + 128.0) * 102.0)) | 0x8000
    return bytes([value >> 8, value & 0xFF])


def encode_float24(value):
    bits = struct.unpack("<I", struct.pack("<f", value))[0]
    sign = bits >> 31
    exponent = ((bits >> 23) & 0xFF) - 127
    mantissa = bits & 0x7FFFFF
    if value == 0 or exponent >= 32 or exponent < -32:
        return b"\0\0\0"
    v = (sign << 23) | ((exponent + 32) << 17) | (mantissa >> 6)
    return bytes([v >> 16, (v >> 8) & 0xFF, v & 0xFF])


def gradient_matrix(p1, p2):
    """Affine (sx, shy, shx, sy, tx, ty) mapping HVIF's linear gradient
    x range -64..64 onto the segment p1 -> p2."""
    dx, dy = (p2[0] - p1[0]) / 128.0, (p2[1] - p1[1]) / 128.0
    return (dx, dy, -dy, dx, (p1[0] + p2[0]) / 2, (p1[1] + p2[1]) / 2)


def encode_style(style):
    if style[0] == "solid":
        r, g, b, a = style[1]
        if a == 255:
            return bytes([STYLE_SOLID_NO_ALPHA, r, g, b])
        return bytes([STYLE_SOLID, r, g, b, a])
    _, p1, p2, stops = style
    opaque = all(c[3] == 255 for _, c in stops)
    flags = GRADIENT_FLAG_TRANSFORM | (GRADIENT_FLAG_NO_ALPHA if opaque else 0)
    out = bytearray([STYLE_GRADIENT, 0, flags, len(stops)])  # 0 = linear
    for value in gradient_matrix(p1, p2):
        out += encode_float24(value)
    for offset, (r, g, b, a) in stops:
        out += bytes([round(offset * 255), r, g, b] + ([] if opaque else [a]))
    return bytes(out)


def encode_path(points, closed):
    assert 0 < len(points) < 256
    flags = PATH_FLAG_CLOSED if closed else 0
    if not any(is_curve_point(p) for p in points):
        out = bytearray([flags | PATH_FLAG_NO_CURVES, len(points)])
        for x, y in points:
            out += encode_coord(x) + encode_coord(y)
        return bytes(out)

    commands, coords = [], bytearray()
    last = (0.0, 0.0)  # PathCommandQueue starts at B_ORIGIN
    for p in points:
        point, pin, pout = point_triplet(p)
        if point == pin == pout:
            if point[0] == last[0]:
                commands.append(CMD_VLINE)
                coords += encode_coord(point[1])
            elif point[1] == last[1]:
                commands.append(CMD_HLINE)
                coords += encode_coord(point[0])
            else:
                commands.append(CMD_LINE)
                coords += encode_coord(point[0]) + encode_coord(point[1])
        else:
            commands.append(CMD_CURVE)
            for c in point + pin + pout:
                coords += encode_coord(c)
        last = point
    packed = bytearray()
    for i in range(0, len(commands), 4):
        byte = 0
        for j, command in enumerate(commands[i:i + 4]):
            byte |= command << (2 * j)
        packed.append(byte)
    return bytes([flags | PATH_FLAG_USES_COMMANDS, len(points)]) \
        + bytes(packed) + bytes(coords)


def encode_hvif(shapes):
    styles, paths, shape_refs = [], [], []
    for shape_paths, style, stroke in shapes:
        if style not in styles:
            styles.append(style)
        indices = []
        for path in shape_paths:
            key = (tuple(path[0]), path[1])
            if key not in paths:
                paths.append(key)
            indices.append(paths.index(key))
        shape_refs.append((styles.index(style), indices, stroke))
    assert len(styles) < 256 and len(paths) < 256 and len(shapes) < 256

    out = bytearray(b"ncif")  # 'ficn' written as a little-endian uint32
    out.append(len(styles))
    for style in styles:
        out += encode_style(style)
    out.append(len(paths))
    for points, closed in paths:
        out += encode_path(list(points), closed)
    out.append(len(shape_refs))
    for style_index, indices, stroke in shape_refs:
        out += bytes([SHAPE_PATH_SOURCE, style_index, len(indices)] + indices)
        if stroke is None:
            out.append(0)
        else:
            out += bytes([SHAPE_FLAG_HAS_TRANSFORMERS, 1, TRANSFORMER_STROKE,
                          stroke + 128, JOIN_ROUND | (CAP_ROUND << 4), 4])
    return bytes(out)


# --------------------------------------------------------------------------
# SVG output

def svg_color(color):
    r, g, b, a = color
    return "#%02x%02x%02x" % (r, g, b), a / 255.0


def svg_path(points, closed):
    triplets = [point_triplet(p) for p in points]
    d = ["M %.3f %.3f" % triplets[0][0]]
    pairs = list(zip(triplets, triplets[1:]))
    if closed:
        pairs.append((triplets[-1], triplets[0]))
    for (a, _, a_out), (b, b_in, _) in pairs:
        if a_out == a and b_in == b:
            d.append("L %.3f %.3f" % b)
        else:
            d.append("C %.3f %.3f %.3f %.3f %.3f %.3f" % (a_out + b_in + b))
    if closed:
        d.append("Z")
    return " ".join(d)


def build_svg(shapes):
    defs, body = [], []
    for index, (paths, style, stroke) in enumerate(shapes):
        if style[0] == "solid":
            paint, opacity = svg_color(style[1])
        else:
            _, p1, p2, stops = style
            gid = "g%d" % index
            stop_tags = "".join(
                '<stop offset="%.3f" stop-color="%s" stop-opacity="%.3f"/>'
                % ((o,) + svg_color(c)) for o, c in stops)
            defs.append('<linearGradient id="%s" gradientUnits="userSpaceOnUse"'
                        ' x1="%g" y1="%g" x2="%g" y2="%g">%s</linearGradient>'
                        % ((gid,) + p1 + p2 + (stop_tags,)))
            paint, opacity = "url(#%s)" % gid, 1.0
        d = " ".join(svg_path(points, closed) for points, closed in paths)
        if stroke is None:
            attrs = 'fill="%s" fill-opacity="%.3f"' % (paint, opacity)
        else:
            attrs = ('fill="none" stroke="%s" stroke-opacity="%.3f" '
                     'stroke-width="%d" stroke-linejoin="round" '
                     'stroke-linecap="round"' % (paint, opacity, stroke))
        body.append('  <path %s d="%s"/>' % (attrs, d))
    return ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<!-- Generated by icon/make_hvif.py; edit the script, not this. -->\n'
            '<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" '
            'viewBox="0 0 64 64">\n  <defs>%s</defs>\n%s\n</svg>\n'
            % ("".join(defs), "\n".join(body)))


def rdef_block(data):
    lines = ['\t$"%s"' % data[i:i + 32].hex().upper()
             for i in range(0, len(data), 32)]
    return "resource vector_icon {\n%s\n};" % "\n".join(lines)


def main():
    shapes = build_icon()
    data = encode_hvif(shapes)
    with open(os.path.join(HERE, "PatchWork.svg"), "w") as f:
        f.write(build_svg(shapes))
    rdef_path = os.path.join(ROOT, "PatchWork.rdef")
    with open(rdef_path) as f:
        rdef = f.read()
    rdef, count = re.subn(r"resource vector_icon \{.*?\n\};",
                          lambda m: rdef_block(data), rdef, flags=re.S)
    assert count == 1, "PatchWork.rdef needs one resource vector_icon block"
    with open(rdef_path, "w") as f:
        f.write(rdef)
    print("wrote icon/PatchWork.svg and %d HVIF bytes into PatchWork.rdef"
          % len(data))


if __name__ == "__main__":
    main()
