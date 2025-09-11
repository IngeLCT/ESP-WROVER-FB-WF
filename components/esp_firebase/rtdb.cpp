#include <iostream>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "rtdb.h"

#include "value.h"
#include "json.h"
#define RTDB_TAG "RTDB"


namespace ESPFirebase {


RTDB::RTDB(FirebaseApp* app, const char * database_url)
    : app(app), base_database_url(database_url)

{
    
}
Json::Value RTDB::getData(const char* path)
{
    
    std::string url = RTDB::base_database_url;
    url += path;
    url += ".json?auth=" + this->app->auth_token;

    this->app->setHeader("content-type", "application/json");
    http_ret_t http_ret = this->app->performRequest(url.c_str(), HTTP_METHOD_GET, "");
    if (http_ret.err == ESP_OK && http_ret.status_code == 200)
    {
        const char* begin = this->app->local_response_buffer;
        const char* end = begin + strlen(this->app->local_response_buffer);

        Json::Reader reader;
        Json::Value data;

        reader.parse(begin, end, data, false);

        ESP_LOGI(RTDB_TAG, "Data with path=%s acquired", path);
        this->app->clearHTTPBuffer();
        return data;
    }
    else
    {   
        ESP_LOGE(RTDB_TAG, "Error while getting data at path %s| esp_err_t=%d | status_code=%d", path, (int)http_ret.err, http_ret.status_code);
    ESP_LOGI(RTDB_TAG, "Token expired ? Trying refreshing auth");
    this->app->loginUserAccount(this->app->user_account);
        this->app->setHeader("content-type", "application/json");
        http_ret = this->app->performRequest(url.c_str(), HTTP_METHOD_GET, "");
        if (http_ret.err == ESP_OK && http_ret.status_code == 200)
        {
            const char* begin = this->app->local_response_buffer;
            const char* end = begin + strlen(this->app->local_response_buffer);

            Json::Reader reader;
            Json::Value data;

            reader.parse(begin, end, data, false);

            ESP_LOGI(RTDB_TAG, "Data with path=%s acquired", path);
            this->app->clearHTTPBuffer();
            return data;
        }
        else
        {
            ESP_LOGE(RTDB_TAG, "Failed to get data after refreshing token. double check account credentials or database rules");
            this->app->clearHTTPBuffer();
            return Json::Value();
        }
    }
}

esp_err_t RTDB::putData(const char* path, const char* json_str)
{
    
    std::string url = RTDB::base_database_url;
    url += path;
    url += ".json?auth=" + this->app->auth_token;
    this->app->setHeader("content-type", "application/json");
    http_ret_t http_ret = this->app->performRequest(url.c_str(), HTTP_METHOD_PUT, json_str);
    if (!(http_ret.err == ESP_OK && http_ret.status_code == 200) && http_ret.status_code == 401) {
        ESP_LOGW(RTDB_TAG, "PUT 401 -> intentando refresh auth");
        this->app->forceRefreshAuth();
        url = RTDB::base_database_url; url += path; url += ".json?auth=" + this->app->auth_token;
        this->app->setHeader("content-type", "application/json");
        http_ret = this->app->performRequest(url.c_str(), HTTP_METHOD_PUT, json_str);
    }
    this->app->clearHTTPBuffer();
    if (http_ret.err == ESP_OK && http_ret.status_code == 200) {
        ESP_LOGI(RTDB_TAG, "PUT successful");
        return ESP_OK;
    }
    ESP_LOGE(RTDB_TAG, "PUT failed");
    return ESP_FAIL;
}

esp_err_t RTDB::putData(const char* path, const Json::Value& data)
{
    Json::FastWriter writer;
    std::string json_str = writer.write(data);
    esp_err_t err = RTDB::putData(path, json_str.c_str());
    return err;

}

esp_err_t RTDB::postData(const char* path, const char* json_str)
{
    
    std::string url = RTDB::base_database_url;
    url += path;
    url += ".json?auth=" + this->app->auth_token;
    this->app->setHeader("content-type", "application/json");
    http_ret_t http_ret = this->app->performRequest(url.c_str(), HTTP_METHOD_POST, json_str);
    if (!(http_ret.err == ESP_OK && http_ret.status_code == 200) && http_ret.status_code == 401) {
        ESP_LOGW(RTDB_TAG, "POST 401 -> intentando refresh auth");
        this->app->forceRefreshAuth();
        url = RTDB::base_database_url; url += path; url += ".json?auth=" + this->app->auth_token;
        this->app->setHeader("content-type", "application/json");
        http_ret = this->app->performRequest(url.c_str(), HTTP_METHOD_POST, json_str);
    }
    this->app->clearHTTPBuffer();
    if (http_ret.err == ESP_OK && http_ret.status_code == 200) {
        ESP_LOGI(RTDB_TAG, "POST successful");
        return ESP_OK;
    }
    ESP_LOGE(RTDB_TAG, "POST failed");
    return ESP_FAIL;
}

esp_err_t RTDB::postData(const char* path, const Json::Value& data)
{
    Json::FastWriter writer;
    std::string json_str = writer.write(data);
    esp_err_t err = RTDB::postData(path, json_str.c_str());
    return err;

}
esp_err_t RTDB::patchData(const char* path, const char* json_str)
{
    
    std::string url = RTDB::base_database_url;
    url += path;
    url += ".json?auth=" + this->app->auth_token;
    this->app->setHeader("content-type", "application/json");
    http_ret_t http_ret = this->app->performRequest(url.c_str(), HTTP_METHOD_PATCH, json_str);
    if (!(http_ret.err == ESP_OK && http_ret.status_code == 200) && http_ret.status_code == 401) {
        ESP_LOGW(RTDB_TAG, "PATCH 401 -> intentando refresh auth");
        this->app->forceRefreshAuth();
        url = RTDB::base_database_url; url += path; url += ".json?auth=" + this->app->auth_token;
        this->app->setHeader("content-type", "application/json");
        http_ret = this->app->performRequest(url.c_str(), HTTP_METHOD_PATCH, json_str);
    }
    this->app->clearHTTPBuffer();
    if (http_ret.err == ESP_OK && http_ret.status_code == 200) {
        ESP_LOGI(RTDB_TAG, "PATCH successful");
        return ESP_OK;
    }
    ESP_LOGE(RTDB_TAG, "PATCH failed");
    return ESP_FAIL;
}

esp_err_t RTDB::patchData(const char* path, const Json::Value& data)
{
    Json::FastWriter writer;
    std::string json_str = writer.write(data);
    esp_err_t err = RTDB::patchData(path, json_str.c_str());
    return err;
}


esp_err_t RTDB::deleteData(const char* path)
{
    // Use print=silent so RTDB does not return the entire deleted payload
    std::string url = RTDB::base_database_url;
    url += path;
    url += ".json?auth=" + this->app->auth_token + "&print=silent";
    this->app->setHeader("content-type", "application/json");
    http_ret_t http_ret = this->app->performRequest(url.c_str(), HTTP_METHOD_DELETE, "");
    if (!(http_ret.err == ESP_OK && http_ret.status_code == 200) && http_ret.status_code == 401) {
        ESP_LOGW(RTDB_TAG, "DELETE 401 -> intentando refresh auth");
        this->app->forceRefreshAuth();
        url = RTDB::base_database_url; url += path; url += ".json?auth=" + this->app->auth_token + "&print=silent";
        this->app->setHeader("content-type", "application/json");
        http_ret = this->app->performRequest(url.c_str(), HTTP_METHOD_DELETE, "");
    }
    if (http_ret.err == ESP_OK && http_ret.status_code == 200) {
        this->app->clearHTTPBuffer();
        ESP_LOGI(RTDB_TAG, "DELETE successful");
        return ESP_OK;
    }

    // Fallback: if the node is too large for a single delete, delete children in chunks
    // Detect typical 400 with message "Data to write exceeds..."
    bool maybe_too_large = (http_ret.status_code == 400);
    if (!maybe_too_large) {
        ESP_LOGE(RTDB_TAG, "DELETE failed (status=%d).", http_ret.status_code);
        this->app->clearHTTPBuffer();
        return ESP_FAIL;
    }

    ESP_LOGW(RTDB_TAG, "DELETE grande: intentando borrado por lotes (shallow)");

    // Get shallow list of children keys
    std::string shallow_url = RTDB::base_database_url;
    shallow_url += path;
    shallow_url += ".json?shallow=true&auth=" + this->app->auth_token;
    this->app->setHeader("content-type", "application/json");
    http_ret_t get_ret = this->app->performRequest(shallow_url.c_str(), HTTP_METHOD_GET, "");
    if (!(get_ret.err == ESP_OK && get_ret.status_code == 200)) {
        ESP_LOGE(RTDB_TAG, "Fallo obteniendo claves (shallow) status=%d", get_ret.status_code);
        this->app->clearHTTPBuffer();
        return ESP_FAIL;
    }

    // Parse keys-only response
    const char* begin = this->app->local_response_buffer;
    const char* end = begin + strlen(this->app->local_response_buffer);
    Json::Reader reader;
    Json::Value keys_obj;
    reader.parse(begin, end, keys_obj, false);
    this->app->clearHTTPBuffer();

    if (!keys_obj.isObject()) {
        // Nothing to delete or unexpected shape
        ESP_LOGW(RTDB_TAG, "Respuesta shallow no es objeto; reintentando DELETE directo");
        // Try direct delete once more (now possibly smaller)
        std::string retry_url = RTDB::base_database_url;
        retry_url += path;
        retry_url += ".json?auth=" + this->app->auth_token + "&print=silent";
        this->app->setHeader("content-type", "application/json");
        http_ret_t retry_ret = this->app->performRequest(retry_url.c_str(), HTTP_METHOD_DELETE, "");
        this->app->clearHTTPBuffer();
        if (retry_ret.err == ESP_OK && retry_ret.status_code == 200) return ESP_OK;
        ESP_LOGE(RTDB_TAG, "DELETE failed tras reintento");
        return ESP_FAIL;
    }

    // Iterate and delete each child key
    std::vector<std::string> keys;
    keys.reserve(keys_obj.getMemberNames().size());
    for (const auto& k : keys_obj.getMemberNames()) {
        keys.emplace_back(k);
    }

    size_t ok_count = 0;
    for (const auto& k : keys) {
        std::string child_path = std::string(path) + "/" + k;
        if (RTDB::deleteData(child_path.c_str()) == ESP_OK) {
            ok_count++;
        } else {
            ESP_LOGW(RTDB_TAG, "Fallo al borrar hijo: %s", child_path.c_str());
        }
        // Pequeña pausa para no saturar
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Try delete parent once children are gone
    std::string final_url = RTDB::base_database_url;
    final_url += path;
    final_url += ".json?auth=" + this->app->auth_token + "&print=silent";
    this->app->setHeader("content-type", "application/json");
    http_ret_t final_ret = this->app->performRequest(final_url.c_str(), HTTP_METHOD_DELETE, "");
    this->app->clearHTTPBuffer();
    if (final_ret.err == ESP_OK && final_ret.status_code == 200) {
        ESP_LOGI(RTDB_TAG, "DELETE por lotes exitoso (%u/%u)", (unsigned)ok_count, (unsigned)keys.size());
        return ESP_OK;
    }

    ESP_LOGE(RTDB_TAG, "DELETE final fallo (status=%d) tras borrar hijos (%u/%u)", final_ret.status_code, (unsigned)ok_count, (unsigned)keys.size());
    return ok_count == keys.size() ? ESP_OK : ESP_FAIL;
}

// Nota: implementación opcional; no se usa en el flujo actual
esp_err_t RTDB::trimDays(const char* root_path, int max_days)
{
    if (max_days <= 0) return ESP_OK;
    std::string url = RTDB::base_database_url;
    url += root_path;
    url += ".json?shallow=true&auth=" + this->app->auth_token;
    this->app->setHeader("content-type", "application/json");
    http_ret_t http_ret = this->app->performRequest(url.c_str(), HTTP_METHOD_GET, "");
    if (!(http_ret.err == ESP_OK && http_ret.status_code == 200)) {
        this->app->clearHTTPBuffer();
        return ESP_FAIL;
    }
    const char* begin = this->app->local_response_buffer;
    const char* end = begin + strlen(this->app->local_response_buffer);
    Json::Reader reader;
    Json::Value obj;
    reader.parse(begin, end, obj, false);
    this->app->clearHTTPBuffer();
    if (!obj.isObject()) return ESP_OK;
    std::vector<std::string> keys = obj.getMemberNames();
    if ((int)keys.size() <= max_days) return ESP_OK;
    std::sort(keys.begin(), keys.end());
    int to_delete = (int)keys.size() - max_days;
    for (int i = 0; i < to_delete; ++i) {
        std::string child = std::string(root_path) + "/" + keys[i];
        RTDB::deleteData(child.c_str());
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_OK;
}

// Borra los N elementos más antiguos bajo root_path usando orderBy=$key y PATCH
int RTDB::trimOldestBatch(const char* root_path, int batch_size)
{
    if (batch_size <= 0) return 0;
    std::string list_url = RTDB::base_database_url;
    list_url += root_path;
    list_url += ".json?orderBy=%22%24key%22&limitToFirst=" + std::to_string(batch_size) + "&auth=" + this->app->auth_token;
    this->app->setHeader("content-type", "application/json");
    http_ret_t get_ret = this->app->performRequest(list_url.c_str(), HTTP_METHOD_GET, "");
    if (!(get_ret.err == ESP_OK && get_ret.status_code == 200)) {
        ESP_LOGE(RTDB_TAG, "trimOldestBatch: GET status=%d", get_ret.status_code);
        this->app->clearHTTPBuffer();
        return -1;
    }

    const char* begin = this->app->local_response_buffer;
    const char* end = begin + strlen(this->app->local_response_buffer);
    Json::Reader reader;
    Json::Value obj;
    reader.parse(begin, end, obj, false);
    this->app->clearHTTPBuffer();
    if (!obj.isObject()) return 0;
    std::vector<std::string> keys = obj.getMemberNames();
    if (keys.empty()) return 0;

    std::string patch_body;
    patch_body.reserve(1024);
    patch_body += "{";
    for (size_t i = 0; i < keys.size(); ++i) {
        if (i) patch_body += ",";
        patch_body += "\""; patch_body += keys[i]; patch_body += "\":null";
    }
    patch_body += "}";

    std::string patch_url = RTDB::base_database_url;
    patch_url += root_path;
    patch_url += ".json?auth=" + this->app->auth_token + "&print=silent";
    this->app->setHeader("content-type", "application/json");
    http_ret_t patch_ret = this->app->performRequest(patch_url.c_str(), HTTP_METHOD_PATCH, patch_body);
    if (!(patch_ret.err == ESP_OK && patch_ret.status_code >= 200 && patch_ret.status_code < 300)) {
        ESP_LOGE(RTDB_TAG, "trimOldestBatch: PATCH status=%d", patch_ret.status_code);
        this->app->clearHTTPBuffer();
        return -2;
    }
    this->app->clearHTTPBuffer();
    ESP_LOGI(RTDB_TAG, "trimOldestBatch: borrados %u", (unsigned)keys.size());
    return (int)keys.size();
}


}
