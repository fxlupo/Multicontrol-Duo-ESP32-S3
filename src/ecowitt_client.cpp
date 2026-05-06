#include "ecowitt_client.h"
#include "config.h"
#include "wifi_manager.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

namespace ecowitt {
    SoilData    soil[ECOWITT_MAX_SOIL_CHANNELS]  = {};
    WeatherData weather  = {NAN, NAN, false};
}

static unsigned long _lastPollMs = 0;

// GW1200 /get_livedata_info – Werte kommen als Strings im JSON.
// Je nach Firmware entweder data.soil oder ch_soil.
// data.soil: {"channel":"1","soilmoisture":"32","soiltemperature":"18.5","soilec":"380"}
// ch_soil:   {"channel":"1","humidity":"41%","ec":"869 uS/cm","temp":"20.2","unit":"C"}
// Format variiert je Firmware-Version – ggf. Parser anpassen.

static float jsonFloat(JsonVariant v) {
    if (v.isNull()) return NAN;
    if (v.is<float>())       return v.as<float>();
    if (v.is<const char*>()) { float f = atof(v.as<const char*>()); return f; }
    return NAN;
}

bool ecowitt::poll() {
    if (!wifi::isConnected()) return false;

    HTTPClient http;
    String url = String("http://") + ECOWITT_IP + ":" + ECOWITT_PORT + "/get_livedata_info";
    http.begin(url);
    http.setTimeout(HTTP_TIMEOUT_MS);
    int code = http.GET();
    if (code != 200) { http.end(); return false; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if (err) return false;
    if (!doc["code"].isNull() && (int)doc["code"] != 0) return false;

    JsonObject data = doc["data"].as<JsonObject>();

    // ── Bodensensoren ──────────────────────────────────
    JsonArray soilArr = data["soil"].as<JsonArray>();
    for (JsonObject s : soilArr) {
        uint8_t ch = (uint8_t)atoi(s["channel"].as<const char*>() ?: "0");
        if (ch < 1 || ch > ECOWITT_MAX_SOIL_CHANNELS) continue;
        SoilData& sd = ecowitt::soil[ch - 1];
        sd.channel  = ch;
        sd.moisture = jsonFloat(s["soilmoisture"]);
        sd.temp     = jsonFloat(s["soiltemperature"]);
        sd.ec       = jsonFloat(s["soilec"]);
        sd.valid    = !isnan(sd.moisture);
        sd.ts       = millis();
    }
    JsonArray chSoilArr = doc["ch_soil"].as<JsonArray>();
    for (JsonObject s : chSoilArr) {
        uint8_t ch = (uint8_t)atoi(s["channel"].as<const char*>() ?: "0");
        if (ch < 1 || ch > ECOWITT_MAX_SOIL_CHANNELS) continue;
        SoilData& sd = ecowitt::soil[ch - 1];
        sd.channel  = ch;
        sd.moisture = jsonFloat(s["humidity"]);
        sd.temp     = jsonFloat(s["temp"]);
        sd.ec       = jsonFloat(s["ec"]);
        sd.valid    = !isnan(sd.moisture);
        sd.ts       = millis();
    }

    // ── Regensensor (optional) ─────────────────────────
    JsonObject rain = data["rainfall"].as<JsonObject>();
    if (!rain.isNull()) {
        weather.hasRain = true;
        // rain_rate: {"val":"0.0","unit":"mm/hr"} oder direkt "0.0"
        JsonVariant rrv = rain["rain_rate"]["val"];
        if (rrv.isNull()) weather.rain_rate = jsonFloat(rain["rain_rate"]);
        else              weather.rain_rate = jsonFloat(rrv);

        JsonVariant hv = rain["hourly"]["val"];
        float hourly;
        if (hv.isNull()) hourly = jsonFloat(rain["hourly"]);
        else             hourly = jsonFloat(hv);
        JsonVariant dv = rain["daily"]["val"];
        float daily;
        if (dv.isNull()) daily = jsonFloat(rain["daily"]);
        else             daily = jsonFloat(dv);
        if (!isnan(hourly))     weather.rain_6h = hourly * 6.0f;
        else if (!isnan(daily)) weather.rain_6h = daily;
        else                    weather.rain_6h = NAN;
    } else {
        weather.hasRain   = false;
        weather.rain_rate = NAN;
        weather.rain_6h   = NAN;
    }

    _lastPollMs = millis();
    return true;
}

bool ecowitt::isFresh(uint8_t channel) {
    if (channel < 1 || channel > ECOWITT_MAX_SOIL_CHANNELS) return false;
    const SoilData& s = ecowitt::soil[channel - 1];
    return s.valid && (millis() - s.ts < SENSOR_STALE_MS);
}

bool ecowitt::ecowittOk() {
    return _lastPollMs > 0 && (millis() - _lastPollMs < ECOWITT_DEAD_MS);
}
