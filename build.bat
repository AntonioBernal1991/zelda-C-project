@echo off
rem =============================================================================
rem build.bat  -  Compila los dos juegos. Ejecutalo con doble clic o desde la
rem terminal escribiendo:  build.bat
rem
rem Usa el compilador gcc. Opciones que veras:
rem   -std=c99        version del lenguaje C
rem   -O2             optimizar (el juego va mas rapido)
rem   -Wall -Wextra   avisar de posibles errores
rem   -DNOMBRE=valor  define una constante, como si estuviera en el codigo
rem   -o archivo.exe  nombre del programa que se crea
rem   -mwindows       programa de ventana (sin consola negra)
rem   -lnombre        usar una libreria de Windows (gdi32, user32, winmm, opengl32)
rem =============================================================================

if not exist build mkdir build
set LIBS=-mwindows -lgdi32 -luser32 -lwinmm
set FLAGS=-std=c99 -O2 -Wall -Wextra

rem --- Mi Planta: dibujo por software a 160x120 ---
gcc %FLAGS% -DSCREEN_W=160 -DSCREEN_H=120 src\main.c src\render.c src\game.c src\tree.c -o build\planta.exe %LIBS%
if %errorlevel% neq 0 exit /b %errorlevel%

rem --- La Leyenda del Bosque: 3D con OpenGL; la interfaz es de 320x180 ---
set AV=src\aventura
gcc %FLAGS% -DSCREEN_W=320 -DSCREEN_H=180 %AV%\main_gl.c %AV%\glload.c %AV%\gfx.c %AV%\mesh.c %AV%\world.c %AV%\aventura.c src\render.c -o build\aventura.exe -lopengl32 %LIBS%
if %errorlevel% neq 0 exit /b %errorlevel%

echo Compilado: build\planta.exe y build\aventura.exe
