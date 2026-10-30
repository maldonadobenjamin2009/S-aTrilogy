/* LAN rooms and deterministic frame-input exchange for Sonic Advance Trilogy.
 * The room password is authenticated with a nonce-based HMAC-SHA1 challenge;
 * the password itself is never written to the socket. */
#include "lan.h"

#include <mgba-util/sha1.h>
#include <mgba-util/threading.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#if !defined(__EMSCRIPTEN__) && !defined(__ORBIS__) && !defined(PS4)
# define SAT_LAN_NATIVE 1
# if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  include <bcrypt.h>
   typedef SOCKET LanSocket;
#  define LAN_INVALID_SOCKET INVALID_SOCKET
# else
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <fcntl.h>
#  include <unistd.h>
#  include <errno.h>
   typedef int LanSocket;
#  define LAN_INVALID_SOCKET (-1)
# endif
#else
# define SAT_LAN_NATIVE 0
#endif

#define LAN_PACKET_SIZE 88
#define LAN_PAYLOAD_SIZE 64
#define LAN_PROTOCOL_VERSION 1
#define LAN_MSG_HELLO 1
#define LAN_MSG_AUTH 2
#define LAN_MSG_ACCEPT 3
#define LAN_MSG_REJECT 4
#define LAN_MSG_INPUT 5

#define LAN_ERR_CONFIG 1
#define LAN_ERR_PORT 2
#define LAN_ERR_CONNECT 3
#define LAN_ERR_NO_RANDOM 4
#define LAN_ERR_PASSWORD 5
#define LAN_ERR_ROM 6
#define LAN_ERR_DISCONNECTED 7
#define LAN_ERR_TIMEOUT 8
#define LAN_ERR_PROTOCOL 9

struct LanSession {
    atomic_int state;
    atomic_int error_code;
    int is_host;
    int player_slot;
    int wsa_started;
    int mutex_ready;
    Mutex input_mutex;
    uint32_t local_input;
    unsigned game_id;
    uint64_t rom_signature;
    char password[LAN_PASSWORD_MAX + 1];
    char local_ip[48];
#if SAT_LAN_NATIVE
    LanSocket listener;
    LanSocket socket_fd;
    uint8_t server_nonce[32];
    uint8_t client_nonce[16];
    uint8_t session_key[20];
    uint8_t rx[LAN_PACKET_SIZE * 2];
    size_t rx_len;
    uint8_t tx[LAN_PACKET_SIZE];
    size_t tx_len;
    size_t tx_offset;
    int become_connected;
#endif
};

static void store_state(LanSession* s, int state) {
    if (s) atomic_store_explicit(&s->state, state, memory_order_release);
}

int lan_state(const LanSession* s) {
    return s ? atomic_load_explicit((atomic_int*) &s->state, memory_order_acquire) : LAN_IDLE;
}

static int lan_error(const LanSession* s) {
    return s ? atomic_load_explicit((atomic_int*) &s->error_code, memory_order_acquire) : 0;
}

int lan_player_slot(const LanSession* s) {
    return s ? s->player_slot : 0;
}

const char* lan_status(const LanSession* s) {
    switch (lan_state(s)) {
    case LAN_WAITING: return "Sala abierta: esperando a otro jugador";
    case LAN_CONNECTING: return "Conectando con la sala...";
    case LAN_HANDSHAKE: return "Verificando contraseña y juego...";
    case LAN_CONNECTED: return "Conexión LAN establecida";
    case LAN_ERROR:
        switch (lan_error(s)) {
        case LAN_ERR_CONFIG: return "Revisá la IP, el puerto y la contraseña";
        case LAN_ERR_PORT: return "No se pudo abrir el puerto. Revisá firewall o si ya está ocupado";
        case LAN_ERR_CONNECT: return "No se pudo conectar. Comprobá IP, puerto y aislamiento Wi-Fi";
        case LAN_ERR_NO_RANDOM: return "No se pudo generar el desafío seguro de la sala";
        case LAN_ERR_PASSWORD: return "Contraseña incorrecta";
        case LAN_ERR_ROM: return "El otro jugador usa un juego o una ROM diferente";
        case LAN_ERR_TIMEOUT: return "Se agotó el tiempo de espera de la conexión";
        case LAN_ERR_PROTOCOL: return "La versión del protocolo LAN no coincide";
        default: return "Se interrumpió la conexión LAN";
        }
    default: return "Sin conexión LAN";
    }
}

