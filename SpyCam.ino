/* ThePirat 2023

Requisites:
- Use esp32 library version 1.0.6 
- Board Type: AI Thinker
- Google App Scripts 

Notes:
- Connect the PIR sensor to the GPIO 13
- ESP32 CAM PINOUT: https://randomnerdtutorials.com/esp32-cam-ai-thinker-pinout/
- ESP32 CAM PIN Description: https://github.com/raphaelbs/esp32-cam-ai-thinker/blob/master/docs/esp32cam-pin-notes.md
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
#include "app_httpd.h"

unsigned long wifi_prev_ms = 0;
unsigned long wifi_interval = 30000;
bool offline = true;
ESP32Time rtc;
String lastConfigString = "";


// **** SETUP **** //
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  pinMode(LED_BUILTIN_GPIO_NUM, OUTPUT);
  pinMode(FLASH_GPIO_NUM, OUTPUT);
  pinMode(PIR_SENSOR_NUM, INPUT);

  SD_init();
  setConfigFromFile();

  flashOff();
  
  SD_createDir(SD_MMC, ("/" + DEVICE_NAME).c_str());

  ledOn();
  
  startWiFi();

  initCamera();
  
  refreshConfigFromWeb();

  ledOff();
}

// **** LOOP **** //
unsigned int cycle_count = 1;

void loop() {
  const unsigned long start_time = millis();

  Serial.printf("-- Cycle: %d - Signal: %d dBm\r\n", cycle_count, WiFi.RSSI());

  // if WiFi is down, try reconnecting
  reconnectWifi();

  // Process serial input
  serialInput();

  if (isStreaming) {
    Serial.println("Currently streaming, will not process cycle");
  }

  // Capture image and send to google drive
  if (!isStreaming && !offline && PARAMS.period_gs_cloud > 0 && (cycle_count % PARAMS.period_gs_cloud == 0)) {
    String response = CaptureAndSend(false, false);
    Serial.println(response);    
  }

  // Capture and store to SD_CARD
  if (!isStreaming && PARAMS.period_sd_card > 0 && (cycle_count % PARAMS.period_sd_card == 0)) {
    CaptureAndStore(1);
  }

  // Reconfigure from web
  if (!offline && PARAMS.period_config_refresh > 0 && (cycle_count % PARAMS.period_config_refresh == 0)) {
    refreshConfigFromWeb();
  }

  // Restart cycle
  if (PARAMS.period_restart > 0 && (cycle_count % PARAMS.period_restart == 0)) {
    Serial.println("Will restart...");
    ESP.restart();
  }

  cycle_count++;

  const unsigned long end_time = millis();
  const unsigned long elapsed_ms = end_time - start_time;
  const unsigned long sleep_ms = max(0UL, (PARAMS.min_cycle_seconds * 1000) - elapsed_ms);
  const unsigned long sleep_end_time = end_time + sleep_ms;

  Serial.printf("Elapsed %d seconds. Min %d seconds. Will delay %d secs if no motion detected...\r\n", (elapsed_ms / 1000), PARAMS.min_cycle_seconds, (sleep_ms / 1000));

  while(millis() < sleep_end_time) {
    // Process serial input
    serialInput();

    if (PARAMS.motion) {
      // Motion processing
      motionDetection(sleep_end_time);
    } else {
      // Motion processing disabled
      delay(50);
    }
  }
}

void motionDetection(unsigned long sleep_end_time) {
  int motion_detected = digitalRead(PIR_SENSOR_NUM);
  if (!isStreaming && motion_detected == HIGH) {
    Serial.println("Motion detected !");
    const unsigned long motion_start_time = millis();

    // Capture image and store to SD card 
    CaptureAndStore(1);

    // Upload image and notify
    if (!offline) {
      String response = CaptureAndSend(true, true);
      Serial.println(response);
    }

    const unsigned long motion_elapsed_ms = millis() - motion_start_time;
    const unsigned long motion_next_cycle_ms = (PARAMS.min_motion_cycle_seconds * 1000UL) > motion_elapsed_ms ? (PARAMS.min_motion_cycle_seconds * 1000) - motion_elapsed_ms : 0; 
    const unsigned long motion_sleep_ms = min(sleep_end_time - millis()/*ms for the next main cycle*/, motion_next_cycle_ms);
    Serial.println("Motion elapsed " + String(motion_elapsed_ms) + " ms. Will delay on motion for " + String(motion_sleep_ms) + " ms");
    delay(motion_sleep_ms);
  }
}

bool initCamera() {
  //esp_camera_deinit();

  camera_config_t cam_config = get_default_camera_config();
  esp_err_t err = esp_camera_init(&cam_config);
  
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return false;
  }

  // Set the default frame_size config
  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, cam_config.frame_size); 

  return true;
}

bool startWiFi() {
  WiFi.mode(WIFI_AP_STA);

  // The following two lines are needed just in case the device is in LR mode 
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

  // Setup AP
  WiFi.softAP(SOFT_AP_SSID, SOFT_AP_PASSWORD); 
  
  // Setup STA
  if (!WiFi.config(LOCAL_IP, GATEWAY, SUBNET, PRIMARYDNS, SECONDARYDNS)) {
    Serial.println("STA Failed to configure");
  }

  WiFi.begin(SSID.c_str(), PASSWORD.c_str());

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
      Serial.printf("Connected to %s. Signal: %d dBm\r\n", SSID, WiFi.RSSI());
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
  Serial.printf("Image size: %d bytes\r\n", fb->len);

  return fb;
}

