#pragma once
#include <AutoOTA.h>
#include <SettingsGyver.h>
#include "config.h"
#include "db.h"
#include "gen.h"
#include "timer.h"
#include "tft.h" // Для функции setBrightness

bool isRussian = (db[kk::lang].toInt() == 0);
SettingsGyver sett("AI Фоторамка v" F_VERSION, &db);
sets::Timer gentmr;
AutoOTA ota(F_VERSION, "AlexGyver/AiFrame/main/project.json");

void changeLanguage() {
    isRussian = !isRussian;
    db.set(kk::lang, isRussian ? 0 : 1);
    sett.reload();
}

void init_tmr() {
    int prd = db[kk::auto_prd].toInt();
    gentmr.setTime(max(prd * 1000, 60000));
    if (db[kk::auto_gen].toBool()) gentmr.startInterval();
    else gentmr.stop();
}

void build(sets::Builder& b) {
    {
        sets::Group g(b, isRussian ? "Генерация" : "Generation");
        b.Select(kk::gen_style, isRussian ? "Стиль" : "Style", gen.styles);
        b.Input(kk::gen_query, isRussian ? "Промт" : "Prompt");
        b.Input(kk::gen_negative, isRussian ? "Исключить" : "Exclude");
        b.Label(SH("status"), isRussian ? "Статус" : "Status", gen.status);
        b.Button(SH("generate"), isRussian ? "Генерировать" : "Generate");
    }
    {
        sets::Group g(b, isRussian ? "Автогенерация" : "Autogeneration");
        b.Switch(kk::auto_gen, isRussian ? "Включить" : "Enable");
        b.Time(kk::auto_prd, isRussian ? "Период" : "Period");
    }
    {
        sets::Group g(b, isRussian ? "Настройки" : "Settings");
        {
            sets::Menu m(b, "WiFi");
            sets::Group g(b);
            b.Input(kk::wifi_ssid, "SSID");
            b.Pass(kk::wifi_pass, isRussian ? "Пароль" : "Password");
            b.Button(SH("wifi_save"), isRussian ? "Подключить" : "Connect");
        }
        {
            sets::Menu m(b, "API");
            sets::Group g(b);
            b.Input(kk::kand_token, isRussian ? "Токен" : "Token");
            b.Pass(kk::kand_secret, "Secret");
            b.Button(SH("api_save"), isRussian ? "Применить" : "Apply");
        }
        {
            sets::Menu m(b, "OpenWeatherMap");
            sets::Group g(b);
            b.Input(kk::weather_api_key, isRussian ? "Токен" : "Token");
            b.Input(kk::weather_city, isRussian ? "Город, код страны" : "City,Country code");
            b.Button(SH("weather_save"), isRussian ? "Применить" : "Apply");
        }
        // Добавляем ползунок яркости
        b.Range(kk::brightness, isRussian ? "Яркость" : "Brightness", 0, 255, db[kk::brightness].toInt());
    }

    if (b.Confirm("update"_h)) ota.update();

    if (b.build.isAction()) {
        switch (b.build.id) {
            case SH("generate"): generate(); init_tmr(); break;
            case SH("wifi_save"): db.update(); ESP.restart(); break;
            case SH("api_save"): gen.setKey(db[kk::kand_token], db[kk::kand_secret]); db.update(); ESP.restart(); break;
            case SH("weather_save"): db.update(); ESP.restart(); break;
            case SH("lang_switch"): changeLanguage(); break;
            case kk::auto_gen:
            case kk::auto_prd: init_tmr(); break;
            case kk::brightness: // Обработка изменения яркости
                setBrightness(db[kk::brightness].toInt());
                manualBrightness = true;
                db.update();
                break;
        }
    }
}

void update(sets::Updater& u) {
    u.update(SH("status"), gen.status);
    if (ota.hasUpdate()) u.update("update"_h, isRussian ? "Доступно обновление. Обновить прошивку?" : "Update available. Update firmware?");
}

void sett_init() {
    sett.begin();
    sett.onBuild(build);
    sett.onUpdate(update);
    init_tmr();
}

void sett_tick() {
    ota.tick();
    sett.tick();
    if (gentmr) generate();
}