const char* lan_local_ipv4(const LanSession* s) {
    return s && s->local_ip[0] ? s->local_ip : "No disponible";
}

int lan_supported(void) {
    return SAT_LAN_NATIVE;
}

static void reset_sockets(LanSession* s);

static void set_error(LanSession* s, int code) {
    if (!s) return;
#if SAT_LAN_NATIVE
    reset_sockets(s);
#endif
    atomic_store_explicit(&s->error_code, code, memory_order_release);
    store_state(s, LAN_ERROR);
}

#if SAT_LAN_NATIVE
# ifndef MSG_NOSIGNAL
#  define MSG_NOSIGNAL 0
# endif
# ifdef _WIN32
static int socket_failed(LanSocket fd) { return fd == INVALID_SOCKET; }
static int would_block(void) {
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY;
}
static void close_socket(LanSocket* fd) {
    if (*fd == LAN_INVALID_SOCKET) return;
    shutdown(*fd, SD_BOTH);
    closesocket(*fd);
    *fd = LAN_INVALID_SOCKET;
}
static int set_nonblocking(LanSocket fd, int enabled) {
    u_long mode = enabled ? 1UL : 0UL;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
}
# else
static int socket_failed(LanSocket fd) { return fd < 0; }
static int would_block(void) { return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS || errno == EALREADY; }
static void close_socket(LanSocket* fd) {
    if (*fd < 0) return;
    shutdown(*fd, SHUT_RDWR);
    close(*fd);
    *fd = -1;
}
static int set_nonblocking(LanSocket fd, int enabled) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return 0;
    if (enabled) flags |= O_NONBLOCK;
    else flags &= ~O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags) == 0;
}
# endif

static int socket_readable(LanSocket fd) {
    fd_set read_set;
    struct timeval tv = {0, 0};
    FD_ZERO(&read_set);
    FD_SET(fd, &read_set);
# ifdef _WIN32
    return select(0, &read_set, NULL, NULL, &tv) > 0;
# else
    return select(fd + 1, &read_set, NULL, NULL, &tv) > 0;
# endif
}

static int socket_writable(LanSocket fd) {
    fd_set write_set;
    struct timeval tv = {0, 0};
    FD_ZERO(&write_set);
    FD_SET(fd, &write_set);
# ifdef _WIN32
    return select(0, NULL, &write_set, NULL, &tv) > 0;
# else
    return select(fd + 1, NULL, &write_set, NULL, &tv) > 0;
# endif
}

static int random_bytes(uint8_t* out, size_t length) {
# ifdef _WIN32
    return BCryptGenRandom(NULL, out, (ULONG) length,
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
# else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return 0;
    size_t done = 0;
    while (done < length) {
        ssize_t n = read(fd, out + done, length - done);
        if (n <= 0) { close(fd); return 0; }
        done += (size_t) n;
    }
    close(fd);
    return 1;
# endif
}

static void put_u32(uint8_t* out, uint32_t v) {
    out[0] = (uint8_t) (v >> 24); out[1] = (uint8_t) (v >> 16);
    out[2] = (uint8_t) (v >> 8); out[3] = (uint8_t) v;
}
static uint32_t get_u32(const uint8_t* p) {
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] << 8) | (uint32_t) p[3];
}
static void put_u64(uint8_t* out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out[i] = (uint8_t) (v >> (56 - i * 8));
}
static uint64_t get_u64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return v;
}

static void make_packet(uint8_t out[LAN_PACKET_SIZE], uint8_t type,
                        uint32_t sequence, uint32_t value, uint64_t signature,
                        const uint8_t payload[LAN_PAYLOAD_SIZE]) {
    memset(out, 0, LAN_PACKET_SIZE);
    out[0] = 'S'; out[1] = 'A'; out[2] = 'T'; out[3] = 'L';
    out[4] = LAN_PROTOCOL_VERSION; out[5] = type;
    put_u32(out + 8, sequence);
    put_u32(out + 12, value);
    put_u64(out + 16, signature);
    if (payload) memcpy(out + 24, payload, LAN_PAYLOAD_SIZE);
}

