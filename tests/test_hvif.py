#!/usr/bin/env python3
"""Decodes the HVIF built by icon/make_hvif.py and checks it round-trips.

The decoder mirrors Haiku's FlatIconImporter.cpp / PathCommandQueue.cpp /
FlatIconFormat.cpp (read_coord, read_float_24) independently of the encoder.

Run: python3 tests/test_hvif.py
"""

import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True  # keep icon/ free of __pycache__
sys.path.insert(0, os.path.join(ROOT, "icon"))
import make_hvif  # noqa: E402


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def u8(self):
        value = self.data[self.pos]
        self.pos += 1
        return value

    def bytes(self, count):
        value = self.data[self.pos:self.pos + count]
        assert len(value) == count, "truncated"
        self.pos += count
        return value

    def coord(self):
        value = self.u8()
        if value & 0x80:
            return (((value & 0x7F) << 8) | self.u8()) / 102.0 - 128.0
        return value - 32.0

    def float24(self):
        b = self.bytes(3)
        v = (b[0] << 16) | (b[1] << 8) | b[2]
        if v == 0:
            return 0.0
        sign = (v & 0x800000) >> 23
        exponent = ((v & 0x7E0000) >> 17) - 32
        mantissa = (v & 0x01FFFF) << 6
        bits = (sign << 31) | ((exponent + 127) << 23) | mantissa
        return struct.unpack("<f", struct.pack("<I", bits))[0]


