"""Peter 27.10.: eine reine V-Carve-Zeichnung zeigte die Vorgabe-Fräser B (Schaft 3 mm) und C (Schaft 6 mm) «nur in
dieser Zeichnung», obwohl keine Bearbeitung sie benutzt. Unbenutzte, nie geänderte Vorgaben fallen beim Lesen weg."""
import json

from hirch_cam import einstellungen as einst


def _zeichnung(werkzeuge, bearbeitungen):
    return einst.laden(json.dumps({"werkzeuge": werkzeuge, "bearbeitungen": bearbeitungen}))


def test_unbenutzte_vorgaben_fallen_weg():
    ws = {k: dict(einst.STANDARD_WERKZEUGE[k], nur_hier=True) for k in "ABC"}
    ws["B"].pop("name")                      # so stehen sie in älteren Zeichnungen
    e = _zeichnung(ws, [{"id": "v1", "art": "vcarve", "werkzeug": "A"}])
    assert einst.vorgaben_ohne_nutzen_weg(e) == ["B", "C"]
    assert list(e["werkzeuge"]) == ["A"]


def test_geaenderte_benutzte_und_verknuepfte_bleiben():
    ws = {k: dict(einst.STANDARD_WERKZEUGE[k]) for k in "ABC"}
    ws["B"]["durchmesser"] = 2.0             # von Hand geändert
    ws["C"]["db_id"] = "f21"                 # aus «Meine Werkzeuge»
    e = _zeichnung(ws, [{"id": "t1", "art": "tasche", "werkzeug": "A", "schlicht_werkzeug": "A"}])
    assert einst.vorgaben_ohne_nutzen_weg(e) == []
    assert sorted(e["werkzeuge"]) == ["A", "B", "C"]


def test_leere_neue_zeichnung_behaelt_vorgaben():
    e = _zeichnung({k: dict(einst.STANDARD_WERKZEUGE[k]) for k in "ABC"}, [])
    assert einst.vorgaben_ohne_nutzen_weg(e) == []


def test_passloch_werkzeug_bleibt():
    e = _zeichnung({k: dict(einst.STANDARD_WERKZEUGE[k]) for k in "ABC"},
                   [{"id": "v1", "art": "vcarve", "werkzeug": "A"}])
    e["zweiseitig"] = {"an": True, "pass_werkzeug": "C"}
    assert einst.vorgaben_ohne_nutzen_weg(e) == ["B"]
