/*
 * hirchgl.dll – Weiche für die drei OpenGL-Funktionen, die GTK (libgdk-3-0.dll) fest aus
 * opengl32.dll holt. bauen.sh biegt diesen Verweis in libgdk auf hirchgl.dll um (gl_weiche.py).
 *
 * Warum: Mit dem festen Verweis lädt Windows beim Programmstart sofort das opengl32 aus
 * System32 – noch bevor inkscape-main.cpp (hirch-cnc.patch) bei schwacher Grafik
 * bin\mesa\opengl32.dll laden kann. GTK und libepoxy nähmen dann das OpenGL 1.1 von Windows
 * («Es ist keine GL-Implementierung verfügbar»).
 *
 * Erst beim ersten Aufruf wird entschieden: Ist schon ein opengl32.dll geladen (Mesa, von
 * inkscape-main.cpp), dann dieses – sonst das von Windows bzw. vom Grafiktreiber. libepoxy
 * lädt «OPENGL32» später selbst und bekommt so dasselbe.
 *
 * Beim Laden (Programmstart, vor Mesa) gibt sie Mesa llvmpipe vor, falls nichts anderes
 * eingestellt ist: Mesa wählte sonst D3D12 über den «Microsoft Basic Render Driver», und
 * das Programm stürzte beim Beenden ab. Ohne Mesa bleibt die Einstellung ohne Wirkung.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <wchar.h>

BOOL WINAPI DllMain(HINSTANCE dll, DWORD grund, LPVOID frei)
{
    (void)dll; (void)frei;
    if (grund == DLL_PROCESS_ATTACH && !getenv("GALLIUM_DRIVER"))
        _putenv("GALLIUM_DRIVER=llvmpipe");     /* auch für getenv der C-Laufzeit, die Mesa nutzt */
    return TRUE;
}

static HMODULE ziel(void)
{
    static HMODULE gl;
    if (!gl) {
        gl = GetModuleHandleW(L"opengl32.dll");
        if (!gl) {
            wchar_t p[MAX_PATH];
            UINT n = GetSystemDirectoryW(p, MAX_PATH);
            if (n && n < MAX_PATH - 14) {
                wcscat(p, L"\\opengl32.dll");
                gl = LoadLibraryW(p);
            }
        }
    }
    return gl;
}

static FARPROC holen(const char *name)
{
    HMODULE m = ziel();
    return m ? GetProcAddress(m, name) : NULL;
}

typedef BOOL (WINAPI *T_delete)(HGLRC);
typedef HGLRC (WINAPI *T_current)(void);
typedef BOOL (WINAPI *T_make)(HDC, HGLRC);

BOOL WINAPI hirch_wglDeleteContext(HGLRC c)
{
    static T_delete f;
    if (!f) f = (T_delete)holen("wglDeleteContext");
    return f ? f(c) : FALSE;
}

HGLRC WINAPI hirch_wglGetCurrentContext(void)
{
    static T_current f;
    if (!f) f = (T_current)holen("wglGetCurrentContext");
    return f ? f() : NULL;
}

BOOL WINAPI hirch_wglMakeCurrent(HDC dc, HGLRC c)
{
    static T_make f;
    if (!f) f = (T_make)holen("wglMakeCurrent");
    return f ? f(dc, c) : FALSE;
}
