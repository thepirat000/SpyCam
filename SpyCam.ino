
/* ThePirat 2023

Requisites:
- Use esp32 library version 1.0.6 
- Board Type: AI Thinker

Notes:
- Original app_httpd.cpp file from Espressif Systems modified to:
  - Remove face recognition
  - Use a single URI handler for all the URLs (control, status & capture)
  - Removing the stream server (temp)
  - Use config.max_open_sockets = 2 (temp)

*/

#include <WiFiClientSecure.h>
#include <ESP32Time.h>
#include "Base64.h"
#include <esp_wifi.h>

#include "configuration.h"
#include "esp_camera.h"
#include "camera_pins.h"
#include "sd_card.h"
#include "http_client.h"


unsigned long wifi_prev_ms = 0;
unsigned long wifi_interval = 30000;
bool offline = true;
ESP32Time rtc;
String lastConfigString = "";

void startCameraServer();
void stopCameraServer();

// **** SETUP **** //
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  pinMode(LED_BUILTIN_GPIO_NUM, OUTPUT);
  pinMode(FLASH_GPIO_NUM, OUTPUT);
  
  SD_init(("/" + DEVICE_NAME).c_str());

  ledOn();
  
  startWiFi();

  initCamera();

  refreshConfigFromWeb();

  ledOff();
}

// **** LOOP **** //
unsigned int cycle_count = 0;

void loop() {
  const unsigned long start_time = millis();
  cycle_count++;

  Serial.println("--cycle " + String(cycle_count) + "--");

  // if WiFi is down, try reconnecting
  reconnectWifi();

  if (PARAMS.period_gs_cloud > 0 && (cycle_count % PARAMS.period_gs_cloud == 0)) {
    // Capture and send to google drive
    String response = CaptureAndSend();
    Serial.println(response);    
  }
  if (PARAMS.period_sd_card > 0 && (cycle_count % PARAMS.period_sd_card == 0)) {
    // Capture and store to SD_CARD
    CaptureAndStore(1);
  }
  if (PARAMS.period_config_refresh > 0 && (cycle_count % PARAMS.period_config_refresh == 0)) {
    refreshConfigFromWeb();
  }
  if (PARAMS.period_restart > 0 && (cycle_count % PARAMS.period_restart == 0)) {
    Serial.println("Will restart...");
    ESP.restart();
  }

  const unsigned long end_time = millis();

  const int elapsed_seconds = (end_time - start_time) / 1000;
  const int sleep_seconds = max(0, PARAMS.min_cycle_seconds - elapsed_seconds);

  Serial.printf("Elapsed %d seconds. Min %d seconds. Will delay %d secs...\r\n", elapsed_seconds, PARAMS.min_cycle_seconds, sleep_seconds);
  delay(sleep_seconds * 1000);
}

bool initCamera() {
  esp_camera_deinit();

  camera_config_t cam_config = get_default_camera_config();
  esp_err_t err = esp_camera_init(&cam_config);
  
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return false;
  }

  // Set the default cam config and discard first image
  /*sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, get_default_camera_config().frame_size); 
  camera_fb_t* fb = esp_camera_fb_get();  
  esp_camera_fb_return(fb);*/

  return true;
}

bool startWiFi() {
  WiFi.mode(WIFI_AP_STA);

  // The following two lines are needed just in case someone has put the device in LR mode previously
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

  // Setup AP
  WiFi.softAP(SOFT_AP_SSID, SOFT_AP_PASSWORD); 
  
  // Setup STA
  if (!WiFi.config(LOCAL_IP, GATEWAY, SUBNET, PRIMARYDNS, SECONDARYDNS)) {
    Serial.println("STA Failed to configure");
  }

  WiFi.begin(SSID, PASSWORD.c_str());

  long int StartTime=millis();
  bool connected = true;
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(100);
    Serial.print(".");
    if ((StartTime+10000) < millis()) { 
      connected = false;
      break; 
    }
  }
  
  offline = !connected;

  Serial.println("");
  Serial.println(connected ? "WiFi connected" : "Offline mode");
  
  if (connected) {
      // Start the camera web server
      startCameraServer();
      Serial.println("Web Server Ready! (STA) Use 'http://" + WiFi.localIP().toString() + "' to connect");
      Serial.println("Web Server Ready! (AP) Use 'http://" + WiFi.softAPIP().toString() + "' to connect");
  }

  return connected;  
}

void reconnectWifi () {
  unsigned long current_ms = millis();

  if ((WiFi.status() != WL_CONNECTED) && (current_ms - wifi_prev_ms >= wifi_interval)) {
    Serial.println("Reconnecting to WiFi... Status: " + String(WiFi.status()));
    WiFi.disconnect();
    startWiFi();
    wifi_prev_ms = current_ms;
  }
}

camera_fb_t* TakePhoto() {
  if (PARAMS.flash) {
    flashOn();
    delay(100);
  }
  camera_fb_t* fb = esp_camera_fb_get();  
  if (PARAMS.flash) {
    delay(100);
    flashOff();
  }
  return fb;
}

