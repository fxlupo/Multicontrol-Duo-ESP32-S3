#include "event_logger.h"
#include "backend_http.h"
#include "config.h"
#include "wifi_manager.h"
#include "valve_driver.h"
#include "scheduler.h"
#include "ecowitt_client.h"
#include "stability.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <time.h>

struct EventEntry {
    uint8_t  zone_id;
    char     action[8];
    char     reason[12];
    char     detail[64];
    uint32_t duration_sec;
    float    moisture;
    float    temp;
    float    ec;
    float    rain_6h;
    char     created_at[20];
};

static EventEntry _buf[EVENT_BUF_SIZE];
static uint8_t   _head  = 0;
static uint8_t   _count = 0;

static const uint8_t RECENT_EVENT_SIZE = 12;
static events::RecentEvent _recent[RECENT_EVENT_SIZE];
static uint8_t _recentHead = 0;
static uint8_t _recentCount = 0;

static String nowStr() {
    struct tm t;
    if (!getLocalTime(&t, 100)) return "1970-01-01 00:00:00";
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    return String(buf);
}

static bool postJson(const char* stage, const char* path, const String& body) {
    backend::Response response;
    return backend::post(stage, path, body, response);
}

// ArduinoJson v7: null via direkte Zuweisung, kein JsonVariant-Konstruktor
static void setNullableFloat(JsonObject& o, const char* key, float v) {
    if (isnan(v)) o[key] = nullptr;
    else          o[key] = v;
}

static const char* runtimeStateText(scheduler::RuntimeState state) {
    switch (state) {
        case scheduler::RuntimeState::Running: return "running";
        case scheduler::RuntimeState::Queued:  return "queued";
        case scheduler::RuntimeState::Idle:
        default:                               return "idle";
    }
}

static void addRecentEvent(const EventEntry& e) {
    uint8_t idx = (_recentHead + _recentCount) % RECENT_EVENT_SIZE;
    if (_recentCount == RECENT_EVENT_SIZE) {
        idx = _recentHead;
        _recentHead = (_recentHead + 1) % RECENT_EVENT_SIZE;
    } else {
        _recentCount++;
    }

    events::RecentEvent& r = _recent[idx];
    r.zone_id = e.zone_id;
    strlcpy(r.action, e.action, sizeof(r.action));
    strlcpy(r.reason, e.reason, sizeof(r.reason));
    strlcpy(r.detail, e.detail, sizeof(r.detail));
    strlcpy(r.created_at, e.created_at, sizeof(r.created_at));
}

static void serializeEvent(JsonObject& o, const EventEntry& e, bool legacyResetEvent) {
    bool mapResetToLegacy = legacyResetEvent && strcmp(e.action, "reset") == 0;
    o["zoneNumber"] = e.zone_id;
    o["action"]     = mapResetToLegacy ? "skip" : e.action;
    o["reason"]     = mapResetToLegacy ? "system" : e.reason;
    o["detail"]     = e.detail;
    if (e.duration_sec > 0) o["durationSec"] = e.duration_sec;
    else                    o["durationSec"]  = nullptr;
    setNullableFloat(o, "soil_moisture", e.moisture);
    setNullableFloat(o, "soil_temp",     e.temp);
    setNullableFloat(o, "soil_ec",       e.ec);
    setNullableFloat(o, "rain_6h",       e.rain_6h);
    o["createdAt"] = e.created_at;
}

static bool buildEventBatch(String& body, uint8_t toSend, bool legacyResetEvents) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < toSend; i++) {
        EventEntry& e = _buf[(_head + i) % EVENT_BUF_SIZE];
        JsonObject o = arr.add<JsonObject>();
        serializeEvent(o, e, legacyResetEvents);
    }
    body = "";
    serializeJson(doc, body);
    return body.length() > 0;
}

static bool batchHasResetEvent(uint8_t toSend) {
    for (uint8_t i = 0; i < toSend; i++) {
        EventEntry& e = _buf[(_head + i) % EVENT_BUF_SIZE];
        if (strcmp(e.action, "reset") == 0) return true;
    }
    return false;
}

static void addRecentEvent(uint8_t zone_id, const char* action, const char* reason,
                           const char* detail, const char* created_at) {
    uint8_t idx = (_recentHead + _recentCount) % RECENT_EVENT_SIZE;
    if (_recentCount == RECENT_EVENT_SIZE) {
        idx = _recentHead;
        _recentHead = (_recentHead + 1) % RECENT_EVENT_SIZE;
    } else {
        _recentCount++;
    }

    events::RecentEvent& r = _recent[idx];
    r.zone_id = zone_id;
    strlcpy(r.action, action ? action : "", sizeof(r.action));
    strlcpy(r.reason, reason ? reason : "", sizeof(r.reason));
    strlcpy(r.detail, detail ? detail : "", sizeof(r.detail));
    strlcpy(r.created_at, created_at ? created_at : "", sizeof(r.created_at));
}

