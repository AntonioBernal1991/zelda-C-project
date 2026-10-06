// =============================================================================
// main.c  —  La plataforma: ventana, teclado, reloj y pantalla (Windows)
// =============================================================================
//
// Este archivo es el único que habla con Windows. Su trabajo es:
//
//   1. Abrir una ventana.
//   2. Escuchar el teclado y apuntar qué teclas están pulsadas.
//   3. Repetir sin parar el "bucle del juego":
//        - preguntar cuánto tiempo ha pasado,
//        - pedir al juego que avance (game_update),
//        - pedir al juego que dibuje (game_render),
//        - copiar el dibujo a la ventana.
//   4. Esperar un poco para ir a 60 imágenes por segundo.
//
// Solo usa librerías que ya trae Windows: user32 (ventanas), gdi32 (dibujar
// en la ventana) y winmm (reloj preciso). Lo usa el juego de la planta; la
// aventura 3D tiene su propia versión con OpenGL (aventura/main_gl.c).
// =============================================================================

#define WIN32_LEAN_AND_MEAN // le dice a windows.h que no cargue partes que no usamos
#include <windows.h>
#include <mmsystem.h>       // timeBeginPeriod: reloj de alta precisión
#include <string.h>         // memset: rellenar memoria con ceros
#include "game.h"

#define FB_W   SCREEN_W            // ancho del lienzo del juego
#define FB_H   SCREEN_H            // alto del lienzo del juego
#define SCALE  (720 / FB_H)        // cuántas veces se agranda: ventana inicial de unos 720 px de alto
#define TARGET_FPS 60              // imágenes por segundo que queremos

// "static" fuera de una función = variable que solo existe en este archivo.
static uint32_t    pixels[FB_W * FB_H];      // la memoria del lienzo
static Framebuffer fb = { pixels, FB_W, FB_H };
static BITMAPINFO  bmi;                      // le explica a Windows el formato de nuestros píxeles
static Input       input;                    // qué teclas están pulsadas
static int         running = 1;              // cuando pase a 0, el programa termina

// Traduce un código de tecla de Windows a una tecla del juego (-1 si no nos interesa).
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

