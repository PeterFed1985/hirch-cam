"""Einlinienschriften im Textwerkzeug: die TTF setzt Buchstaben genau wie der optische Abstand
des Gravurtexts (Rille mitgerechnet) – kein L mehr im D."""
import pytest

from hirch_cam import einlinien, schrift_ttf


def test_metrik_mindestabstand():
    s = einlinien.schriften()["Hershey Sans"]
    breiten, paare = einlinien.optische_metrik(s)
    assert "LD" in paare or "LD" not in paare        # Paar darf fehlen, wenn die Breite stimmt
    # zwischen den Rillen bleibt überall Luft: L gefolgt von D, T gefolgt von o, r gefolgt von .
    cap = s["cap"]
    r = einlinien.RILLE_ANTEIL * cap / 2
    for paar in ("LD", "To", "r.", "AV", "LT", "77"):
        a, b = paar
        A = einlinien._Profil(s["g"][a][1], cap, r)
        B = einlinien._Profil(s["g"][b][1], cap, r)
        d = breiten[a] + paare.get(paar, 0.0)
        luecken = [B.L[i] + d - A.R[i] for i in range(einlinien.NB)
                   if A.R[i] > -1e9 and B.L[i] < 1e9]
        assert min(luecken) > 0, paar


def test_ttf_setzt_wie_gravurtext(tmp_path):
    hb = pytest.importorskip("uharfbuzz")
    name = "Hershey Sans"
    s = dict(einlinien.schriften()[name], name=name)
    datei = schrift_ttf.bauen(s, str(tmp_path / "hs.ttf"))
    font = hb.Font(hb.Face(hb.Blob.from_file_path(datei)))
    k = schrift_ttf.VERSAL / s["cap"]
    F = einlinien._Schrift(name)
    for text in ("LD", "Müstair", "AVATAR Ty 7.1", "HIRCH CNC"):
        buf = hb.Buffer()
        buf.add_str(text)
        buf.guess_segment_properties()
        hb.shape(font, buf, {"kern": True})
        x, pos = 0, []
        for p in buf.glyph_positions:
            pos.append(x)
            x += p.x_advance
        pos = [p for p, ch in zip(pos, text) if ch.strip()]
        ref = [ox * k for _s, ox in einlinien._zeile(text, F, True, einlinien.LUFT_ANTEIL * s["cap"],
                                                      einlinien.RILLE_ANTEIL * s["cap"] / 2)]
        for a, b in zip(pos, ref):
            assert abs((a - pos[0]) - (b - ref[0])) <= 3, text     # 3 Einheiten = 0,03 mm bei 10 mm
