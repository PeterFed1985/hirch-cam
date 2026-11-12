"""Passlöcher: ohne eigene Wahl ein passendes Werkzeug (Peter, 21.10.: «gar nicht dazu gekommen, ein anderes
Werkzeug auszusuchen»); der Fehler «Schneide zu kurz» bietet gleich «Werkzeug wählen …» an."""
from hirch_cam import einstellungen as einst

W = {"A": {"typ": "v", "durchmesser": 12, "schneidenlaenge": 11},
     "B": {"typ": "schaft", "durchmesser": 3, "schneidenlaenge": 12},
     "C": {"typ": "schaft", "durchmesser": 6, "schneidenlaenge": 22}}


def _e(**z):
    return {"zweiseitig": dict({"an": True, "passloecher": True, "pass_d": 6.0}, **z)}


def test_bohrer_im_durchmesser_wird_genommen():
    w = dict(W, D={"typ": "bohrer", "durchmesser": 6, "schneidenlaenge": 60})
    assert einst.pass_werkzeug(_e(), w, 33) == "D"


def test_ohne_bohrer_ein_fraeser_der_tief_genug_schneidet():
    w = dict(W, E={"typ": "schaft", "durchmesser": 4, "schneidenlaenge": 40})
    assert einst.pass_werkzeug(_e(), w, 33) == "E"
    # keiner reicht: wie bisher C
    assert einst.pass_werkzeug(_e(), W, 33) == "C"


def test_eigene_wahl_gilt():
    w = dict(W, D={"typ": "bohrer", "durchmesser": 6, "schneidenlaenge": 60})
    assert einst.pass_werkzeug(_e(pass_werkzeug="B"), w, 33) == "B"


def test_auftrag_nimmt_den_vorschlag():
    e = _e()
    e["werkzeuge"] = dict(einst.STANDARD_WERKZEUGE)
    e["werkzeuge"]["D"] = {"typ": "bohrer", "durchmesser": 6.0, "schneidenlaenge": 60, "nummer": 4}
    e = einst.laden(__import__("json").dumps(e))
    a = einst.auftrag(e, {}, 30.0, "/tmp/x.tap", flaeche=(100.0, 100.0))
    pass_ = [b for b in a["bearbeitungen"] if b.get("passloecher")][0]
    assert pass_["werkzeug"] == "D"


def test_fehler_schneide_zu_kurz_hat_aktion():
    from hirch_cam import pruefung
    import inspect
    assert '"aktion"] = "werkzeug"' in inspect.getsource(pruefung)
