# Proyecto juego

Dos juegos escritos en C desde cero, sin librerías ni motores: solo lo que trae Windows y el driver de la tarjeta gráfica.

## Mi Planta
Tamagotchi de una planta en pixel art (160×120). Riégala, dale sol y mímala para que crezca de brote a árbol mágico. Todo se dibuja por software, píxel a píxel; el árbol se genera por código.

Controles: **← →** elegir, **Espacio/Enter** hacer la acción, **Esc** salir.

## La Leyenda del Bosque
Aventura 3D estilo Zelda. Motor propio en OpenGL 3.3 con shaders escritos a mano: sombras, hierba con viento, agua, niebla y un héroe articulado con animación procedural. Derrota a los limos del valle para encontrar el tesoro.

Controles: **WASD** moverse, **J/Espacio** espada, **Q/E** o **← →** cámara, **Enter** empezar.

## Compilar
Necesitas `gcc` (MinGW-w64) en Windows. Desde la carpeta del proyecto:

```
build.bat
```

Se crean `build\planta.exe` y `build\aventura.exe`.

El código está comentado en español para poder aprender cómo funciona cada parte. Para empezar a leer: `src/game.h` y `src/main.c`.
