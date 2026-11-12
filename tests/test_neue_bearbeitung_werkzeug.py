"""Neue Bearbeitung (automatisch angelegt, z. B. eine Fräsart zum ersten Mal auf Seite 2): passendes Werkzeug
statt der Vorgabe (Peter, 22.10.: Gewinde bekam den Schaftfräser B und wurde ausgelassen)."""
from hirch_cam import einstellungen as einst

W = {"B": {"typ": "schaft", "durchmesser": 3}, "C": {"typ": "schaft", "durchmesser": 6},
     "G": {"typ": "gewinde", "durchmesser": 5}, "V": {"typ": "v", "durchmesser": 6, "v_winkel": 60}}


def test_gewinde_bekommt_den_gewindefraeser():
    e = {"werkzeuge": dict(W), "bearbeitungen": []}
    assert einst.neue_bearbeitung(e, "gewinde")["werkzeug"] == "G"
    assert einst.neue_bearbeitung(e, "vcarve")["werkzeug"] == "V"


def test_wie_dieselbe_fraesart_auf_der_anderen_seite():
    e = {"werkzeuge": dict(W), "bearbeitungen": [{"id": "t1", "art": "tasche", "werkzeug": "C"}]}
    assert einst.neue_bearbeitung(e, "tasche")["werkzeug"] == "C"


def test_ohne_passendes_werkzeug_die_vorgabe():
    e = {"werkzeuge": {"B": W["B"]}, "bearbeitungen": []}
    assert einst.neue_bearbeitung(e, "gewinde")["werkzeug"] == "B"
    assert einst.neue_bearbeitung(e, "tasche")["werkzeug"] == "B"


def test_ergaenzen_seite_2():
    e = einst.laden('{"werkzeuge": {"B": {"typ": "schaft", "durchmesser": 3}, "G": {"typ": "gewinde", '
                    '"durchmesser": 5}}, "zweiseitig": {"an": true}, "bearbeitungen": [{"id": "w1", '
                    '"art": "gewinde", "werkzeug": "G"}]}')
    e = einst.ergaenzen(e, {("gewinde", 2)})
    neu = [b for b in e["bearbeitungen"] if b.get("auf_seite") == 2]
    assert neu and neu[0]["werkzeug"] == "G"
