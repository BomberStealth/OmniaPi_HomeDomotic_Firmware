/**
 * OmniaPi Gateway - Captive Portal
 * Web server for WiFi configuration in AP mode
 */

#include "captive_portal.h"
#include "wifi_manager.h"
#include <string.h>
#include <stdlib.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "cJSON.h"

static const char *TAG = "captive_portal";

// HTTP server handle
static httpd_handle_t s_server = NULL;
static bool s_running = false;

// ============== HTML Page ==============
static const char SETUP_HTML[] =
"<!DOCTYPE html>"
"<html>"
"<head>"
"<title>OmniaPi Setup</title>"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<meta charset=\"UTF-8\">"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
"background:linear-gradient(135deg,#1a1a2e 0%,#16213e 100%);"
"min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}"
".card{background:#0f0f23;border-radius:20px;padding:30px;width:100%;max-width:400px;"
"box-shadow:0 20px 60px rgba(0,0,0,0.5)}"
".logo{text-align:center;margin-bottom:25px}"
".logo h1{color:#4ade80;font-size:28px;margin-bottom:5px}"
".logo p{color:#888;font-size:14px}"
".form-group{margin-bottom:20px}"
"label{display:block;color:#ccc;font-size:14px;margin-bottom:8px}"
"input,select{width:100%;padding:14px 16px;border:2px solid #2a2a4a;border-radius:12px;"
"background:#1a1a3e;color:#fff;font-size:16px;outline:none;transition:border-color 0.3s}"
"input:focus,select:focus{border-color:#4ade80}"
"select{cursor:pointer;appearance:none;background-image:url(\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' fill='%23888' viewBox='0 0 16 16'%3E%3Cpath d='M8 11L3 6h10l-5 5z'/%3E%3C/svg%3E\");"
"background-repeat:no-repeat;background-position:right 16px center}"
"button{width:100%;padding:16px;border:none;border-radius:12px;background:#4ade80;color:#000;"
"font-size:16px;font-weight:600;cursor:pointer;transition:all 0.3s}"
"button:hover{background:#22c55e;transform:translateY(-2px)}"
"button:disabled{background:#555;cursor:not-allowed;transform:none}"
".status{text-align:center;margin-top:20px;padding:15px;border-radius:12px;display:none}"
".status.error{display:block;background:#ef444420;color:#ef4444;border:1px solid #ef444440}"
".status.success{display:block;background:#4ade8020;color:#4ade80;border:1px solid #4ade8040}"
".networks{max-height:200px;overflow-y:auto;margin-bottom:20px}"
".network{padding:12px 16px;border:2px solid #2a2a4a;border-radius:10px;margin-bottom:8px;"
"cursor:pointer;display:flex;justify-content:space-between;align-items:center;transition:all 0.2s}"
".network:hover{border-color:#4ade80;background:#1a1a3e}"
".network.selected{border-color:#4ade80;background:#4ade8020}"
".network-name{color:#fff;font-size:14px}"
".network-rssi{color:#888;font-size:12px}"
".scan-btn{background:#2a2a4a;margin-bottom:20px}"
".scan-btn:hover{background:#3a3a5a}"
".loading{display:inline-block;width:20px;height:20px;border:3px solid #fff;border-radius:50%;"
"border-top-color:transparent;animation:spin 1s linear infinite}"
"@keyframes spin{to{transform:rotate(360deg)}}"
"</style>"
"</head>"
"<body>"
"<div class=\"card\">"
"<div class=\"logo\">"
"<h1>OmniaPi</h1>"
"<p>Gateway WiFi Setup</p>"
"</div>"
"<form id=\"form\" method=\"POST\" action=\"/configure\">"
"<button type=\"button\" class=\"scan-btn\" onclick=\"scanNetworks()\">Cerca Reti WiFi</button>"
"<div id=\"networks\" class=\"networks\"></div>"
"<div class=\"form-group\">"
"<label>Nome Rete WiFi (SSID)</label>"
"<input type=\"text\" name=\"ssid\" id=\"ssid\" required maxlength=\"32\" placeholder=\"Inserisci SSID\">"
"</div>"
"<div class=\"form-group\">"
"<label>Password</label>"
"<input type=\"password\" name=\"password\" id=\"password\" maxlength=\"64\" placeholder=\"Lascia vuoto se rete aperta\">"
"</div>"
"<button type=\"submit\" id=\"submitBtn\">Connetti</button>"
"<div id=\"status\" class=\"status\"></div>"
"</form>"
"</div>"
"<script>"
"function scanNetworks(){"
"var btn=event.target;btn.disabled=true;btn.innerHTML='<span class=\"loading\"></span> Scansione...';"
"fetch('/scan').then(r=>r.json()).then(data=>{"
"var html='';data.networks.forEach(n=>{"
"html+='<div class=\"network\" onclick=\"selectNetwork(\\''+n.ssid+'\\')\">'"
"+'<span class=\"network-name\">'+n.ssid+'</span>'"
"+'<span class=\"network-rssi\">'+n.rssi+' dBm</span></div>';"
"});document.getElementById('networks').innerHTML=html||'<p style=\"color:#888;text-align:center\">Nessuna rete trovata</p>';"
"btn.disabled=false;btn.textContent='Cerca Reti WiFi';"
"}).catch(e=>{btn.disabled=false;btn.textContent='Cerca Reti WiFi';});"
"}"
"function selectNetwork(ssid){"
"document.getElementById('ssid').value=ssid;"
"document.querySelectorAll('.network').forEach(n=>n.classList.remove('selected'));"
"event.target.closest('.network').classList.add('selected');"
"}"
"document.getElementById('form').onsubmit=function(e){"
"e.preventDefault();var btn=document.getElementById('submitBtn');"
"btn.disabled=true;btn.innerHTML='<span class=\"loading\"></span> Connessione...';"
"var fd=new FormData(this);"
"fetch('/configure',{method:'POST',body:new URLSearchParams(fd)}).then(r=>r.json()).then(data=>{"
"var st=document.getElementById('status');"
"if(data.success){st.className='status success';st.textContent='Configurazione salvata! Il Gateway si riavviera...';"
"}else{st.className='status error';st.textContent=data.error||'Errore durante la configurazione';"
"btn.disabled=false;btn.textContent='Connetti';}"
"}).catch(e=>{var st=document.getElementById('status');st.className='status error';"
"st.textContent='Errore di connessione';btn.disabled=false;btn.textContent='Connetti';});"
"};"
"</script>"
"</body>"
"</html>";

