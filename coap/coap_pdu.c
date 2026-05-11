/*
 * CoAP PDU encode / decode (RFC 7252 §3).
 */
#include "coap.h"
#include <stdlib.h>
#include <string.h>

coap_pdu_t *coap_pdu_new(void) {
    coap_pdu_t *pdu = calloc(1, sizeof(coap_pdu_t));
    return pdu;
}

void coap_pdu_free(coap_pdu_t *pdu) {
    free(pdu);
}

void coap_pdu_init(coap_pdu_t *pdu, uint8_t type, uint8_t code, uint16_t mid) {
    memset(pdu, 0, sizeof(*pdu));
    pdu->type = type;
    pdu->code = code;
    pdu->mid  = mid;
}

int coap_add_token(coap_pdu_t *pdu, const uint8_t *token, uint8_t len) {
    if (len > 8) return -1;
    memcpy(pdu->token, token, len);
    pdu->token_len = len;
    return 0;
}

int coap_add_option(coap_pdu_t *pdu, uint16_t opt_num,
                     const uint8_t *val, uint16_t val_len) {
    if (pdu->num_options >= COAP_MAX_OPTIONS) return -1;
    if (pdu->opt_store_used + val_len > COAP_MAX_OPT_VAL) return -1;

    /* Keep options sorted by number (insert in order) */
    int pos = pdu->num_options;
    for (int i = 0; i < pdu->num_options; i++) {
        if (pdu->options[i].number > opt_num) { pos = i; break; }
    }
    /* Shift entries right */
    for (int i = pdu->num_options; i > pos; i--) {
        pdu->options[i] = pdu->options[i-1];
    }

    uint8_t *store = pdu->opt_store + pdu->opt_store_used;
    memcpy(store, val, val_len);
    pdu->options[pos].number = opt_num;
    pdu->options[pos].length = val_len;
    pdu->options[pos].value  = store;
    pdu->opt_store_used += val_len;
    pdu->num_options++;
    return 0;
}

int coap_set_payload(coap_pdu_t *pdu, const uint8_t *data, size_t len) {
    pdu->payload     = data;
    pdu->payload_len = len;
    return 0;
}

const coap_opt_t *coap_find_option(const coap_pdu_t *pdu, uint16_t opt_num) {
    for (int i = 0; i < pdu->num_options; i++) {
        if (pdu->options[i].number == opt_num) return &pdu->options[i];
    }
    return NULL;
}

/* Encode option delta/length field with extended bytes */
static int encode_opt_hdr_nibble(uint16_t val, uint8_t *nibble, uint8_t *ext, int *ext_len) {
    if (val <= 12) {
        *nibble  = (uint8_t)val;
        *ext_len = 0;
    } else if (val <= 268) {
        *nibble = 13;
        ext[0]  = (uint8_t)(val - 13);
        *ext_len = 1;
    } else {
        *nibble = 14;
        uint16_t v = val - 269;
        ext[0] = (uint8_t)(v >> 8);
        ext[1] = (uint8_t)(v & 0xFF);
        *ext_len = 2;
    }
    return 0;
}

int coap_encode_pdu(coap_pdu_t *pdu) {
    uint8_t *buf = pdu->wire;
    size_t   cap = COAP_MAX_PDU_SIZE;
    size_t   pos = 0;

    if (pos + 4 > cap) return -1;

    buf[0] = (1u << 6) | ((pdu->type & 0x3) << 4) | (pdu->token_len & 0xF);
    buf[1] = pdu->code;
    buf[2] = (uint8_t)(pdu->mid >> 8);
    buf[3] = (uint8_t)(pdu->mid & 0xFF);
    pos = 4;

    /* Token */
    if (pdu->token_len > 0) {
        if (pos + pdu->token_len > cap) return -1;
        memcpy(buf + pos, pdu->token, pdu->token_len);
        pos += pdu->token_len;
    }

    /* Options */
    uint16_t prev_num = 0;
    for (int i = 0; i < pdu->num_options; i++) {
        uint16_t delta  = pdu->options[i].number - prev_num;
        uint16_t optlen = pdu->options[i].length;
        uint8_t  delta_nibble, len_nibble;
        uint8_t  delta_ext[2], len_ext[2];
        int      delta_ext_len, len_ext_len;

        encode_opt_hdr_nibble(delta,  &delta_nibble, delta_ext, &delta_ext_len);
        encode_opt_hdr_nibble(optlen, &len_nibble,   len_ext,   &len_ext_len);

        if (pos + 1 + delta_ext_len + len_ext_len + optlen > cap) return -1;

        buf[pos++] = (delta_nibble << 4) | (len_nibble & 0xF);
        memcpy(buf + pos, delta_ext, delta_ext_len); pos += delta_ext_len;
        memcpy(buf + pos, len_ext,   len_ext_len);   pos += len_ext_len;
        if (optlen > 0) {
            memcpy(buf + pos, pdu->options[i].value, optlen);
            pos += optlen;
        }
        prev_num = pdu->options[i].number;
    }

    /* Payload */
    if (pdu->payload_len > 0) {
        if (pos + 1 + pdu->payload_len > cap) return -1;
        buf[pos++] = 0xFF;  /* payload marker */
        memcpy(buf + pos, pdu->payload, pdu->payload_len);
        pos += pdu->payload_len;
    }

    pdu->wire_len = pos;
    return (int)pos;
}

