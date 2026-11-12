import math

from hirch_cam.einlinien import schriftnamen, text_linien, versatz


def _breite(linien):
    xs = [p[0] for l in linien for p in l]
    return max(xs) - min(xs)


def test_alle_schriften_mit_umlauten():
    for s in schriftnamen():
        l = text_linien("Äpfel & Öl, Grün! 123", s, 10)
        assert l, s
        ys = [p[1] for q in l for p in q]
        assert max(ys) < 15 and min(ys) > -7     # inkl. Umlautpunkte und Unterlängen


def test_versalhoehe_skaliert():
    a = text_linien("H", "Hershey Sans", 10)
    b = text_linien("H", "Hershey Sans", 20)
    hoehe = lambda l: max(p[1] for q in l for p in q) - min(p[1] for q in l for p in q)
    assert hoehe(b) == 2 * hoehe(a)
    assert abs(hoehe(a) - 10) < 0.5


def test_optischer_abstand_mit_fraeserbreite():
    # Breiterer Fräser -> optisch mehr Abstand nötig -> Text breiter
    schmal = text_linien("HAVANNA", "Hershey Sans", 20, schnittbreite=0.5)
    breit = text_linien("HAVANNA", "Hershey Sans", 20, schnittbreite=3.0)
    assert _breite(breit) > _breite(schmal)
    # Paar "AV" rückt optisch enger zusammen als mit festen Breiten
    opt = text_linien("AV", "Hershey Sans", 20, optisch=True, luft=0.1)
    fest = text_linien("AV", "Hershey Sans", 20, optisch=False)
    assert _breite(opt) < _breite(fest)


def test_mehrere_linien_und_zeilen():
    eins = text_linien("I", "Hershey Sans", 20, linien=1)
    drei = text_linien("I", "Hershey Sans", 20, linien=3, linienabstand=0.8)
    assert len(drei) == 3 * len(eins)
    zwei_zeilen = text_linien("A\nB", "Hershey Sans", 20, zeilenabstand=1.5)
    ys = [p[1] for q in zwei_zeilen for p in q]
    assert min(ys) < -25


def test_versatz_parallel():
    p = [(0, 0), (10, 0)]
    q = versatz(p, 1.0)
    assert all(math.isclose(y, 1.0) for _x, y in q)
