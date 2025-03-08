#include <Arduino.h>
#include "config.h"
#include "db.h"
#include "gen.h"
#include "settings.h"
#include "tft.h"
#include <time.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>
#include "Kandinsky/tjpgd/TJpg_Decoder.h"
#include <lvgl.h>
#include "lv_conf.h"

#define TFT_PURPLE     0xA81F
#define TFT_BLACK      0x0000
#define TFT_WHITE      0xFFFF
#define TFT_GREEN      0x07E0
#define TFT_LIGHT_BLUE 0xBDFE
#define NTP_SERVER "pool.ntp.org"

namespace AppState {
    time_t now;
    struct tm timeinfo;
    int last_min = -1;
    char temperature[8] = "N/A";
    float current_temp = -1000.0;
    char weather_desc[64] = "";
    char iconCode[8] = "";
    int timezoneOffset = 0;
    unsigned long lastCombinedUpdate = 0;
    int w = 0, h = 0;
}

bool manualBrightness = false;
unsigned long lastTouchTime = 0;
bool isTouched = false;

void ntpGetTime() {
    configTime(0, 0, NTP_SERVER);
    time_t nowSecs = time(nullptr);
    unsigned long startTime = millis();
    while (nowSecs < 8 * 3600 * 2 && (millis() - startTime) < 10000) {
        yield();
        nowSecs = time(nullptr);
    }
    nowSecs += AppState::timezoneOffset;
    localtime_r(&nowSecs, &AppState::timeinfo);
}

void getWeatherData() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected, trying to reconnect...");
        WiFi.disconnect();
        WiFi.begin(db[kk::wifi_ssid], db[kk::wifi_pass]);
        int tries = 20;
        while (WiFi.status() != WL_CONNECTED && tries--) {
            delay(500);
            Serial.print(".");
        }
        Serial.println();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi connection failed!");
            return;
        }
        Serial.println("WiFi reconnected!");
    }

    WiFiClient client;
    HTTPClient http;
    String weatherUrl = String("http://api.openweathermap.org/data/2.5/weather?q=") +
                        String(db[kk::weather_city]) +
                        String("&appid=") +
                        String(db[kk::weather_api_key]) +
                        String("&units=metric");
    http.begin(client, weatherUrl);

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("HTTP error: %d\n", httpCode);
        snprintf(AppState::temperature, sizeof(AppState::temperature), "ERR%d", httpCode);
        http.end();
        return;
    }

    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        Serial.printf("JSON parsing error: %s\n", error.c_str());
        strncpy(AppState::temperature, "JSON_ERR", sizeof(AppState::temperature) - 1);
        http.end();
        return;
    }

    float temp = doc["main"]["temp"];
    const char* desc = doc["weather"][0]["description"] | "N/A";
    const char* icon = doc["weather"][0]["icon"] | "01d";
    AppState::timezoneOffset = doc["timezone"] | 0;

    if (temp != AppState::current_temp) {
        AppState::current_temp = temp;
        snprintf(AppState::temperature, sizeof(AppState::temperature), "%d", (int)round(temp));
    }
    strncpy(AppState::weather_desc, desc, sizeof(AppState::weather_desc) - 1);
    strncpy(AppState::iconCode, icon, sizeof(AppState::iconCode) - 1);
    http.end();
}

void checkWiFi() {
    static unsigned long lastAttempt = 0;
    if (WiFi.status() != WL_CONNECTED && millis() - lastAttempt > 20000) {
        Serial.println("Attempting WiFi reconnection...");
        WiFi.disconnect();
        WiFi.begin(db[kk::wifi_ssid], db[kk::wifi_pass]);
        lastAttempt = millis();
    }
}

#define LV_HOR_RES_MAX 320
#define LV_VER_RES_MAX 480

static lv_color_t lv_buf[LV_HOR_RES_MAX * 5];

void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *color_p) {
    int32_t w = (area->x2 - area->x1 + 1);
    int32_t h = (area->y2 - area->y1 + 1);
    tft.pushImage(area->x1, area->y1, w, h, (uint16_t*)color_p);
    lv_display_flush_ready(disp);
}

void my_touchpad_read(lv_indev_t *indev_driver, lv_indev_data_t *data) {
    uint16_t x, y;
    bool touched = tft.getTouch(&x, &y);
    if (touched) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
        lastTouchTime = millis();
        isTouched = true;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        isTouched = false;
    }
}

lv_obj_t *main_screen;
lv_obj_t *settings_screen;
lv_obj_t *time_label;
lv_obj_t *temp_label;
lv_obj_t *weather_icon;
lv_obj_t *weather_desc_label;
static lv_indev_t *indev;

