"""Peter 09.11.: «In Windows habe ich jetzt rechts zwei Spalten … das geht so nicht.» Ein älterer Fensterstand mit
einer zweiten angedockten Spalte (Einstellungen) wird beim Start zu einer Spalte mit Fräsen vorne."""
from hirch_cam import profil

ZWEI = """[Windows]
Count=1

[Window0Column0]
Notebook0Dialogs=Fraesen;
Notebook0Height=889
Notebook0ActiveTab=0
ColumnWidth=330
NotebookCount=1
BeforeCanvas=false

[Window0Column1]
Notebook0Dialogs=HirchAnsicht;Objects;
Notebook0Height=889
Notebook0ActiveTab=0
ColumnWidth=360
NotebookCount=1
BeforeCanvas=false

[Window0]
ColumnCount=2
Floating=false

[transient]
count=0
"""


def test_zwei_spalten_werden_eine(tmp_path):
    (tmp_path / "dialogs-state-ex.ini").write_text(ZWEI, encoding="utf-8")
    assert profil.eine_spalte(str(tmp_path))
    t = (tmp_path / "dialogs-state-ex.ini").read_text(encoding="utf-8")
    assert "ColumnCount=1" in t and "Window0Column1" not in t
    assert "Notebook0Dialogs=Fraesen;HirchAnsicht;Objects;" in t
    assert "ColumnWidth=330" in t
    assert not profil.eine_spalte(str(tmp_path))          # schon eine Spalte: nichts ändern


def test_eine_spalte_bleibt_unberuehrt(tmp_path):
    eins = ZWEI.replace("[Window0Column1]\nNotebook0Dialogs=HirchAnsicht;Objects;\nNotebook0Height=889\n"
                        "Notebook0ActiveTab=0\nColumnWidth=360\nNotebookCount=1\nBeforeCanvas=false\n\n", "") \
               .replace("ColumnCount=2", "ColumnCount=1")
    (tmp_path / "dialogs-state-ex.ini").write_text(eins, encoding="utf-8")
    assert not profil.eine_spalte(str(tmp_path))
    assert (tmp_path / "dialogs-state-ex.ini").read_text(encoding="utf-8") == eins


def test_ohne_datei(tmp_path):
    assert not profil.eine_spalte(str(tmp_path))


SCHWEBEND = """[Windows]
Count=3

[Window0Column0]
Notebook0Dialogs=Fraesen;LivePathEffect;
Notebook0Height=889
Notebook0ActiveTab=0
ColumnWidth=480
NotebookCount=1
BeforeCanvas=false

[Window0]
ColumnCount=1
Floating=false

[Window1Column0]
Notebook0Dialogs=Objects;Find;
NotebookCount=1

[Window1]
ColumnCount=1
Floating=true
x=100
y=100

[Window2Column0]
Notebook0Dialogs=UndoHistory;Fraesen;
NotebookCount=1

[Window2]
ColumnCount=1
Floating=true
"""


def test_schwebende_fenster_kommen_in_die_spalte(tmp_path):
    """Windows-Nacht a349d01, Funde 1 und 2: schwebende Fenster (Absturz beim Beenden, zweites Fräsen)."""
    (tmp_path / "dialogs-state-ex.ini").write_text(SCHWEBEND, encoding="utf-8")
    assert profil.eine_spalte(str(tmp_path))
    t = (tmp_path / "dialogs-state-ex.ini").read_text(encoding="utf-8")
    assert "Floating=true" not in t and "Window1" not in t and "Window2" not in t and "Count=1" in t
    assert "Notebook0Dialogs=Fraesen;LivePathEffect;Objects;Find;UndoHistory;" in t
    assert not profil.eine_spalte(str(tmp_path))
