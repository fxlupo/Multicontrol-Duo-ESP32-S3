#include "backend_http.h"
#include "config.h"
#include "stability.h"
#include "watchdog.h"
#include "wifi_manager.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

namespace {
    void markStage(const char* stage, const char* suffix) {
        char buf[24];
        snprintf(buf, sizeof(buf), "http:%s:%s", stage ? stage : "?", suffix ? suffix : "?");
        stability::mark(buf);
    }

    void addAuth(HTTPClient& http) {
    #if API_AUTH_BEARER
        http.addHeader("Authorization", String("Bearer ") + ESP_API_KEY);
    #else
        http.addHeader("X-API-Key", ESP_API_KEY);
    #endif
    }

    void configureHttp(HTTPClient& http) {
        http.setReuse(false);
        http.setTimeout(HTTP_TIMEOUT_MS);
        http.setConnectTimeout(HTTP_TIMEOUT_MS);
    }

    String backendUrl(const String& path) {
        if (path.startsWith("http://") || path.startsWith("https://")) return path;
        if (path.length() > 0 && path[0] == '/') return String(API_BASE_URL) + path;
        return String(API_BASE_URL) + "/" + path;
    }

    bool finishRequest(HTTPClient& http, const char* stage, int code, backend::Response& response, bool readBody) {
        response.code = code;
        if (readBody && code >= 200 && code < 300) {
            markStage(stage, "body");
            response.body = http.getString();
        } else {
            response.body = "";
        }
        markStage(stage, "end");
        http.end();
        wdt::feed();
        return response.ok();
    }

    template <typename ClientT, typename RequestFn>
    bool withClient(const char* stage, const String& path, backend::Response& response, RequestFn requestFn) {
        ClientT client;
    #if API_AUTH_BEARER
        client.setInsecure();
    #endif
        client.setTimeout(HTTP_TIMEOUT_MS);

        HTTPClient http;
        markStage(stage, "begin");
        if (!http.begin(client, backendUrl(path))) {
            response.code = -1;
            response.body = "";
            wdt::feed();
            return false;
        }

        addAuth(http);
        configureHttp(http);
        markStage(stage, "send");
        return requestFn(http);
    }
}

bool backend::get(const char* stage, const String& path, Response& response, bool readBody) {
    response = Response();
#if API_AUTH_BEARER
    return withClient<WiFiClientSecure>(stage, path, response, [&](HTTPClient& http) {
#else
    return withClient<WiFiClient>(stage, path, response, [&](HTTPClient& http) {
#endif
        int code = http.GET();
        return finishRequest(http, stage, code, response, readBody);
    });
}

bool backend::post(const char* stage, const String& path, const String& body, Response& response) {
    response = Response();
#if API_AUTH_BEARER
    return withClient<WiFiClientSecure>(stage, path, response, [&](HTTPClient& http) {
#else
    return withClient<WiFiClient>(stage, path, response, [&](HTTPClient& http) {
#endif
        http.addHeader("Content-Type", "application/json");
        int code = http.POST(body);
        return finishRequest(http, stage, code, response, false);
    });
}