static int packet_valid(const uint8_t* p) {
    return p[0] == 'S' && p[1] == 'A' && p[2] == 'T' && p[3] == 'L' &&
           p[4] == LAN_PROTOCOL_VERSION;
}

static void hmac_sha1(const char* key, size_t key_len,
                      const uint8_t* message, size_t message_len,
                      uint8_t digest[20]) {
    uint8_t k[64] = {0}, inner[20], ipad[64], opad[64];
    if (key_len > sizeof(k)) {
        sha1Buffer(key, key_len, k);
    } else if (key_len) {
        memcpy(k, key, key_len);
    }
    for (size_t i = 0; i < sizeof(k); ++i) {
        ipad[i] = (uint8_t) (k[i] ^ 0x36);
        opad[i] = (uint8_t) (k[i] ^ 0x5c);
    }
    struct SHA1Context ctx;
    sha1Init(&ctx);
    sha1Update(&ctx, ipad, sizeof(ipad));
    sha1Update(&ctx, message, message_len);
    sha1Finalize(inner, &ctx);
    sha1Init(&ctx);
    sha1Update(&ctx, opad, sizeof(opad));
    sha1Update(&ctx, inner, sizeof(inner));
    sha1Finalize(digest, &ctx);
    memset(k, 0, sizeof(k));
    memset(inner, 0, sizeof(inner));
    memset(ipad, 0, sizeof(ipad));
    memset(opad, 0, sizeof(opad));
}

static int constant_time_equal(const uint8_t* a, const uint8_t* b, size_t n) {
    uint8_t diff = 0;
    for (size_t i = 0; i < n; ++i) diff |= (uint8_t) (a[i] ^ b[i]);
    return diff == 0;
}

static void derive_room_key(LanSession* s, const uint8_t client_nonce[16], uint8_t key[20]) {
    static const uint8_t label[] = "SAT-LAN-PBKDF2-v1";
    uint8_t salt[32 + 16 + sizeof(label) - 1];
    memcpy(salt, s->server_nonce, 32);
    memcpy(salt + 32, client_nonce, 16);
    memcpy(salt + 48, label, sizeof(label) - 1);
    /* PBKDF2 slows offline guessing of captured challenge responses. */
    uint8_t block[68], u[20], next[20], t[20];
    memcpy(block, salt, sizeof(salt));
    block[sizeof(salt)] = 0; block[sizeof(salt) + 1] = 0;
    block[sizeof(salt) + 2] = 0; block[sizeof(salt) + 3] = 1;
    const size_t password_len = strlen(s->password);
    hmac_sha1(s->password, password_len, block, sizeof(salt) + 4, u);
    memcpy(t, u, sizeof(t));
    for (unsigned i = 1; i < 60000; ++i) {
        hmac_sha1(s->password, password_len, u, sizeof(u), next);
        memcpy(u, next, sizeof(u));
        for (size_t j = 0; j < sizeof(t); ++j) t[j] ^= u[j];
    }
    memcpy(key, t, sizeof(t));
    memset(salt, 0, sizeof(salt)); memset(block, 0, sizeof(block));
    memset(u, 0, sizeof(u)); memset(next, 0, sizeof(next)); memset(t, 0, sizeof(t));
}

static void derive_session_key(LanSession* s, const uint8_t client_nonce[16]) {
    static const uint8_t label[] = "SAT-LAN-SESSION-v1";
    uint8_t room_key[20];
    uint8_t message[sizeof(label) - 1 + 32 + 16 + 4 + 8];
    size_t at = 0;
    derive_room_key(s, client_nonce, room_key);
    memcpy(message + at, label, sizeof(label) - 1); at += sizeof(label) - 1;
    memcpy(message + at, s->server_nonce, 32); at += 32;
    memcpy(message + at, client_nonce, 16); at += 16;
    put_u32(message + at, s->game_id); at += 4;
    put_u64(message + at, s->rom_signature); at += 8;
    hmac_sha1((const char*) room_key, sizeof(room_key), message, at, s->session_key);
    memcpy(s->client_nonce, client_nonce, sizeof(s->client_nonce));
    memset(room_key, 0, sizeof(room_key)); memset(message, 0, sizeof(message));
}

