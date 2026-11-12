# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Zwischenspeicher und gleichzeitiges Rechnen der 3D-Bearbeitungen (Etappe «Schneller rechnen»).

1. Zwischenspeicher: Ein 3D-Schruppen oder 3D-Schlichten hängt nur von seinen eigenen Werten,
   dem Werkzeug, dem Modell (Daten, Lage), dem Material und den Höhen ab – nicht von den anderen
   Bearbeitungen. Gleiche Eingaben geben die gleiche Bahn; sie liegt unter
   ~/.cache/hirch-cam/3d-bahnen (HIRCH_3D_SPEICHER: anderer Ordner, «aus»: nie speichern).
   Im Schlüssel steckt auch der Stand des Rechenkerns (Quelltext der 3D-Module, hirch_relief),
   damit ein Update nie eine alte Bahn liefert. Die Bahnen werden Zahl für Zahl (float64)
   abgelegt – der G-Code ist derselbe wie frisch gerechnet.

2. Gleichzeitig: Sind mehrere 3D-Bearbeitungen neu zu rechnen, laufen alle bis auf eine in
   eigenen Prozessen (python -m hirch_cam.ops.relief_speicher), die letzte – meist das
   Schlichten, das OpenCAMLib ohnehin auf alle Kerne verteilt – hier. Stirbt die Rechnung
   («Abbrechen»), sterben die Kinder mit (PR_SET_PDEATHSIG).
