#ifndef SAT_LAN_H
#define SAT_LAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAN_DEFAULT_PORT 5738
#define LAN_PASSWORD_MAX 63

typedef struct LanSession LanSession;

enum LanState {
    LAN_IDLE = 0,
    LAN_WAITING,
    LAN_CONNECTING,
    LAN_HANDSHAKE,
    LAN_CONNECTED,
    LAN_ERROR
};

LanSession* lan_create(void);
void lan_destroy(LanSession* session);
int lan_supported(void);

int lan_host_start(LanSession* session, unsigned short port,
                   const char* password, unsigned game_id, uint64_t rom_signature);
int lan_join_start(LanSession* session, const char* host_ipv4, unsigned short port,
                   const char* password, unsigned game_id, uint64_t rom_signature);
void lan_poll(LanSession* session);
void lan_disconnect(LanSession* session);

int lan_state(const LanSession* session);
int lan_player_slot(const LanSession* session); /* host = 0, joining player = 1 */
const char* lan_status(const LanSession* session);
const char* lan_local_ipv4(const LanSession* session);

void lan_set_local_input(LanSession* session, uint32_t keys);
/* Bloquea hasta intercambiar el estado de entrada del fotograma indicado.
 * Debe llamarse desde un único hilo por sesión y en ambos extremos. */
int lan_exchange_input(LanSession* session, uint32_t frame,
                       uint32_t* local_keys, uint32_t* remote_keys);

#ifdef __cplusplus
}
#endif
#endif
