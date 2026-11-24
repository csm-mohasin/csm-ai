#ifndef MUSIC_PLAYER_H
#define MUSIC_PLAYER_H

// Internet theke Ogg-Opus gaan stream kore bajay (Xiaozhi-r built-in notify player diye).
// Dorkar: application.h-te "friend class MusicPlayer;" line (private: er niche).
// Gaan thamate: BOOT button click ba "Hi ESP" bolo.

#include <memory>
#include <string>
#include <vector>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"

class MusicPlayer {
public:
    // AI "Playing..." bolar jonno kichu shomoy diye chat bondho kore, tarpor gaan shuru kore.
    static void Play(const std::string& url) {
        auto* arg = new std::string(url);
        if (xTaskCreate(&MusicPlayer::StartTask, "music_start", 4096, arg, 3, nullptr) != pdPASS) {
            delete arg;
            ESP_LOGE(TAG, "Could not start music task");
        }
    }

private:
    static constexpr const char* TAG = "MusicPlayer";
    static constexpr int kStartDelayMs = 3500;

    static void StartTask(void* param) {
        {
            std::unique_ptr<std::string> url(static_cast<std::string*>(param));
            auto& app = Application::GetInstance();
            vTaskDelay(pdMS_TO_TICKS(kStartDelayMs));

            app.Schedule([&app]() {
                if (app.protocol_ && app.protocol_->IsAudioChannelOpened()) {
                    app.protocol_->CloseAudioChannel();
                }
            });
            for (int i = 0; i < 50 && app.GetDeviceState() != kDeviceStateIdle; i++) {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            if (app.GetDeviceState() != kDeviceStateIdle) {
                ESP_LOGW(TAG, "Device did not become idle; music not started");
            } else {
                app.Schedule([&app, u = *url]() mutable {
                    std::vector<NotifySubtitle> subtitles;
                    subtitles.push_back({0, "Playing music"});
                    app.StartNotification(std::move(u), std::move(subtitles));
                });
            }
        }
        vTaskDelete(nullptr);
    }
};

#endif  // MUSIC_PLAYER_H
