#ifndef SAT_DEBUG_LOG_H
#define SAT_DEBUG_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Registro común para el launcher y el núcleo integrado. El archivo se crea junto al .exe
 * en Windows y en el directorio de trabajo en otros sistemas. */
void debug_log_init(void);
void debug_log_close(void);
void debug_log_info(const char* fmt, ...);
void debug_log_warn(const char* fmt, ...);
void debug_log_error(const char* fmt, ...);

#ifdef __cplusplus
}
#endif
#endif
