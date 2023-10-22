/* ThePirat 2023 - Http client helper */

#include <WiFiClientSecure.h>
#include "http_client.h"

#include <HTTPClient.h>

String HttpGet(const String& url, int& statusCode)
{
  WiFiClientSecure *client = new WiFiClientSecure;
  client->setInsecure();
  String payload;
  {
    HTTPClient https;
    https.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    if (https.begin(*client, url))
    {
      statusCode = https.GET();
      if (statusCode >= 200 && statusCode <= 301) 
      {
        payload = https.getString();
      }
      https.end();
    }
    else
    {
      statusCode = -1;
    }
  }
  delete client;
  return payload;
}

String GetPublicIp() 
{
    int statusCode;
    String result = HttpGet("https://api.ipify.org/", statusCode);
    return result;
}

String GetClientResponseLocationHeader(WiFiClientSecure &client, bool stop) 
{
  String location;

  long runTime = millis();
  while (location.length() == 0 && millis() < (runTime + 5000))
  {
    while (location.length() == 0 && client.available()) {
      String line = client.readStringUntil('\n');

      if (line.indexOf("Location: ") >= 0) {
        location = line.substring(line.indexOf(":") + 2);
      }
    }

    runTime = millis();
  }

  if (stop) {
    client.stop();
  }

  return location;
}