void update_main_screen(lv_timer_t *timer) {
    AppState::now = time(nullptr) + AppState::timezoneOffset;
    localtime_r(&AppState::now, &AppState::timeinfo);

    int hour = AppState::timeinfo.tm_hour;
    int min = AppState::timeinfo.tm_min;
    lv_label_set_text_fmt(time_label, "%02d:%02d", hour, min);
    lv_label_set_text_fmt(temp_label, "Temp: %s°C", AppState::temperature);

    String iconPath = "/icons/" + String(AppState::iconCode) + ".jpg";
    if (SPIFFS.exists(iconPath)) {
        lv_img_set_src(weather_icon, iconPath.c_str());
    } else {
        lv_img_set_src(weather_icon, "");
    }
    lv_label_set_text(weather_desc_label, AppState::weather_desc);

    // Автоматическая регулировка яркости
    if (!manualBrightness) {
        if (hour >= 22 || hour < 6) {
            setBrightness(51); // 20% яркости (51 из 255)
        } else {
            setBrightness(255); // 100% яркости
        }
    }
}

void create_main_screen() {
    main_screen = lv_obj_create(NULL);
    time_label = lv_label_create(main_screen);
    lv_obj_align(time_label, LV_ALIGN_TOP_LEFT, 5, 5);
    temp_label = lv_label_create(main_screen);
    lv_obj_align(temp_label, LV_ALIGN_TOP_RIGHT, -5, 5);
    weather_icon = lv_img_create(main_screen);
    lv_obj_align(weather_icon, LV_ALIGN_TOP_RIGHT, -5, 30);
    weather_desc_label = lv_label_create(main_screen);
    lv_obj_align(weather_desc_label, LV_ALIGN_TOP_RIGHT, -5, 60);

    lv_obj_t *btn = lv_btn_create(main_screen);
    lv_obj_set_size(btn, 60, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 120, 0);
    lv_obj_add_event_cb(btn, [](lv_event_t *e) { lv_scr_load(settings_screen); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, ">");
    lv_obj_center(btn_label);
}

void create_settings_screen() {
    settings_screen = lv_obj_create(NULL);
    lv_obj_t *back_btn = lv_btn_create(settings_screen);
    lv_obj_set_size(back_btn, 60, 60);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) { 
        lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_target(e));
        lv_obj_t *screen = lv_obj_get_parent(target);
        lv_scr_load(main_screen);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "<");
    lv_obj_center(back_label);

    lv_obj_t *title = lv_label_create(settings_screen);
    lv_label_set_text(title, "Settings");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *ta_ssid = lv_textarea_create(settings_screen);
    lv_obj_set_width(ta_ssid, 220);
    lv_obj_align(ta_ssid, LV_ALIGN_TOP_MID, 0, 60);
    lv_textarea_set_placeholder_text(ta_ssid, "SSID");
    lv_textarea_set_text(ta_ssid, db[kk::wifi_ssid].c_str());

    lv_obj_t *ta_pass = lv_textarea_create(settings_screen);
    lv_obj_set_width(ta_pass, 220);
    lv_obj_align(ta_pass, LV_ALIGN_TOP_MID, 0, 110);
    lv_textarea_set_placeholder_text(ta_pass, "Password");
    lv_textarea_set_text(ta_pass, db[kk::wifi_pass].c_str());

    lv_obj_t *ta_api = lv_textarea_create(settings_screen);
    lv_obj_set_width(ta_api, 220);
    lv_obj_align(ta_api, LV_ALIGN_TOP_MID, 0, 160);
    lv_textarea_set_placeholder_text(ta_api, "API Key");
    lv_textarea_set_text(ta_api, db[kk::weather_api_key].c_str());

    // Ползунок для яркости
    lv_obj_t *brightness_label = lv_label_create(settings_screen);
    lv_label_set_text(brightness_label, "Brightness");
    lv_obj_align(brightness_label, LV_ALIGN_TOP_MID, 0, 210);

    lv_obj_t *brightness_slider = lv_slider_create(settings_screen);
    lv_slider_set_range(brightness_slider, 0, 255);
    lv_slider_set_value(brightness_slider, db[kk::brightness].toInt(), LV_ANIM_OFF);
    lv_obj_set_width(brightness_slider, 200);
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_MID, 0, 230);
    lv_obj_add_event_cb(brightness_slider, [](lv_event_t *e) {
        lv_obj_t *slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
        int brightness = lv_slider_get_value(slider);
        setBrightness(brightness);
        db[kk::brightness] = brightness;
        db.update();
        manualBrightness = true;
    }, LV_EVENT_VALUE_CHANGED, NULL);

    static lv_obj_t *kb;
    kb = lv_keyboard_create(settings_screen);
    lv_obj_set_size(kb, LV_PCT(100), 150);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_add_event_cb(ta_ssid, [](lv_event_t *e) {
        lv_obj_t *ta = static_cast<lv_obj_t*>(lv_event_get_target(e));
        lv_keyboard_set_textarea(kb, ta);
    }, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta_pass, [](lv_event_t *e) {
        lv_obj_t *ta = static_cast<lv_obj_t*>(lv_event_get_target(e));
        lv_keyboard_set_textarea(kb, ta);
    }, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta_api, [](lv_event_t *e) {
        lv_obj_t *ta = static_cast<lv_obj_t*>(lv_event_get_target(e));
        lv_keyboard_set_textarea(kb, ta);
    }, LV_EVENT_FOCUSED, NULL);

    lv_obj_t *save_btn = lv_btn_create(settings_screen);
    lv_obj_align(save_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(save_btn, [](lv_event_t *e) {
        lv_obj_t *screen = lv_obj_get_parent(static_cast<lv_obj_t*>(lv_event_get_target(e)));
        db[kk::wifi_ssid] = lv_textarea_get_text(lv_obj_get_child(screen, 2));
        db[kk::wifi_pass] = lv_textarea_get_text(lv_obj_get_child(screen, 3));
        db[kk::weather_api_key] = lv_textarea_get_text(lv_obj_get_child(screen, 4));
        db.update();
        lv_scr_load(main_screen);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "Save");
    lv_obj_center(save_label);
}

void setup() {
    Serial.begin(115200);
    Serial.println();

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
        return;
    }

    db_init();
    sett_init(); // Инициализация веб-сервера из SettingsGyver
    tft_init();

    AppState::w = tft.width();
    AppState::h = tft.height();

    gen.setKey(db[kk::kand_token], db[kk::kand_secret]);

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("Kandinsky AP");

    bool wifi_ok = false;
    if (db[kk::wifi_ssid].length()) {
        WiFi.begin(db[kk::wifi_ssid], db[kk::wifi_pass]);
        int tries = 20;
        while (WiFi.status() != WL_CONNECTED && tries--) {
            delay(500);
            Serial.print('.');
        }
        Serial.println();
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("IP: " + WiFi.localIP().toString());
            WiFi.mode(WIFI_STA);
            wifi_ok = true;
        } else {
            Serial.println("WiFi connection failed!");
        }
    }

    if (wifi_ok) {
        getWeatherData();
        ntpGetTime();
        gen.getStyles();
        gen.begin();
    }

    if (!SPIFFS.exists("/calibration.txt")) {
        uint16_t calData[5];
        tft.calibrateTouch(calData, TFT_WHITE, TFT_BLACK, 15);
        File calFile = SPIFFS.open("/calibration.txt", "w");
        if (calFile) {
            calFile.write((uint8_t*)calData, sizeof(calData));
            calFile.close();
        }
    } else {
        File calFile = SPIFFS.open("/calibration.txt", "r");
        if (calFile) {
            uint16_t calData[5];
            calFile.read((uint8_t*)calData, sizeof(calData));
            tft.setTouch(calData);
            calFile.close();
        }
    }

    lv_init();
    tft.begin();
    tft.setRotation(2);

    static lv_display_t *disp = lv_display_create(LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_display_set_flush_cb(disp, my_disp_flush);
    static lv_color_t *buf = lv_buf;
    lv_display_set_buffers(disp, buf, NULL, LV_HOR_RES_MAX * 5, LV_DISPLAY_RENDER_MODE_PARTIAL);

    static lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touchpad_read);

    create_main_screen();
    create_settings_screen();
    lv_scr_load(main_screen);

    lv_timer_create(update_main_screen, 1000, NULL);

    // Снижаем частоту процессора для экономии энергии
    setCpuFrequencyMhz(80);
}

void loop() {
    lv_timer_handler();
    delay(50); // Увеличиваем задержку для экономии энергии

    static unsigned long lastHeapCheck = 0;
    if (millis() - lastHeapCheck > 5000) {
        Serial.printf("Free Heap: %d\n", ESP.getFreeHeap());
        lastHeapCheck = millis();
    }

    static unsigned long lastWeatherUpdate = 0;
    if (millis() - lastWeatherUpdate > 60000) {
        getWeatherData();
        checkWiFi();
        lastWeatherUpdate = millis();
    }

    // Light Sleep, если нет касаний 30 секунд
    if (millis() - lastTouchTime > 30000 && !isTouched) {
        WiFi.mode(WIFI_OFF); // Отключаем WiFi
        esp_sleep_enable_timer_wakeup(5000000); // Спим 5 секунд
        esp_light_sleep_start();
        WiFi.mode(WIFI_STA); // Включаем WiFi обратно
        WiFi.begin(db[kk::wifi_ssid], db[kk::wifi_pass]);
    }

    sett_tick(); // Обработка веб-сервера из SettingsGyver
    gen_tick();
}