int coap_decode_pdu(coap_pdu_t *pdu, const uint8_t *buf, size_t len) {
    if (len < 4) return -1;

    uint8_t ver     = (buf[0] >> 6) & 0x3;
    if (ver != 1) return -1;

    pdu->type      = (buf[0] >> 4) & 0x3;
    pdu->token_len = buf[0] & 0xF;
    pdu->code      = buf[1];
    pdu->mid       = ((uint16_t)buf[2] << 8) | buf[3];

    if (pdu->token_len > 8) return -1;
    size_t pos = 4;
    if (pos + pdu->token_len > len) return -1;
    memcpy(pdu->token, buf + pos, pdu->token_len);
    pos += pdu->token_len;

    /* Parse options */
    pdu->num_options      = 0;
    pdu->opt_store_used   = 0;
    uint16_t opt_num = 0;

    while (pos < len && buf[pos] != 0xFF) {
        if (pdu->num_options >= COAP_MAX_OPTIONS) return -1;

        uint8_t hdr        = buf[pos++];
        uint8_t delta_nib  = (hdr >> 4) & 0xF;
        uint8_t len_nib    = hdr & 0xF;
        if (delta_nib == 15 || len_nib == 15) return -1; /* reserved */

        uint16_t delta = delta_nib;
        if (delta_nib == 13) {
            if (pos >= len) return -1;
            delta = buf[pos++] + 13;
        } else if (delta_nib == 14) {
            if (pos + 1 >= len) return -1;
            delta = (((uint16_t)buf[pos] << 8) | buf[pos+1]) + 269;
            pos += 2;
        }

        uint16_t optlen = len_nib;
        if (len_nib == 13) {
            if (pos >= len) return -1;
            optlen = buf[pos++] + 13;
        } else if (len_nib == 14) {
            if (pos + 1 >= len) return -1;
            optlen = (((uint16_t)buf[pos] << 8) | buf[pos+1]) + 269;
            pos += 2;
        }

        opt_num += delta;
        if (pos + optlen > len) return -1;

        /* Copy value into opt_store so it outlives the wire buffer */
        if (pdu->opt_store_used + optlen > COAP_MAX_OPT_VAL) return -1;
        uint8_t *dst = pdu->opt_store + pdu->opt_store_used;
        memcpy(dst, buf + pos, optlen);

        pdu->options[pdu->num_options].number = opt_num;
        pdu->options[pdu->num_options].length = optlen;
        pdu->options[pdu->num_options].value  = dst;
        pdu->opt_store_used += optlen;
        pdu->num_options++;
        pos += optlen;
    }

    /* Payload */
    if (pos < len && buf[pos] == 0xFF) {
        pos++;
        pdu->payload     = buf + pos;
        pdu->payload_len = len - pos;
    } else {
        pdu->payload     = NULL;
        pdu->payload_len = 0;
    }

    /* Store a copy of the wire bytes for the caller */
    size_t copy_len = len < COAP_MAX_PDU_SIZE ? len : COAP_MAX_PDU_SIZE;
    memcpy(pdu->wire, buf, copy_len);
    pdu->wire_len = copy_len;

    /* Fix up payload pointer to point into our copy */
    if (pdu->payload) {
        pdu->payload = pdu->wire + (pdu->payload - buf);
    }

    return 0;
}
