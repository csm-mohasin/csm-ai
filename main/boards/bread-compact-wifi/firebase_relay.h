#ifndef FIREBASE_RELAY_H
#define FIREBASE_RELAY_H

// Voice thekey Firebase Realtime Database-er /relayControl update korar MCP tool.
// AI ei tool gulo dakbe: self.relay.set, self.relay.set_all, self.relay.get_status

#include <cstdlib>
#include <expected>
#include <memory>
#include <mutex>
#include <string>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "board.h"
#include "cjson_utils.h"
#include "firebase_config.h"
#include "mcp_server.h"

class FirebaseRelay {
private:
    static constexpr const char* TAG = "FirebaseRelay";

    std::mutex mutex_;
    std::string id_token_;
    int64_t token_expire_us_ = 0;

    // HTTP request pathay. 2xx hole true. Response body response-e.
    bool Request(const std::string& method, const std::string& url, const std::string& body,
                 std::string& response) {
        auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
        http->SetHeader("Content-Type", "application/json");
        http->SetContent(std::string(body));

        if (auto opened = http->Open(method, url); !opened) {
            ESP_LOGE(TAG, "Open failed (%s): %s", method.c_str(), opened.error().ToString().c_str());
            return false;
        }
        auto status = http->GetStatusCode();
        if (!status) {
            ESP_LOGE(TAG, "Status read failed: %s", status.error().ToString().c_str());
            http->Close();
            return false;
        }
        response = http->ReadAll();
        http->Close();
        if (*status < 200 || *status >= 300) {
            ESP_LOGE(TAG, "%s failed, status %d", method.c_str(), *status);
            return false;
        }
        return true;
    }

    // Email/password diye sign-in kore idToken ney, ghonta-r moddhe cache kore.
    bool EnsureToken() {
        if (!id_token_.empty() && esp_timer_get_time() < token_expire_us_ - 60LL * 1000000LL) {
            return true;
        }
        std::string url =
            std::string("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") +
            FIREBASE_API_KEY;

        CJsonUniquePtr req(cJSON_CreateObject());
        if (!req) {
            return false;
        }
        cJSON_AddStringToObject(req.get(), "email", FIREBASE_EMAIL);
        cJSON_AddStringToObject(req.get(), "password", FIREBASE_PASSWORD);
        cJSON_AddBoolToObject(req.get(), "returnSecureToken", true);
        CJsonStringUniquePtr body(cJSON_PrintUnformatted(req.get()));
        if (!body) {
            return false;
        }

        std::string resp;
        if (!Request("POST", url, body.get(), resp)) {
            ESP_LOGE(TAG, "Firebase sign-in failed");
            return false;
        }
        CJsonUniquePtr root(cJSON_Parse(resp.c_str()));
        if (!root) {
            return false;
        }
        cJSON* token = cJSON_GetObjectItem(root.get(), "idToken");
        cJSON* expires = cJSON_GetObjectItem(root.get(), "expiresIn");
        if (!cJSON_IsString(token) || token->valuestring == nullptr) {
            ESP_LOGE(TAG, "No idToken in sign-in response");
            return false;
        }
        int seconds = 3600;
        if (cJSON_IsString(expires) && expires->valuestring != nullptr) {
            seconds = std::atoi(expires->valuestring);
            if (seconds <= 0) {
                seconds = 3600;
            }
        }
        id_token_ = token->valuestring;
        token_expire_us_ = esp_timer_get_time() + (int64_t)seconds * 1000000LL;
        ESP_LOGI(TAG, "Firebase sign-in ok");
        return true;
    }

    std::string DbUrl(const std::string& path) {
        return std::string(FIREBASE_DB_URL) + "/" + path + ".json?auth=" + id_token_;
    }

    // Value likhe (PUT). PUT na choley POST + x-http-method-override try kore.
    bool DbPut(const std::string& path, const std::string& json) {
        if (!EnsureToken()) {
            return false;
        }
        std::string resp;
        std::string url = DbUrl(path);
        if (Request("PUT", url, json, resp)) {
            return true;
        }
        if (Request("POST", url + "&x-http-method-override=PUT", json, resp)) {
            return true;
        }
        id_token_.clear();  // token expired hote pare, porer bar abar sign-in hobe
        return false;
    }

    bool DbGet(const std::string& path, std::string& out) {
        if (!EnsureToken()) {
            return false;
        }
        if (Request("GET", DbUrl(path), "", out)) {
            return true;
        }
        id_token_.clear();
        return false;
    }

    bool SetRelay(int relay, bool on) {
        return DbPut("relayControl/relay" + std::to_string(relay), on ? "true" : "false");
    }

public:
    FirebaseRelay() {
        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool(
            "self.relay.set",
            "Turn one home appliance relay ON or OFF. Use this when the user asks to switch a "
            "light, fan or other device on or off.\n"
            "Args:\n"
            "  `relay`: relay number, 1 to 4.\n"
            "  `on`: true to turn ON, false to turn OFF.",
            PropertyList({Property("relay", kPropertyTypeInteger, 1, 4),
                          Property("on", kPropertyTypeBoolean)}),
            [this](const PropertyList& properties) -> ToolResult {
                std::lock_guard<std::mutex> lock(mutex_);
                int relay = properties["relay"].value<int>();
                bool on = properties["on"].value<bool>();
                if (!SetRelay(relay, on)) {
                    return std::unexpected(std::string("Failed to update Firebase"));
                }
                return true;
            });

        mcp_server.AddTool(
            "self.relay.set_all",
            "Turn ALL four relays ON or OFF at once. Use this when the user says things like "
            "'turn everything on' or 'turn everything off'.\n"
            "Args:\n"
            "  `on`: true to turn all ON, false to turn all OFF.",
            PropertyList({Property("on", kPropertyTypeBoolean)}),
            [this](const PropertyList& properties) -> ToolResult {
                std::lock_guard<std::mutex> lock(mutex_);
                bool on = properties["on"].value<bool>();
                bool ok = true;
                for (int i = 1; i <= 4; i++) {
                    ok = SetRelay(i, on) && ok;
                }
                if (!ok) {
                    return std::unexpected(std::string("Failed to update some relays"));
                }
                return true;
            });

        mcp_server.AddTool(
            "self.relay.get_status",
            "Get the current confirmed ON/OFF state of all four relays as JSON, like "
            "{\"relay1\":true,\"relay2\":false,...}. Use this when the user asks whether a "
            "device is on or off.",
            PropertyList(),
            [this](const PropertyList&) -> ToolResult {
                std::lock_guard<std::mutex> lock(mutex_);
                std::string out;
                if (!DbGet("relayStatus", out)) {
                    return std::unexpected(std::string("Failed to read Firebase"));
                }
                return std::string(out);
            });
    }
};

#endif  // FIREBASE_RELAY_H
