/* ThePirat 2023 - Http client helper */
#pragma once

#include <WiFiClientSecure.h>

struct HttpResponse
{
    int status;
    String body;
    String location;
};

String HttpGet(const String& url, int& statusCode);
String GetClientResponseLocationHeader(WiFiClientSecure &client, bool stop);
String GetPublicIp();
