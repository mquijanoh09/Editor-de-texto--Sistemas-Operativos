#include "shell.h"
#include <unistd.h>   /* close(), read(), write(), lseek() */
#include <fcntl.h>    /* open(), O_RDWR, O_CREAT */
#include <errno.h>    /* errno */
#include <string.h>   /* strncpy(), strerror() */

/**
 * ====================================================================================
 * CATEGORÍA: editor
 * COMANDO DE ENTRADA: editor
 * ====================================================================================
 * A diferencia de todos los demás comandos del shell (que son atómicos: hacen
 * una cosa y devuelven el control de inmediato), este comando ABRE UNA SESIÓN
 * INTERACTIVA PROPIA -un segundo bucle "REPL" anidado dentro del REPL principal
 * de eafitOS. Mientras estés dentro, el prompt cambia a "editor>", y solo
 * regresas al prompt normal "eafitOS>" escribiendo 'q'.
 */

/* Estado que el editor debe "recordar" mientras dura la sesión.
   A diferencia de los comandos de 'datos', aquí el archivo permanece
   abierto entre un comando interno y el siguiente. */
typedef struct {
    int fd;                /* -1 = no hay ningún archivo abierto todavía */
    char filename[256];   //Guardamos el nombre de un archivo de MAX 256 caracteres
} EditorState;

