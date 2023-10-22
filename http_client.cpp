/* ThePirat 2023 - Http client helper */
#include <WiFiClientSecure.h>
#include "http_client.h"
#include <HTTPClient.h>

HttpResponse HttpGet(const String& url, bool followRedirects)
{
  HttpResponse response;
  WiFiClientSecure *client = new WiFiClientSecure;
  client->setInsecure();
  String payload;
  {
    HTTPClient https;
    https.setFollowRedirects(followRedirects ? HTTPC_FORCE_FOLLOW_REDIRECTS : HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (https.begin(*client, url))
    {
      response.status = https.GET();
      response.body = https.getString();
      https.end();
    }
    else
    {
      response.status = -1;
    }
  }
  delete client;
  return response;
}

HttpResponse HttpPost(const String& url, const String& payload, bool followRedirects)
{
  HttpResponse response;
  WiFiClientSecure *client = new WiFiClientSecure;
  client->setInsecure();
  {
    HTTPClient https;
    https.setFollowRedirects(followRedirects ? HTTPC_FORCE_FOLLOW_REDIRECTS : HTTPC_DISABLE_FOLLOW_REDIRECTS);
    https.addHeader("Content-Length", String(payload.length()));
    https.addHeader("Content-Type", "application/json");

    if (https.begin(*client, url))
    {
      response.status = https.POST(payload);
      response.body = https.getString();
      response.location = https.getLocation();
      https.end();
    }
    else
    {
      response.status = -1;
    }
  }
  delete client;
  return response;
}

String GetPublicIp() 
{
    HttpResponse result = HttpGet("https://api.ipify.org/");
    return result.body;
}
