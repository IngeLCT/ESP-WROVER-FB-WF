// Geoapify.c  -> Unwired Labs WiFi positioning (10 APs, bssid+channel+frequency)
//
// Cambios solicitados:
//  - Limitar a 10 redes a enviar (top por RSSI).
//  - Cada AP solo incluye: "bssid", "channel", "frequency".
//  - Se mantiene "considerIp": true.
//  - Buffers grandes estáticos (no en pila) para evitar overflow.
//
// Requiere en Privado.h:  #define UNWIRED_TOKEN "tu_token"

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sensors.h"     // sensors_set_city_state(...)
#include "Privado.h"     // UNWIRED_TOKEN

#ifndef UNWIRED_URL
#define UNWIRED_URL "https://us1.unwiredlabs.com/v2/process.php"
#endif

#define GEO_TAG            "UnwiredLabs"
#define WIFI_SCAN_MAX      32
#define AP_SEND_MAX        10   // <- límite solicitado
#define REQ_MAX            2048
#define RESP_MAX           2048
#define HTTP_TIMEOUT_MS    10000

// ---------------- Buffers estáticos (evita pila) ----------------
static char g_req_body[REQ_MAX];     // JSON request
typedef struct {
    char buf[RESP_MAX];
    int  len;
} geo_accum_t;
static geo_accum_t g_acc;            // acumulador de respuesta

// ---------------- Utilidades de log/parse ----------------
static void log_json_chunks(const char *buf, int len) {
    const int chunk = 384;
    for (int i = 0; i < len; i += chunk) {
        int n = (i + chunk <= len) ? chunk : (len - i);
        ESP_LOGI(GEO_TAG, "Resp(%d..%d): %.*s", i, i + n, n, buf + i);
    }
}

static bool sniff_kv(const char *json, const char *key, char *out, size_t outlen) {
    const char *p = strstr(json, key);
    if (!p) return false;
    p = strchr(p, ':'); if (!p) return false; p++;
    while (*p == ' ' || *p == '"') p++;
    const char *start = p;
    while (*p && *p != '"' && *p != ',' && *p != '}' && *p != '\n' && *p != '\r') p++;
    size_t n = (size_t)(p - start);
    if (n >= outlen) n = outlen - 1;
    memcpy(out, start, n);
    out[n] = 0;
    return true;
}

static bool parse_unwired_response(const char *json, double *lat, double *lon, int *acc) {
    const char *p; char *endp;
    p = strstr(json, "\"lat\""); if (!p) return false;
    p = strchr(p, ':'); if (!p) return false; p++;
    double vlat = strtod(p, &endp); if (p == endp) return false;

    p = strstr(endp, "\"lon\""); if (!p) return false;
    p = strchr(p, ':'); if (!p) return false; p++;
    double vlon = strtod(p, &endp); if (p == endp) return false;

    int vacc = -1;
    p = strstr(endp, "\"accuracy\"");
    if (p) { p = strchr(p, ':'); if (p) { p++; vacc = (int)strtol(p, &endp, 10); } }

    if (lat) *lat = vlat;
    if (lon) *lon = vlon;
    if (acc) *acc = vacc;
    return true;
}

// ---------------- WiFi helpers ----------------
static int channel_to_freq_mhz(int ch) {
    if (ch >= 1 && ch <= 14) return 2407 + 5 * ch;    // 1->2412 ... 14->2484
    if (ch >= 32 && ch <= 196) return 5000 + 5 * ch;  // 36->5180 etc.
    return 0;
}
static int cmp_rssi_desc(const void *a, const void *b) {
    const wifi_ap_record_t *x = (const wifi_ap_record_t*)a;
    const wifi_ap_record_t *y = (const wifi_ap_record_t*)b;
    return (y->rssi - x->rssi);
}
static void mac_to_lower_colon(char *dst, size_t dstlen, const uint8_t mac[6]) {
    snprintf(dst, dstlen, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ---------------- HTTP event ----------------
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    geo_accum_t *acc = (geo_accum_t*)evt->user_data;
    if (!acc) return ESP_OK;
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data && evt->data_len > 0 && acc->len < (int)sizeof(acc->buf)) {
                int can = sizeof(acc->buf) - acc->len - 1; if (can < 0) can = 0;
                int n = (evt->data_len < can) ? evt->data_len : can;
                if (n > 0) {
                    memcpy(acc->buf + acc->len, evt->data, n);
                    acc->len += n;
                    acc->buf[acc->len] = 0;
                }
            }
            break;
        default: break;
    }
    return ESP_OK;
}

