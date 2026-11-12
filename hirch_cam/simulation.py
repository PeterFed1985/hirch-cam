# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Abtragssimulation (Höhenfeld) für Prüfungen und Tests.

Unabhängig von der Browser-Vorschau in Python/NumPy umgesetzt, damit
Tests nachrechnen können, was nach einem oder mehreren .tap-Programmen
vom Material übrig bleibt.
"""
from __future__ import annotations

import math

import numpy as np

from .model import Job


class Hoehenfeld:
    def __init__(self, x0, y0, x1, y1, zelle=0.1):
        self.x0, self.y0, self.c = x0, y0, zelle
        self.nx = int(math.ceil((x1 - x0) / zelle)) + 1
        self.ny = int(math.ceil((y1 - y0) / zelle)) + 1
        self.H = np.zeros((self.ny, self.nx), dtype=np.float32)

    def koordinaten(self):
        xs = self.x0 + np.arange(self.nx) * self.c
        ys = self.y0 + np.arange(self.ny) * self.c
        return np.meshgrid(xs, ys)

    def _stempel(self, form, durchmesser, v_winkel, spitze=0.0, eckradius=0.0):
        r = durchmesser / 2
        n = int(math.ceil(r / self.c))
        g = np.arange(-n, n + 1) * self.c
        dx, dy = np.meshgrid(g, g)
        d = np.hypot(dx, dy)
        if form == "v":
            prof = np.maximum(0.0, d - spitze / 2) / math.tan(math.radians(v_winkel / 2))
        elif form == "konisch":
            rb = max(0.01, min(eckradius, r))
            halb = math.radians(max(1.0, min(179.0, v_winkel)) / 2)
            dt, ht = rb * math.cos(halb), rb - rb * math.sin(halb)
            prof = np.where(d <= dt, rb - np.sqrt(np.maximum(0.0, rb * rb - np.minimum(d, rb) ** 2)),
                            ht + (d - dt) / math.tan(halb))
        elif form == "kugel" and eckradius > 0:
            rc = min(eckradius, r)
            aussen = np.clip(d - (r - rc), 0.0, rc)
            prof = rc - np.sqrt(np.maximum(0.0, rc * rc - aussen * aussen))
        else:
            prof = np.zeros_like(d)
        prof[d > r + 1e-9] = np.inf
        return n, prof.astype(np.float32)

    def fraesen(self, job: Job, form="flach", durchmesser=3.0, v_winkel=60.0, start=None,
                spitze=0.0, eckradius=0.0):
        n, prof = self._stempel(form, durchmesser, v_winkel, spitze, eckradius)
        schritt = self.c / 2
        p = start or (job.home[0], job.home[1], job.safe_z)
        for tp in job.toolpaths:
            for m in tp.moves:
                q = (m.x, m.y, m.z)
                if min(p[2], q[2]) < 0:
                    l = math.dist(p, q)
                    k = max(1, int(math.ceil(l / schritt)))
                    for s in range(1, k + 1):
                        t = s / k
                        self._stempeln(p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t,
                                       p[2] + (q[2] - p[2]) * t, n, prof)
                p = q
        return self

    def _stempeln(self, x, y, z, n, prof):
        if z >= 0:
            return
        i = int(round((x - self.x0) / self.c))
        j = int(round((y - self.y0) / self.c))
        i0, i1 = max(0, i - n), min(self.nx, i + n + 1)
        j0, j1 = max(0, j - n), min(self.ny, j + n + 1)
        if i0 >= i1 or j0 >= j1:
            return
        sub = prof[j0 - (j - n):j1 - (j - n), i0 - (i - n):i1 - (i - n)] + z
        np.minimum(self.H[j0:j1, i0:i1], sub, out=self.H[j0:j1, i0:i1])
