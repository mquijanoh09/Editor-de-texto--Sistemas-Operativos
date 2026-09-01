# Editor de Texto con Llamadas al Sistema (Linux)

Editor de texto escrito en **C** que trabaja directamente contra el kernel de Linux
usando **llamadas al sistema** (`open`, `read`, `write`, `lseek`, `ftruncate`, `close`),
sin pasar por la capa de `stdio.h` (`fopen`, `fprintf`, …).

El editor vive dentro de un shell educativo (`eafitOS`) que muestra en pantalla,
estilo `strace` simplificado y en color magenta, cada syscall que se ejecuta con
sus parámetros y su valor de retorno.

---

## Compilar y ejecutar

Requiere Linux con `gcc` y `make`.

```bash
make          # compila y produce el binario ./eafitOS
./eafitOS     # inicia el shell
make run      # atajo: compila y ejecuta
make clean    # borra los .o y el binario
```

Dentro del shell, para entrar al editor:

```
Wordsito> editor
editor>
```

---

## Comandos del editor

| Comando       | Qué hace                                                  | Syscalls involucradas          |
|---------------|-----------------------------------------------------------|--------------------------------|
| `o <archivo>` | Abre el archivo (lo crea si no existe) en lectura/escritura | `open(2)`, `close(2)`          |
| `p`           | Imprime el archivo completo, bloque a bloque de 512 bytes  | `lseek(2)`, `read(2)`, `write(2)` |
| `p <n>`       | Imprime solo la línea `n`, leyendo byte a byte             | `lseek(2)`, `read(2)`, `write(2)` |
| `a "<texto>"` | Agrega el texto como nueva línea al final del archivo      | `lseek(2)`, `write(2)`         |
| `d <n>`       | Elimina la línea `n` y recorta el archivo                  | `lseek(2)`, `read(2)`, `write(2)`, `ftruncate(2)` |
| `q`           | Cierra el archivo y vuelve al shell                        | `close(2)`                     |

**Importante:** el texto de `a` debe ir **entre comillas dobles** para que el
tokenizador `parse_line()` lo entregue como un solo argumento aunque contenga
espacios.

### Sesión de ejemplo

```
Wordsito> editor

--- Editor de Texto (sesión interactiva) ---
editor> o notas.txt
[syscall] open("notas.txt", O_RDWR|O_CREAT, 0644) ... = 3
Archivo 'notas.txt' abierto correctamente (fd=3).

editor> a "primera linea"
[syscall] lseek(3, 0, SEEK_END) ... = 0
[syscall] write(3, "primera linea", 13) ... = 13
[syscall] write(3, "\n", 1) ... = 1
Línea agregada al final de 'notas.txt'.

editor> p
[syscall] lseek(3, 0, SEEK_SET) ... = 0
[syscall] read(3, buffer, 512) ... = 14
primera linea

editor> d 1
[syscall] lseek(3, 0, SEEK_END) ... = 14
[syscall] read(3, buffer, 14) ... = 14
[syscall] write(3, buffer, 0) ... = 0
[syscall] ftruncate(3, 0) ... = 0
Línea 1 eliminada de 'notas.txt'.

editor> q
[syscall] close(3) ... = 0
```

---

## Cómo funciona por dentro

- **Sesión con estado.** A diferencia del resto de comandos del shell (atómicos:
  abren, operan y cierran), el editor mantiene un `struct EditorState` con el
  descriptor de archivo (`fd`) abierto entre un comando y el siguiente. Por eso
  cada operación empieza reposicionando el cursor con `lseek()`: el `fd` es
  compartido y pudo quedar en cualquier posición.
- **`O_RDWR | O_CREAT`, sin `O_TRUNC`.** El editor necesita leer y escribir sobre
  el mismo descriptor. Se omite `O_TRUNC` a propósito: truncar borraría el
  contenido al abrir.
- **Borrado de líneas.** `d <n>` trae el archivo completo a memoria con `malloc()`,
  "tapa el hueco" de la línea con `memmove()`, reescribe desde el byte 0 y usa
  `ftruncate()` para recortar el sobrante. Sin el `ftruncate()` quedarían bytes
  fantasma al final.
- **`write(1, ...)` en lugar de `printf`.** El contenido leído se vuelca crudo al
  descriptor 1 (stdout). Como `printf()` pasa por el buffer de stdio en espacio de
  usuario y `write()` va directo al kernel, se hace `fflush(stdout)` antes de cada
  `write()` para que las trazas y el contenido salgan en orden.

---

## Estructura del proyecto

```
Editor_de_Texto/
├── Makefile          # compilación del proyecto
├── shell.h           # macros de color, macros LOG_SYSCALL, struct Command, prototipos
├── main.c            # REPL principal, tabla de comandos, parse_line(), help
├── cat_editor.c      # EL EDITOR: comandos o, p, a, d, q
├── cat_datos.c       # d_create, d_read, d_info, d_copy, d_append
├── cat_memoria.c     # m_sbrk, m_mmap, m_info
├── cat_monitoreo.c   # p_fork, p_exec, p_kill, p_monitor
├── cat_listado.c     # l_dir
└── cat_util.c        # saludar, despedir, hora, fecha
```

---

## Correcciones aplicadas a la versión anterior

| Problema | Efecto | Corrección |
|---|---|---|
| `}if (strcmp(eargv[0], "a") ...)` en vez de `} else if` | La cadena `if/else` quedaba partida en dos: tras `p` se reevaluaban `a` y `d`, y los comandos inválidos se ignoraban en silencio | Se restauró `else if` y se agregó un `else` final con mensaje de error |
| `printf()` y `write(1, ...)` mezclados sin sincronizar | El contenido del archivo aparecía en pantalla **antes** que las trazas `[syscall]` | `fflush(stdout)` antes de cada `write(1, ...)` |
| `printf(... porfavor poner entre comillas "" el texto ...)` | Las dos comillas se concatenaban como cadena vacía y el mensaje salía incompleto | Mensaje de bienvenida reescrito con la lista de comandos |
| `d <n>` sobre la última línea sin `\n` final | Dejaba un salto de línea sobrante al final del archivo | Se incluye el `\n` de la línea anterior en el rango eliminado |
| `make clean` usaba `$(TARGET)`, variable inexistente (era `ARCHSALIDA`) | Nunca borraba el ejecutable | Variable unificada como `TARGET` |
| El objetivo `all` ejecutaba `./eafitOS` al compilar | `make` se quedaba colgado esperando entrada | Se separó en un objetivo `run` |
| `help` anunciaba comandos `i` y `s` del editor | Comandos inexistentes en la implementación | Descripción corregida a `o, p, a, d, q` |
| Salida del editor sin traza de `close()` | Se cerraba el `fd` en silencio | Se agregó `LOG_SYSCALL("close", ...)` al salir |
| `.o`, binarios, `.idea/`, `*Zone.Identifier`, `.txt` de prueba en el repo | Ruido y archivos generados versionados | Eliminados; `.gitignore` actualizado |
