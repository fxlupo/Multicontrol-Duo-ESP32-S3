#include "config_sync.h"
#include "config.h"
#include "backend_http.h"
#include "mqtt_transport.h"
#include "wifi_manager.h"
#include "watchdog.h"
#include "stability.h"
#include "valve_driver.h"
#include <ArduinoJson.h>
#include <Preferences.h>

#if DEBUG_API_SYNC
#define API_DBG_PRINT(x) Serial.print(x)
#define API_DBG_PRINTLN(x) Serial.println(x)
#define API_DBG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define API_DBG_PRINT(x) do {} while (0)
#define API_DBG_PRINTLN(x) do {} while (0)
#define API_DBG_PRINTF(...) do {} while (0)
#endif

#ifndef BACKEND_ERROR_BACKOFF_MS
#define BACKEND_ERROR_BACKOFF_MS 120000UL
#endif

namespace cfg {
    ZoneConfig    zones[MAX_ZONES] = {};
    uint8_t       zoneCount      = 0;
    Schedule      schedules[MAX_SCHEDULES] = {};
    uint8_t       schedCount     = 0;
    ManualCommand commands[5]    = {};
    uint8_t       cmdCount       = 0;
    uint32_t      version        = 0;
    bool          controllerEnabled = true;
    uint32_t      rainDelayUntilEpoch = 0;
}

static unsigned long _lastBackendOkMs = 0;
static unsigned long _backendBackoffUntilMs = 0;
static uint8_t _backendErrorCount = 0;

static void markBackendOk() {
    _lastBackendOkMs = millis();
    _backendErrorCount = 0;
    _backendBackoffUntilMs = 0;
}

static void markBackendError(const char* stage) {
    if (_backendErrorCount < 255) _backendErrorCount++;
    if (_backendErrorCount >= 3) {
        _backendBackoffUntilMs = millis() + BACKEND_ERROR_BACKOFF_MS;
        API_DBG_PRINTF("[cfg] backend backoff after %u errors at %s\n",
                       (unsigned)_backendErrorCount,
                       stage ? stage : "?");
    }
}

static bool backendBackoffActive() {
    return _backendBackoffUntilMs != 0 && (long)(millis() - _backendBackoffUntilMs) < 0;
}

static void saveToNVS() {
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putUInt("cfg_version", cfg::version);
    p.putBytes("cfg_zones",  cfg::zones,     sizeof(ZoneConfig)    * cfg::zoneCount);
    p.putUChar("cfg_zc",     cfg::zoneCount);
    p.putBytes("cfg_scheds", cfg::schedules, sizeof(Schedule)      * cfg::schedCount);
    p.putUChar("cfg_sc",     cfg::schedCount);
    p.end();
}

static uint16_t stableId(const char* s) {
    uint16_t h = 21661;
    if (!s) return 0;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 16719;
    }
    return h == 0 ? 1 : h;
}

static char _zoneUuid[cfg::MAX_ZONES][40] = {};

static uint8_t zoneNumberFromZoneId(const char* uuid) {
    if (!uuid || !uuid[0]) return 0;
    for (uint8_t i = 0; i < cfg::zoneCount && i < cfg::MAX_ZONES; i++) {
        if (strncmp(_zoneUuid[i], uuid, sizeof(_zoneUuid[i])) == 0) {
            return cfg::zones[i].id;
        }
    }
    return 0;
}

static bool postCommandState(const char* id, const char* state, bool ok, const char* resultText) {
    if (!id || !id[0]) return false;
    stability::mark("config:ack");
    String path = String("/commands/") + id + "/" + state;
    API_DBG_PRINT("[cfg] POST ");
    API_DBG_PRINTLN(path);

    JsonDocument doc;
    if (strcmp(state, "done") == 0) {
        doc["ok"] = ok;
        doc["result"] = resultText ? resultText : "";
    }
    String body;
    serializeJson(doc, body);
    backend::Response response;
    backend::post("cmd-ack", path, body, response);
    int code = response.code;
    API_DBG_PRINTF("[cfg] %s code=%d\n", state, code);
    if (code >= 200 && code < 300) markBackendOk();
    else markBackendError("ack");
    return code >= 200 && code < 300;
}