void events::log(uint8_t zone_id, const char* action, const char* reason,
                 const char* detail, uint32_t duration_sec,
                 float moisture, float temp, float ec, float rain_6h) {
    uint8_t idx = (_head + _count) % EVENT_BUF_SIZE;
    if (_count == EVENT_BUF_SIZE) _head = (_head + 1) % EVENT_BUF_SIZE;
    else _count++;

    EventEntry& e  = _buf[idx];
    e.zone_id      = zone_id;
    e.duration_sec = duration_sec;
    e.moisture     = moisture;
    e.temp         = temp;
    e.ec           = ec;
    e.rain_6h      = rain_6h;
    strlcpy(e.action,    action,  sizeof(e.action));
    strlcpy(e.reason,    reason,  sizeof(e.reason));
    strlcpy(e.detail,    detail,  sizeof(e.detail));
    strlcpy(e.created_at, nowStr().c_str(), sizeof(e.created_at));

    addRecentEvent(e);
}

uint8_t events::recentCount() {
    return _recentCount;
}

bool events::recentAt(uint8_t newestIndex, RecentEvent& out) {
    if (newestIndex >= _recentCount) return false;
    uint8_t idx = (_recentHead + _recentCount - 1 - newestIndex + RECENT_EVENT_SIZE) % RECENT_EVENT_SIZE;
    out = _recent[idx];
    return true;
}

bool events::loadRecentFromBackend(uint8_t limit) {
    if (!wifi::isConnected()) return false;
    if (valve::getOpenZone() > 0) return false;
    limit = min<uint8_t>(max<uint8_t>(limit, 1), RECENT_EVENT_SIZE);

    backend::Response response;
    backend::get("events-load", String("/events?limit=") + String(limit), response);
    if (response.code != 200) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, response.body);
    if (err) return false;

    JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc["events"].as<JsonArray>();
    _recentHead = 0;
    _recentCount = 0;

    for (int i = (int)arr.size() - 1; i >= 0; i--) {
        JsonObject e = arr[i].as<JsonObject>();
        int zid = e["zoneNumber"] | e["zone_number"] | e["zone_id"] | 0;
        addRecentEvent((uint8_t)constrain(zid, 0, 4),
                       e["action"] | "",
                       e["reason"] | "",
                       e["detail"] | "",
                       e["createdAt"] | e["created_at"] | "");
    }
    return true;
}

void events::flush() {
    if (!wifi::isConnected() || _count == 0) return;
    if (valve::getOpenZone() > 0) return;
    uint8_t toSend = min(_count, (uint8_t)20);
    String body;
    if (!buildEventBatch(body, toSend, false)) return;

    bool posted = postJson("events-post", "/events", body);
    if (!posted && batchHasResetEvent(toSend)) {
        // Older backends may only accept irrigation actions. Fall back to the
        // previous boot-event shape so diagnostics do not block the event queue.
        if (buildEventBatch(body, toSend, true)) {
            posted = postJson("events-legacy", "/events", body);
        }
    }

    if (posted) {
        _head  = (_head + toSend) % EVENT_BUF_SIZE;
        _count -= toSend;
    }
}

void events::postStatus() {
    if (!wifi::isConnected()) return;
    JsonDocument doc;
    doc["wifiRssi"]        = wifi::rssi();
    doc["uptimeSec"]       = wifi::uptimeSec();
    doc["freeHeap"]        = (uint32_t)ESP.getFreeHeap();
    doc["minFreeHeap"]     = stability::minFreeHeap();
    doc["resetReason"]     = stability::resetReasonCode();
    doc["resetReasonText"] = stability::resetReasonText();
    doc["bootCount"]       = stability::bootCount();
    doc["crashCount"]      = stability::crashCount();
    doc["previousUptimeSec"] = stability::previousUptimeSec();
    doc["lastCrashStage"]  = stability::lastBreadcrumbStage();
    doc["lastCrashUptimeSec"] = stability::lastBreadcrumbUptimeSec();
    doc["lastCrashHeap"]   = stability::lastBreadcrumbHeap();
    doc["ecowittOk"]       = ecowitt::ecowittOk();
    doc["valveStates"]     = valve::stateStr();
    doc["firmwareVersion"] = "2.2.11";
    doc["ipAddress"]       = WiFi.localIP().toString();

    JsonObject runtime = doc["runtime"].to<JsonObject>();
    runtime["queueLength"] = scheduler::queueLength();
    JsonArray zones = runtime["zones"].to<JsonArray>();
    for (uint8_t zone = 1; zone <= RELAY_ZONE_COUNT; zone++) {
        JsonObject z = zones.add<JsonObject>();
        z["zone"] = zone;
        z["state"] = runtimeStateText(scheduler::runtimeState(zone));
        z["remainingSec"] = scheduler::remainingSec(zone);
    }

    String body;
    serializeJson(doc, body);
    postJson("status", "/status", body);
}

void events::uploadSensors() {
    if (!wifi::isConnected()) return;
    if (valve::getOpenZone() > 0) return;
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t ch = 1; ch <= ECOWITT_MAX_SOIL_CHANNELS; ch++) {
        const ecowitt::SoilData& s = ecowitt::soil[ch - 1];
        if (!s.valid) continue;
        JsonObject o = arr.add<JsonObject>();
        o["channel"]        = ch;
        o["soilMoisture"]   = s.moisture;
        o["soilTemp"]       = s.temp;
        setNullableFloat(o, "soilEc", s.ec);
        o["batteryOk"]      = nullptr;
        o["createdAt"]      = nowStr();
    }
    if (arr.size() == 0) return;
    String body;
    serializeJson(doc, body);
    postJson("sensors", "/sensors", body);
}
