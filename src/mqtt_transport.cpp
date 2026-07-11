#include "mqtt_transport.h"
#include "config.h"
#include "wifi_manager.h"
#include "stability.h"
#include "watchdog.h"
#include <PubSubClient.h>
#include <WiFi.h>

#ifndef MQTT_ENABLED
#define MQTT_ENABLED 0
#endif

#ifndef MQTT_HOST
#define MQTT_HOST ""
#endif

#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif

#ifndef MQTT_DEVICE_ID
#define MQTT_DEVICE_ID "esp32-01"
#endif

#ifndef MQTT_TOPIC_PREFIX
#define MQTT_TOPIC_PREFIX "irrigation"
#endif

#ifndef MQTT_USER
#define MQTT_USER ""
#endif

#ifndef MQTT_PASS
#define MQTT_PASS ""
#endif

#ifndef MQTT_RECONNECT_MS
#define MQTT_RECONNECT_MS 10000UL
#endif

#ifndef MQTT_SOCKET_TIMEOUT_SEC
#define MQTT_SOCKET_TIMEOUT_SEC 3
#endif

namespace {
    WiFiClient wifiClient;
    PubSubClient client(wifiClient);
    unsigned long lastReconnectMs = 0;
    bool initialized = false;
    mqtt::CommandCallback commandCallback = nullptr;
    mqtt::ConfigCallback configCallback = nullptr;

    String baseTopic() {
        return String(MQTT_TOPIC_PREFIX) + "/" + MQTT_DEVICE_ID;
    }

    String topic(const char* suffix) {
        String t = baseTopic();
        if (suffix && suffix[0]) {
            if (suffix[0] != '/') t += "/";
            t += suffix;
        }
        return t;
    }

    bool connect() {
        if (!mqtt::enabled() || !wifi::isConnected()) return false;
        if (client.connected()) return true;

        stability::mark("mqtt:connect");
        wdt::feed();
        String clientId = String("irrigation-") + MQTT_DEVICE_ID;
        String willTopic = topic("availability");
        bool ok = client.connect(
            clientId.c_str(),
            MQTT_USER,
            MQTT_PASS,
            willTopic.c_str(),
            1,
            true,
            "offline"
        );
        if (ok) {
            client.publish(willTopic.c_str(), "online", true);
            String commandTopic = topic("commands/+");
            client.subscribe(commandTopic.c_str());
            String configTopic = topic("config");
            client.subscribe(configTopic.c_str());
            stability::mark("mqtt:online");
        } else {
            stability::mark("mqtt:fail");
        }
        wdt::feed();
        return ok;
    }

    void onMessage(char* rawTopic, byte* payload, unsigned int length) {
        String topicText(rawTopic ? rawTopic : "");
        String body;
        body.reserve(length);
        for (unsigned int i = 0; i < length; i++) {
            body += (char)payload[i];
        }

        if (topicText == topic("config")) {
            if (configCallback) configCallback(body);
            return;
        }

        String prefix = topic("commands/");
        if (!topicText.startsWith(prefix)) return;
        String commandId = topicText.substring(prefix.length());
        if (commandId.length() == 0 || commandId.indexOf('/') >= 0) return;

        if (commandCallback) commandCallback(commandId.c_str(), body);
    }
}

bool mqtt::enabled() {
    return MQTT_ENABLED == 1 && MQTT_HOST[0] != '\0';
}

void mqtt::init() {
    if (!enabled()) return;
    client.setServer(MQTT_HOST, MQTT_PORT);
    client.setBufferSize(4096);
    client.setSocketTimeout(MQTT_SOCKET_TIMEOUT_SEC);
    client.setCallback(onMessage);
    initialized = true;
    lastReconnectMs = 0;
}

void mqtt::setCommandCallback(CommandCallback callback) {
    commandCallback = callback;
}

void mqtt::setConfigCallback(ConfigCallback callback) {
    configCallback = callback;
}

void mqtt::loop() {
    if (!initialized || !enabled()) return;
    if (!wifi::isConnected()) return;

    unsigned long now = millis();
    if (!client.connected() && now - lastReconnectMs >= MQTT_RECONNECT_MS) {
        lastReconnectMs = now;
        connect();
    }
    if (client.connected()) client.loop();
}

bool mqtt::publishJson(const char* topicSuffix, const String& body, bool retained) {
    if (!initialized || !enabled() || body.length() == 0) return false;
    if (!connect()) return false;
    String t = topic(topicSuffix);
    bool ok = client.publish(t.c_str(), body.c_str(), retained);
    wdt::feed();
    return ok;
}

bool mqtt::connected() {
    return initialized && client.connected();
}