static bool publishMqttCommandResult(const char* id, const char* status, bool ok, const char* resultText) {
    if (!id || !id[0]) return false;
    JsonDocument doc;
    doc["id"] = id;
    doc["status"] = status ? status : "done";
    doc["ok"] = ok;
    doc["result"] = resultText ? resultText : "";
    doc["source"] = "esp32";
    String body;
    serializeJson(doc, body);
    return mqtt::publishJson((String("commands/") + id + "/result").c_str(), body);
}

void cfg::loadFromNVS() {
    Preferences p;
    p.begin(NVS_NAMESPACE, true);
    version    = p.getUInt("cfg_version", 0);
    zoneCount  = p.getUChar("cfg_zc", 0);
    schedCount = p.getUChar("cfg_sc", 0);
    if (zoneCount  > MAX_ZONES) zoneCount = 0;
    if (schedCount > MAX_SCHEDULES) schedCount = 0;
    if (zoneCount)  p.getBytes("cfg_zones",  zones,     sizeof(ZoneConfig) * zoneCount);
    if (schedCount) p.getBytes("cfg_scheds", schedules, sizeof(Schedule)   * schedCount);
    p.end();
}

static void parseZones(JsonArray arr) {
    stability::mark("config:parse");
    cfg::zoneCount = 0;
    memset(_zoneUuid, 0, sizeof(_zoneUuid));
    for (JsonObject z : arr) {
        if (cfg::zoneCount >= cfg::MAX_ZONES) break;
        uint8_t idx = cfg::zoneCount++;
        ZoneConfig& zc = cfg::zones[idx];
        strlcpy(_zoneUuid[idx], z["id"] | "", sizeof(_zoneUuid[idx]));
        zc.id                 = z["valveNumber"]       | z["valve_number"] | (idx + 1);
        strlcpy(zc.name, z["name"] | "", sizeof(zc.name));
        zc.wh52_channel       = z["wh52Channel"]       | z["wh52_channel"]      | zc.id;
        zc.moisture_threshold = z["moistureThreshold"] | z["moisture_threshold"]| 35;
        zc.temp_minimum       = z["tempMinimum"]       | z["temp_minimum"]      | 8.0f;
        zc.rain_threshold_6h  = z["rainThreshold6h"]   | z["rain_threshold_6h"] | 5.0f;
        zc.temp_factor_above  = z["tempFactorAbove"]   | z["temp_factor_above"] | 25.0f;
        zc.temp_factor_mult   = z["tempFactorMult"]    | z["temp_factor_mult"]  | 1.25f;
        zc.max_duration_min   = z["maxDurationMin"]    | z["max_duration_min"]  | 90;
        zc.active             = (int)(z["active"] | 1) == 1;
    }
}

