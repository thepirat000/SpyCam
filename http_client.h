/* ThePirat 2023 - Http client helper */
#pragma once

struct HttpResponse
{
    int status;
    String body;
    String location;
};

HttpResponse HttpGet(const String& url, bool followRedirects = true);
HttpResponse HttpPost(const String& url, const String& payload, bool followRedirects = false);
String GetPublicIp();