static void build_auth_proof(LanSession* s, const uint8_t client_nonce[16],
                             uint8_t proof[20]) {
    static const uint8_t label[] = "SAT-LAN-ROOM-v1";
    uint8_t room_key[20];
    uint8_t message[sizeof(label) - 1 + 32 + 16 + 4 + 8];
    size_t at = 0;
    derive_room_key(s, client_nonce, room_key);
    memcpy(message + at, label, sizeof(label) - 1); at += sizeof(label) - 1;
    memcpy(message + at, s->server_nonce, 32); at += 32;
    memcpy(message + at, client_nonce, 16); at += 16;
    put_u32(message + at, s->game_id); at += 4;
    put_u64(message + at, s->rom_signature); at += 8;
    hmac_sha1((const char*) room_key, sizeof(room_key), message, at, proof);
    memset(room_key, 0, sizeof(room_key)); memset(message, 0, sizeof(message));
}

static void build_accept_proof(LanSession* s, uint8_t proof[20]) {
    static const uint8_t message[] = "SAT-LAN-ACCEPT-v1";
    hmac_sha1((const char*) s->session_key, sizeof(s->session_key), message, sizeof(message) - 1, proof);
}

static void build_input_proof(LanSession* s, uint32_t frame, uint32_t keys,
                              unsigned sender_slot, uint8_t proof[20]) {
    static const uint8_t label[] = "SAT-LAN-INPUT-v1";
    uint8_t message[sizeof(label) - 1 + 4 + 4 + 1 + 8];
    size_t at = 0;
    memcpy(message + at, label, sizeof(label) - 1); at += sizeof(label) - 1;
    put_u32(message + at, frame); at += 4;
    put_u32(message + at, keys); at += 4;
    message[at++] = (uint8_t) sender_slot;
    put_u64(message + at, s->rom_signature); at += 8;
    hmac_sha1((const char*) s->session_key, sizeof(s->session_key), message, at, proof);
    memset(message, 0, sizeof(message));
}

static void queue_packet(LanSession* s, uint8_t type, uint32_t sequence,
                         uint32_t value, uint64_t signature,
                         const uint8_t payload[LAN_PAYLOAD_SIZE]) {
    make_packet(s->tx, type, sequence, value, signature, payload);
    s->tx_len = LAN_PACKET_SIZE;
    s->tx_offset = 0;
}

static int flush_packet(LanSession* s) {
    while (s->tx_offset < s->tx_len) {
# ifdef _WIN32
        int n = send(s->socket_fd, (const char*) s->tx + s->tx_offset,
                     (int) (s->tx_len - s->tx_offset), 0);
# else
        ssize_t n = send(s->socket_fd, s->tx + s->tx_offset,
                         s->tx_len - s->tx_offset, MSG_NOSIGNAL);
# endif
        if (n > 0) s->tx_offset += (size_t) n;
        else if (n < 0 && would_block()) return 0;
        else return -1;
    }
    s->tx_len = s->tx_offset = 0;
    return 1;
}

static int receive_packets(LanSession* s) {
    while (s->rx_len < sizeof(s->rx)) {
# ifdef _WIN32
        int n = recv(s->socket_fd, (char*) s->rx + s->rx_len,
                     (int) (sizeof(s->rx) - s->rx_len), 0);
# else
        ssize_t n = recv(s->socket_fd, s->rx + s->rx_len,
                         sizeof(s->rx) - s->rx_len, 0);
# endif
        if (n > 0) s->rx_len += (size_t) n;
        else if (n == 0) return -1;
        else if (would_block()) break;
        else return -1;
    }
    return 1;
}

static int socket_interrupted(void) {
# ifdef _WIN32
    return WSAGetLastError() == WSAEINTR;
# else
    return errno == EINTR;
# endif
}