// ---------------- Escaneo y request ----------------
static int scan_wifi(wifi_ap_record_t *out, int cap) {
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_NULL) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    wifi_scan_config_t cfg = {
        .ssid = 0, .bssid = 0, .channel = 0, .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 50, .scan_time.active.max = 120,
    };

    ESP_LOGI(GEO_TAG, "Iniciando escaneo WiFi para Unwired Labs...");
    ESP_ERROR_CHECK(esp_wifi_scan_start(&cfg, true)); // bloqueante
    uint16_t n = cap;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&n, out));
    ESP_LOGI(GEO_TAG, "APs encontrados: %u", (unsigned)n);
    return (int)n;
}

static int build_unwired_wifi_body_inplace(wifi_ap_record_t *aps, int n,
                                           const char *token,
                                           char *out, size_t outlen)
{
    if (n > WIFI_SCAN_MAX) n = WIFI_SCAN_MAX;

    // Ordena por RSSI descendente y limita a 10
    qsort(aps, n, sizeof(wifi_ap_record_t), cmp_rssi_desc);
    if (n > AP_SEND_MAX) n = AP_SEND_MAX;

    // Si hay <2 APs válidos, UL no puede posicionar por WiFi. (No forzamos IP aquí.)
    if (n < 2) {
        return snprintf(out, outlen,
                        "{"
                          "\"token\":\"%s\","
                          "\"address\":1,"
                          "\"fallbacks\":{\"ipf\":1}"
                        "}", token);
    }

    int wr = 0;
    wr += snprintf(out + wr, outlen - wr,
                   "{"
                     "\"token\":\"%s\","
                     "\"address\":2,"
                     "\"fallbacks\":{\"ipf\":1},"
                     "\"wifi\":[",
                   token);

    for (int i = 0; i < n && wr < (int)outlen; ++i) {
        char bssid[20]; mac_to_lower_colon(bssid, sizeof(bssid), aps[i].bssid);
        int ch   = aps[i].primary;
        int freq = channel_to_freq_mhz(ch);

        wr += snprintf(out + wr, outlen - wr,
                       "%s{\"bssid\":\"%s\"",
                       (i==0?"":","), bssid);
        if (ch > 0)   wr += snprintf(out + wr, outlen - wr, ",\"channel\":%d", ch);
        if (freq > 0) wr += snprintf(out + wr, outlen - wr, ",\"frequency\":%d", freq);
        wr += snprintf(out + wr, outlen - wr, "}");
    }

    if (wr < (int)outlen) wr += snprintf(out + wr, outlen - wr, "]}");
    return wr;
}

static bool parse_address_detail_city_state(const char *json,
                                            char *city, size_t city_sz,
                                            char *state, size_t state_sz)
{
    const char *p = strstr(json, "\"address_detail\"");
    if (!p) return false;
    p = strchr(p, '{');                 // abre del objeto
    if (!p) return false;
    const char *obj = ++p;              // inicio del contenido
    int depth = 1;
    while (*p && depth > 0) {
        if (*p == '{') depth++;
        else if (*p == '}') depth--;
        p++;
    }
    if (depth != 0) return false;       // objeto malformado
    size_t len = (size_t)((p - 1) - obj);
    if (len == 0) return false;

    // Trabajamos sobre un buffer temporal limitado
    char tmp[256];
    if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
    memcpy(tmp, obj, len); tmp[len] = 0;

    bool ok = false;
    if (city && city_sz)  ok |= sniff_kv(tmp, "\"city\"",  city,  city_sz);
    if (state && state_sz) ok |= sniff_kv(tmp, "\"state\"", state, state_sz);
    return ok;
}

static void make_city_state_hyphen(const char *city, const char *state,
                                   char *out, size_t outlen)
{
    if (!out || outlen == 0) return;
    out[0] = '\0';

    if (city && city[0]) {
        strlcpy(out, city, outlen);
    }
    if (state && state[0]) {
        if (out[0]) strlcat(out, "-", outlen);
        strlcat(out, state, outlen);
    }
    if (out[0] == '\0') {
        strlcpy(out, "SinCiudad", outlen);
    }
}

