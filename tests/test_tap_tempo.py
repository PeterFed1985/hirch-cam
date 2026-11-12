"""G-Code schneller schreiben (Etappe «Schneller rechnen»): koerper() muss Zeile für Zeile
dasselbe liefern wie die frühere, langsamere Fassung (hier als Vorlage abgelegt)."""
import random

from hirch_cam.model import Job, Move, Toolpath
from hirch_cam.post import tap_mm
from hirch_cam.post.tap_mm import EPS, _c, _f


def koerper_alt(job: Job, anfaenge: list | None = None, boegen: bool = False,
            toleranz: float = 0.005, zeilen: list | None = None) -> list[str]:
    """Die Werkzeugwege eines Jobs als G-Code-Zeilen (ohne Kopf und Schluss).
    Beginnt an job.home auf Sicherheitshöhe. anfaenge (falls gegeben) bekommt
    je Werkzeugweg den Index seiner ersten Zeile. boegen: Folgen kurzer
    Geraden auf einem Kreis als G2/G3 (I, J relativ zum Bogenanfang)."""
    out: list[str] = []
    hx, hy = job.home
    pos = {"X": hx, "Y": hy, "Z": job.safe_z}
    last_feed: str | None = None
    modus = 0                                     # zuletzt geschriebenes G0/G1/G2/G3

    def gerade(m: Move, first: bool):
        nonlocal pos, last_feed, modus
        vals = {"X": m.x, "Y": m.y, "Z": m.z}
        words = "".join(
            a + _c(vals[a])
            for a in "XYZ"
            if (m.rapid and first) or abs(vals[a] - pos[a]) > EPS
        )
        if m.rapid:
            out.append("G0" + words)
            modus = 0
        else:
            if m.feed is None:
                raise ValueError("Vorschubbewegung ohne Vorschubwert")
            f = _f(m.feed)
            if modus != 1 or f != last_feed:
                out.append("G1" + words + "F" + f)
                last_feed = f
            elif words:
                out.append(words)
            modus = 1
        pos = vals

    def bogen(ziel, mx, my, ccw, feed):
        nonlocal pos, last_feed, modus
        f = _f(feed)
        g = 3 if ccw else 2
        words = f"X{_c(ziel[0])}Y{_c(ziel[1])}"
        if abs(ziel[2] - pos["Z"]) > EPS:
            words += f"Z{_c(ziel[2])}"
        words += f"I{_c(mx - pos['X'])}J{_c(my - pos['Y'])}"
        zeile = "G%d" % g + words                # G2/G3 in jeder Bogenzeile (sicher für jede Steuerung)
        if f != last_feed or modus == 0:
            zeile += "F" + f
            last_feed = f
        out.append(zeile)
        modus = g
        pos = {"X": ziel[0], "Y": ziel[1], "Z": ziel[2]}

    for t_nr, tp in enumerate(job.toolpaths):
        if anfaenge is not None:
            anfaenge.append(len(out))
        first = True
        ms = tp.moves
        i = 0
        while i < len(ms):
            m = ms[i]
            if not boegen or m.rapid:
                gerade(m, first)
                if zeilen is not None:          # (Werkzeugweg, Bewegung, Zeilen danach) – Wiederaufnahme
                    zeilen.append((t_nr, i, len(out)))
                first = False
                i += 1
                continue
            # Folge von Vorschüben mit gleichem Vorschub
            j = i
            while j + 1 < len(ms) and not ms[j + 1].rapid and ms[j + 1].feed == m.feed:
                j += 1
            pts = [(pos["X"], pos["Y"], pos["Z"])] + [(q.x, q.y, q.z) for q in ms[i:j + 1]]
            if len(pts) < 5:
                for q in ms[i:j + 1]:
                    gerade(q, False)
            else:
                from hirch_cam.post.boegen import zerlegen
                for stueck in zerlegen(pts, toleranz):
                    if stueck[0] == "linie":
                        gerade(ms[i + stueck[2] - 1], False)     # pts[q] = ms[i + q − 1]
                    else:
                        _a, ziel, _q, mx, my, ccw = stueck
                        bogen(ziel, mx, my, ccw, m.feed)
            first = False
            i = j + 1
    return out


def _job(seed):
    r = random.Random(seed)
    job = Job(safe_z=20.0, home=(0.0, 0.0))
    for _ in range(r.randint(1, 6)):
        tp = Toolpath()
        x, y = r.uniform(0, 300), r.uniform(0, 200)
        tp.moves.append(Move(True, x, y, 5.0))
        z = 5.0
        for k in range(r.randint(1, 400)):
            wahl = r.random()
            if wahl < 0.05:
                tp.moves.append(Move(True, x, y, 5.0))
                z = 5.0
                continue
            if wahl < 0.3:                      # Kreisstück (für Bögen)
                import math
                mx, my, rr = x - 10, y, 10.0
                for t in range(1, 12):
                    a = t * 0.08
                    tp.moves.append(Move(False, mx + rr * math.cos(a), my + rr * math.sin(a), z, 800.0))
                x, y = tp.moves[-1].x, tp.moves[-1].y
                continue
            dx = r.choice([0.0, 1e-10, 3e-4, r.uniform(-5, 5)])
            dy = r.choice([0.0, 2e-10, r.uniform(-5, 5)])
            dz = r.choice([0.0, 0.0, -0.5, r.uniform(-1, 1)])
            x, y, z = x + dx, y + dy, z + dz
            f = r.choice([1200.0, 1200.0, 1200.04, 600.0, 1000.0 / 3])
            tp.moves.append(Move(False, x, y, z, f))
        job.toolpaths.append(tp)
    return job


def test_koerper_gleich_wie_vorher():
    for seed in range(60):
        job = _job(seed)
        for boegen in (False, True):
            a1, a2 = [], []
            assert tap_mm.koerper(job, a1, boegen=boegen) == koerper_alt(job, a2, boegen=boegen)
            assert a1 == a2
        z1, z2 = [], []
        assert tap_mm.koerper(job, zeilen=z1) == koerper_alt(job, zeilen=z2)
        assert z1 == z2


def test_merkliste():
    job = tap_mm.merken(_job(7))
    a = []
    erst = tap_mm.koerper(job, a)
    b = []
    assert tap_mm.koerper(job, b) == erst and a == b
    assert tap_mm.write(job) == tap_mm.NL.join(["T1M6", "G17", "G0Z20.000", "G0X0.000Y0.000S12000M3"]
                                               + erst + ["G0Z20.000", "G0X0.000Y0.000", "M30"]) + tap_mm.NL
    assert tap_mm.koerper(job, boegen=True) == koerper_alt(job, boegen=True)