// ============== URL Decode Helper ==============
static void url_decode(char *dst, const char *src, size_t dst_size)
{
    char a, b;
    size_t i = 0;
    while (*src && i < dst_size - 1) {
        if (*src == '%' && src[1] && src[2]) {
            a = src[1];
            b = src[2];
            if (a >= '0' && a <= '9') a -= '0';
            else if (a >= 'a' && a <= 'f') a = a - 'a' + 10;
            else if (a >= 'A' && a <= 'F') a = a - 'A' + 10;
            else { dst[i++] = *src++; continue; }

            if (b >= '0' && b <= '9') b -= '0';
            else if (b >= 'a' && b <= 'f') b = b - 'a' + 10;
            else if (b >= 'A' && b <= 'F') b = b - 'A' + 10;
            else { dst[i++] = *src++; continue; }

            dst[i++] = (char)(16 * a + b);
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

// ============== HTTP Handlers ==============

// GET / - Serve setup page
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SETUP_HTML, strlen(SETUP_HTML));
    return ESP_OK;
}

// GET /scan - Scan for WiFi networks
static esp_err_t scan_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Scanning for networks...");

    // Need to be in AP+STA mode for scanning
    esp_wifi_set_mode(WIFI_MODE_APSTA);

    wifi_ap_record_t ap_records[20];
    uint16_t ap_count = wifi_manager_scan(ap_records, 20);

    // Go back to AP only mode
    esp_wifi_set_mode(WIFI_MODE_AP);

    cJSON *json = cJSON_CreateObject();
    cJSON *networks = cJSON_CreateArray();

    for (int i = 0; i < ap_count; i++) {
        // Skip hidden networks
        if (strlen((char *)ap_records[i].ssid) == 0) continue;

        // Skip duplicates
        bool duplicate = false;
        for (int j = 0; j < i; j++) {
            if (strcmp((char *)ap_records[i].ssid, (char *)ap_records[j].ssid) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        cJSON *network = cJSON_CreateObject();
        cJSON_AddStringToObject(network, "ssid", (char *)ap_records[i].ssid);
        cJSON_AddNumberToObject(network, "rssi", ap_records[i].rssi);
        cJSON_AddNumberToObject(network, "auth", ap_records[i].authmode);
        cJSON_AddItemToArray(networks, network);
    }

    cJSON_AddItemToObject(json, "networks", networks);
    cJSON_AddNumberToObject(json, "count", cJSON_GetArraySize(networks));

    char *response = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));

    free(response);
    cJSON_Delete(json);

    return ESP_OK;
}