"""
from __future__ import annotations

import hashlib
import json
import os
import pickle
import subprocess
import sys
import tempfile
import time
from typing import Dict, List, Optional, Tuple

import numpy as np

ARTEN = ("relief_schruppen", "relief_schlichten")
AUFTRAG_TEILE = ("material_flaeche", "hoehen", "reliefs", "zweiseitig", "rund")   # was relief3d.bearbeiten liest
HOECHSTENS_DATEIEN = 40
HOECHSTENS_BYTES = 600 * 1024 * 1024
FASSUNG = 1

_kern_stand: Optional[str] = None
_fertig: Dict[str, Tuple] = {}         # eben (in diesem Prozess) gerechnet: Schlüssel -> (tp, warn)


def ordner() -> Optional[str]:
    o = os.environ.get("HIRCH_3D_SPEICHER")
    if o == "aus":
        return None
    if not o:
        basis = os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache")
        o = os.path.join(basis, "hirch-cam", "3d-bahnen")
    return o


def _stand() -> str:
    """Stand des Rechenkerns: Quelltext der 3D-Module und das C++-Modul."""
    global _kern_stand
    if _kern_stand is None:
        h = hashlib.sha256()
        hier = os.path.dirname(os.path.abspath(__file__))
        paket = os.path.dirname(hier)
        for d in ("relief3d.py", "relief_zweiseitig.py", "bahn.py", "relief_speicher.py"):
            try:
                with open(os.path.join(hier, d), "rb") as fh:
                    h.update(fh.read())
            except OSError:
                pass
        for d in ("relief.py", "relief_import.py", "model.py"):
            try:
                with open(os.path.join(paket, d), "rb") as fh:
                    h.update(fh.read())
            except OSError:
                pass
        for n in sorted(os.listdir(paket)):
            if n.startswith("hirch_relief") and n.endswith(".so"):
                st = os.stat(os.path.join(paket, n))
                h.update(f"{n}:{st.st_size}:{int(st.st_mtime)}".encode())
        _kern_stand = h.hexdigest()
    return _kern_stand


def auftrag_teil(art: str, auftrag: Dict) -> Dict:
    """Nur die Teile des Auftrags, die eine 3D-Bearbeitung liest (Entwurf nur beim Schlichten)."""
    t = {k: auftrag.get(k) for k in AUFTRAG_TEILE if k in auftrag}
    if art == "relief_schlichten" and auftrag.get("entwurf"):
        t["entwurf"] = True
    return t


def _daten_kennung(text: str) -> str:
    """Kennung der Modelldaten – ohne die Zeit im gzip-Kopf (gleiches Modell, neu eingebettet)."""
    import base64
    try:
        roh = bytearray(base64.b64decode(text, validate=False))
        if roh[:2] == b"\x1f\x8b" and len(roh) > 10:
            roh[4:8] = b"\0\0\0\0"
        return hashlib.sha256(bytes(roh)).hexdigest()
    except Exception:
        return hashlib.sha256(text.encode("utf-8", "replace")).hexdigest()


def _vorher_kennung(vorher) -> str:
    """Kennung der vorherigen Bahnen («Nur Restmaterial»): Bewegungen und Werkzeugform."""
    if not vorher:
        return ""
    h = hashlib.sha256()
    for tp, wz in vorher:
        ms = tp.moves
        h.update(np.array([(m.x, m.y, m.z) for m in ms], np.float64).tobytes())
        h.update(json.dumps({k: wz.get(k) for k in ("typ", "durchmesser", "eckradius", "v_winkel", "spitze")},
                            sort_keys=True, default=str).encode())
    return h.hexdigest()


def schluessel(art, p, w, auftrag, seite, cz, sz, start, name, tol, vorher=None) -> str:
    teil = auftrag_teil(art, auftrag)
    if teil.get("reliefs"):
        teil["reliefs"] = [dict(r, daten=_daten_kennung(r["daten"])) if isinstance(r.get("daten"), str) else r
                           for r in teil["reliefs"]]
        # zweiseitig: wie weit diese Seite über die Trennlinie fräst, hängt (automatisch) vom Schlichtfräser
        # der Seite ab – der steht nicht in dieser Bearbeitung
        from .relief_zweiseitig import einstellungen, ueber_trennlinie
        teil["_ueber"] = [ueber_trennlinie(einstellungen(r.get("info")), int(seite), auftrag)
                          for r in teil["reliefs"]]
    daten = {"fassung": FASSUNG, "stand": _stand(), "art": art, "p": p, "w": w,
             "auftrag": teil, "seite": int(seite), "cz": cz, "sz": sz,
             "start": list(start), "name": name, "tol": tol, "vorher": _vorher_kennung(vorher)}
    text = json.dumps(daten, sort_keys=True, ensure_ascii=False, default=str)
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:40]


# --- Bahn ablegen und wieder lesen ----------------------------------------------------------

def packen(tp, warn) -> Dict:
    ms = tp.moves
    n = len(ms)
    rapid = np.fromiter((m.rapid for m in ms), bool, n)
    xyz = np.array([(m.x, m.y, m.z) for m in ms], np.float64).reshape(n, 3)
    feed = np.fromiter((np.nan if m.feed is None else m.feed for m in ms), np.float64, n)
    extra = {k: v for k, v in vars(tp).items() if k != "moves"}
    return {"fassung": FASSUNG, "rapid": rapid, "xyz": xyz, "feed": feed, "extra": extra,
            "warn": list(warn)}


def auspacken(d: Dict):
    from ..model import Move, Toolpath
    tp = Toolpath()
    for k, v in d["extra"].items():
        setattr(tp, k, v)
    xs, ys, zs = d["xyz"][:, 0].tolist(), d["xyz"][:, 1].tolist(), d["xyz"][:, 2].tolist()
    fs = d["feed"].tolist()
    tp.moves = [Move(r, x, y, z, None if f != f else f)
                for r, x, y, z, f in zip(d["rapid"].tolist(), xs, ys, zs, fs)]
    return tp, list(d["warn"])


def _schreiben(pfad: str, d: Dict):
    tmp = f"{pfad}.{os.getpid()}.neu"
    with open(tmp, "wb") as fh:
        pickle.dump(d, fh, protocol=pickle.HIGHEST_PROTOCOL)
    os.replace(tmp, pfad)


def _lesen(pfad: str) -> Optional[Dict]:
    try:
        with open(pfad, "rb") as fh:
            d = pickle.load(fh)
        return d if isinstance(d, dict) and d.get("fassung") == FASSUNG else None
    except Exception:
        return None


def laden(key: str):
    if key in _fertig:
        return _fertig.pop(key)
    o = ordner()
    if not o:
        return None
    pfad = os.path.join(o, key + ".bahn")
    d = _lesen(pfad) if os.path.exists(pfad) else None
    if d is None:
        return None
    try:
        os.utime(pfad)                       # zuletzt gebraucht (fürs Aufräumen)
    except OSError:
        pass
    return auspacken(d)


def vorhanden(key: str) -> bool:
    if key in _fertig:
        return True
    o = ordner()
    return bool(o) and os.path.exists(os.path.join(o, key + ".bahn"))


def ablegen(key: str, d: Dict):
    o = ordner()
    if not o:
        return
    try:
        os.makedirs(o, exist_ok=True)
        _schreiben(os.path.join(o, key + ".bahn"), d)
        _aufraeumen(o)
    except OSError:
        pass


def _aufraeumen(o: str):
    dateien = []
    for n in os.listdir(o):
        if n.endswith(".bahn"):
            try:
                st = os.stat(os.path.join(o, n))
                dateien.append((st.st_mtime, st.st_size, n))
            except OSError:
                pass
    dateien.sort(reverse=True)
    summe = 0
    for i, (_t, groesse, n) in enumerate(dateien):
        summe += groesse
        if i >= HOECHSTENS_DATEIEN or summe > HOECHSTENS_BYTES:
            try:
                os.unlink(os.path.join(o, n))
            except OSError:
                pass


# --- Rechnen --------------------------------------------------------------------------------

def _rechnen(args, vorher=None):
    from . import relief3d
    art, p, w, auftrag, seite, cz, sz, start, name, tol = args
    return relief3d.bearbeiten(art, p, w, auftrag_teil(art, auftrag), seite, cz, sz, tuple(start), name, tol,
                               vorher=vorher)


def bearbeiten(art, p, w, auftrag, seite, cz, sz, start, name, tol, vorher=None):
    """Wie relief3d.bearbeiten – aber gleiche Eingaben nur einmal rechnen. vorher: Bahnen der
    früheren 3D-Bearbeitungen der Seite (nur bei «Nur Restmaterial»)."""
    args = (art, p, w, auftrag, seite, cz, sz, start, name, tol)
    vorher = vorher if p.get("rest") else None
    key = schluessel(*args, vorher=vorher)
    gemerkt = laden(key)
    if gemerkt is not None:
        return gemerkt
    tp, warn = _rechnen(args, vorher)
    ablegen(key, packen(tp, warn))
    return tp, warn


def _todesbote():
    """Im Kindprozess: sterben, wenn die Rechnung (der Elternprozess) stirbt."""
    try:
        import ctypes
        import signal
        ctypes.CDLL("libc.so.6", use_errno=True).prctl(1, signal.SIGKILL)   # PR_SET_PDEATHSIG
    except Exception:
        pass


def vorab(aufgaben: List[Tuple]) -> int:
    """aufgaben: Argumente wie für bearbeiten(). Rechnet alle, die noch nicht gespeichert sind –
    gleichzeitig, wenn es mehrere sind und der Rechner mehr als einen Kern hat. Liefert, wie
    viele gleichzeitig liefen (0: nichts gleichzeitig)."""
    offen, gesehen = [], set()
    for a in aufgaben:
        k = schluessel(*a)
        if k not in gesehen and not vorhanden(k):
            gesehen.add(k)
            offen.append((k, a))
    if len(offen) < 2 or (os.cpu_count() or 1) < 2:
        return 0
    # das Schlichten (OpenCAMLib nutzt alle Kerne) hier, die anderen in Kindprozessen
    offen.sort(key=lambda ka: ka[1][0] == "relief_schlichten")
    hier_k, hier_a = offen[-1]
    tmp = tempfile.mkdtemp(prefix="hirch-3d-")
    kinder = []
    paket_eltern = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    env = dict(os.environ)
    env["PYTHONPATH"] = paket_eltern + (os.pathsep + env["PYTHONPATH"] if env.get("PYTHONPATH") else "")
    env.pop("HIRCH_FORTSCHRITT", None)
    # Windows kennt preexec_fn nicht (Popen verweigert sonst den Start); dort ohne Konsolenfenster
    start = ({"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt"
             else {"preexec_fn": _todesbote})
    for i, (k, a) in enumerate(offen[:-1]):
        ein, aus = os.path.join(tmp, f"{i}.ein"), os.path.join(tmp, f"{i}.aus")
        with open(ein, "wb") as fh:
            pickle.dump(a, fh, protocol=pickle.HIGHEST_PROTOCOL)
        try:
            pr = subprocess.Popen([sys.executable, "-m", "hirch_cam.ops.relief_speicher", ein, aus],
                                  env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL, **start)
        except OSError:
            continue
        kinder.append((k, a, pr, aus))
    try:
        tp, warn = _rechnen(hier_a)
        d = packen(tp, warn)
        ablegen(hier_k, d)
        _fertig[hier_k] = (tp, warn)
        for k, a, pr, aus in kinder:
            pr.wait()
            d = _lesen(aus) if pr.returncode == 0 and os.path.exists(aus) else None
            if d is None:
                continue                      # bearbeiten() rechnet es dann hier
            ablegen(k, d)
            if not ordner():
                _fertig[k] = auspacken(d)
    finally:
        for _k, _a, pr, _aus in kinder:
            if pr.poll() is None:
                pr.kill()
        for n in os.listdir(tmp):
            try:
                os.unlink(os.path.join(tmp, n))
            except OSError:
                pass
        try:
            os.rmdir(tmp)
        except OSError:
            pass
    return len(kinder) + 1


def main(argv=None):
    """Kindprozess: python -m hirch_cam.ops.relief_speicher EIN AUS"""
    argv = sys.argv[1:] if argv is None else argv
    with open(argv[0], "rb") as fh:
        args = pickle.load(fh)
    t = time.time()
    tp, warn = _rechnen(args)
    d = packen(tp, warn)
    d["sekunden"] = round(time.time() - t, 2)
    _schreiben(argv[1], d)
    return 0


if __name__ == "__main__":
    sys.exit(main())