def decode(data):
    r = Reader(data)
    assert r.bytes(4) == b"ncif", "bad magic"

    def color(alpha, gray):
        if gray:
            value = r.u8()
            return (value, value, value, r.u8() if alpha else 255)
        return tuple(r.bytes(4)) if alpha else tuple(r.bytes(3)) + (255,)

    styles = []
    for _ in range(r.u8()):
        kind = r.u8()
        if kind in (1, 3, 4, 5):  # solid: color/no alpha/gray/gray no alpha
            styles.append(("solid", color(kind in (1, 4), kind in (4, 5))))
        elif kind == 2:
            gradient_type, flags, count = r.u8(), r.u8(), r.u8()
            matrix = [r.float24() for _ in range(6)] if flags & 2 else None
            alpha, gray = not flags & 4, bool(flags & 16)
            stops = [(r.u8(), color(alpha, gray)) for _ in range(count)]
            styles.append(("gradient", gradient_type, matrix, stops))
        else:  # unknown style: tagged, skipped like the importer does
            r.bytes(struct.unpack("<H", r.bytes(2))[0])

    paths = []
    for _ in range(r.u8()):
        flags, count = r.u8(), r.u8()
        points = []
        if flags & 8:  # no curves
            for _ in range(count):
                p = (r.coord(), r.coord())
                points.append((p, p, p))
        elif flags & 4:  # command queue
            commands = r.bytes((count + 3) // 4)
            last = (0.0, 0.0)
            for i in range(count):
                command = (commands[i // 4] >> (2 * (i % 4))) & 3
                if command == 0:
                    p = (r.coord(), last[1])
                elif command == 1:
                    p = (last[0], r.coord())
                elif command == 2:
                    p = (r.coord(), r.coord())
                if command == 3:
                    p = (r.coord(), r.coord())
                    points.append((p, (r.coord(), r.coord()),
                                   (r.coord(), r.coord())))
                else:
                    points.append((p, p, p))
                last = p
        else:
            for _ in range(count):
                points.append(((r.coord(), r.coord()), (r.coord(), r.coord()),
                               (r.coord(), r.coord())))
        paths.append((points, bool(flags & 2)))

    shapes = []
    for _ in range(r.u8()):
        assert r.u8() == 10, "unexpected shape type"
        style, count = r.u8(), r.u8()
        indices = list(r.bytes(count))
        flags = r.u8()
        if flags & 2:  # full transform
            [r.float24() for _ in range(6)]
        elif flags & 32:  # translation
            r.coord(), r.coord()
        if flags & 8:  # level-of-detail scale range
            r.bytes(2)
        transformers = []
        if flags & 16:
            for _ in range(r.u8()):
                kind = r.u8()
                if kind == 20:  # affine: six little-endian floats
                    r.bytes(24)
                elif kind == 21:  # contour
                    transformers.append(("contour",) + tuple(r.bytes(3)))
                elif kind == 22:  # perspective
                    [r.float24() for _ in range(9)]
                elif kind == 23:  # stroke
                    width, options, miter = r.u8(), r.u8(), r.u8()
                    transformers.append((width - 128, options & 15,
                                         options >> 4, miter))
                else:
                    r.bytes(struct.unpack("<H", r.bytes(2))[0])
        shapes.append((style, indices, flags, transformers))

    assert r.pos == len(data), "%d trailing bytes" % (len(data) - r.pos)
    return styles, paths, shapes


def close(a, b, tolerance=0.5 / 102 + 1e-6):
    return abs(a - b) <= tolerance


def test_coord_encoding():
    enc, dec = make_hvif.encode_coord, lambda b: Reader(b).coord()
    assert enc(0) == b"\x20" and enc(-32) == b"\x00" and enc(95) == b"\x7f"
    assert len(enc(96)) == 2 and len(enc(-33)) == 2 and len(enc(10.5)) == 2
    for value in (-40.0, -1.25, 0.0, 10.5, 31.37, 63.99, 96.0, 150.0):
        assert close(dec(enc(value)), value), value
    for value in (0.25, -0.5, 1.0, 0.0078125, -3.75, 64.0):
        assert abs(Reader(make_hvif.encode_float24(value)).float24() - value) \
            < abs(value) * 1e-4, value


def test_round_trip():
    shapes = make_hvif.build_icon()
    data = make_hvif.encode_hvif(shapes)
    styles, paths, decoded = decode(data)
    assert len(decoded) == len(shapes)
    assert len(styles) == len({repr(s) for _, s, _ in shapes})
    assert len(paths) == len({(tuple(p), c) for ps, _, _ in shapes
                              for p, c in ps})
    point_count = 0
    for (want_paths, want_style, stroke), (style, indices, flags,
                                           transformers) in zip(shapes, decoded):
        assert len(indices) == len(want_paths)
        for (want_points, want_closed), index in zip(want_paths, indices):
            points, closed = paths[index]
            assert closed == want_closed and len(points) == len(want_points)
            for want, got in zip(want_points, points):
                for wp, gp in zip(make_hvif.point_triplet(want), got):
                    assert close(wp[0], gp[0]) and close(wp[1], gp[1]), \
                        (wp, gp)
                point_count += 1

        got_style = styles[style]
        if want_style[0] == "solid":
            assert got_style == want_style
        else:
            _, p1, p2, stops = want_style
            assert got_style[1] == 0  # linear
            matrix = got_style[2]
            # -64 and +64 on the gradient axis must land on p1 and p2.
            for x, want in ((-64, p1), (64, p2)):
                gx = x * matrix[0] + matrix[4]
                gy = x * matrix[1] + matrix[5]
                assert abs(gx - want[0]) < 0.01 and abs(gy - want[1]) < 0.01
            assert [(o, c) for o, c in got_style[3]] == \
                [(round(o * 255), c) for o, c in stops]

        if stroke is None:
            assert flags == 0 and transformers == []
        else:
            assert transformers == [(stroke, 2, 2, 4)]
    return len(data), len(styles), len(paths), len(decoded), point_count


def test_rdef_matches():
    with open(os.path.join(ROOT, "PatchWork.rdef")) as f:
        block = re.search(r"resource vector_icon \{(.*?)\n\};", f.read(), re.S)
    hexdata = "".join(re.findall(r'\$"([0-9A-F]*)"', block.group(1)))
    expected = make_hvif.encode_hvif(make_hvif.build_icon())
    assert bytes.fromhex(hexdata) == expected, \
        "PatchWork.rdef is stale; run icon/make_hvif.py"


if __name__ == "__main__":
    test_coord_encoding()
    size, styles, paths, shapes, points = test_round_trip()
    test_rdef_matches()
    print("test_hvif: all passed (%d bytes: %d styles, %d paths, %d shapes, "
          "%d points round-tripped)" % (size, styles, paths, shapes, points))
