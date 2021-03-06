#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "../includes/request.h"

static void remaining_set(request_parser *p, const int n)
{
    p->remaining = n;
    p->read = 0;
}

static int remaining_is_done(request_parser *p)
{
    return p->read >= p->remaining;
}

static enum request_state version(uint8_t b)
{
    enum request_state next;
    if (b == 0x05)
    {
        next = request_cmd;
    }
    else
    {
        next = request_error_unsupported_version;
    }
    return next;
}

static enum request_state cmd(request_parser *p, uint8_t b)
{
    enum request_state next;
    if (b > 0x00 && b < 0x04)
    {
        p->request->cmd = b;
        next = request_rsv;
    }
    else
    {
        next = request_error_usupported_cmd;
    }
    return next;
}

static enum request_state atyp(request_parser *p, uint8_t b)
{
    enum request_state next;
    p->request->dest_addr_type = b;
    switch (p->request->dest_addr_type)
    {
    case ipv4_type:
        remaining_set(p, 4);
        memset(&(p->request->dest_addr.ipv4), 0, sizeof(p->request->dest_addr.ipv4));
        p->request->dest_addr.ipv4.sin_family = AF_INET;
        next = request_dest_addr;
        break;
    case domainname_type:
        next = request_dest_addr_fqdn;
        break;
    case ipv6_type:
        remaining_set(p, 16);
        memset(&(p->request->dest_addr.ipv6), 0, sizeof(p->request->dest_addr.ipv6));
        p->request->dest_addr.ipv6.sin6_family = AF_INET6;
        next = request_dest_addr;
        break;
    default:
        next = request_error_usupported_atyp;
        break;
    }
    return next;
}

static enum request_state dest_addr_fqdn(request_parser *p, uint8_t b)
{
    remaining_set(p, b);
    p->request->dest_addr.fqdn[p->remaining - 1] = 0;
    return request_dest_addr;
}

static enum request_state dest_addr(request_parser *p, uint8_t b)
{
    enum request_state next;
    switch (p->request->dest_addr_type)
    {
    case ipv4_type:
        ((uint8_t *)&(p->request->dest_addr.ipv4.sin_addr))[p->read++] = b;
        break;
    case domainname_type:
        p->request->dest_addr.fqdn[p->read++] = b;
        break;
    case ipv6_type:
        ((uint8_t *)&(p->request->dest_addr.ipv6.sin6_addr))[p->read++] = b;
        break;
    }
    if (remaining_is_done(p))
    {
        remaining_set(p, 2);
        p->request->dest_port = 0;
        next = request_dest_port;
    }
    else
    {
        next = request_dest_addr;
    }
    return next;
}

static enum request_state dest_port(request_parser *p, uint8_t b)
{
    enum request_state next = request_dest_port;
    *(((uint8_t *)&(p->request->dest_port)) + p->read) = b;
    p->read++;
    if (remaining_is_done(p))
    {
        next = request_done;
    }
    return next;
}

enum request_state request_parser_feed(request_parser *p, uint8_t b)
{
    enum request_state next = request_error;
    switch (p->state)
    {
    case request_version:
        next = version(b);
        break;
    case request_cmd:
        next = cmd(p, b);
        break;
    case request_rsv:
        next = request_atyp;
        break;
    case request_atyp:
        next = atyp(p, b);
        break;
    case request_dest_addr_fqdn:
        next = dest_addr_fqdn(p, b);
        break;
    case request_dest_addr:
        next = dest_addr(p, b);
        break;
    case request_dest_port:
        next = dest_port(p, b);
        break;
    case request_done:
        next = request_done;
        break;
    case request_error_unsupported_version:
        next = request_error_unsupported_version;
        break;
    case request_error_usupported_cmd:
        next = request_error_usupported_cmd;
        break;
    case request_error_usupported_atyp:
        next = request_error_usupported_atyp;
        break;
    default:
        abort();
        break;
    }
    p->state = next;
    return p->state;
}

void request_parser_init(request_parser *p)
{
    p->state = request_version;
    memset(p->request, 0, sizeof(*(p->request)));
}

