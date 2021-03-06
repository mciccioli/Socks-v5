#ifndef AUTH_H
#define AUTH_H

#include "buffer.h"

#define MAX_USR_PASS_SIZE 0XFF
#define AUTH_FAIL 0x01
#define AUTH_SUCCESS 0x00
#define AUTH_BAD_CREDENTIALS 0x02
/* Establece para que protocolo se va a usar el auth */
enum auth_type{
    
    // AUTH para identificación en socks5
    AUTH_SOCKS,

    // AUTH para identificación en protocol de configuración
    AUTH_MNG,
};


/* estados posibles del parser de autenticación */
enum auth_state
{
    auth_version,
    auth_ulen,
    auth_uname,
    auth_plen,
    auth_passwd,

    // done section
    auth_done,

    // error section
    auth_error_unsupported_version,
    auth_error,
    auth_error_invalid_ulen,
    auth_error_invalid_plen,
    
};

struct usr {
    uint8_t ulen;
    uint8_t uname[MAX_USR_PASS_SIZE];
};

struct pass {
    uint8_t plen;
    uint8_t passwd[MAX_USR_PASS_SIZE];
};

typedef struct auth_parser
{
    enum auth_state state;

    struct usr usr;
    struct pass pass;

    uint8_t remaining;
    uint8_t read;

    uint8_t version;
} auth_parser ;





/** inicializa el parser **/
void auth_parser_init(struct auth_parser *p,enum auth_type type);

/** entrega un byte al parser **/
enum auth_state auth_parser_feed(auth_parser *p, uint8_t b);

/** consume los bytes del mensaje del cliente y se los entrega al parser 
 * hasta que se termine de parsear 
**/
enum auth_state auth_consume(buffer *b, auth_parser *p, bool *error);

bool auth_is_done(const enum auth_state state, bool *error);

/** ensambla la respuesta del request dentro del buffer con el metodo 
 * seleccionado.
**/
int auth_marshal(buffer *b, const uint8_t status, uint8_t version);


#endif