String CaptureAndSend() {
  Serial.println("--CAPTURE & SEND--");

  String body = "";
  camera_fb_t* fb = TakePhoto();  
  if(!fb) {
    Serial.println("Camera capture failed");
    return "Camera capture failed";
  }  

  WiFiClientSecure client_upload;
  client_upload.setInsecure();

  if (client_upload.connect(SCRIPT_DOMAIN, 443)) {
    String head = "--ThePiratCam\r\nContent-Disposition: form-data; name=\"device\"\r\n\r\n" + DEVICE_NAME + "\r\n" \ 
      "--ThePiratCam\r\nContent-Disposition: form-data; name=\"image\"\r\n\r\n" \
      "data:image/png;base64,";
    String tail = "\r\n--ThePiratCam--\r\n";
    int estimatedImageLen = base64_enc_len(fb->len);
    int extraLen = head.length() + tail.length();
    int totalLen = estimatedImageLen + extraLen;
    
    client_upload.printf("POST %s HTTP/1.0\r\n", SCRIPT_URL_SEND_IMAGE);
    client_upload.printf("Host: %s\r\n", SCRIPT_DOMAIN);
    client_upload.println("Connection: close");
    client_upload.printf("Content-Length: %d\r\n", totalLen);
    client_upload.println("Content-Type: multipart/form-data; boundary=ThePiratCam");
    client_upload.println();
    client_upload.print(head);

    int realLen = 0;
    int step = 1500;  // Chunk size to send the file (must be multiple of 3)
    char *input = (char *)fb->buf;
    char output[base64_enc_len(step)];
    for (int i = 0; i+step < fb->len; i += step) {
      realLen+=base64_encode(output, input, step);
      client_upload.print(output);
      input += step;
      
      if (i%1000<step) Serial.print(".");
    }
    // Remainder
    int remainder = fb->len % step;
    if (remainder > 0) {
      char output_r[base64_enc_len(remainder)];
      realLen+=base64_encode(output_r, input, remainder);
      client_upload.print(output_r);
    }

    client_upload.print(tail);

    Serial.println();
    if (realLen != estimatedImageLen) {
      Serial.println("Length not matching!");
      Serial.println("fb len: " + String(fb->len));
      Serial.println("est img len: " + String(estimatedImageLen));
      Serial.println("real len: " + String(realLen));
    }

    esp_camera_fb_return(fb);
    
    // Wait for the response
    HttpResponse response = GetClientResponseBody(15, client_upload, true);
    if (response.location.length() > 4) {
      response = GetHttpGetResponseBody(SCRIPT_DOMAIN, 443, response.location.c_str());
    }
    body = response.body;
  }
  else {
    esp_camera_fb_return(fb);
    Serial.printf("Connect to %s failed.", SCRIPT_DOMAIN);
  }
  
  return body;
}

String CaptureAndStore(int count) {
  Serial.println("--CAPTURE & STORE--");

  for(int i = 0; i < count; i++) {
    camera_fb_t* fb = TakePhoto();  
    if(!fb) {
      Serial.println("Camera capture failed");
      return "Camera capture failed";
    }  
    String folder = "/" + DEVICE_NAME + "/" + rtc.getTime("%Y-%m-%d");
    SD_createDir(SD_MMC, folder.c_str());
    String filename = folder + "/" + rtc.getTime("Capture %Y-%m-%d %H.%M.%S.jpg");
    if (SD_writeFile(SD_MMC, filename.c_str(), fb->buf, fb->len)) {
      Serial.printf("File %s saved\r\n", filename.c_str());
    } else {
      Serial.println("Saving failed");
    }

    esp_camera_fb_return(fb);
  }

  return "";
}

void refreshConfigFromWeb() {
  Serial.println("--CONFIG REFRESH--");

  // Call getConfig google script and store in PARAMS
  HttpResponse response = GetHttpGetResponseBody(SCRIPT_DOMAIN, 443, SCRIPT_URL_GET_CONFIG);
  if (response.location.length() > 4) {
      response = GetHttpGetResponseBody(SCRIPT_DOMAIN, 443, response.location.c_str());
  }

  if (response.status == 200 && response.body.indexOf(":") > 0) {
    String configString = response.body.substring(response.body.indexOf(":") + 1, response.body.length());

    if (lastConfigString != configString) {
      
      Serial.println(String(lastConfigString.length() == 0 ? "Initial" : "New") + " params: " + configString);
      lastConfigString = configString;
      cycle_count = 0;

      //${unixDate}:${minCycleSeconds},${period_gs},${period_sd},${period_conf},${flash},${frame_size},${v_flip},${quality}
      unsigned long epoch;
      sscanf(response.body.c_str(), "%U:%d,%d,%d,%d,%d,%d,%d,%d,%d", &epoch, &PARAMS.min_cycle_seconds, &PARAMS.period_gs_cloud, &PARAMS.period_sd_card,
      &PARAMS.period_config_refresh, &PARAMS.period_restart, &PARAMS.flash, &PARAMS.frame_size, &PARAMS.vflip, &PARAMS.quality);

      // Set clock time
      rtc.setTime(epoch);

      Serial.println(rtc.getTime("Set clock to: %A, %B %d %Y %H:%M:%S"));

      // Set cam settings
      sensor_t * s = esp_camera_sensor_get();
      s->set_framesize(s, (framesize_t)PARAMS.frame_size); 
      s->set_vflip(s, PARAMS.vflip); 
      s->set_quality(s, PARAMS.quality); 
      
    } else {
      Serial.println("Params didn't change");
    }
  }
}