// Windows llama a esta función cada vez que le pasa algo a nuestra ventana
// (se pulsa una tecla, se cierra, pierde el foco...). A esto se le llama
// "mensaje". Lo que no tratamos se lo devolvemos a Windows (DefWindowProc).
static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:   // han pulsado la X de la ventana
    case WM_DESTROY:
        running = 0;
        return 0;
    case WM_KEYDOWN: {   // se ha apretado una tecla (se repite si se mantiene)
        int k = map_key(wp);
        if (k >= 0) {
            // "pressed" solo se marca la primera vez, no en las repeticiones
            if (!input.down[k]) input.pressed[k] = 1;
            input.down[k] = 1;
        }
        return 0;
    }
    case WM_KEYUP: {     // se ha soltado una tecla
        int k = map_key(wp);
        if (k >= 0) input.down[k] = 0;
        return 0;
    }
    case WM_KILLFOCUS:   // la ventana deja de estar activa: soltamos todas las teclas
        memset(input.down, 0, sizeof input.down);
        return 0;
    case WM_PAINT: {     // Windows pide repintar; lo hacemos nosotros en el bucle
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; // no dejamos que Windows borre el fondo: evitaría parpadeos
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// Copia el lienzo a la ventana, agrandado.
// Se agranda por un número ENTERO (x2, x3, x4...) para que todos los píxeles
// tengan el mismo tamaño y el pixel art se vea nítido. Lo que sobra se
// rellena con bandas negras.
static void present(HWND hwnd, HDC dc) {
    RECT r;
    GetClientRect(hwnd, &r);           // tamaño de la zona útil de la ventana
    int cw = r.right, ch = r.bottom;
    if (cw <= 0 || ch <= 0) return;    // ventana minimizada

    // El mayor factor entero que cabe a lo ancho y a lo alto
    int k = cw / FB_W;
    if (ch / FB_H < k) k = ch / FB_H;
    if (k < 1) k = 1;
    int dw = FB_W * k, dh = FB_H * k;              // tamaño final de la imagen
    int dx = (cw - dw) / 2, dy = (ch - dh) / 2;    // para centrarla

    // Bandas negras arriba, abajo, izquierda y derecha
    PatBlt(dc, 0, 0, cw, dy, BLACKNESS);
    PatBlt(dc, 0, dy + dh, cw, ch - dy - dh, BLACKNESS);
    PatBlt(dc, 0, dy, dx, dh, BLACKNESS);
    PatBlt(dc, dx + dw, dy, cw - dx - dw, dh, BLACKNESS);

    // StretchDIBits copia nuestros píxeles a la ventana estirándolos
    StretchDIBits(dc, dx, dy, dw, dh, 0, 0, FB_W, FB_H,
                  pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
}

// En los programas de ventana de Windows, la función principal se llama
// WinMain en vez de main.
int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev; (void)cmd;       // no los usamos; así el compilador no avisa
    SetProcessDPIAware();        // que Windows no reescale la ventana por su cuenta
    timeBeginPeriod(1);          // que Sleep() sea preciso al milisegundo

    // 1) Registrar el "tipo" de ventana y decir qué función atiende sus mensajes
    WNDCLASSA wc = {0};
    wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = window_proc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "JuegoWindow";
    RegisterClassA(&wc);

    // 2) Calcular el tamaño de ventana para que la zona útil mida lo que queremos
    //    (AdjustWindowRect suma la barra de título y los bordes)
    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT wr = { 0, 0, FB_W * SCALE, FB_H * SCALE };
    AdjustWindowRect(&wr, style, FALSE);

    // 3) Crear la ventana y mostrarla
    HWND hwnd = CreateWindowA(wc.lpszClassName, game_title, style,
                              CW_USEDEFAULT, CW_USEDEFAULT,
                              wr.right - wr.left, wr.bottom - wr.top,
                              NULL, NULL, inst, NULL);
    if (!hwnd) return 1;
    ShowWindow(hwnd, show);
    HDC dc = GetDC(hwnd); // "contexto de dibujo": lo que usa Windows para pintar en la ventana

    // 4) Describir nuestros píxeles a Windows: 32 bits por píxel, sin comprimir
    bmi.bmiHeader.biSize        = sizeof bmi.bmiHeader;
    bmi.bmiHeader.biWidth       = FB_W;
    bmi.bmiHeader.biHeight      = -FB_H; // negativo = la primera fila es la de arriba
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    // 5) Preparar el reloj. QueryPerformanceCounter cuenta "tics" muy pequeños;
    //    dividiendo por la frecuencia (tics por segundo) obtenemos segundos.
    LARGE_INTEGER freq, last, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    const double frame_time = 1.0 / TARGET_FPS; // segundos que debe durar cada imagen

    game_init();

    // 6) El bucle del juego: se repite hasta que running sea 0
    while (running) {
        // Atender todos los mensajes pendientes de Windows (teclas, cerrar...)
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = 0;
            TranslateMessage(&msg);
            DispatchMessageA(&msg); // esto acaba llamando a window_proc
        }

        // dt = cuánto tiempo ha pasado desde la imagen anterior, en segundos.
        // El juego multiplica las velocidades por dt para que todo vaya igual
        // de rápido en un ordenador lento que en uno rápido.
        QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - last.QuadPart) / freq.QuadPart);
        last = now;
        if (dt > 0.05f) dt = 0.05f; // si la ventana se congela (al arrastrarla), no damos un salto enorme

        if (!game_update(&input, dt)) running = 0;
        memset(input.pressed, 0, sizeof input.pressed); // "pressed" solo dura una imagen

        game_render(&fb);   // el juego dibuja en el lienzo...
        present(hwnd, dc);  // ...y nosotros lo enseñamos en la ventana

        // Esperar hasta completar el tiempo de una imagen (1/60 de segundo).
        // Dormimos de milisegundo en milisegundo y el último trocito lo
        // esperamos "en activo" para ser precisos.
        for (;;) {
            QueryPerformanceCounter(&now);
            double elapsed = (double)(now.QuadPart - last.QuadPart) / freq.QuadPart;
            if (elapsed >= frame_time) break;
            if (frame_time - elapsed > 0.002) Sleep(1);
        }
    }

    timeEndPeriod(1);
    return 0;
}
