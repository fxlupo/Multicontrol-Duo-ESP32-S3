#pragma once
#include <Arduino.h>

namespace backend {
    struct Response {
        int code = -1;
        String body;

        bool ok() const {
            return code >= 200 && code < 300;
        }
    };

    bool get(const char* stage, const String& path, Response& response, bool readBody = true);
    bool post(const char* stage, const String& path, const String& body, Response& response);
}
