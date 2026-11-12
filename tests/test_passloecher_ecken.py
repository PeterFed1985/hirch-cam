"""Passlöcher in den vier Ecken: spiegelbildlich zur Wendeachse."""
import json

from hirch_cam import einstellungen as einst


def test_ecken_symmetrisch():
    for achse in ("x", "y"):
        e = einst.laden(json.dumps({"zweiseitig": {"an": True, "achse": achse, "pass_rand": 10, "pass_lage": "ecken"}}))
        pts = einst.passloecher(e, 200, 100)
        assert sorted(pts) == sorted([(10, 10), (190, 10), (10, 90), (190, 90)])
        # nach dem Wenden liegt jedes Loch wieder auf einem Loch
        gewendet = {(x, 100 - y) if achse == "x" else (200 - x, y) for x, y in pts}
        assert gewendet == set(pts)
        a = einst.auftrag(e, {}, 18.0, "/tmp/x.tap", flaeche=(200, 100))
        pl = [b for b in a["bearbeitungen"] if b["id"] == einst.PASS_ID]
        assert len(pl) == 1 and len(pl[0]["objekte"]) == 4
