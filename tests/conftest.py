"""Tests laufen auf Deutsch (die Texte im Quelltext); test_sprache schaltet selbst um.

Jeder Test bekommt einen eigenen, leeren Einstellungsordner (XDG_CONFIG_HOME): kein Test
schreibt in ~/.config/hirch-cam (Werkzeuge, Postprozessoren, Schriften), und ein zweiter
Lauf ergibt dasselbe wie der erste."""
import os

import pytest

os.environ.setdefault("HIRCH_SPRACHE", "de")

# 3D-Zwischenspeicher der Tests in einen eigenen Ordner (nicht ~/.cache), für jeden Lauf neu
if "HIRCH_3D_SPEICHER" not in os.environ:
    import tempfile
    os.environ["HIRCH_3D_SPEICHER"] = tempfile.mkdtemp(prefix="hirch-test-3d-")
# 2D-Bahnen auf dem Rechner (B-26): in den Tests aus – «SPEICHER.leeren()» soll dort wirklich neu rechnen lassen;
# test_speicher_rechner_05_11 schaltet sie für sich ein
os.environ.setdefault("HIRCH_2D_SPEICHER", "aus")


@pytest.fixture(autouse=True)
def _eigener_einstellungsordner(tmp_path_factory, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path_factory.mktemp("konfig")))
    if os.name == "nt":
        # Windows: orte.konfig() liest %APPDATA% – in den Tests wie unter Linux XDG_CONFIG_HOME
        # folgen, auch wenn ein Test es umstellt (zwei PCs nachspielen)
        from hirch_cam import orte
        monkeypatch.setattr(orte, "konfig",
                            lambda: os.path.join(os.environ["XDG_CONFIG_HOME"], "hirch-cam"))
