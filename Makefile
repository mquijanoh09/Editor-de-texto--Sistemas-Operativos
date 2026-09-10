# ====================================================================================
# Makefile del Editor de Texto (Wordsito) basado en llamadas al sistema
# ====================================================================================
# make          -> compila y produce el binario ./eafitOS (NO lo ejecuta)
# make run      -> compila y además lo ejecuta
# make clean    -> borra los .o y el binario
# ====================================================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -std=gnu99 -g -D_GNU_SOURCE
TARGET  = eafitOS

# Módulos del proyecto. cat_editor.c es el que implementa el editor interactivo.
SRCS = main.c cat_datos.c cat_memoria.c cat_monitoreo.c cat_util.c cat_listado.c cat_editor.c buffer.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Cada .o depende de shell.h: si cambia la cabecera, se recompila todo.
%.o: %.c shell.h buffer.h
	$(CC) $(CFLAGS) -c $< -o $@

# Antes 'all' ejecutaba el binario automáticamente, lo que rompía 'make' en
# entornos no interactivos. Ahora ejecutar es un objetivo aparte.
run: $(TARGET)
	./$(TARGET)

# Antes usaba $(TARGET), que no existía (la variable se llamaba ARCHSALIDA),
# así que 'make clean' nunca borraba el ejecutable.
clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all run clean