static void parseSchedules(JsonArray arr) {
    cfg::schedCount = 0;
    API_DBG_PRINTF("[cfg] schedules received=%u\n", (unsigned)arr.size());
    for (JsonObject s : arr) {
        if (cfg::schedCount >= cfg::MAX_SCHEDULES) break;
        bool active = true;
        if (s["active"].is<bool>()) {
            active = s["active"].as<bool>();
        } else if (s["active"].is<int>()) {
            active = s["active"].as<int>() != 0;
        }
        if (!active) {
            API_DBG_PRINTLN("[cfg] schedule skipped: inactive");
            continue;
        }
        uint8_t zoneId = 0;
        if (s["zoneNumber"].is<uint8_t>()) {
            zoneId = s["zoneNumber"].as<uint8_t>();
        } else if (s["zone_number"].is<uint8_t>()) {
            zoneId = s["zone_number"].as<uint8_t>();
        }
        if (zoneId == 0) {
            zoneId = zoneNumberFromZoneId(s["zoneId"] | s["zone_id"] | "");
        }
        if (zoneId == 0) {
            API_DBG_PRINTLN("[cfg] schedule skipped: no zone");
            continue;
        }
        Schedule& sc = cfg::schedules[cfg::schedCount++];
        const char* sid = s["id"] | "";
        sc.id           = stableId(sid);
        sc.zone_id      = zoneId;
        const char* prog = s["program"]     | "A";
        sc.program      = prog[0];
        sc.duration_min = s["durationMin"]  | s["duration_min"] | 15;
        sc.weekdays     = s["weekdays"]     | 127;
        sc.active       = true;
        // start_time "HH:MM:SS"
        const char* t   = s["startTime"]   | s["start_time"]  | "06:00:00";
        sc.start_hour   = atoi(t);
        sc.start_min    = (strlen(t) >= 5) ? atoi(t + 3) : 0;
        API_DBG_PRINTF("[cfg] schedule %u: zone=%u %02u:%02u dur=%u wd=%u\n",
                       (unsigned)cfg::schedCount,
                       (unsigned)sc.zone_id,
                       (unsigned)sc.start_hour,
                       (unsigned)sc.start_min,
                       (unsigned)sc.duration_min,
                       (unsigned)sc.weekdays);
    }
    API_DBG_PRINTF("[cfg] schedules parsed=%u\n", (unsigned)cfg::schedCount);
}

static void parseControl(JsonObject control) {
    cfg::controllerEnabled = control["controllerEnabled"] | true;
    cfg::rainDelayUntilEpoch = control["rainDelayUntilEpoch"] | 0;
}

static void parseCommands(JsonArray arr) {
    stability::mark("config:commands");
    cfg::cmdCount = 0;
    for (JsonObject c : arr) {
        if (cfg::cmdCount >= 5) break;
        ManualCommand& mc = cfg::commands[cfg::cmdCount++];
        strlcpy(mc.id, c["id"] | "", sizeof(mc.id));
        mc.zone_id      = c["zoneNumber"]   | c["zone_number"] | zoneNumberFromZoneId(c["zoneId"] | c["zone_id"] | "");
        strlcpy(mc.command, c["command"] | "close", sizeof(mc.command));
        mc.duration_min = c["durationMin"]  | c["duration_min"] | 10;
        mc.source_mqtt  = false;
    }
}

static bool commandAlreadyQueued(const char* id) {
    if (!id || !id[0]) return false;
    for (uint8_t i = 0; i < cfg::cmdCount; i++) {
        if (strncmp(cfg::commands[i].id, id, sizeof(cfg::commands[i].id)) == 0) return true;
    }
    return false;
}

void cfg::handleMqttCommand(const char* id, const String& payload) {
    if (!id || !id[0]) return;
    if (commandAlreadyQueued(id)) {
        publishMqttCommandResult(id, "acked", true, "duplicate");
        return;
    }
    if (cmdCount >= 5) {
        publishMqttCommandResult(id, "failed", false, "queue_full");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        publishMqttCommandResult(id, "failed", false, "json_error");
        return;
    }

    JsonObject c = doc.as<JsonObject>();
    ManualCommand& mc = commands[cmdCount++];
    strlcpy(mc.id, id, sizeof(mc.id));
    mc.zone_id = c["zoneNumber"] | c["zone_number"] | zoneNumberFromZoneId(c["zoneId"] | c["zone_id"] | "");
    strlcpy(mc.command, c["command"] | "close", sizeof(mc.command));
    mc.duration_min = c["durationMin"] | c["duration_min"] | 10;
    mc.source_mqtt = true;
    publishMqttCommandResult(id, "acked", true, "received");
}

