"""Biegt in einer DLL den festen Verweis auf OPENGL32.dll auf hirchgl.dll um (siehe hirchgl.c).

    python gl_weiche.py PFAD\\libgdk-3-0.dll

Überschreibt nur den Namen im Importverzeichnis (gleich lang oder kürzer, Rest mit Nullen);
die Funktionsnamen bleiben, hirchgl.dll bietet genau diese an. Ein zweiter Lauf ändert nichts.
"""
import struct
import sys

ALT, NEU = b"opengl32.dll", b"hirchgl.dll"


def umbiegen(pfad):
    d = bytearray(open(pfad, "rb").read())
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    anzahl = struct.unpack_from("<H", d, pe + 6)[0]
    opt = struct.unpack_from("<H", d, pe + 20)[0]
    o = pe + 24
    verz = o + (112 if struct.unpack_from("<H", d, o)[0] == 0x20B else 96)
    rva = struct.unpack_from("<I", d, verz + 8)[0]
    teile = [struct.unpack_from("<8sIIII", d, o + opt + 40 * i) for i in range(anzahl)]

    def lage(r):
        for _, vgr, va, rgr, ro in teile:
            if va <= r < va + max(vgr, rgr):
                return r - va + ro
        raise ValueError(f"{pfad}: Adresse {r:#x} in keinem Abschnitt")

    i, gefunden = lage(rva), False
    while struct.unpack_from("<I", d, i + 12)[0]:
        e = lage(struct.unpack_from("<I", d, i + 12)[0])
        name = bytes(d[e:d.index(0, e)])
        if name.lower() == NEU:
            return "schon umgebogen"
        if name.lower() == ALT:
            d[e:e + len(name)] = NEU + b"\0" * (len(name) - len(NEU))
            gefunden = True
        i += 20
    if not gefunden:
        raise SystemExit(f"{pfad}: kein Verweis auf {ALT.decode()}")
    open(pfad, "wb").write(d)
    return "umgebogen"


if __name__ == "__main__":
    print(f"{sys.argv[1]}: {umbiegen(sys.argv[1])}")