static int send_all(LanSocket fd, const uint8_t* data, size_t length) {
    size_t done = 0;
    while (done < length) {
# ifdef _WIN32
        int n = send(fd, (const char*) data + done, (int) (length - done), 0);
# else
        ssize_t n = send(fd, data + done, length - done, MSG_NOSIGNAL);
# endif
        if (n > 0) done += (size_t) n;
        else if (n < 0 && socket_interrupted()) continue;
        else return 0;
    }
    return 1;
}

static int recv_all(LanSocket fd, uint8_t* data, size_t length) {
    size_t done = 0;
    while (done < length) {
# ifdef _WIN32
        int n = recv(fd, (char*) data + done, (int) (length - done), 0);
# else
        ssize_t n = recv(fd, data + done, length - done, 0);
# endif
        if (n > 0) done += (size_t) n;
        else if (n < 0 && errno == EINTR) continue;
        else return 0;
    }
    return 1;
}

static void configure_connected_socket(LanSocket fd) {
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*) &yes, sizeof(yes));
# ifdef SO_NOSIGPIPE
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
# endif
# ifdef _WIN32
    DWORD timeout_ms = 300000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*) &timeout_ms, sizeof(timeout_ms));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*) &timeout_ms, sizeof(timeout_ms));
# else
    struct timeval timeout = {300, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
# endif
}

static void find_local_ipv4(char* out, size_t size) {
    if (!out || size == 0) return;
    snprintf(out, size, "No disponible");
    char hostname[128];
    if (gethostname(hostname, sizeof(hostname) - 1) != 0) return;
    hostname[sizeof(hostname) - 1] = 0;
    struct addrinfo hints, *list = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(hostname, NULL, &hints, &list) != 0) return;
    for (struct addrinfo* it = list; it; it = it->ai_next) {
        struct sockaddr_in* addr = (struct sockaddr_in*) it->ai_addr;
        if ((ntohl(addr->sin_addr.s_addr) >> 24) == 127) continue;
# ifdef _WIN32
        if (inet_ntop(AF_INET, &addr->sin_addr, out, (DWORD) size)) break;
# else
        if (inet_ntop(AF_INET, &addr->sin_addr, out, (socklen_t) size)) break;
# endif
    }
    freeaddrinfo(list);
}

static int begin_winsock(LanSession* s) {
# ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 0;
    s->wsa_started = 1;
# endif
    return 1;
}

static void reset_sockets(LanSession* s) {
    if (!s) return;
    close_socket(&s->listener);
    close_socket(&s->socket_fd);
    s->rx_len = 0;
    s->tx_len = s->tx_offset = 0;
    s->become_connected = 0;
}

static int setup_password(LanSession* s, const char* password,
                          unsigned game_id, uint64_t rom_signature) {
    if (!password || strlen(password) < 8 || strlen(password) > LAN_PASSWORD_MAX) return 0;
    snprintf(s->password, sizeof(s->password), "%s", password);
    s->game_id = game_id;
    s->rom_signature = rom_signature;
    return 1;
}

static void complete_connection(LanSession* s) {
    set_nonblocking(s->socket_fd, 0);
    configure_connected_socket(s->socket_fd);
    memset(s->password, 0, sizeof(s->password));
    store_state(s, LAN_CONNECTED);
}

