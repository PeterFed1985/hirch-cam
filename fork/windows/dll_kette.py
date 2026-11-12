"""Kopiert die DLLs, die Module laden, ins bin\\ des Pakets – samt allem, was diese DLLs wieder laden.

    python dll_kette.py ZIEL_BIN SUCHORDNER[;SUCHORDNER…] DATEI…

Was schon in ZIEL_BIN liegt, bleibt (dessen Bedarf hat Inkscapes Installation mitgebracht);
was in keinem Suchordner liegt, ist von Windows. Liest nur die Importtabelle der PE-Datei
(objdump -p liest auch die Exporte und braucht je OpenCASCADE-DLL Sekunden bis Minuten).
"""
import os
import shutil
import struct
import sys


def importe(pfad):
    d = open(pfad, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    anzahl = struct.unpack_from("<H", d, pe + 6)[0]
    opt = struct.unpack_from("<H", d, pe + 20)[0]
    o = pe + 24
    verz = o + (112 if struct.unpack_from("<H", d, o)[0] == 0x20B else 96)
    rva = struct.unpack_from("<I", d, verz + 8)[0]          # Datenverzeichnis 1: Importe
    teile = [struct.unpack_from("<8sIIII", d, o + opt + 40 * i) for i in range(anzahl)]

    def lage(r):
        for _, vgr, va, rgr, ro in teile:
            if va <= r < va + max(vgr, rgr):
                return r - va + ro
        raise ValueError(f"{pfad}: Adresse {r:#x} in keinem Abschnitt")

    namen, i = [], lage(rva) if rva else None
    while i is not None:
        n = struct.unpack_from("<I", d, i + 12)[0]
        if not n:
            break
        e = lage(n)
        namen.append(d[e:d.index(b"\0", e)].decode("ascii"))
        i += 20
    return namen


def main():
    ziel, such, dateien = sys.argv[1], sys.argv[2].split(";"), sys.argv[3:]
    da = {f.lower() for f in os.listdir(ziel)}
    gesehen, offen = set(), list(dateien)
    while offen:
        for name in importe(offen.pop()):
            k = name.lower()
            if k in gesehen or k in da:
                continue
            gesehen.add(k)
            for o in such:
                q = os.path.join(o, name)
                if os.path.isfile(q):
                    shutil.copy2(q, ziel)
                    da.add(k)
                    print("  +", name)
                    offen.append(os.path.join(ziel, name))
                    break


if __name__ == "__main__":
    main()
