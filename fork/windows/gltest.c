/*
 * hirch-gltest.exe – kann die Grafikkarte OpenGL 3.3 (Core)?
 *
 * Läuft einmal beim ersten Start von Hirch CNC (eigener Prozess, damit ein
 * abstürzender Grafiktreiber nicht das Programm mitreisst). Rückgabe:
 *   0 = Grafikkarte genügt, 1 = nein (Software-Darstellung mit Mesa nehmen).
 * Auf stdout steht eine Zeile mit Hersteller, Grafik und Version.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

typedef HGLRC (WINAPI *T_create)(HDC);
typedef BOOL (WINAPI *T_make)(HDC, HGLRC);
typedef BOOL (WINAPI *T_delete)(HGLRC);
typedef PROC (WINAPI *T_proc)(LPCSTR);
typedef const unsigned char *(WINAPI *T_string)(unsigned int);
typedef HGLRC (WINAPI *T_attribs)(HDC, HGLRC, const int *);

int WINAPI WinMain(HINSTANCE inst, HINSTANCE vorher, LPSTR zeile, int zeigen)
{
    (void)vorher; (void)zeile; (void)zeigen;
    /* immer das OpenGL von Windows bzw. des Grafiktreibers */
    char sys[MAX_PATH];
    GetSystemDirectoryA(sys, MAX_PATH);
    strncat(sys, "\\opengl32.dll", MAX_PATH - strlen(sys) - 1);
    HMODULE gl = LoadLibraryA(sys);
    if (!gl) { printf("kein opengl32\n"); return 1; }
    T_create create = (T_create)GetProcAddress(gl, "wglCreateContext");
    T_make make = (T_make)GetProcAddress(gl, "wglMakeCurrent");
    T_delete del = (T_delete)GetProcAddress(gl, "wglDeleteContext");
    T_proc proc = (T_proc)GetProcAddress(gl, "wglGetProcAddress");
    T_string str = (T_string)GetProcAddress(gl, "glGetString");
    if (!create || !make || !del || !proc || !str) return 1;

    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = inst;
    wc.lpszClassName = "HirchGlTest";
    RegisterClassA(&wc);
    HWND w = CreateWindowA("HirchGlTest", "", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL, inst, NULL);
    if (!w) return 1;
    HDC dc = GetDC(w);
    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof pfd);
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    int pf = ChoosePixelFormat(dc, &pfd);
    if (!pf || !SetPixelFormat(dc, pf, &pfd)) { printf("kein Pixelformat\n"); return 1; }
    HGLRC alt = create(dc);
    if (!alt || !make(dc, alt)) { printf("kein OpenGL\n"); return 1; }
    T_attribs attribs = (T_attribs)proc("wglCreateContextAttribsARB");
    int ok = 0;
    if (attribs) {
        int a[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                   WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0};
        HGLRC neu = attribs(dc, NULL, a);
        if (neu && make(dc, neu)) {
            const char *v = (const char *)str(GL_VERSION);
            const char *r = (const char *)str(GL_RENDERER);
            const char *h = (const char *)str(GL_VENDOR);
            printf("%s | %s | %s\n", h ? h : "?", r ? r : "?", v ? v : "?");
            int ma = 0, mi = 0;
            if (v && sscanf(v, "%d.%d", &ma, &mi) == 2 && (ma > 3 || (ma == 3 && mi >= 3))) ok = 1;
            /* Microsofts Ersatz ohne Treiber zählt nicht */
            if (r && strstr(r, "GDI Generic")) ok = 0;
            make(dc, NULL);
            del(neu);
        }
    } else {
        const char *v = (const char *)str(GL_VERSION);
        printf("nur OpenGL %s\n", v ? v : "?");
    }
    make(dc, NULL);
    del(alt);
    ReleaseDC(w, dc);
    DestroyWindow(w);
    return ok ? 0 : 1;
}
