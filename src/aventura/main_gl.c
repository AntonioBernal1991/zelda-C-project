// =============================================================================
// main_gl.c  —  La plataforma de la aventura 3D: ventana + OpenGL + bucle
// =============================================================================
//
// Hace lo mismo que src/main.c (ventana, teclado, reloj y bucle del juego),
// pero en vez de copiar píxeles a la ventana, prepara OpenGL para que la
// tarjeta gráfica dibuje directamente en ella.
//
// Lo más raro de este archivo es crear el "contexto" de OpenGL (la conexión
// entre nuestra ventana y la tarjeta gráfica). Para pedir uno moderno
// (OpenGL 3.3) primero hay que crear uno antiguo provisional, porque la
// función que crea el moderno solo se puede pedir teniendo ya uno. Es un
// paso histórico de Windows; se hace así en todos los programas.
//
// Solo usa Windows y el OpenGL del driver, sin librerías externas.
// =============================================================================

#include "glload.h"
#include <GL/wglext.h>   // constantes para pedir el contexto moderno
#include <mmsystem.h>
#include <string.h>
#include "../game.h"
#include "gfx.h"

void game_render_gl(int width, int height); // la define aventura.c

static Input input;                     // estado del teclado
static int   running = 1;               // 0 = salir
static int   win_w = 1280, win_h = 720; // tamaño de la ventana (cambia si la redimensionas)

// Traduce las teclas de Windows a las del juego (igual que en src/main.c)
static int map_key(WPARAM vk) {
    switch (vk) {
        case VK_UP:     return KEY_UP;
        case VK_DOWN:   return KEY_DOWN;
        case VK_LEFT:   return KEY_LEFT;
        case VK_RIGHT:  return KEY_RIGHT;
        case 'W':       return KEY_W;
        case 'S':       return KEY_S;
        case 'A':       return KEY_A;
        case 'D':       return KEY_D;
        case 'Q':       return KEY_Q;
        case 'E':       return KEY_E;
        case 'J':       return KEY_J;
        case VK_SPACE:  return KEY_SPACE;
        case VK_RETURN: return KEY_ENTER;
        case VK_ESCAPE: return KEY_ESCAPE;
    }
    return -1;
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
    case WM_DESTROY:
        running = 0;
        return 0;
    case WM_SIZE:            // la ventana ha cambiado de tamaño: apuntamos el nuevo
        win_w = LOWORD(lp);
        win_h = HIWORD(lp);
        return 0;
    case WM_KEYDOWN: {
        int k = map_key(wp);
        if (k >= 0) {
            if (!input.down[k]) input.pressed[k] = 1;
            input.down[k] = 1;
        }
        return 0;
    }
    case WM_KEYUP: {
        int k = map_key(wp);
        if (k >= 0) input.down[k] = 0;
        return 0;
    }
    case WM_KILLFOCUS:
        memset(input.down, 0, sizeof input.down);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// Muestra un mensaje de error en una ventanita y termina.
static int fail(HWND hwnd, const char *msg) {
    MessageBoxA(hwnd, msg, game_title, MB_OK | MB_ICONERROR);
    return 1;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev; (void)cmd;
    SetProcessDPIAware();
    timeBeginPeriod(1);

    // Registrar y crear la ventana (igual que en src/main.c)
    WNDCLASSA wc = {0};
    wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = window_proc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "JuegoGLWindow";
    RegisterClassA(&wc);

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT wr = { 0, 0, win_w, win_h };
    AdjustWindowRect(&wr, style, FALSE);
    HWND hwnd = CreateWindowA(wc.lpszClassName, game_title, style, CW_USEDEFAULT, CW_USEDEFAULT,
                              wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, inst, NULL);
    if (!hwnd) return 1;
    HDC dc = GetDC(hwnd);

    // 1) Decirle a Windows cómo queremos los píxeles de la ventana: color de
    //    32 bits, profundidad de 24 bits y "doble buffer" (se dibuja en una
    //    imagen oculta y al terminar se intercambia con la visible, así no
    //    se ve cómo se va pintando). Con eso, un contexto provisional...
    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof pfd);
    pfd.nSize      = sizeof pfd;
    pfd.nVersion   = 1;
    pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    HGLRC tmp = wglCreateContext(dc);
    wglMakeCurrent(dc, tmp);

    // 2) ...que sirve para pedir un contexto moderno (OpenGL 3.3 "core",
    //    es decir, solo la forma moderna de trabajar, sin lo antiguo)
    PFNWGLCREATECONTEXTATTRIBSARBPROC create_ctx =
        (PFNWGLCREATECONTEXTATTRIBSARBPROC)(void (*)(void))wglGetProcAddress("wglCreateContextAttribsARB");
    int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };
    HGLRC rc = create_ctx ? create_ctx(dc, 0, attribs) : 0;
    if (!rc) return fail(hwnd, "La tarjeta grafica no soporta OpenGL 3.3.");
    wglMakeCurrent(dc, rc);
    wglDeleteContext(tmp);
    // 3) Ahora que hay contexto, pedimos al driver todas las funciones modernas
    if (!gl_load()) return fail(hwnd, "No se pudieron cargar las funciones de OpenGL.");

    // Sincronía vertical (vsync): esperar al refresco del monitor para
    // enseñar cada imagen. Evita "cortes" en la imagen y limita a 60 fps.
    PFNWGLSWAPINTERVALEXTPROC swap_interval = (PFNWGLSWAPINTERVALEXTPROC)(void (*)(void))wglGetProcAddress("wglSwapIntervalEXT");
    if (swap_interval) swap_interval(1);

    ShowWindow(hwnd, show);
    game_init();                 // aquí se compilan los shaders y se sube todo a la tarjeta
    if (gfx_error()) return fail(hwnd, gfx_error());

    LARGE_INTEGER freq, last, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);

    // El bucle del juego
    while (running) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = 0;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - last.QuadPart) / freq.QuadPart);
        last = now;
        if (dt > 0.05f) dt = 0.05f; // tiempo desde la imagen anterior (máximo 0,05 s)

        if (!game_update(&input, dt)) running = 0;
        memset(input.pressed, 0, sizeof input.pressed);

        if (win_w > 0 && win_h > 0) {
            game_render_gl(win_w, win_h); // la tarjeta gráfica dibuja en la imagen oculta...
            SwapBuffers(dc);              // ...y la intercambiamos con la visible
        } else {
            Sleep(10); // minimizada
        }
    }

    timeEndPeriod(1);
    return 0;
}