static void process_packet(LanSession* s, const uint8_t* p) {
    if (!packet_valid(p)) { set_error(s, LAN_ERR_PROTOCOL); return; }
    uint8_t type = p[5];
    uint32_t game = get_u32(p + 12);
    uint64_t signature = get_u64(p + 16);

    if (!s->is_host && type == LAN_MSG_HELLO && lan_state(s) == LAN_HANDSHAKE) {
        if (game != s->game_id || signature != s->rom_signature) {
            set_error(s, LAN_ERR_ROM);
            return;
        }
        if (!random_bytes(s->tx, 16)) {
            set_error(s, LAN_ERR_NO_RANDOM);
            return;
        }
        memcpy(s->server_nonce, p + 24, sizeof(s->server_nonce));
        memcpy(s->client_nonce, s->tx, sizeof(s->client_nonce));
        derive_session_key(s, s->client_nonce);
        uint8_t payload[LAN_PAYLOAD_SIZE] = {0};
        uint8_t proof[20];
        memcpy(payload, s->tx, 16);
        build_auth_proof(s, payload, proof);
        memcpy(payload + 16, proof, sizeof(proof));
        queue_packet(s, LAN_MSG_AUTH, 0, s->game_id, s->rom_signature, payload);
        memset(proof, 0, sizeof(proof));
        return;
    }

    if (s->is_host && type == LAN_MSG_AUTH && lan_state(s) == LAN_HANDSHAKE) {
        uint8_t expected[20];
        if (game != s->game_id || signature != s->rom_signature) {
            queue_packet(s, LAN_MSG_REJECT, 0, LAN_ERR_ROM, 0, NULL);
            s->become_connected = -LAN_ERR_ROM;
            return;
        }
        memcpy(s->client_nonce, p + 24, sizeof(s->client_nonce));
        build_auth_proof(s, s->client_nonce, expected);
        if (!constant_time_equal(expected, p + 40, sizeof(expected))) {
            queue_packet(s, LAN_MSG_REJECT, 0, LAN_ERR_PASSWORD, 0, NULL);
            s->become_connected = -LAN_ERR_PASSWORD;
        } else {
            derive_session_key(s, s->client_nonce);
            uint8_t accept_proof[LAN_PAYLOAD_SIZE] = {0};
            build_accept_proof(s, accept_proof);
            queue_packet(s, LAN_MSG_ACCEPT, 0, 1, s->rom_signature, accept_proof);
            memset(accept_proof, 0, sizeof(accept_proof));
            s->become_connected = 1;
        }
        memset(expected, 0, sizeof(expected));
        return;
    }

    if (!s->is_host && type == LAN_MSG_ACCEPT && lan_state(s) == LAN_HANDSHAKE) {
        uint8_t expected[20];
        build_accept_proof(s, expected);
        int valid = signature == s->rom_signature && game == 1 &&
                    constant_time_equal(expected, p + 24, sizeof(expected));
        memset(expected, 0, sizeof(expected));
        if (!valid) { set_error(s, LAN_ERR_PROTOCOL); return; }
        s->player_slot = 1;
        complete_connection(s);
        return;
    }

    if (!s->is_host && type == LAN_MSG_REJECT) {
        set_error(s, (int) get_u32(p + 12));
        return;
    }
    set_error(s, LAN_ERR_PROTOCOL);
}

static int process_receive_buffer(LanSession* s) {
    while (s->rx_len >= LAN_PACKET_SIZE) {
        uint8_t packet[LAN_PACKET_SIZE];
        memcpy(packet, s->rx, sizeof(packet));
        memmove(s->rx, s->rx + LAN_PACKET_SIZE, s->rx_len - LAN_PACKET_SIZE);
        s->rx_len -= LAN_PACKET_SIZE;
        process_packet(s, packet);
        if (lan_state(s) == LAN_CONNECTED || lan_state(s) == LAN_ERROR) break;
    }
    return lan_state(s) != LAN_ERROR;
}
#endif /* SAT_LAN_NATIVE */

LanSession* lan_create(void) {
    LanSession* s = (LanSession*) calloc(1, sizeof(*s));
    if (!s) return NULL;
    atomic_init(&s->state, LAN_IDLE);
    atomic_init(&s->error_code, 0);
    if (MutexInit(&s->input_mutex) == 0) s->mutex_ready = 1;
#if SAT_LAN_NATIVE
    s->listener = LAN_INVALID_SOCKET;
    s->socket_fd = LAN_INVALID_SOCKET;
    if (!begin_winsock(s)) set_error(s, LAN_ERR_CONNECT);
#endif
    return s;
}

void lan_disconnect(LanSession* s) {
    if (!s) return;
#if SAT_LAN_NATIVE
    reset_sockets(s);
#endif
    memset(s->password, 0, sizeof(s->password));
    memset(s->session_key, 0, sizeof(s->session_key));
    memset(s->server_nonce, 0, sizeof(s->server_nonce));
    memset(s->client_nonce, 0, sizeof(s->client_nonce));
    s->local_input = 0;
    atomic_store_explicit(&s->error_code, 0, memory_order_release);
    store_state(s, LAN_IDLE);
}

