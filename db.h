#pragma once
#include <GyverDBFile.h>
#include <FS.h>
#include <SPIFFS.h>

GyverDBFile db(&SPIFFS, "settings.db");

enum kk : size_t {
    wifi_ssid,
    wifi_pass,
    kand_token,
    kand_secret,
    gen_query,
    gen_negative,
    gen_style,
    auto_gen,
    auto_prd,
    weather_api_key,
    weather_city,
    lang,
    brightness, 
};

void db_init() {
    SPIFFS.begin(true);
    db.begin();

    db.init(kk::wifi_ssid, "");
    db.init(kk::wifi_pass, "");
    db.init(kk::kand_token, "");
    db.init(kk::kand_secret, "");
    db.init(kk::gen_query, "");
    db.init(kk::gen_negative, "");
    db.init(kk::gen_style, 0);
    db.init(kk::auto_gen, 0);
    db.init(kk::auto_prd, 60);
    db.init(kk::weather_api_key, "");
    db.init(kk::weather_city, "");
    db.init(kk::lang, 0);
    db.init(kk::brightness, 255); // Начальная яркость (максимум)
}