int cmd_editor(int argc, char **argv) {//firma de las función editor
    (void)argc; /* de momento, el comando de entrada no usa argumentos */
    (void)argv;

    EditorState state;
    state.fd = -1;
    state.filename[0] = '\0';

    printf(COLOR_TITLE "\n--- Editor de Texto (sesión interactiva) ---\n" COLOR_RESET);
    printf("Comandos disponibles dentro del editor:\n");
    printf("  " COLOR_PROMPT "o <archivo>" COLOR_RESET "   Abre (o crea) un archivo para lectura y escritura.\n");
    printf("  " COLOR_PROMPT "p [n]" COLOR_RESET "         Imprime todo el archivo, o solo la linea n.\n");
    printf("  " COLOR_PROMPT "a \"<texto>\"" COLOR_RESET "   Agrega el texto como nueva linea al final.\n");
    printf("  " COLOR_PROMPT "d <n>" COLOR_RESET "         Elimina la linea n.\n");
    printf("  " COLOR_PROMPT "q" COLOR_RESET "             Cierra el archivo y vuelve al shell.\n\n");
    /* El texto de 'a' debe ir entre comillas dobles para que el tokenizador
       parse_line() lo entregue como UN solo argumento aunque tenga espacios. */
    printf(COLOR_INFO "Recuerda: el texto de 'a' va entre comillas dobles. Ej: a \"hola mundo\"\n\n" COLOR_RESET);

    char line[256]; //desde aqwuí inicializamos el while para que editor se quede en un
                    //bucle infinito y solo salga cuando lea q
    char *eargv[16];

    while (1) {
        printf(COLOR_PROMPT "editor> " COLOR_RESET);
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            break; /* Ctrl+D dentro del editor también termina la sesión */
        }

        int eargc = parse_line(line, eargv);
        if (eargc == 0) continue;

              if (strcmp(eargv[0], "q") == 0) {
            printf(COLOR_INFO "Saliendo del editor...\n" COLOR_RESET);
            break;
        } else if (strcmp(eargv[0], "o") == 0) {
            /* ================================================================
             * COMANDO: o <archivo>
             * Abre (o crea, si no existe) un archivo para lectura Y escritura
             * al mismo tiempo -por eso O_RDWR, distinto a O_WRONLY (solo
             * escritura, usado en d_create) o O_RDONLY (solo lectura, usado
             * en d_read). El editor necesita ambas cosas: leer para mostrar
             * contenido (p), escribir para modificarlo (a, d, i).
             * ================================================================ */
            if (eargc < 2) { //es eargc <2 debido a que si el usuario escribió algo aparte más de o eargc[]>2
                printf(COLOR_ERROR "Uso: o <archivo>\n" COLOR_RESET);
                continue;
            }

            /* Si ya había un archivo abierto de una sesión 'o' anterior,
               lo cerramos primero -evita fugas de file descriptors. */
            if (state.fd != -1) {//recordar que si state.fd= -1 -> que no hay nada abierto.
                LOG_SYSCALL("close", "%d", state.fd);
                int cres = close(state.fd);
                LOG_SYSCALL_RESULT(cres);
                printf(COLOR_INFO "Archivo anterior '%s' cerrado.\n" COLOR_RESET, state.filename);
            }

            LOG_SYSCALL("open", "\"%s\", O_RDWR|O_CREAT, 0644", eargv[1]);// nos dice O_RDWR que si existe lo habra y lo edite | (ó) O_CREAT no dice que lo cree
            //la diferencia que tenemos con el O_TRUNC de d_create es que O_TRUNC nos dice que borre el contenido del archivo si este ya
            int fd = open(eargv[1], O_RDWR | O_CREAT, 0644);
            if (fd == -1) {
                LOG_SYSCALL_ERROR(strerror(errno));
                continue;
            }
            LOG_SYSCALL_RESULT(fd);

            state.fd = fd; //guarda el estado de state.fd para que los nuevos comandos que se  vayan a hcaer sepan en que archivo se están haciendo
            strncpy(state.filename, eargv[1], sizeof(state.filename) - 1);//el usuario le puede de poner como máximo al nombre del archivo 255 bits. El bit 256 el /0
            state.filename[sizeof(state.filename) - 1] = '\0';

            printf(COLOR_RESULT "Archivo '%s' abierto correctamente (fd=%d).\n" COLOR_RESET,
                   state.filename, state.fd);
        } else if (strcmp(eargv[0], "p") == 0) {
            /* ================================================================
             * COMANDO: p [n]
             * Sin argumento: imprime todo el archivo tal cual está en disco.
             * Con argumento numérico: imprime solo la línea n (contando desde 1).
             *
             * Usamos lseek() para reposicionar el cursor de lectura al inicio
             * del archivo -el fd es compartido entre comandos, y pudo haber
             * quedado en otra posición por una operación anterior (por
             * ejemplo, si 'a' ya escribió algo al final).
             *
             * Para mostrar el contenido en consola usamos write(1, ...) en
             * vez de printf, siguiendo la recomendación del enunciado:
             * escribimos los bytes crudos leídos del disco directamente.
             * ================================================================ */
            if (state.fd == -1) {
                printf(COLOR_ERROR "No hay ningún archivo abierto. Usa 'o <archivo>' primero.\n" COLOR_RESET);
                continue;
            }

            int target_line = 0; /* 0 = imprimir el archivo completo */
            if (eargc >= 2) { //Aquí revisa si el esccribió mas aparte del comando  p, para así revisar si empezará a leer desde el byte 0 el escriyo
                target_line = atoi(eargv[1]);// el atoi convierte el string del # que significa el numero de línea al quue queremos acceder.
                if (target_line <= 0) {
                    printf(COLOR_ERROR "El número de línea debe ser un entero positivo.\n" COLOR_RESET);
                    continue;
                }
            }

            LOG_SYSCALL("lseek", "%d, 0, SEEK_SET", state.fd); //aquí hacemos la call system de lseek
            off_t off = lseek(state.fd, 0, SEEK_SET);
            if (off == (off_t)-1) {
                LOG_SYSCALL_ERROR(strerror(errno));
                continue;
            }
            LOG_SYSCALL_RESULT((int)off);

            if (target_line == 0) {
                /* Caso 1: todo el archivo -volcado bloque a bloque */
                //************esta parte es muy copie y pegue del while que está en cdm_d_copy.
                char buffer[512]; //variable que nos sirve para "reservar" espacio de 512 en la memoria y así "procesar" el  archcivo por partes
                ssize_t bytes_read;//variable para almacenar cuantos bytes leyó del archivo
                while ((bytes_read = read(state.fd, buffer, sizeof(buffer))) > 0) {
                //la condición de este while lo que hace es traer los bytes que vamos a leer del archivo(state.fd) y los almacena en buffer.Luego los
                //pasa a bytes_read para poder comparar con cer y así saber cuando ya se halla leído todo lo del archivo.
                    LOG_SYSCALL("read", "%d, buffer, %zu", state.fd, sizeof(buffer));
                    LOG_SYSCALL_RESULT(bytes_read);//me imprime el valor total de bytes leídos en pantaalla
                    /* printf() escribe en el buffer de stdio (espacio de usuario) y
                       write() va directo al kernel. Sin este fflush(), los dos flujos
                       salen desordenados en pantalla: el contenido del archivo
                       aparecía ANTES de las trazas [syscall]. */
                    fflush(stdout);
                    write(1, buffer, bytes_read);
                    //el 1 le dice al kernell que está bien el proceso, vamos a imprimir lo que hay en el buffer pero donde ponemos la
                    //variable de cantidad de bytes que mostraremos lo que nhacemos es poner bytes_read porqué el buffer no siempre estará
                    // lleno,lo que implica que en archivos menores a 512 bytes sacaría error.
                }
                if (bytes_read == -1) {
                    LOG_SYSCALL_ERROR(strerror(errno));
                }
                printf("\n");
            } else {
                /* Caso 2: una línea específica -leemos byte a byte contando
                   saltos de línea ('\n'). No se hace LOG_SYSCALL por cada
                   byte para no inundar la pantalla; solo se explica una vez. */
                printf(COLOR_INFO "(leyendo byte a byte hasta encontrar la línea %d...)\n" COLOR_RESET, target_line);
                fflush(stdout); /* mismo motivo que arriba: sincronizar stdio con write() */
                char c;
                int current_line = 1;
                int line_found = 0;
                ssize_t n;
                while ((n = read(state.fd, &c, 1)) > 0) { //el read(state.fd,&c,1) hace que se lea de 1 bytdel archivo y el &c hace leerlos como char
                    if (current_line == target_line) {
                        write(1, &c, 1);
                        line_found = 1;
                    }
                    if (c == '\n') {
                        current_line++;
                        if (current_line > target_line) break;
                    }
                }
                if (n == -1) {
                    LOG_SYSCALL_ERROR(strerror(errno));
                } else if (!line_found) {
                    printf(COLOR_ERROR "La línea %d no existe en el archivo.\n" COLOR_RESET, target_line);
                } else {
                    printf("\n");
                }
            }
        } else if (strcmp(eargv[0], "a") == 0) {
            /* BUG CORREGIDO: antes aquí decía "}if (...)" en vez de "} else if (...)",
               lo que partía la cadena en dos cadenas independientes. El efecto era
               que tras ejecutar 'p' el programa volvía a evaluar 'a' y 'd', y que
               un comando inválido no producía ningún mensaje de error. */
            /* ================================================================
             * COMANDO: a <texto>
             * Agrega el texto como una NUEVA LÍNEA al final del archivo.
             *
             * A diferencia de d_append (que abría el archivo con la bandera
             * O_APPEND para que el kernel posicionara el cursor automática-
             * mente), aquí 'o' abrió el archivo sin esa bandera -porque el
             * mismo fd también se usa para leer desde el principio (p).
             * Por eso, movemos el cursor MANUALMENTE al final con lseek()
             * antes de escribir.
             * ================================================================ */
            if (state.fd == -1) {
                printf(COLOR_ERROR "No hay ningún archivo abierto. Usa 'o <archivo>' primero.\n" COLOR_RESET);
                continue;
            }
            if (eargc < 2) {
                printf(COLOR_ERROR "Uso: a \"<texto>\"\n" COLOR_RESET);
                continue;
            }

            LOG_SYSCALL("lseek", "%d, 0, SEEK_END", state.fd);  //SEEK_END -> Desde el final del archivo -> lseek(fd, 0, SEEK_END) → cursor va justo después del último byte
            off_t end_pos = lseek(state.fd, 0, SEEK_END);
            if (end_pos == (off_t)-1) {
                LOG_SYSCALL_ERROR(strerror(errno));
                continue;
            }
            LOG_SYSCALL_RESULT((int)end_pos);

            const char *text = eargv[1];
            size_t len = strlen(text);

            LOG_SYSCALL("write", "%d, \"%s\", %zu", state.fd, text, len);
            ssize_t written = write(state.fd, text, len);
            if (written == -1) {
                LOG_SYSCALL_ERROR(strerror(errno));
                continue;
            }
            LOG_SYSCALL_RESULT(written);

            /* La línea nueva necesita su propio '\n' al final -si no lo
               agregamos, el siguiente 'a' quedaría pegado en la misma línea. */
            LOG_SYSCALL("write", "%d, \"\\n\", 1", state.fd);
            ssize_t nl_written = write(state.fd, "\n", 1);
            LOG_SYSCALL_RESULT(nl_written);

            printf(COLOR_RESULT "Línea agregada al final de '%s'.\n" COLOR_RESET, state.filename);
        } else if (strcmp(eargv[0], "d") == 0) {
            /* ================================================================
             * COMANDO: d <n>
             * Borra la línea n, desplazando los bytes posteriores y
             * truncando el archivo a su nuevo tamaño más corto.
             *
             * A diferencia de p y a, aquí no basta con leer/escribir en el
             * punto exacto -hay que RECONSTRUIR el archivo completo sin esa
             * línea. Estrategia (recomendada por el enunciado): traer todo
             * el archivo a un buffer dinámico (malloc), eliminar en memoria
             * los bytes de la línea buscada, reescribir el archivo entero,
             * y usar ftruncate() para recortar el sobrante.
             * ================================================================ */
            if (state.fd == -1) {
                printf(COLOR_ERROR "No hay ningún archivo abierto. Usa 'o <archivo>' primero.\n" COLOR_RESET);
                continue;
            }
            if (eargc < 2) {
                printf(COLOR_ERROR "Uso: d <n>\n" COLOR_RESET);
                continue;
            }
            int target_line = atoi(eargv[1]);//atoi convierte el string del # a un numero
            if (target_line <= 0) {
                printf(COLOR_ERROR "El número de línea debe ser un entero positivo.\n" COLOR_RESET);
                continue;
            }

            /* 1. Tamaño total del archivo (lseek al final) */
            LOG_SYSCALL("lseek", "%d, 0, SEEK_END", state.fd);
            /*Aquí lseek no se usa para "preparar" una lectura o escritura — se usa como truco para averiguar cuántos
            bytes tiene el archivo. lseek(fd, 0, SEEK_END) mueve el cursor al final (desplazamiento 0 desde el final),
            y su valor de retorno es justo la posición donde quedó — que, al ser el final, es exactamente el tamaño del
            archivo en bytes.
            */
            off_t total_size = lseek(state.fd, 0, SEEK_END);
            if (total_size == (off_t)-1) {
                LOG_SYSCALL_ERROR(strerror(errno));
                continue;
            }//no entiendo que hace este if
            LOG_SYSCALL_RESULT((int)total_size);

            if (total_size == 0) {
                printf(COLOR_ERROR "El archivo está vacío.\n" COLOR_RESET);
                continue;
            }

            /* 2. Volver al inicio y traer TODO el archivo a memoria */
            LOG_SYSCALL("lseek", "%d, 0, SEEK_SET", state.fd);
            lseek(state.fd, 0, SEEK_SET);
            LOG_SYSCALL_RESULT(0);

            char *buffer = malloc(total_size); //aquí con malloc pedimos un bloque de memoria dinamica en el heap del tamaño exacto del archivo
            if (buffer == NULL) {// buena practica de captar el error
                printf(COLOR_ERROR "No se pudo reservar memoria para leer el archivo.\n" COLOR_RESET);
                continue;
            }

            LOG_SYSCALL("read", "%d, buffer, %ld", state.fd, (long)total_size);
            ssize_t total_read = read(state.fd, buffer, total_size);
            LOG_SYSCALL_RESULT(total_read);
            if (total_read != total_size) {
                LOG_SYSCALL_ERROR(strerror(errno));
                free(buffer);
                continue;
            }

            /* 3. Encontrar en memoria dónde empieza y termina la línea n

            line_start = 0 → asumimos que la línea 1 empieza en la posición 0 del buffer.
            El for recorre todo el contenido ya cargado en memoria, byte por byte.
            Cada vez que encuentra un '\n', significa "aquí termina una línea". Si esa línea que acaba de terminar es la que buscamos (current_line == target_line), guardamos sus límites: found_start (dónde empezó) y found_end (justo después del '\n', para incluirlo también en lo que vamos a borrar).
            Si no era la línea buscada, avanzamos: current_line++ (pasamos a contar la siguiente línea), y line_start = i + 1 (la siguiente línea empieza justo después de este '\n').
            break; → en cuanto encontramos lo que buscábamos, dejamos de recorrer el resto del archivo (no hace falta seguir).

             */
            long line_start = 0;
            long found_start = -1, found_end = -1;
            int current_line = 1;
            for (long i = 0; i < total_read; i++) {
                if (buffer[i] == '\n') {
                    if (current_line == target_line) {
                        found_start = line_start;
                        found_end = i + 1; /* incluye el '\n' */
                        break;
                    }
                    current_line++;
                    line_start = i + 1;
                }
            }
            /* Caso especial: la última línea no termina en '\n' (por ejemplo, la última línea del archivo no tiene
            salto de línea al final), el bucle anterior nunca la "cerraría" con un '\n' — este bloque atrapa ese caso, tratando el
            final del archivo (total_read) como el límite de esa última línea.*/
            if (found_start == -1 && current_line == target_line && line_start < total_read) {
                found_start = line_start;
                found_end = total_read;
                /* Al borrar la ÚLTIMA línea (que no trae '\n' propio), hay que
                   llevarse también el '\n' de la línea anterior; si no, el
                   archivo queda terminado en un salto de línea sobrante. */
                if (found_start > 0) {
                    found_start--;
                }
            }



            if (found_start == -1) {
                printf(COLOR_ERROR "La línea %d no existe en el archivo.\n" COLOR_RESET, target_line);
                free(buffer);
                continue;
            }

            /* 4. "Tapar el hueco" en memoria: desplazar los bytes posteriores
               hacia atrás, sobrescribiendo la línea eliminada. */
            long removed_len = found_end - found_start;
            long new_size = total_read - removed_len;
            memmove(buffer + found_start, buffer + found_end, total_read - found_end);
            /* memmove(buffer + found_start, buffer + found_end, total_read - found_end) → esta es la línea clave de
            todo el comando. Copia los bytes que estaban después de la línea eliminada (desde buffer + found_end hasta el final)
            hacia la posición donde empezaba la línea eliminada (buffer + found_start) — literalmente "corriendo" todo el contenido
            posterior hacia atrás, tapando el espacio que dejó la línea borrada.*/

            /* 5. Reescribir el archivo completo con el contenido reducido */
            LOG_SYSCALL("lseek", "%d, 0, SEEK_SET", state.fd);
            lseek(state.fd, 0, SEEK_SET);
            LOG_SYSCALL_RESULT(0);

            LOG_SYSCALL("write", "%d, buffer, %ld", state.fd, new_size);
            ssize_t written = write(state.fd, buffer, new_size);
            LOG_SYSCALL_RESULT(written);

            /* 6. Truncar: el archivo viejo era más largo -sin esto, quedarían
               bytes "fantasma" de la línea borrada al final del archivo. */
            LOG_SYSCALL("ftruncate", "%d, %ld", state.fd, new_size);
            int tres = ftruncate(state.fd, new_size);
            LOG_SYSCALL_RESULT(tres);

            free(buffer);
            printf(COLOR_RESULT "Línea %d eliminada de '%s'.\n" COLOR_RESET, target_line, state.filename);

        } else {
            /* Cualquier palabra que no sea o/p/a/d/q cae aquí. Antes, por la
               cadena if/else rota, estos casos se ignoraban en silencio. */
            printf(COLOR_ERROR "Comando '%s' no reconocido dentro del editor.\n" COLOR_RESET, eargv[0]);
            printf(COLOR_INFO "Comandos válidos: o <archivo> | p [n] | a \"<texto>\" | d <n> | q\n" COLOR_RESET);
        }
    }

    /* Si el usuario sale sin haber cerrado explícitamente el archivo,
       lo cerramos aquí para no dejar el file descriptor abierto (fuga de recursos). */
    if (state.fd != -1) {
        LOG_SYSCALL("close", "%d", state.fd);
        int cres = close(state.fd);
        LOG_SYSCALL_RESULT(cres);
        printf(COLOR_INFO "Archivo '%s' cerrado al salir del editor.\n" COLOR_RESET, state.filename);
    }

    return 0;
}