enum request_state request_consume(buffer *b, request_parser *p, bool *error)
{
    enum request_state st = p->state;
    bool finished = false;
    while (buffer_can_read(b) && !finished)
    {
        uint8_t byte = buffer_read(b);
        st = request_parser_feed(p, byte);
        if (request_is_done(st, error))
        {
            finished = true;
        }
    }
    return st;
}

int request_marshal(buffer *b, const enum socks_reply_status status, const enum socks_atyp atyp, const union socks_addr addr, const in_port_t dest_port)
{
    size_t n, len = 6;
    uint8_t *buff = buffer_write_ptr(b, &n);
    uint8_t *aux = NULL;
    int addr_size = 0;
    switch (atyp)
    {
    case ipv4_type:
        addr_size = 4;
        len += addr_size;
        aux = (uint8_t *)malloc(4 * sizeof(uint8_t));
        memcpy(aux, &addr.ipv4.sin_addr, 4);
        break;
    case ipv6_type:
        addr_size = 16;
        len += addr_size;
        aux = (uint8_t *)malloc(16 * sizeof(uint8_t));
        memcpy(aux, &addr.ipv6.sin6_addr, 16);
        break;
    case domainname_type:
        addr_size = strlen(addr.fqdn);
        aux = (uint8_t *)malloc((addr_size + 1) * sizeof(uint8_t));
        aux[0] = addr_size;
        memcpy(aux + 1, addr.fqdn, addr_size);
        addr_size++;
        len += addr_size;
        break;
    }
    if (n < len)
    {
        free(aux);
        return -1;
    }
    buff[0] = 0x05;
    buff[1] = status;
    buff[2] = 0x00;
    buff[3] = atyp;
    memcpy(&buff[4], aux, addr_size);
    free(aux);
    memcpy(&buff[4 + addr_size], &dest_port, 2);
    buffer_write_adv(b, len);
    return len;
}

bool request_is_done(const enum request_state state, bool *error)
{
    bool ret = false;
    if (state == request_error ||
        state == request_error_unsupported_version ||
        state == request_error_usupported_atyp ||
        state == request_error_usupported_cmd)
    {
        if (error != 0)
        {
            *error = true;
        }
        ret = true;
    }
    else if (state == request_done)
    {
        ret = true;
    }
    return ret;
}

enum socks_reply_status errno_to_socks(int e)
{
    enum socks_reply_status ret = status_general_socks_server_failure;

    switch (e)
    {
    case 0:
        ret = status_succeeded;
        break;
    case ECONNREFUSED:
        ret = status_connection_refused;
        break;
    case EHOSTUNREACH:
        ret = status_host_unreachable;
        break;
    case ENETUNREACH:
        ret = status_network_unreachable;
        break;
    case ETIMEDOUT:
        ret = status_ttl_expired;
        break;
    }

    return ret;
}

enum socks_reply_status cmd_resolve(struct request *request, struct sockaddr **originaddr, socklen_t *originlen, int *domain)
{
    enum socks_reply_status ret = status_general_socks_server_failure;

    *domain = AF_INET;
    struct sockaddr *addr = 0x00;
    socklen_t addrlen = 0;

    switch (request->dest_addr_type)
    {
    case domainname_type:
    {
        struct hostent *hp = gethostbyname(request->dest_addr.fqdn);
        if (hp == 0)
        {
            memset(&request->dest_addr, 0x00, sizeof(request->dest_addr));
            break;
        }
        request->dest_addr.ipv4.sin_family = hp->h_addrtype;
        memcpy((char *)&request->dest_addr.ipv4.sin_addr, *hp->h_addr_list, hp->h_length);
    }
    // fall through
    case ipv4_type:
    {
        *domain = AF_INET;
        addr = (struct sockaddr *)&(request->dest_addr.ipv4);
        addrlen = sizeof(request->dest_addr.ipv4);
        request->dest_addr.ipv4.sin_port = request->dest_port;
        break;
    }
    case ipv6_type:
    {
        *domain = AF_INET6;
        addr = (struct sockaddr *)&(request->dest_addr.ipv6);
        addrlen = sizeof(request->dest_addr.ipv6);
        request->dest_addr.ipv6.sin6_port = request->dest_port;
        break;
    }
    default:
        return status_address_type_not_supported;
    }

    *originaddr = addr;
    *originlen = addrlen;

    return ret;
}