// Punto de entrada público
void geoapify_fetch_once_wifi_unwired(void) {
#ifndef UNWIREDLABS_TOKEN
    #error "Define UNWIRED_TOKEN en Privado.h"
#endif
    const char *token = UNWIREDLABS_TOKEN;

    // Debounce simple: ignora llamadas si ocurrió otra en < 3 s
    static int64_t s_last_call_us = 0;
    int64_t now_us = esp_timer_get_time();
    if (now_us - s_last_call_us < 3000000) {  // 3 s
        ESP_LOGW(GEO_TAG, "Llamada ignorada (debounce)");
        return;
    }
    s_last_call_us = now_us;

    // 1) Escaneo
    wifi_ap_record_t aps[WIFI_SCAN_MAX] = {0};
    int ap_count = scan_wifi(aps, WIFI_SCAN_MAX);

    // 2) Construir body (máx. 10 APs, solo bssid+channel+frequency)
    int body_len = build_unwired_wifi_body_inplace(aps, ap_count, token,
                                               g_req_body, sizeof(g_req_body));
    ESP_LOGI(GEO_TAG, "Request JSON: %.*s", body_len, g_req_body);

    if (body_len <= 0) {
        ESP_LOGW(GEO_TAG, "No se pudo construir el body UL");
        sensors_set_city_state("Unwired-BadBody");
        return;
    }
    ESP_LOGI(GEO_TAG, "Request JSON: %.*s", body_len, g_req_body);

    // 3) HTTP client
    memset(&g_acc, 0, sizeof(g_acc));

    esp_http_client_config_t cfg = {
        .url = UNWIRED_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &g_acc,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGW(GEO_TAG, "No se pudo crear HTTP client");
        sensors_set_city_state("Unwired-NoClient");
        return;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, g_req_body, body_len);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(GEO_TAG, "HTTP status=%d, response len=%d", status, g_acc.len);

        if (g_acc.len > 0) {
            ESP_LOGI(GEO_TAG, "Resp cruda (len=%d):", g_acc.len);
            log_json_chunks(g_acc.buf, g_acc.len);

            char fb[16] = {0};
            if (sniff_kv(g_acc.buf, "\"fallback\"", fb, sizeof(fb))) {
                ESP_LOGW(GEO_TAG, "Usando fallback=%s", fb);  // "ipf" = IP fallback
            } else {
                ESP_LOGI(GEO_TAG, "Posición por WiFi (sin fallback)");
            }

            char st[16] = {0}, msg[160] = {0};
            if (sniff_kv(g_acc.buf, "\"status\"", st, sizeof(st))) ESP_LOGI(GEO_TAG, "status=%s", st);
            if (sniff_kv(g_acc.buf, "\"message\"", msg, sizeof(msg))) ESP_LOGW(GEO_TAG, "message=%s", msg);

            double lat=0, lon=0; int accm=-1;
            char city[48] = {0}, state[48] = {0};
            char ciudad[112] = {0};  // 48 + 1 + 48 cabe sin warnings

            bool have_addr = parse_address_detail_city_state(g_acc.buf,
                                                            city, sizeof(city),
                                                            state, sizeof(state));
            if (have_addr && (city[0] || state[0])) {
                make_city_state_hyphen(city, state, ciudad, sizeof(ciudad));
                sensors_set_city_state(ciudad);
                ESP_LOGI(GEO_TAG, "Ciudad (city-state) = %s", ciudad);
            } else {
                // respaldo: lat/lon si no vino address_detail
                char coords[64];
                if (accm >= 0) snprintf(coords, sizeof(coords), "%.6f,%.6f (acc=%dm)", lat, lon, accm);
                else           snprintf(coords, sizeof(coords), "%.6f,%.6f", lat, lon);
                sensors_set_city_state(coords);
                ESP_LOGW(GEO_TAG, "Sin address_detail: usando coords %s", coords);
            }
        } else {
            ESP_LOGW(GEO_TAG, "Respuesta vacía");
            sensors_set_city_state("Unwired-EmptyResp");
        }
    } else {
        ESP_LOGW(GEO_TAG, "esp_http_client_perform fallo: %s", esp_err_to_name(err));
        sensors_set_city_state("Unwired-HTTPfail");
    }

    esp_http_client_cleanup(client);
}
