#ifndef WEB_SEARCH_H
#define WEB_SEARCH_H

// Internet search MCP tool: self.search.web
// Search shuru hole OLED-e "Searching..." dekhay, tarpor Cloudflare Worker theke uttor ane.

#include <cstdint>
#include <expected>
#include <string>

#include <esp_log.h>

#include "board.h"
#include "mcp_server.h"
#include "search_config.h"

class WebSearch {
private:
    static constexpr const char* TAG = "WebSearch";

    static std::string UrlEncode(const std::string& s) {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        for (unsigned char c : s) {
            if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                c == '-' || c == '_' || c == '.' || c == '~') {
                out += (char)c;
            } else {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 15];
            }
        }
        return out;
    }

    bool Fetch(const std::string& query, std::string& response) {
        auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
        http->SetHeader("X-Auth", SEARCH_AUTH_TOKEN);
        std::string url = std::string(SEARCH_WORKER_URL) + "/?q=" + UrlEncode(query);

        if (auto opened = http->Open("GET", url); !opened) {
            ESP_LOGE(TAG, "Open failed: %s", opened.error().ToString().c_str());
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
        if (*status != 200) {
            ESP_LOGE(TAG, "Search failed, status %d", *status);
            return false;
        }
        return true;
    }

public:
    WebSearch() {
        McpServer::GetInstance().AddTool(
            "self.search.web",
            "Search the internet for facts you do not know or that may have changed recently "
            "(news, people, places, science, definitions). Before calling, tell the user briefly "
            "that you are searching. Do NOT use this for Bangladesh government facts that are in "
            "the knowledge base, and not for controlling relays.\n"
            "Args:\n"
            "  `query`: short search phrase, English is best (example: 'Tarique Rahman').",
            PropertyList({Property("query", kPropertyTypeString)}),
            [this](const PropertyList& properties) -> ToolResult {
                std::string query = properties["query"].value<std::string>();
                if (query.empty()) {
                    return std::unexpected(std::string("Empty query"));
                }
                auto display = Board::GetInstance().GetDisplay();
                if (display) {
                    display->ShowNotification("Searching...", 5000);
                }
                std::string result;
                if (!Fetch(query, result)) {
                    return std::unexpected(std::string("Search failed"));
                }
                return result;
            });
    }
};

#endif  // WEB_SEARCH_H
