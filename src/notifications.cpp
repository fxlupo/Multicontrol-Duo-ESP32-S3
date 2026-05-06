#include "notifications.h"
#include "config.h"
#include "wifi_manager.h"
#include <Callmebot_ESP32.h>

#ifndef WHATSAPP_NOTIFICATIONS_ENABLED
#define WHATSAPP_NOTIFICATIONS_ENABLED 0
#endif

#ifndef WHATSAPP_SEND_INTERVAL_MS
#define WHATSAPP_SEND_INTERVAL_MS 5000UL
#endif

#ifndef WHATSAPP_QUEUE_SIZE
#define WHATSAPP_QUEUE_SIZE 8
#endif

#ifndef WHATSAPP_RECIPIENT_1_ENABLED
#define WHATSAPP_RECIPIENT_1_ENABLED 0
#endif

#ifndef WHATSAPP_RECIPIENT_1_PHONE
#define WHATSAPP_RECIPIENT_1_PHONE ""
#endif

#ifndef WHATSAPP_RECIPIENT_1_API_KEY
#define WHATSAPP_RECIPIENT_1_API_KEY ""
#endif

#ifndef WHATSAPP_RECIPIENT_2_ENABLED
#define WHATSAPP_RECIPIENT_2_ENABLED 0
#endif

#ifndef WHATSAPP_RECIPIENT_2_PHONE
#define WHATSAPP_RECIPIENT_2_PHONE ""
#endif

#ifndef WHATSAPP_RECIPIENT_2_API_KEY
#define WHATSAPP_RECIPIENT_2_API_KEY ""
#endif

namespace {
    struct Recipient {
        bool enabled;
        const char* phone;
        const char* apiKey;
    };

    struct Job {
        bool used;
        uint8_t recipient;
        char message[180];
    };

    static const Recipient RECIPIENTS[] = {
        { WHATSAPP_RECIPIENT_1_ENABLED == 1, WHATSAPP_RECIPIENT_1_PHONE, WHATSAPP_RECIPIENT_1_API_KEY },
        { WHATSAPP_RECIPIENT_2_ENABLED == 1, WHATSAPP_RECIPIENT_2_PHONE, WHATSAPP_RECIPIENT_2_API_KEY },
    };

    static Job _queue[WHATSAPP_QUEUE_SIZE];
    static uint8_t _head = 0;
    static uint8_t _count = 0;
    static unsigned long _lastSend = 0;

    bool recipientConfigured(const Recipient& r) {
        return r.enabled && r.phone && r.phone[0] != '\0' && r.apiKey && r.apiKey[0] != '\0';
    }

    bool pushJob(uint8_t recipient, const String& message) {
        if (_count >= WHATSAPP_QUEUE_SIZE) return false;
        uint8_t idx = (_head + _count) % WHATSAPP_QUEUE_SIZE;
        _queue[idx].used = true;
        _queue[idx].recipient = recipient;
        strlcpy(_queue[idx].message, message.c_str(), sizeof(_queue[idx].message));
        _count++;
        return true;
    }

    bool popJob(Job& out) {
        if (_count == 0) return false;
        out = _queue[_head];
        _queue[_head].used = false;
        _head = (_head + 1) % WHATSAPP_QUEUE_SIZE;
        _count--;
        return true;
    }
}

void notify::init() {
    _head = 0;
    _count = 0;
    _lastSend = 0;
}

bool notify::enabled() {
    return WHATSAPP_NOTIFICATIONS_ENABLED == 1;
}

bool notify::hasRecipients() {
    for (uint8_t i = 0; i < sizeof(RECIPIENTS) / sizeof(RECIPIENTS[0]); i++) {
        if (recipientConfigured(RECIPIENTS[i])) return true;
    }
    return false;
}

bool notify::enqueue(const char* title, const String& message) {
    if (!enabled() || !hasRecipients()) return false;

    String fullMessage = "Multicontrol Duo";
    if (title && title[0] != '\0') {
        fullMessage += " - ";
        fullMessage += title;
    }
    if (message.length() > 0) {
        fullMessage += ": ";
        fullMessage += message;
    }

    bool queued = false;
    for (uint8_t i = 0; i < sizeof(RECIPIENTS) / sizeof(RECIPIENTS[0]); i++) {
        if (!recipientConfigured(RECIPIENTS[i])) continue;
        queued = pushJob(i, fullMessage) || queued;
    }
    return queued;
}

void notify::loop() {
    if (!enabled() || _count == 0 || !wifi::isConnected()) return;

    unsigned long now = millis();
    if (_lastSend != 0 && now - _lastSend < WHATSAPP_SEND_INTERVAL_MS) return;

    Job job;
    if (!popJob(job)) return;
    if (job.recipient >= sizeof(RECIPIENTS) / sizeof(RECIPIENTS[0])) return;

    const Recipient& recipient = RECIPIENTS[job.recipient];
    if (!recipientConfigured(recipient)) return;

    Callmebot.whatsappMessage(String(recipient.phone), String(recipient.apiKey), String(job.message));
    _lastSend = now;
}
