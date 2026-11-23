#ifndef KNOWLEDGE_BASE_H
#define KNOWLEDGE_BASE_H

// Nijer Knowledge Base: Firebase /knowledge theke tottho porhe AI-ke dey.
// Tool: self.kb.get   (admin.html dashboard theke tottho add/edit/delete kora jay)
// Firebase-e protita entry: /knowledge/<id> = { "t": "title", "x": "text" }

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

class KnowledgeBase {
private:
    static constexpr const char* TAG = "KnowledgeBase";
    static constexpr size_t kMaxBytes = 8000;  // admin.html-er limit-er sathe mil rakhbe

    std::mutex mutex_;
    std::string id_token_;
    int64_t token_expire_us_ = 0;

    bool Request(const std::string& method, const std::string& url, const std::string& body,
                 std::string& response) {
        auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
        http->SetHeader("Content-Type", "application/json");
        if (!body.empty()) {
            http->SetContent(std::string(body));
        }
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
        return true;
    }

    // /knowledge porhe, "- title: text" line-e shajiye out-e dey.
    bool Load(std::string& out) {
        if (!EnsureToken()) {
            return false;
        }
        std::string url = std::string(FIREBASE_DB_URL) + "/knowledge.json?auth=" + id_token_;
        std::string resp;
        if (!Request("GET", url, "", resp)) {
            id_token_.clear();
            return false;
        }
        CJsonUniquePtr root(cJSON_Parse(resp.c_str()));
        if (!root || root.get() == nullptr || !cJSON_IsObject(root.get())) {
            out = "The knowledge base is empty.";
            return true;
        }
        std::string text;
        bool truncated = false;
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, root.get()) {
            cJSON* t = cJSON_GetObjectItem(item, "t");
            cJSON* x = cJSON_GetObjectItem(item, "x");
            if (!cJSON_IsString(x) || x->valuestring == nullptr) {
                continue;
            }
            std::string line = "- ";
            if (cJSON_IsString(t) && t->valuestring != nullptr && t->valuestring[0] != '\0') {
                line += t->valuestring;
                line += ": ";
            }
            line += x->valuestring;
            line += "\n";
            if (text.size() + line.size() > kMaxBytes) {
                truncated = true;
                break;
            }
            text += line;
        }
        if (text.empty()) {
            out = "The knowledge base is empty.";
            return true;
        }
        if (truncated) {
            text += "(more entries exist but were cut off)\n";
        }
        out = text;
        return true;
    }

public:
    KnowledgeBase() {
        McpServer::GetInstance().AddTool(
            "self.kb.get",
            "Read our own knowledge base: verified facts about Bangladesh government, the Prime "
            "Minister, this science fair project and other custom facts. Returns all entries as "
            "lines like '- title: text'. Use it FIRST for such questions and answer ONLY from the "
            "returned lines. If the answer is not in the lines, say you do not have it and then "
            "you may search the web.",
            PropertyList(),
            [this](const PropertyList&) -> ToolResult {
                std::lock_guard<std::mutex> lock(mutex_);
                auto display = Board::GetInstance().GetDisplay();
                if (display) {
                    display->ShowNotification("Checking memory...", 4000);
                }
                std::string out;
                if (!Load(out)) {
                    return std::unexpected(std::string("Failed to read knowledge base"));
                }
                return out;
            });
    }
};

#endif  // KNOWLEDGE_BASE_H
