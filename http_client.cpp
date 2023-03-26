/* ThePirat 2023 - Http client helper */

#include <WiFiClientSecure.h>
#include "http_client.h"

String GetPublicIp() {
    return GetHttpGetResponseBody("api.ipify.org", 443, "/").body;
}

// Makes an HTTP GET request and return the response body
HttpResponse GetHttpGetResponseBody(const char* domain, uint16_t port, const char* url) {
  Serial.printf("Connecting to %s\r\n", domain);

  WiFiClientSecure client_ipify;
  client_ipify.setInsecure();

  if (!client_ipify.connect(domain, port)) {
    Serial.println("Connection failed!");
  }
  else {
    // Make a HTTP request:
    client_ipify.printf("GET %s HTTP/1.0\r\n", url);
    client_ipify.printf("Host: %s\r\n", domain);
    client_ipify.println("Connection: close");
    client_ipify.println();

    HttpResponse response = GetClientResponseBody(10, client_ipify, false);
    return response;
  }
  HttpResponse empty;
  return empty;
}


// Retrieves and return the response body from a WiFiClientSecure instance
HttpResponse GetClientResponseBody(int timeoutSeconds, WiFiClientSecure &client, bool stop) {
    int statusCode = 0;
    String getBody = "";
    String getAll = "";
    int waitTime = timeoutSeconds > 0 ? (timeoutSeconds * 1000) : 30000;   // timeout 30 seconds
    long startTime = millis();
    bool state = false;
    bool firstLine = true;
    bool timedout = true;
    String location = "";
    while ((startTime + waitTime) > millis())
    {
      Serial.print(".");
      delay(100);      
      while (client.available()) 
      {
        char c = client.read();

        if (state==true) { 
          getBody += String(c); 
        }
        if (c == '\n') { 
          // A new line in the response
          //Serial.println("LINE (" + String(state) + "): " + getAll);
          if (getAll.length()==0) {
            // Headers ended, the body is next
            state = true; 
          } else if (firstLine) {
            // First line ended, get the status code
            int fi0 = getAll.indexOf(' ', 0);
            if (fi0 > 0) {
              int li0 = getAll.indexOf(' ', fi0 + 1);
              if (li0 > 0) {
                statusCode = getAll.substring(fi0 + 1, li0).toInt();
              }
            } 
            firstLine = false;
          } else if (!state) {
            // It's a header
            if (location.length() == 0 && getAll.indexOf("Location: ") == 0) {
			    location = getAll.substring(getAll.indexOf(":")+2);
            }
          }
          getAll = "";
        } 
        else if (c != '\r') {
          getAll += String(c);
        }
        startTime = millis();
      }
      if (getBody.length()>0) {
        timedout = false;
        break;
      }
    }
    if (stop) {
      client.stop();
    }
    Serial.println();

    if (timedout) {
      Serial.println("HTTP Timeout");
    }

    if (getBody.length() > 0 && getBody[getBody.length()-1] == (char)255) {
      getBody = getBody.substring(0, getBody.length()-1);
    } 

    HttpResponse response = {
        .status = statusCode,
        .body = getBody,
        .location = location
    };

    return response;
}