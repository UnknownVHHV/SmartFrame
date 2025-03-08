#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <GSON.h>
#include <GyverHTTP.h>
#include "StreamB64.h"
#include "tjpgd/tjpgd.h"

#define FUSION_HOST "api-key.fusionbrain.ai"
#define FUSION_PORT 443
#define FUSION_PERIOD 6000
#define FUSION_TRIES 5
#define FUS_LOG(x) Serial.println(x)
#define FUSION_CLIENT WiFiClientSecure

class Kandinsky {
    typedef std::function<void(int x, int y, int w, int h, uint8_t* buf)> RenderCallback;
    typedef std::function<void()> RenderEndCallback;

    enum class State : uint8_t {
        GetModels,
        Generate,
        Status,
        GetStyles,
    };

public:
    Kandinsky() {}
    Kandinsky(const String& apikey, const String& secret_key) {
        setKey(apikey, secret_key);
    }

    void setKey(const String& apikey, const String& secret_key) {
        if (apikey.length() && secret_key.length()) {
            _api_key = "Key " + apikey;
            _secret_key = "Secret " + secret_key;
        }
    }

    void onRender(RenderCallback cb) { _rnd_cb = cb; }
    void onRenderEnd(RenderEndCallback cb) { _end_cb = cb; }

    void setScale(uint8_t scale) {
        switch (scale) {
            case 1: _scale = 0; break;
            case 2: _scale = 1; break;
            case 4: _scale = 2; break;
            case 8: _scale = 3; break;
            default: _scale = 0; break;
        }
    }

    bool begin() {
        if (!_api_key.length()) return false;
        return request(State::GetModels, FUSION_HOST, "/key/api/v1/models");
    }

    bool getStyles() {
        if (!_api_key.length()) return false;
        return request(State::GetStyles, "cdn.fusionbrain.ai", "/static/styles/key");
    }

    bool generate(Text query, uint16_t width = 512, uint16_t height = 512, Text style = "DEFAULT", Text negative = "") {
        status = "wrong config";
        if (!_api_key.length() || !style.length() || !query.length() || _id < 0) return false;

        gson::string json;
        json.beginObj();
        json.addString(F("type"), F("GENERATE"));
        json.addString(F("style"), style);
        json.addString(F("negativePromptUnclip"), negative);
        json.addInt(F("width"), width);
        json.addInt(F("height"), height);
        json.addInt(F("num_images"), 1);
        json.beginObj(F("generateParams"));
        json.addString(F("query"), query);
        json.endObj();
        json.endObj(true);

        ghttp::Client::FormData data;
        data.add("model_id", "", "", Value(_id));
        data.add("params", "blob", "application/json", json);

        uint8_t tries = FUSION_TRIES;
        while (tries--) {
            if (request(State::Generate, FUSION_HOST, "/key/api/v1/text2image/run", "POST", &data)) {
                FUS_LOG("Gen request sent");
                status = "wait result";
                return true;
            } else {
                FUS_LOG("Gen request error");
                delay(2000);
            }
        }
        status = "gen request error";
        return false;
    }

    bool getImage() {
        if (!_api_key.length() || !_uuid.length()) return false;
        FUS_LOG("Check status...");
        String url = "/key/api/v1/text2image/status/" + _uuid;
        return request(State::Status, FUSION_HOST, url);
    }

    void tick() {
        if (_uuid.length() && millis() - _tmr >= FUSION_PERIOD) {
            _tmr = millis();
            getImage();
        }
    }

    int modelID() { return _id; }
    String styles = "DEFAULT;ANIME;UHD;KANDINSKY";
    String status = "idle";

private:
    String _api_key;
    String _secret_key;
    String _uuid;
    uint8_t _scale = 0;
    uint32_t _tmr = 0;
    int _id = -1;
    RenderCallback _rnd_cb = nullptr;
    RenderEndCallback _end_cb = nullptr;
    StreamB64* _stream = nullptr;
    static Kandinsky* self;

    static size_t jd_input_cb(JDEC* jdec, uint8_t* buf, size_t len) {
        if (self && self->_stream) {
            self->_stream->readBytes(buf, len);
        }
        return len;
    }

    static int jd_output_cb(JDEC* jdec, void* bitmap, JRECT* rect) {
        if (self && self->_rnd_cb) {
            self->_rnd_cb(rect->left, rect->top, rect->right - rect->left + 1, rect->bottom - rect->top + 1, (uint8_t*)bitmap);
        }
        return 1;
    }

