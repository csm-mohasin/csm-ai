#ifndef FIREBASE_RELAY_H
#define FIREBASE_RELAY_H

// Voice thekey Firebase Realtime Database-er /relayControl update korar MCP tool.
// AI ei tool gulo dakbe: self.relay.set, self.relay.set_all, self.relay.get_status

#include <cstdio>
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
#include "search_config.h"  // Worker URL + token (/time endpoint-er jonno)

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

    // ---------- Schedule (website + ESP8266 er /schedules format) ----------
    static constexpr int kMaxSched = 20;
    static constexpr int64_t kTzOffsetS = 6 * 3600;  // Bangladesh = UTC+6 (ESP8266 er moto)

    // Asol UTC epoch Cloudflare Worker-er /time theke ney (ESP32 clock-er upor bharosha na kore)
    bool GetUtcNow(int64_t& now) {
        auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
        http->SetHeader("X-Auth", SEARCH_AUTH_TOKEN);
        if (auto opened = http->Open("GET", std::string(SEARCH_WORKER_URL) + "/time"); !opened) {
            ESP_LOGE(TAG, "Time open failed: %s", opened.error().ToString().c_str());
            return false;
        }
        auto status = http->GetStatusCode();
        if (!status || *status != 200) {
            http->Close();
            return false;
        }
        std::string body = http->ReadAll();
        http->Close();
        char* end = nullptr;
        long long v = std::strtoll(body.c_str(), &end, 10);
        if (end == body.c_str() || v < 1700000000LL) {
            return false;
        }
        now = v;
        return true;
    }

    static bool ParseSched(const char* s, int& relay, int& on, long long& t, char& st, int& mask) {
        long long last = 0;
        int n = std::sscanf(s, "%d,%d,%lld,%c,%d,%lld", &relay, &on, &t, &st, &mask, &last);
        return n >= 5 && relay >= 1 && relay <= 4 && (on == 0 || on == 1) && t > 0;
    }

    static std::string ClockText(int64_t utc) {
        int64_t tod = (utc + kTzOffsetS) % 86400;
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02d:%02d", (int)(tod / 3600), (int)((tod % 3600) / 60));
        return buf;
    }

    // Prothom khali (ba shesh hoye jaoya one-time) slot khuje ber kore. Na pele 0.
    int FindFreeSlot(cJSON* root) {
        for (int i = 1; i <= kMaxSched; i++) {
            std::string key = "s" + std::to_string(i);
            cJSON* v = root ? cJSON_GetObjectItem(root, key.c_str()) : nullptr;
            if (v == nullptr || !cJSON_IsString(v) || v->valuestring == nullptr) {
                return i;
            }
            int relay, on, mask;
            long long t;
            char st;
            if (!ParseSched(v->valuestring, relay, on, t, st, mask)) {
                return i;
            }
            if (mask == 0 && (st == 'd' || st == 'm')) {
                return i;
            }
        }
        return 0;
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
            "Get the current ON/OFF state of all four relays. ALWAYS call this tool when the "
            "user asks whether a device is on or off; never guess. Returns lines like "
            "'relay1: ON'. If a relay shows 'unknown', say you do not know its state.",
            PropertyList(),
            [this](const PropertyList&) -> ToolResult {
                std::lock_guard<std::mutex> lock(mutex_);
                std::string raw;
                // relayControl-e website/AI jei state lekhe seta thake (relayStatus nirbharjoggo na)
                if (!DbGet("relayControl", raw)) {
                    return std::unexpected(std::string("Failed to read Firebase"));
                }
                CJsonUniquePtr root(cJSON_Parse(raw.c_str()));
                std::string out;
                for (int i = 1; i <= 4; i++) {
                    std::string key = "relay" + std::to_string(i);
                    cJSON* v = root ? cJSON_GetObjectItem(root.get(), key.c_str()) : nullptr;
                    const char* state = "unknown";
                    if (v != nullptr && cJSON_IsBool(v)) {
                        state = cJSON_IsTrue(v) ? "ON" : "OFF";
                    }
                    out += key + ": " + state + "\n";
                }
                return out;
            });

        mcp_server.AddTool(
            "self.schedule.add",
            "Schedule a relay to turn ON or OFF later (same schedule system as the website). "
            "Use for requests like 'turn the light off in 10 minutes' or 'turn relay 2 on at "
            "9:30 every day'. Times are Bangladesh time, 24-hour clock.\n"
            "Args:\n"
            "  `relay`: relay number 1-4.\n"
            "  `on`: true = turn ON, false = turn OFF.\n"
            "  `after_minutes`: run after this many minutes from now (1-1440). Use this OR hour.\n"
            "  `hour`: hour of day 0-23 for a clock time (leave unset when using after_minutes).\n"
            "  `minute`: minute 0-59, used with hour.\n"
            "  `repeat_daily`: true to repeat every day at that hour:minute (only with hour).",
            PropertyList({Property("relay", kPropertyTypeInteger, 1, 4),
                          Property("on", kPropertyTypeBoolean),
                          Property("after_minutes", kPropertyTypeInteger, 0, 0, 1440),
                          Property("hour", kPropertyTypeInteger, -1, -1, 23),
                          Property("minute", kPropertyTypeInteger, 0, 0, 59),
                          Property("repeat_daily", kPropertyTypeBoolean, false)}),
            [this](const PropertyList& properties) -> ToolResult {
                std::lock_guard<std::mutex> lock(mutex_);
                int relay = properties["relay"].value<int>();
                bool on = properties["on"].value<bool>();
                int after = properties["after_minutes"].value<int>();
                int hour = properties["hour"].value<int>();
                int minute = properties["minute"].value<int>();
                bool repeat = properties["repeat_daily"].value<bool>();

                if (after <= 0 && hour < 0) {
                    return std::unexpected(std::string("Give after_minutes or hour"));
                }
                if (after > 0 && repeat) {
                    return std::unexpected(std::string("repeat_daily needs hour, not after_minutes"));
                }
                int64_t now = 0;
                if (!GetUtcNow(now)) {
                    return std::unexpected(std::string("Could not get current time"));
                }
                int64_t target;  // UTC epoch
                if (after > 0) {
                    target = now + (int64_t)after * 60;
                } else {
                    int64_t local = now + kTzOffsetS;
                    int64_t local_target = local - (local % 86400) + hour * 3600 + minute * 60;
                    if (local_target <= local + 30) {
                        local_target += 86400;  // aj-er shomoy chole gele agami kal
                    }
                    target = local_target - kTzOffsetS;
                }

                std::string raw;
                if (!DbGet("schedules", raw)) {
                    return std::unexpected(std::string("Failed to read schedules"));
                }
                CJsonUniquePtr root(cJSON_Parse(raw.c_str()));
                int slot = FindFreeSlot(root.get());
                if (slot == 0) {
                    return std::unexpected(std::string("Schedule list is full (20)"));
                }
                char val[64];
                std::snprintf(val, sizeof(val), "\"%d,%d,%lld,p,%d,0\"", relay, on ? 1 : 0,
                              (long long)target, repeat ? 127 : 0);
                if (!DbPut("schedules/s" + std::to_string(slot), val)) {
                    return std::unexpected(std::string("Failed to save schedule"));
                }
                int64_t day_diff = (target + kTzOffsetS) / 86400 - (now + kTzOffsetS) / 86400;
                std::string when = repeat ? "every day" : (day_diff == 0 ? "today" : "tomorrow");
                if (!repeat && day_diff > 1) {
                    when = "in " + std::to_string((long long)day_diff) + " days";
                }
                return "Scheduled relay " + std::to_string(relay) + (on ? " ON" : " OFF") +
                       " at " + ClockText(target) + " Bangladesh time, " + when + ".";
            });

        mcp_server.AddTool(
            "self.schedule.list",
            "List the pending schedules (relay, ON/OFF, time). Use when the user asks what is "
            "scheduled.",
            PropertyList(),
            [this](const PropertyList&) -> ToolResult {
                std::lock_guard<std::mutex> lock(mutex_);
                std::string raw;
                if (!DbGet("schedules", raw)) {
                    return std::unexpected(std::string("Failed to read schedules"));
                }
                CJsonUniquePtr root(cJSON_Parse(raw.c_str()));
                int64_t now = 0;
                bool have_now = GetUtcNow(now);
                std::string out;
                for (int i = 1; i <= kMaxSched; i++) {
                    std::string key = "s" + std::to_string(i);
                    cJSON* v = root ? cJSON_GetObjectItem(root.get(), key.c_str()) : nullptr;
                    if (v == nullptr || !cJSON_IsString(v) || v->valuestring == nullptr) {
                        continue;
                    }
                    int relay, on, mask;
                    long long t;
                    char st;
                    if (!ParseSched(v->valuestring, relay, on, t, st, mask)) {
                        continue;
                    }
                    if (mask == 0 && st != 'p') {
                        continue;  // shesh/missed one-time schedule dekhabo na
                    }
                    std::string when = mask != 0 ? "every day" : "once";
                    if (mask == 0 && have_now) {
                        int64_t d = (t + kTzOffsetS) / 86400 - (now + kTzOffsetS) / 86400;
                        when = d == 0 ? "today" : (d == 1 ? "tomorrow" : "later");
                    }
                    out += "relay " + std::to_string(relay) + (on ? " ON" : " OFF") + " at " +
                           ClockText(t) + " (" + when + ")\n";
                }
                if (out.empty()) {
                    return std::string("No pending schedules.");
                }
                return out;
            });
    }
};

#endif  // FIREBASE_RELAY_H
