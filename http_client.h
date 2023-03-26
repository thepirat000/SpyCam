/* ThePirat 2023 - Http client helper */
#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <WiFiClientSecure.h>

struct HttpResponse
{
    int status;
    String body;
    String location;
};

HttpResponse GetHttpGetResponseBody(const char* domain, uint16_t port, const char* url);
String GetPublicIp();
HttpResponse GetClientResponseBody(int timeoutSeconds, WiFiClientSecure &client, bool stop);

#endif 