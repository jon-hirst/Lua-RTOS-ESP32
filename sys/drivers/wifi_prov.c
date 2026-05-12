/*
 * Copyright (C) 2015 - 2020, IBEROXARXA SERVICIOS INTEGRALES, S.L.
 * Copyright (C) 2015 - 2020, Jaume Olivé Petrus (jolive@whitecatboard.org)
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Lua RTOS, WiFi captive-portal provisioning driver
 *
 */

#include "sdkconfig.h"

#if CONFIG_LUA_RTOS_WIFI_PROV

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "esp_system.h"
#include "esp_wifi.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/ip_addr.h"

#include <sys/driver.h>
#include <sys/syslog.h>
#include <sys/delay.h>

#include <drivers/net.h>
#include <drivers/wifi.h>
#include <drivers/wifi_prov.h>

#define PROV_NVS_NAMESPACE  "wifiprov"
#define PROV_NVS_KEY_SSID   "ssid"
#define PROV_NVS_KEY_PASS   "pass"

#define PROV_HTTP_PORT      80
#define PROV_HTTP_BUF_LEN   2048
#define PROV_HTTP_TASK_STACK 8192
#define PROV_HTTP_TASK_PRIO  5

extern int captivedns_start_raw(void);
extern void captivedns_stop(void);

static TaskHandle_t prov_http_task_handle = NULL;

/* -----------------------------------------------------------------------
 * NVS credential storage
 * ----------------------------------------------------------------------- */