// If forget==true, this functions will be much faster but will not wait for the server response
String CaptureAndSend(bool sendTelegram, bool forget) {
  Serial.println("-- CAPTURE & SEND --");
  String body = "";
  camera_fb_t* fb = TakePhoto();  
  if(!fb) {
    Serial.println("Camera capture failed");
    return "Camera capture failed";
  }  

  WiFiClientSecure client_upload;
  client_upload.setInsecure();
  ledOn();

  if (client_upload.connect(SCRIPT_DOMAIN, 443)) {
    String head = "--ThePiratCam\r\nContent-Disposition: form-data; name=\"image\"\r\n\r\n" \
      "data:image/png;base64,";
    String tail = "\r\n--ThePiratCam--\r\n";
    int estimatedImageLen = base64_enc_len(fb->len);
    int extraLen = head.length() + tail.length();
    int totalLen = estimatedImageLen + extraLen;
    
    String url = String(SCRIPT_URL_SEND_IMAGE) + "?device=" + String(DEVICE_NAME) + "&telegram=" + (sendTelegram ? "1" : "0");

    client_upload.printf("POST %s HTTP/1.0\r\n", url.c_str());
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
    if (!forget) {
      HttpResponse response = GetClientResponseBody(15, client_upload, true);
      if (response.location.length() > 4) {
        response = GetHttpGetResponseBody(SCRIPT_DOMAIN, 443, response.location.c_str());
      }
      body = response.body;
    }
  }
  else {
    esp_camera_fb_return(fb);
    Serial.printf("Connect to %s failed.", SCRIPT_DOMAIN);
  }

  ledOff();

  return body;
}

String CaptureAndStore(int count) {
  Serial.println("-- CAPTURE & STORE --");

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

// Sets the configuration from the /config.txt file in the SD card, if present
void setConfigFromFile() {
  if (SD_MMC.exists(CONFIG_FILE)) {
    Serial.println("Will set config from config.txt");
    File file = SD_MMC.open(CONFIG_FILE);
    if(file) {
      if(file.available()) {
        DEVICE_NAME = file.readStringUntil('\n');
        DEVICE_NAME.trim();

        SSID = file.readStringUntil('\n');
        SSID.trim();

        PASSWORD = file.readStringUntil('\n');
        PASSWORD.trim();

        Serial.println(DEVICE_NAME + ", " + SSID + ", " + PASSWORD);
      }
      file.close();
    }
  }
}

// Refreshes the clock, the cycles configuration and camera configuration from gs web
void refreshConfigFromWeb() {
  Serial.println("-- CONFIG REFRESH --");

  // Call getConfig google script and store in PARAMS
  HttpResponse response = GetHttpGetResponseBody(SCRIPT_DOMAIN, 443, SCRIPT_URL_GET_CONFIG);
  if (response.location.length() > 4) {
      response = GetHttpGetResponseBody(SCRIPT_DOMAIN, 443, response.location.c_str());
  }

  if (response.status == 200 && response.body.indexOf(":") > 0) {
    String configString = response.body.substring(response.body.indexOf(":") + 1, response.body.length());
    unsigned long epoch;

    if (lastConfigString != configString) {
      Serial.println(String(lastConfigString.length() == 0 ? "Initial" : "New") + " params: " + configString);
      lastConfigString = configString;
      cycle_count = 1;

      //${unixDate}:${minCycleSeconds},${period_gs},${period_sd},${period_conf},${flash},${frame_size},${v_flip},${quality},${telegram_chat_id},${re}
      sscanf(response.body.c_str(), "%lu:%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", &epoch, &PARAMS.min_cycle_seconds, &PARAMS.period_gs_cloud, &PARAMS.period_sd_card,
      &PARAMS.period_config_refresh, &PARAMS.period_restart, &PARAMS.flash, &PARAMS.frame_size, &PARAMS.vflip, &PARAMS.brigthness, &PARAMS.saturation, &PARAMS.quality, &PARAMS.motion,	&PARAMS.min_motion_cycle_seconds);
      Serial.printf("New values\r\nepoch: %lu, min_cycle: %d, period_gs: %d, period sd: %d, period conf: %d, period restart: %d, flash: %d, framesize: %d, vflip: %d, bright: %d. sat: %d. quality: %d. motion: %d. min motion: %d\r\n", 
        epoch, PARAMS.min_cycle_seconds, PARAMS.period_gs_cloud, PARAMS.period_sd_card,
        PARAMS.period_config_refresh, PARAMS.period_restart, PARAMS.flash, PARAMS.frame_size, PARAMS.vflip, PARAMS.brigthness, PARAMS.saturation, PARAMS.quality,
        PARAMS.motion, PARAMS.min_motion_cycle_seconds);

      // Set cam settings
      sensor_t * s = esp_camera_sensor_get();
      s->set_framesize(s, (framesize_t)PARAMS.frame_size); 
      s->set_vflip(s, PARAMS.vflip); 
      s->set_quality(s, PARAMS.quality); 
      s->set_brightness(s, PARAMS.brigthness);
      s->set_saturation(s, PARAMS.saturation);
    } else {
      Serial.println("Params didn't change");
      sscanf(response.body.c_str(), "%U:%*s", &epoch);
    }

    // Set clock time
    rtc.setTime(epoch);
    Serial.println(rtc.getTime("Set clock to: %A, %B %d %Y %H:%M:%S"));
  }
}

// Serial commands (for debug)
void serialInput() {
    if (Serial.available() > 0) {
      String recv = Serial.readStringUntil('\r');
      recv.trim();
      if (recv.startsWith("stop")) {
        Serial.println("Will stop");
        PARAMS.motion = 0;
      }
  }
}
