#ifndef SYS_H
#define SYS_H
#include <stddef.h>
#include <stdint.h>

int sys_mkdir_p(const char* path);
int sys_file_exists(const char* path);
int64_t sys_mtime(const char* path);          /* segundos desde 1970, o 0 */
int sys_remove(const char* path);
/* Llama a cb(nombre, ctx) por cada archivo de la carpeta (no recursivo). */
void sys_list_dir(const char* path, void (*cb)(const char* name, void* ctx), void* ctx);
int sys_strcasecmp(const char* a, const char* b);
void sys_stem(const char* filename, char* out, size_t n);   /* "a/b/juego.gba" -> "juego" */
int sys_format_time(int64_t t, char* out, size_t n);        /* "dd/mm/aaaa  hh:mm" */

#endif
