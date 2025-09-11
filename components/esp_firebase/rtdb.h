#ifndef _ESP_FIREBASE_RTDB_H_
#define  _ESP_FIREBASE_RTDB_H_
#include "app.h"


#include "value.h"
#include "json.h"

namespace ESPFirebase 
{

    
    
    class RTDB
    {
    private:
        FirebaseApp* app;
        std::string base_database_url;


    public:
                
        Json::Value getData(const char* path);

        esp_err_t putData(const char* path, const char* json_str);
        esp_err_t putData(const char* path, const Json::Value& data);

        esp_err_t postData(const char* path, const char* json_str);
        esp_err_t postData(const char* path, const Json::Value& data);

        esp_err_t patchData(const char* path, const char* json_str);
        esp_err_t patchData(const char* path, const Json::Value& data);
        
        esp_err_t deleteData(const char* path);
        // Borra subrutas por día para mantener máximo de días (keys lex ordenadas)
        esp_err_t trimDays(const char* root_path, int max_days);
        // Borra los N más antiguos en un nodo con push-keys (sin cambiar estructura)
        // Devuelve cantidad borrada o <0 en error
        int trimOldestBatch(const char* root_path, int batch_size);
        RTDB(FirebaseApp* app, const char* database_url);
    };




}


#endif