    bool request(State state, Text host, Text url, Text method = "GET", ghttp::Client::FormData* data = nullptr) {
        FUSION_CLIENT client;
        client.setInsecure();
        ghttp::Client http(client, host.str(), FUSION_PORT);

        ghttp::Client::Headers headers;
        headers.add("Accept", "application/json, text/plain, */*");
        headers.add("Accept-Language", "ru-RU,ru;q=0.9,en;q=0.8");
        headers.add("Cache-Control", "no-cache");
        headers.add("Connection", "keep-alive");
        headers.add("DNT", "1");
        headers.add("Host", host.str());
        headers.add("Origin", "cdn.fusionbrain.ai");
        headers.add("Referer", "cdn.fusionbrain.ai");
        headers.add("Sec-fetch-dest", "document");
        headers.add("Sec-fetch-mode", "navigate");
        headers.add("Sec-fetch-site", "none");
        headers.add("Sec-fetch-user", "?1");
        headers.add("Upgrade-Insecure-Requests", "1");
        headers.add("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36");
        headers.add("Sec-ch-ua", "Google Chrome;v=131, Chromium;v=131, Not_A Brand;v=24");
        headers.add("Sec-ch-ua-mobile", "?0");
        headers.add("Sec-ch-ua-platform", "Windows");
        headers.add("X-Key", _api_key);
        headers.add("X-Secret", _secret_key);

        bool ok = data ? http.request(url, method, headers, *data) : http.request(url, method, headers);
        if (!ok) {
            FUS_LOG("Request error");
            return false;
        }

        ghttp::Client::Response resp = http.getResponse();
        if (resp && resp.code() >= 200 && resp.code() < 300) {
            if (state == State::Status) {
                bool ok = parseStatus(resp.body());
                http.flush();
                return ok;
            } else {
                gtl::stack_uniq<uint8_t> str;
                resp.body().writeTo(str);
                gson::Parser json;
                if (json.parse(str.buf(), str.length())) {
                    return parse(state, json);
                } else {
                    FUS_LOG("Parse error");
                }
            }
        } else {
            FUS_LOG("Response error: " + String(resp.code()));
        }
        http.flush();
        return false;
    }

    bool parseStatus(Stream& stream) {
        bool found = false;
        while (stream.available()) {
            stream.readStringUntil('\"');
            String key = stream.readStringUntil('\"');
            if (key == "images") {
                found = true;
                break;
            }
            stream.readStringUntil('\"');
            String val = stream.readStringUntil('\"');
            if (!key.length() || !val.length()) break;

            if (key == "status") {
                switch (Text(val).hash()) {
                    case SH("INITIAL"):
                    case SH("PROCESSING"):
                        return true;
                    case SH("DONE"):
                        _uuid = "";
                        break;
                    case SH("FAIL"):
                        _uuid = "";
                        status = "gen fail";
                        return false;
                }
            }
        }
        if (found) {
            stream.readStringUntil('\"');
            uint8_t* workspace = new uint8_t[TJPGD_WORKSPACE_SIZE];
            if (!workspace) {
                FUS_LOG("allocate error");
                return false;
            }

            JDEC jdec;
            jdec.swap = 0;
            JRESULT jresult = JDR_OK;
            StreamB64 sb64(stream);
            _stream = &sb64;
            self = this;

            jresult = jd_prepare(&jdec, jd_input_cb, workspace, TJPGD_WORKSPACE_SIZE, 0);
            if (jresult == JDR_OK) {
                jresult = jd_decomp(&jdec, jd_output_cb, _scale);
                if (jresult == JDR_OK && _end_cb) _end_cb();
                else FUS_LOG("jdec error");
            } else {
                FUS_LOG("jdec error");
            }

            self = nullptr;
            delete[] workspace;
            status = (jresult == JDR_OK) ? "gen done" : "jpg error";
            return (jresult == JDR_OK);
        }
        return true;
    }

    bool parse(State state, gson::Parser& json) {
        switch (state) {
            case State::GetStyles:
                styles = "";
                for (int i = 0; i < (int)json.rootLength(); i++) {
                    if (i) styles += ';';
                    json[i]["name"].addString(styles);
                }
                return styles.length() > 0;
            case State::GetModels:
                _id = json[0]["id"];
                return _id >= 0;
            case State::Generate:
                _tmr = millis();
                json["uuid"].toString(_uuid);
                return _uuid.length() > 0;
            default:
                return false;
        }
    }
};

Kandinsky* Kandinsky::self = nullptr;