void lan_destroy(LanSession* s) {
    if (!s) return;
    lan_disconnect(s);
    if (s->mutex_ready) MutexDeinit(&s->input_mutex);
#if SAT_LAN_NATIVE && defined(_WIN32)
    if (s->wsa_started) WSACleanup();
#endif
    memset(s, 0, sizeof(*s));
    free(s);
}

int lan_host_start(LanSession* s, unsigned short port, const char* password,
                   unsigned game_id, uint64_t rom_signature) {
    if (!s || !lan_supported() || !port) return 0;
#if SAT_LAN_NATIVE
    lan_disconnect(s);
    if (!setup_password(s, password, game_id, rom_signature)) {
        set_error(s, LAN_ERR_CONFIG); return 0;
    }
    s->is_host = 1;
    s->player_slot = 0;
    s->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_failed(s->listener)) { set_error(s, LAN_ERR_PORT); return 0; }
    int yes = 1;
    setsockopt(s->listener, SOL_SOCKET, SO_REUSEADDR, (const char*) &yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(s->listener, (struct sockaddr*) &addr, sizeof(addr)) != 0 ||
        listen(s->listener, 1) != 0 || !set_nonblocking(s->listener, 1)) {
        reset_sockets(s); set_error(s, LAN_ERR_PORT); return 0;
    }
    find_local_ipv4(s->local_ip, sizeof(s->local_ip));
    store_state(s, LAN_WAITING);
    return 1;
#else
    (void) password; (void) game_id; (void) rom_signature; (void) port;
    set_error(s, LAN_ERR_CONNECT); return 0;
#endif
}

int lan_join_start(LanSession* s, const char* host_ipv4, unsigned short port,
                   const char* password, unsigned game_id, uint64_t rom_signature) {
    if (!s || !lan_supported() || !host_ipv4 || !port) return 0;
#if SAT_LAN_NATIVE
    lan_disconnect(s);
    if (!setup_password(s, password, game_id, rom_signature)) {
        set_error(s, LAN_ERR_CONFIG); return 0;
    }
    s->is_host = 0;
    s->player_slot = 1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host_ipv4, &addr.sin_addr) != 1) {
        set_error(s, LAN_ERR_CONFIG); return 0;
    }
    s->socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_failed(s->socket_fd) || !set_nonblocking(s->socket_fd, 1)) {
        reset_sockets(s); set_error(s, LAN_ERR_CONNECT); return 0;
    }
    int rc = connect(s->socket_fd, (struct sockaddr*) &addr, sizeof(addr));
    if (rc == 0) store_state(s, LAN_HANDSHAKE);
    else if (would_block()) store_state(s, LAN_CONNECTING);
    else { reset_sockets(s); set_error(s, LAN_ERR_CONNECT); return 0; }
    return 1;
#else
    (void) host_ipv4; (void) password; (void) game_id; (void) rom_signature; (void) port;
    set_error(s, LAN_ERR_CONNECT); return 0;
#endif
}

void lan_poll(LanSession* s) {
    if (!s) return;
#if SAT_LAN_NATIVE
    int state = lan_state(s);
    if (state == LAN_WAITING) {
        if (!socket_readable(s->listener)) return;
        s->socket_fd = accept(s->listener, NULL, NULL);
        if (socket_failed(s->socket_fd)) {
            if (!would_block()) set_error(s, LAN_ERR_CONNECT);
            return;
        }
        close_socket(&s->listener);
        if (!set_nonblocking(s->socket_fd, 1) || !random_bytes(s->server_nonce, sizeof(s->server_nonce))) {
            set_error(s, LAN_ERR_NO_RANDOM); return;
        }
        uint8_t payload[LAN_PAYLOAD_SIZE] = {0};
        memcpy(payload, s->server_nonce, sizeof(s->server_nonce));
        queue_packet(s, LAN_MSG_HELLO, 0, s->game_id, s->rom_signature, payload);
        store_state(s, LAN_HANDSHAKE);
        state = LAN_HANDSHAKE;
    }

    if (state == LAN_CONNECTING) {
        if (!socket_writable(s->socket_fd)) return;
        int err = 0;
# ifdef _WIN32
        int len = sizeof(err);
        getsockopt(s->socket_fd, SOL_SOCKET, SO_ERROR, (char*) &err, &len);
# else
        socklen_t len = sizeof(err);
        getsockopt(s->socket_fd, SOL_SOCKET, SO_ERROR, &err, &len);
# endif
        if (err) { set_error(s, LAN_ERR_CONNECT); return; }
        store_state(s, LAN_HANDSHAKE);
        state = LAN_HANDSHAKE;
    }

    if (state != LAN_HANDSHAKE) return;
    if (s->tx_len) {
        int sent = flush_packet(s);
        if (sent < 0) { set_error(s, LAN_ERR_DISCONNECTED); return; }
        if (sent == 0) return;
        if (s->become_connected) {
            int result = s->become_connected;
            s->become_connected = 0;
            if (result > 0) {
                s->player_slot = 0;
                complete_connection(s);
            } else {
                set_error(s, -result);
            }
            return;
        }
    }

    if (socket_readable(s->socket_fd)) {
        if (receive_packets(s) < 0) { set_error(s, LAN_ERR_DISCONNECTED); return; }
        (void) process_receive_buffer(s);
    }
#else
    (void) s;
#endif
}

