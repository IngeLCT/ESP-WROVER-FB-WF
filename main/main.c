// Core
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <string.h>

// ESP-IDF
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <netdb.h>
#include <sys/socket.h>

// Project
#include "sensors.h"
#include "firebase.h"
#include "wifi.h"
#include "Privado.h"

// I2C and sensor details are moved to sensors.c

static const char *TAG = "ESP-WROVER-FB";
// Geoapify ahora separado en Geoapify.c
void geoapify_fetch_once(void);
#define SENSOR_TASK_STACK 10240
// Activa logs verbosos del cliente HTTP/TLS (cambiar a 0 para desactivar)
#define ENABLE_HTTP_VERBOSE 1
// Habilita/deshabilita log detallado de cada muestra
#define LOG_EACH_SAMPLE 1

static void init_sntp_and_time(void) {
    
    // Configurar NTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_init();

    // Zona horaria UTC-6 (sin DST). Para México/CST puede usar "CST6CDT,M3.2.0,M11.1.0" si desea DST.
    setenv("TZ", "UTC6", 1);
    tzset();

    // Esperar sincronización de hora (hasta ~10s)
    for (int i = 0; i < 100; ++i) {
        time_t now = 0;
        time(&now);
        if (now > 1609459200) { // > 2021-01-01 como umbral
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

}

// ----------- MAIN TASK -----------
void sensor_task(void *pv) {
    SensorData data;
    int64_t start_time = esp_timer_get_time(); // aún disponible si se quisiera calcular duración, ya no se usa para JSON

    // Guardar la hora de inicio en formato HH:MM:SS
    time_t start_epoch;
    struct tm start_tm_info;
    char inicio_str[20];
    time(&start_epoch);
    localtime_r(&start_epoch, &start_tm_info);
    strftime(inicio_str, sizeof(inicio_str), "%H:%M:%S", &start_tm_info);

    bool first_send = true;
    const uint32_t TOKEN_REFRESH_INTERVAL_SEC = 50 * 60; // 50 minutos
    time_t last_token_refresh = time(NULL);
    // Ejecutar petición Geoapify aquí (stack grande de esta tarea) en lugar de app_main
    geoapify_fetch_once();

    // Inicializar Firebase DESPUES de Geoapify según orden solicitado
    if (firebase_init() != 0) {
        ESP_LOGE(TAG, "Error inicializando Firebase");
        vTaskDelete(NULL);
    }
    // Borrar historial antes del primer push/put
    // Wipe historial (la librería ya loguea resultado)
    firebase_delete("/historial_mediciones");
    // --- Nuevo esquema: muestrear cada 60s, enviar promedio cada 30 muestras (≈30 min) ---
    const int SAMPLES_PER_BATCH = 30;           // DEBUG: 30 muestras -> 30 minutos (antes 30)
    const TickType_t SAMPLE_DELAY_TICKS = pdMS_TO_TICKS(60000); // 60 segundos
    int sample_count = 0;
    // Acumuladores
    double sum_pm1p0=0, sum_pm2p5=0, sum_pm4p0=0, sum_pm10p0=0, sum_voc=0, sum_nox=0, sum_avg_temp=0, sum_avg_hum=0; 
    uint32_t sum_co2 = 0;

    char last_fecha_str[20] = ""; // para detectar cambio de día en envíos reducidos
    while (1) {
        // Leer sensor (si falla, no incrementa contador, reintenta próximo minuto)
        if (sensors_read(&data) == ESP_OK) {
            sample_count++;
            sum_pm1p0 += data.pm1p0;
            sum_pm2p5 += data.pm2p5;
            sum_pm4p0 += data.pm4p0;
            sum_pm10p0 += data.pm10p0;
            sum_voc += data.voc;
            sum_nox += data.nox;
            sum_avg_temp += data.avg_temp;
            sum_avg_hum += data.avg_hum;
            sum_co2 += data.co2;
#if LOG_EACH_SAMPLE
            ESP_LOGI(TAG,
                     "Muestra %d/%d: PM1.0=%.2f PM2.5=%.2f PM4.0=%.2f PM10=%.2f VOC=%.1f NOx=%.1f CO2=%u Temp=%.2fC Hum=%.2f%%", \
                     sample_count, SAMPLES_PER_BATCH, data.pm1p0, data.pm2p5, data.pm4p0, data.pm10p0, data.voc, data.nox, data.co2, data.avg_temp, data.avg_hum);
#endif
        } else {
            ESP_LOGW(TAG, "Error leyendo sensores (batch %d)", sample_count);
        }

        // Refresh token (independiente de envío) cada iteración verifica tiempo
        time_t now_epoch_check = time(NULL);
        if ((now_epoch_check - last_token_refresh) >= TOKEN_REFRESH_INTERVAL_SEC) {
            ESP_LOGI(TAG, "Refrescando token (intervalo periódico 50m)...");
            int r = firebase_refresh_token();
            if (r == 0) ESP_LOGI(TAG, "Token refresh OK"); else ESP_LOGW(TAG, "Fallo al refrescar token (%d)", r);
            last_token_refresh = now_epoch_check;
        }

        if (sample_count >= SAMPLES_PER_BATCH) {
            // Obtener hora y fecha actuales (hora de envío real)
            time_t now_epoch;
            struct tm tm_info;
            time(&now_epoch);
            localtime_r(&now_epoch, &tm_info);
            char hora_envio[16];
            strftime(hora_envio, sizeof(hora_envio), "%H:%M:%S", &tm_info);
            char fecha_actual[20];
            strftime(fecha_actual, sizeof(fecha_actual), "%d-%m-%y", &tm_info);

            // Construir datos promedio
            SensorData avg = {0};
            double denom = (double)sample_count;
            avg.pm1p0 = (float)(sum_pm1p0 / denom);
            avg.pm2p5 = (float)(sum_pm2p5 / denom);
            avg.pm4p0 = (float)(sum_pm4p0 / denom);
            avg.pm10p0 = (float)(sum_pm10p0 / denom);
            avg.voc = (float)(sum_voc / denom);
            avg.nox = (float)(sum_nox / denom);
            avg.avg_temp = (float)(sum_avg_temp / denom);
            avg.avg_hum  = (float)(sum_avg_hum / denom);
            avg.co2 = (uint16_t)(sum_co2 / sample_count);
            // Para consistencia rellenamos derivadas que no usamos directamente
            avg.scd_temp = avg.avg_temp; // aproximación
            avg.scd_hum = avg.avg_hum;
            avg.sen_temp = avg.avg_temp;
            avg.sen_hum = avg.avg_hum;

            char json[384];
            if (first_send) {
                sensors_format_json(&avg, hora_envio, fecha_actual, inicio_str, json, sizeof(json));
                strncpy(last_fecha_str, fecha_actual, sizeof(last_fecha_str)-1);
                last_fecha_str[sizeof(last_fecha_str)-1] = '\0';
                first_send = false;
            } else {
                if (strncmp(last_fecha_str, fecha_actual, sizeof(last_fecha_str)) != 0) {
                    // Día cambió: incluir fecha de nuevo
                    snprintf(json, sizeof(json), "{\"pm1p0\":%.2f,\"pm2p5\":%.2f,\"pm4p0\":%.2f,\"pm10p0\":%.2f,\"voc\":%.1f,\"nox\":%.1f,\"cTe\":%.2f,\"cHu\":%.2f,\"co2\":%u,\"fecha\":\"%s\",\"hora\":\"%s\"}",
                        avg.pm1p0, avg.pm2p5, avg.pm4p0, avg.pm10p0, avg.voc, avg.nox, avg.avg_temp, avg.avg_hum, avg.co2, fecha_actual, hora_envio);
                    strncpy(last_fecha_str, fecha_actual, sizeof(last_fecha_str)-1);
                    last_fecha_str[sizeof(last_fecha_str)-1] = '\0';
                } else {
                    // Mismo día: sólo hora
                    snprintf(json, sizeof(json), "{\"pm1p0\":%.2f,\"pm2p5\":%.2f,\"pm4p0\":%.2f,\"pm10p0\":%.2f,\"voc\":%.1f,\"nox\":%.1f,\"cTe\":%.2f,\"cHu\":%.2f,\"co2\":%u,\"hora\":\"%s\"}",
                        avg.pm1p0, avg.pm2p5, avg.pm4p0, avg.pm10p0, avg.voc, avg.nox, avg.avg_temp, avg.avg_hum, avg.co2, hora_envio);
                }
            }
            ESP_LOGI(TAG, "JSON promedio 30m: %s", json);
            firebase_push("/historial_mediciones", json);

            // Reset acumuladores para el siguiente bloque
            sample_count = 0;
            sum_pm1p0=sum_pm2p5=sum_pm4p0=sum_pm10p0=sum_voc=sum_nox=sum_avg_temp=sum_avg_hum=0;
            sum_co2 = 0;
        }

        vTaskDelay(SAMPLE_DELAY_TICKS);
    }
}

// ----------- APP MAIN -----------
void app_main(void) {

#if ENABLE_HTTP_VERBOSE
    // Subir nivel de log ANTES de iniciar operaciones HTTP
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);    // capa TLS
    esp_log_level_set("tls", ESP_LOG_VERBOSE);        // a veces usa esta etiqueta
    esp_log_level_set("FirebaseApp", ESP_LOG_VERBOSE);
    esp_log_level_set("RTDB", ESP_LOG_VERBOSE);
#endif

    // Conectar Wi-Fi (bloqueante hasta conectar)
    if (wifi_connect(SSID, PASSWORD, 20000) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo conectar al Wi-Fi");
        return;
    }
    // Sincronizar hora por NTP y zona horaria -6
    init_sntp_and_time();
    vTaskDelay(pdMS_TO_TICKS(1000));
    char strftime_buf[64];
    time_t now;
    struct tm timeinfo;
    time(&now); // corregir uso de now sin inicializar
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);
    ESP_LOGI(TAG, "Inicializando sensores...");
    esp_err_t ret = sensors_init_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al inicializar sensores: %s", esp_err_to_name(ret));
        return;
    }
    // Crear tarea principal de sensores y Firebase
    xTaskCreate(sensor_task, "sensor_task", SENSOR_TASK_STACK, NULL, 5, NULL);
}