int wifi_prov_has_credentials(void) {
    nvs_handle_t h;
    if (nvs_open(PROV_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return 0;
    }

    char ssid[64] = {0};
    size_t ssid_len = sizeof(ssid);
    esp_err_t rc = nvs_get_str(h, PROV_NVS_KEY_SSID, ssid, &ssid_len);
    nvs_close(h);

    return (rc == ESP_OK && ssid[0] != '\0') ? 1 : 0;
}

void wifi_prov_load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len) {
    nvs_handle_t h;
    if (nvs_open(PROV_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        if (ssid && ssid_len > 0) ssid[0] = '\0';
        if (pass && pass_len > 0) pass[0] = '\0';
        return;
    }

    if (ssid && ssid_len > 0) {
        if (nvs_get_str(h, PROV_NVS_KEY_SSID, ssid, &ssid_len) != ESP_OK) {
            ssid[0] = '\0';
        }
    }

    if (pass && pass_len > 0) {
        if (nvs_get_str(h, PROV_NVS_KEY_PASS, pass, &pass_len) != ESP_OK) {
            pass[0] = '\0';
        }
    }

    nvs_close(h);
}

void wifi_prov_save_credentials(const char *ssid, const char *pass) {
    nvs_handle_t h;
    if (nvs_open(PROV_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }

    nvs_set_str(h, PROV_NVS_KEY_SSID, ssid ? ssid : "");
    nvs_set_str(h, PROV_NVS_KEY_PASS, pass  ? pass  : "");
    nvs_commit(h);
    nvs_close(h);
}

void wifi_prov_erase_credentials(void) {
    nvs_handle_t h;
    if (nvs_open(PROV_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }

    nvs_erase_key(h, PROV_NVS_KEY_SSID);
    nvs_erase_key(h, PROV_NVS_KEY_PASS);
    nvs_commit(h);
    nvs_close(h);
}

/* -----------------------------------------------------------------------
 * URL-decode helper
 * ----------------------------------------------------------------------- */

static void url_decode(const char *src, char *dst, size_t dst_len) {
    size_t i = 0, j = 0;
    while (src[i] && j + 1 < dst_len) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            char hex[3] = { src[i+1], src[i+2], '\0' };
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
}

/* Extract value for key= from an application/x-www-form-urlencoded body */
static void form_field(const char *body, const char *key, char *out, size_t out_len) {
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            p += klen + 1;
            const char *end = strchr(p, '&');
            size_t vlen = end ? (size_t)(end - p) : strlen(p);
            if (vlen >= out_len) vlen = out_len - 1;
            char raw[256] = {0};
            if (vlen >= sizeof(raw)) vlen = sizeof(raw) - 1;
            memcpy(raw, p, vlen);
            raw[vlen] = '\0';
            url_decode(raw, out, out_len);
            return;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    out[0] = '\0';
}

/* -----------------------------------------------------------------------
 * Provisioning HTTP server task
 * ----------------------------------------------------------------------- */

static const char *prov_page_head =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>WiFi Setup</title>"
    "<style>body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:0 20px}"
    "input,select{width:100%;padding:8px;margin:6px 0 16px;box-sizing:border-box}"
    "button{width:100%;padding:10px;background:#0078d4;color:#fff;border:none;font-size:1em;cursor:pointer}"
    "</style></head><body>"
    "<h2>WiFi Setup</h2>"
    "<form method='POST' action='/'>";

static const char *prov_page_tail =
    "<label>Password:</label>"
    "<input type='password' name='pass'>"
    "<button type='submit'>Save &amp; Connect</button>"
    "</form></body></html>";

static const char *prov_page_redirect =
    "HTTP/1.0 302 Found\r\n"
    "Location: /\r\n"
    "Connection: close\r\n"
    "\r\n";

static const char *prov_page_saved =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<title>Saved</title></head><body>"
    "<h2>Credentials saved</h2>"
    "<p>The device will now reboot and connect to your network.</p>"
    "</body></html>";

static void wifi_prov_http_task(void *arg) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(PROV_HTTP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(server_fd, 4) < 0) {
        close(server_fd);
        vTaskDelete(NULL);
        return;
    }

    char *buf = malloc(PROV_HTTP_BUF_LEN);
    if (!buf) {
        close(server_fd);
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            break;
        }

        int total = 0;
        int n;
        while (total < PROV_HTTP_BUF_LEN - 1) {
            n = recv(client_fd, buf + total, PROV_HTTP_BUF_LEN - 1 - total, 0);
            if (n <= 0) break;
            total += n;
            buf[total] = '\0';
            if (strstr(buf, "\r\n\r\n")) break;
        }
        buf[total] = '\0';

        int is_post = (strncmp(buf, "POST ", 5) == 0);
        int is_get  = (strncmp(buf, "GET ",  4) == 0);

        /* Determine requested path */
        char path[64] = "/";
        if (is_post || is_get) {
            char *start = buf + (is_post ? 5 : 4);
            char *end   = strchr(start, ' ');
            if (end) {
                size_t plen = (size_t)(end - start);
                if (plen >= sizeof(path)) plen = sizeof(path) - 1;
                memcpy(path, start, plen);
                path[plen] = '\0';
            }
        }

        if (is_post && path[0] == '/' && path[1] == '\0') {
            /* Find the body after \r\n\r\n */
            char *body = strstr(buf, "\r\n\r\n");
            if (body) {
                body += 4;
                /* Read remaining body bytes if Content-Length says more */
                char *cl_hdr = strstr(buf, "Content-Length:");
                if (!cl_hdr) cl_hdr = strstr(buf, "content-length:");
                if (cl_hdr) {
                    int clen = atoi(cl_hdr + 15);
                    int already = (int)(buf + total - body);
                    while (already < clen && total < PROV_HTTP_BUF_LEN - 1) {
                        n = recv(client_fd, buf + total, PROV_HTTP_BUF_LEN - 1 - total, 0);
                        if (n <= 0) break;
                        total += n;
                        already += n;
                    }
                    buf[total] = '\0';
                    body = strstr(buf, "\r\n\r\n");
                    if (body) body += 4;
                }
            }

            char ssid[64] = {0};
            char pass[64] = {0};
            if (body) {
                form_field(body, "ssid", ssid, sizeof(ssid));
                form_field(body, "pass", pass, sizeof(pass));
            }

            send(client_fd, prov_page_saved, strlen(prov_page_saved), 0);
            close(client_fd);

            wifi_prov_save_credentials(ssid, pass);
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
            /* unreachable */
            break;
        } else if (is_get && path[0] == '/' && path[1] == '\0') {
            /* Scan for nearby APs and build the form */
            uint16_t ap_count = 0;
            wifi_ap_record_t *ap_list = NULL;
            wifi_scan(ap_count ? &ap_count : &ap_count, &ap_list);

            send(client_fd, prov_page_head, strlen(prov_page_head), 0);

            /* SSID select + manual entry */
            const char *sel_open = "<label>Select network:</label><select name='ssid'>";
            const char *sel_close = "</select>"
                "<label>Or enter manually:</label>"
                "<input type='text' name='ssid' placeholder='SSID'>";
            send(client_fd, sel_open, strlen(sel_open), 0);

            wifi_scan(&ap_count, &ap_list);
            if (ap_list && ap_count > 0) {
                for (uint16_t i = 0; i < ap_count; i++) {
                    char opt[128];
                    snprintf(opt, sizeof(opt), "<option value='%s'>%s</option>",
                             (char *)ap_list[i].ssid, (char *)ap_list[i].ssid);
                    send(client_fd, opt, strlen(opt), 0);
                }
                free(ap_list);
            }

            send(client_fd, sel_close, strlen(sel_close), 0);
            send(client_fd, prov_page_tail, strlen(prov_page_tail), 0);
        } else {
            send(client_fd, prov_page_redirect, strlen(prov_page_redirect), 0);
        }

        close(client_fd);
    }

    free(buf);
    close(server_fd);
    prov_http_task_handle = NULL;
    vTaskDelete(NULL);
}

/* -----------------------------------------------------------------------
 * SoftAP startup / teardown
 * ----------------------------------------------------------------------- */

driver_error_t *wifi_prov_start(const char *ap_ssid, const char *ap_pass) {
    driver_error_t *error;

    if (!ap_ssid || ap_ssid[0] == '\0') {
        ap_ssid = "LuaRTOS-Setup";
    }
    if (!ap_pass) {
        ap_pass = "";
    }

    /* Bring up the SoftAP */
    error = wifi_setup(WIFI_MODE_AP, (char *)ap_ssid, (char *)ap_pass,
                       0, 0, 0, 0, 0, 0, 1, 0, 0);
    if (error) return error;

    error = wifi_start(0);
    if (error) return error;

    /* Start captive DNS so all domains resolve to 192.168.4.1 */
    if (captivedns_start_raw() != 0) {
        syslog(LOG_WARNING, "wifi_prov: captive DNS start failed (continuing without it)");
    }

    /* Spawn the provisioning HTTP server */
    BaseType_t rc = xTaskCreate(wifi_prov_http_task, "wifi_prov_http",
                                PROV_HTTP_TASK_STACK, NULL,
                                PROV_HTTP_TASK_PRIO, &prov_http_task_handle);
    if (rc != pdPASS) {
        captivedns_stop();
        return driver_error(NET_DRIVER, NET_ERR_NOT_AVAILABLE,
                            "could not create provisioning HTTP task");
    }

    return NULL;
}

void wifi_prov_stop(void) {
    captivedns_stop();
    wifi_stop();
    prov_http_task_handle = NULL;
}

#endif /* CONFIG_LUA_RTOS_WIFI_PROV */