void lan_set_local_input(LanSession* s, uint32_t keys) {
    if (!s || lan_state(s) != LAN_CONNECTED) return;
    if (s->mutex_ready) MutexLock(&s->input_mutex);
    s->local_input = keys;
    if (s->mutex_ready) MutexUnlock(&s->input_mutex);
}

int lan_exchange_input(LanSession* s, uint32_t frame,
                       uint32_t* local_keys_out, uint32_t* remote_keys) {
    if (local_keys_out) *local_keys_out = 0;
    if (remote_keys) *remote_keys = 0;
    if (!s || lan_state(s) != LAN_CONNECTED) return 0;
#if SAT_LAN_NATIVE
    uint8_t packet[LAN_PACKET_SIZE], incoming[LAN_PACKET_SIZE];
    uint8_t payload[LAN_PAYLOAD_SIZE] = {0}, proof[20], expected[20];
    uint32_t local_keys = 0;
    if (s->mutex_ready) MutexLock(&s->input_mutex);
    local_keys = s->local_input;
    if (s->mutex_ready) MutexUnlock(&s->input_mutex);
    if (local_keys_out) *local_keys_out = local_keys;
    build_input_proof(s, frame, local_keys, s->is_host ? 0u : 1u, proof);
    memcpy(payload, proof, sizeof(proof));
    make_packet(packet, LAN_MSG_INPUT, frame, local_keys, s->rom_signature, payload);
    memset(proof, 0, sizeof(proof));
    if (!send_all(s->socket_fd, packet, sizeof(packet))) {
        set_error(s, LAN_ERR_DISCONNECTED);
        return 0;
    }
    /* A handshake poll may have read the ACCEPT packet and the first input
     * packet in a single TCP receive. Preserve and consume the buffered frame. */
    if (s->rx_len >= LAN_PACKET_SIZE) {
        memcpy(incoming, s->rx, LAN_PACKET_SIZE);
        memmove(s->rx, s->rx + LAN_PACKET_SIZE, s->rx_len - LAN_PACKET_SIZE);
        s->rx_len -= LAN_PACKET_SIZE;
    } else if (!recv_all(s->socket_fd, incoming, sizeof(incoming))) {
        set_error(s, LAN_ERR_DISCONNECTED);
        return 0;
    }
    uint32_t received_frame = get_u32(incoming + 8);
    uint32_t received_keys = get_u32(incoming + 12);
    build_input_proof(s, received_frame, received_keys, s->is_host ? 1u : 0u, expected);
    int valid = packet_valid(incoming) && incoming[5] == LAN_MSG_INPUT &&
                received_frame == frame && get_u64(incoming + 16) == s->rom_signature &&
                constant_time_equal(expected, incoming + 24, sizeof(expected));
    memset(expected, 0, sizeof(expected));
    if (!valid) { set_error(s, LAN_ERR_PROTOCOL); return 0; }
    if (remote_keys) *remote_keys = received_keys;
    return 1;
#else
    (void) frame;
    set_error(s, LAN_ERR_CONNECT);
    return 0;
#endif
}