// POST /configure - Save WiFi credentials
static esp_err_t configure_handler(httpd_req_t *req)
{
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    content[ret] = '\0';

    ESP_LOGI(TAG, "Configure request: %s", content);

    // Parse form data (ssid=xxx&password=yyy)
    char ssid[33] = {0};
    char password[65] = {0};
    char ssid_encoded[65] = {0};
    char password_encoded[129] = {0};

    char *ssid_ptr = strstr(content, "ssid=");
    char *pass_ptr = strstr(content, "password=");

    if (ssid_ptr) {
        ssid_ptr += 5;  // Skip "ssid="
        char *end = strchr(ssid_ptr, '&');
        if (end) {
            strncpy(ssid_encoded, ssid_ptr, end - ssid_ptr);
        } else {
            strncpy(ssid_encoded, ssid_ptr, sizeof(ssid_encoded) - 1);
        }
        url_decode(ssid, ssid_encoded, sizeof(ssid));
    }

    if (pass_ptr) {
        pass_ptr += 9;  // Skip "password="
        char *end = strchr(pass_ptr, '&');
        if (end) {
            strncpy(password_encoded, pass_ptr, end - pass_ptr);
        } else {
            strncpy(password_encoded, pass_ptr, sizeof(password_encoded) - 1);
        }
        url_decode(password, password_encoded, sizeof(password));
    }

    ESP_LOGI(TAG, "SSID: %s, Password length: %d", ssid, strlen(password));

    cJSON *response = cJSON_CreateObject();

    if (strlen(ssid) == 0) {
        cJSON_AddBoolToObject(response, "success", false);
        cJSON_AddStringToObject(response, "error", "SSID richiesto");
    } else {
        // Save credentials
        esp_err_t err = wifi_manager_save_credentials(ssid, password);

        if (err == ESP_OK) {
            cJSON_AddBoolToObject(response, "success", true);
            cJSON_AddStringToObject(response, "message", "Configurazione salvata");

            // Send response before reboot
            char *resp_str = cJSON_PrintUnformatted(response);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, resp_str, strlen(resp_str));
            free(resp_str);
            cJSON_Delete(response);

            ESP_LOGI(TAG, "Configuration saved, restarting in 2 seconds...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
            return ESP_OK;
        } else {
            cJSON_AddBoolToObject(response, "success", false);
            cJSON_AddStringToObject(response, "error", "Errore salvataggio credenziali");
        }
    }

    char *resp_str = cJSON_PrintUnformatted(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, strlen(resp_str));

    free(resp_str);
    cJSON_Delete(response);

    return ESP_OK;
}

// Captive portal redirect handler - for any unknown URLs
static esp_err_t redirect_handler(httpd_req_t *req)
{
    // Redirect to root
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// Common captive portal detection URLs
static esp_err_t captive_handler(httpd_req_t *req)
{
    // For Android/Chrome captive portal detection
    if (strstr(req->uri, "generate_204") || strstr(req->uri, "gen_204")) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    // For Apple captive portal detection
    if (strstr(req->uri, "hotspot-detect") || strstr(req->uri, "library/test")) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    // Default: redirect to setup page
    return redirect_handler(req);
}

// ============== URI Definitions ==============
static const httpd_uri_t uri_root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_handler,
};

static const httpd_uri_t uri_scan = {
    .uri = "/scan",
    .method = HTTP_GET,
    .handler = scan_handler,
};

static const httpd_uri_t uri_configure = {
    .uri = "/configure",
    .method = HTTP_POST,
    .handler = configure_handler,
};

static const httpd_uri_t uri_generate_204 = {
    .uri = "/generate_204",
    .method = HTTP_GET,
    .handler = captive_handler,
};

static const httpd_uri_t uri_hotspot = {
    .uri = "/hotspot-detect.html",
    .method = HTTP_GET,
    .handler = captive_handler,
};

static const httpd_uri_t uri_captive = {
    .uri = "/captive",
    .method = HTTP_GET,
    .handler = captive_handler,
};

// ============== Public Functions ==============
esp_err_t captive_portal_start(void)
{
    if (s_running) {
        ESP_LOGW(TAG, "Already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting Captive Portal");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register URI handlers
    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_scan);
    httpd_register_uri_handler(s_server, &uri_configure);
    httpd_register_uri_handler(s_server, &uri_generate_204);
    httpd_register_uri_handler(s_server, &uri_hotspot);
    httpd_register_uri_handler(s_server, &uri_captive);

    s_running = true;

    char ap_ssid[32];
    wifi_manager_get_ap_ssid(ap_ssid, sizeof(ap_ssid));

    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "  CAPTIVE PORTAL ACTIVE");
    ESP_LOGI(TAG, "  Connect to WiFi: %s", ap_ssid);
    ESP_LOGI(TAG, "  Password: omniapi123");
    ESP_LOGI(TAG, "  Open: http://192.168.4.1");
    ESP_LOGI(TAG, "===========================================");

    return ESP_OK;
}

esp_err_t captive_portal_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }

    s_running = false;
    ESP_LOGI(TAG, "Captive Portal stopped");

    return ESP_OK;
}

bool captive_portal_is_running(void)
{
    return s_running;
}
