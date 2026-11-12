// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Windows-Teil des Programmstarts (aufgerufen aus set_win32_env in src/inkscape-main.cpp).
 * Unter Linux leer.
 */
#ifndef HIRCH_START_WIN32_H
#define HIRCH_START_WIN32_H

#ifdef _WIN32
/// Sprache, eigenes Profil, Rechenkern im Programmordner, Grafik (Karte oder Mesa) – vor GTK aufrufen
void hirch_win32_start();
#endif

#endif // HIRCH_START_WIN32_H