static bool fetchCommands() {
    if (cfg::cmdCount > 0) {
        API_DBG_PRINTLN("[cfg] commands skip: local queue active");
        return false;
    }

    stability::mark("config:commands");
    String cmdUrl = "/commands";
    API_DBG_PRINT("[cfg] GET ");
    API_DBG_PRINTLN(cmdUrl);
    backend::Response cmdResponse;
    backend::get("commands", cmdUrl, cmdResponse);
    int cmdCode = cmdResponse.code;
    API_DBG_PRINTF("[cfg] commands code=%d\n", cmdCode);
    if (cmdCode == 200) {
        markBackendOk();
        JsonDocument cmdDoc;
        DeserializationError cmdErr = deserializeJson(cmdDoc, cmdResponse.body);
        wdt::feed();
        if (!cmdErr) {
            if (cmdDoc.is<JsonArray>()) parseCommands(cmdDoc.as<JsonArray>());
            else parseCommands(cmdDoc["commands"].as<JsonArray>());
            API_DBG_PRINTF("[cfg] commands parsed=%u\n", cfg::cmdCount);
        } else {
            API_DBG_PRINTF("[cfg] commands json error=%s\n", cmdErr.c_str());
            cfg::cmdCount = 0;
            markBackendError("config:commands-json");
        }
    } else {
        cfg::cmdCount = 0;
        markBackendError("config:commands");
    }

    for (uint8_t i = 0; i < cfg::cmdCount; i++) {
        bool ok = postCommandState(cfg::commands[i].id, "ack", true, "received");
        API_DBG_PRINTF("[cfg] ack %s %s\n", cfg::commands[i].id, ok ? "ok" : "fail");
    }

    return cfg::cmdCount > 0;
}

bool cfg::sync() {
    if (!wifi::isConnected()) {
        API_DBG_PRINTLN("[cfg] skip: no wifi");
        return false;
    }
    bool valveOpen = valve::getOpenZone() > 0;
    if (backendBackoffActive() && !valveOpen) {
        API_DBG_PRINTLN("[cfg] skip: backend backoff");
        return false;
    }
    if (valveOpen) {
        API_DBG_PRINTLN("[cfg] valve open: commands only");
        return fetchCommands();
    }
    static String lastConfigPayload;

    stability::mark("config:get");
    String url = "/config";
    API_DBG_PRINT("[cfg] GET ");
    API_DBG_PRINTLN(url);

    backend::Response response;
    backend::get("config", url, response);
    int code = response.code;
    API_DBG_PRINTF("[cfg] config code=%d\n", code);
    if (code != 200) {
        markBackendError("config:get");
        return false;
    }
    markBackendOk();

    String payload = response.body;
    bool configChanged = payload != lastConfigPayload;
    if (configChanged) lastConfigPayload = payload;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        API_DBG_PRINTF("[cfg] config json error=%s\n", err.c_str());
        markBackendError("config:parse");
        return false;
    }

    if (configChanged) {
        version++;
        parseZones(doc["zones"].as<JsonArray>());
        parseSchedules(doc["schedules"].as<JsonArray>());
        parseControl(doc["control"].as<JsonObject>());
        API_DBG_PRINTF("[cfg] config parsed zones=%u schedules=%u version=%lu\n",
                       (unsigned)zoneCount,
                       (unsigned)schedCount,
                       (unsigned long)version);
    }

    bool commandsChanged = fetchCommands();

    if (configChanged) {
        stability::mark("config:save");
        saveToNVS();
        wdt::feed();
    }
    return configChanged || commandsChanged;
}

void cfg::ackCommands() {
    if (!wifi::isConnected() || cmdCount == 0) return;

    for (uint8_t i = 0; i < cmdCount; i++) {
        bool mqttDone = false;
        if (commands[i].source_mqtt) {
            mqttDone = publishMqttCommandResult(commands[i].id, "done", true, "executed");
        }
        if (!commands[i].source_mqtt || !mqttDone) {
            postCommandState(commands[i].id, "done", true, "executed");
        }
    }
    cmdCount = 0;
}

bool cfg::backendOk() {
    return _lastBackendOkMs > 0 && (millis() - _lastBackendOkMs < BACKEND_DEAD_MS);
}

unsigned long cfg::lastBackendOkMs() {
    return _lastBackendOkMs;
}
