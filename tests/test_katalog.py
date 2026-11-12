"""Jeder Text, der im C++-Teil mit TR()/TRF() übersetzt wird, steht im englischen Katalog.

Fehlt ein Eintrag, erscheint der Text in der englischen Oberfläche deutsch.
Neue Texte in hirch_cam/sprache/en.json eintragen (deutsch → englisch, «{}» gleich oft).
"""
import ast
import glob
import json
import os
import re

HIER = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QUELLEN = sorted(glob.glob(os.path.join(HIER, "fork", "src", "ui", "dialog", "hirch-*.cpp")))
KATALOG = os.path.join(HIER, "hirch_cam", "sprache", "en.json")


def _literale(src, i):
    """Aneinandergereihte C-Literale ab i: (Text, Ende) oder None."""
    teile, n = [], len(src)
    while True:
        while i < n and src[i] in " \t\r\n":
            i += 1
        if i < n and src[i] == '"':
            j = i + 1
            while src[j] != '"':
                j += 2 if src[j] == "\\" else 1
            teile.append(src[i:j + 1])
            i = j + 1
        else:
            break
    if not teile:
        return None
    return "".join(ast.literal_eval(t) for t in teile), i


def tr_texte():
    texte = {}
    for pfad in QUELLEN:
        src = open(pfad, encoding="utf-8").read()
        for m in re.finditer(r"\bTRF?\(", src):
            r = _literale(src, m.end())
            if not r:
                continue
            text, ende = r
            rest = src[ende:].lstrip()
            if not rest.startswith((")", ",")):
                continue
            if re.search(r"[A-Za-zÄÖÜäöüß]{2,}", re.sub(r"<[^>]*>", "", text)):
                texte.setdefault(text, "%s:%d" % (os.path.basename(pfad), src.count("\n", 0, m.start()) + 1))
    return texte


def test_alle_texte_im_katalog():
    with open(KATALOG, encoding="utf-8") as f:
        kat = json.load(f)
    fehlt = {t: wo for t, wo in tr_texte().items() if t not in kat}
    assert not fehlt, "Nicht im Katalog:\n" + "\n".join("  %s  %r" % (wo, t) for t, wo in fehlt.items())


def test_katalog_ohne_bausteine_aus_dem_code():
    """Keine Einträge, die der Code als Kennung braucht (Dateinamen, Befehle)."""
    with open(KATALOG, encoding="utf-8") as f:
        kat = json.load(f)
    for verboten in ("Fraesen", "Adwaita", "Projekte", "Downloads"):
        assert kat.get(verboten, verboten) == verboten, verboten


def test_mitgelieferte_werkzeugnamen_im_katalog():
    """Die mitgelieferten Fräser («Meine Werkzeuge», Vorgaben neuer Zeichnungen) heissen in der englischen
    Oberfläche englisch (werkzeug_name übersetzt den gespeicherten Namen über den Katalog)."""
    from hirch_cam.einstellungen import STANDARD_WERKZEUGE
    from hirch_cam.werkzeugdb import BEISPIELE
    with open(KATALOG, encoding="utf-8") as f:
        k = json.load(f)
    namen = {w["name"] for w in BEISPIELE} | {w["name"] for w in STANDARD_WERKZEUGE.values()}
    assert not sorted(n for n in namen if n not in k)
