/* ThePirat 2023

Requisites:
- Use esp32 library version 1.0.6 
- Board Type: AI Thinker
- Google App Scripts 

Notes:
- Connect the PIR sensor to the GPIO 12
- ESP32 CAM PINOUT: https://randomnerdtutorials.com/esp32-cam-ai-thinker-pinout/
- ESP32 CAM PIN Description: https://github.com/raphaelbs/esp32-cam-ai-thinker/blob/master/docs/esp32cam-pin-notes.md
- Needs manual installation of latest Universal-Arduino-Telegram-Bot Arduino library (https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot)
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
#include "telegram.h"

#ifdef DISABLE_BROWNOUT
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#endif

unsigned long wifi_prev_ms = 0;
unsigned long wifi_interval = 30000;
bool offline = true;
ESP32Time rtc;
String lastConfigString = "";
unsigned int cycle_count = 1;

void HandleTelegramMessage(const String& text, const String& chat_id, const String& from);
Telegram telegramBot(TLGRM_BOT_TOKEN, TLGRM_CHAT_ID, HandleTelegramMessage);

// Pics counters since restart
unsigned long pics_count_cloud_gs = 0;    //Cloud gs pics count
unsigned long pics_count_sd = 0;          //SD card pics count
unsigned long pics_count_motion = 0;      //Motion pics count 

// Config query paramaters for gs script POST call
const String EXTRA_PARAMS_FORMAT = "&su={mins_up}&ws={wifi_signal}&cc={counter_cycles}&cm={counter_pics_motion}&cg={counter_pics_gs}&cs={counter_pics_sd}&bf={bytes_free}&tc={temperature_celsius}&us={used_size_mb}";
// Status values format
const String STATUS_PARAMS_FORMAT = "{mins_up} mins up - WiFi: {wifi_signal} dBm\r\nCounters: Cycle={counter_cycles} - MO={counter_pics_motion} GS={counter_pics_gs} SD={counter_pics_sd}\r\n{bytes_free} bytes free - Temp: {temperature_celsius}ºC - Used: {used_size_mb}MB";

bool reconfigOnNextCycle = false;

// **** SETUP **** //
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  #ifdef DISABLE_BROWNOUT
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  #endif

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
void loop() {
  const unsigned long start_time = millis();

  Serial.printf("--> Cycle: %d - Signal: %d dBm - Temp: %.0f ºC\r\n", cycle_count, WiFi.RSSI(), temperatureRead());

  // if WiFi is down, try reconnecting
  reconnectWifi();

  // Process serial input
  serialInput();

  if (isStreaming) {
    Serial.println("Currently streaming, will not process cycle");
  }

  if (cycle_count == 1) {
    // Send the start message
    telegramBot.SendMessage(DEVICE_NAME + " is online\r\n" + GetStatusMessage());
  }

  // Check for telegram new message commands
  if (!isStreaming && !offline && PARAMS.period_telegram > 0 && (cycle_count % PARAMS.period_telegram == 0)) {
    telegramBot.ProcessInputMessages();
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
  if (!offline && ((PARAMS.period_config_refresh > 0 && (cycle_count % PARAMS.period_config_refresh == 0)) || reconfigOnNextCycle)) {
    String newConfig = refreshConfigFromWeb();
    if (reconfigOnNextCycle) {
      telegramBot.SendMessage("Reconfigured with: " + newConfig);
      reconfigOnNextCycle = false;
    }
  }
  

  // Restart cycle
  if (PARAMS.period_restart > 0 && (cycle_count % PARAMS.period_restart == 0)) {
    Serial.println("Will restart...");
    ESP.restart();
  }

  cycle_count++;

  const unsigned long end_time = millis();
  const unsigned long elapsed_ms = end_time - start_time;
  const unsigned long sleep_ms = (elapsed_ms >= (PARAMS.min_cycle_seconds * 1000)) ? 0UL : (PARAMS.min_cycle_seconds * 1000) - elapsed_ms;
  const unsigned long sleep_end_time = end_time + sleep_ms;

  Serial.printf("Elapsed %lu seconds. Min %d seconds. Will delay %lu ms...\r\n", elapsed_ms / 1000UL, PARAMS.min_cycle_seconds, sleep_ms);

  if (PARAMS.motion) {
    Serial.println("Motion detection enabled");
  }

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
  // The following two lines are needed just in case the device is in LR mode 
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

#ifdef DISABLE_AP
  WiFi.mode(WIFI_STA);
#else
  WiFi.mode(WIFI_AP_STA);
  // Setup AP
  WiFi.softAP(SOFT_AP_SSID, SOFT_AP_PASSWORD); 
#endif
  
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
  Serial.printf("IP: %s\r\nGateway: %s\r\nSubnet: %s\r\nPrimary DNS: %s\r\nSecondary DNS: %s\r\n", LOCAL_IP.toString().c_str(),
        GATEWAY.toString().c_str(), SUBNET.toString().c_str(), PRIMARYDNS.toString().c_str(), SECONDARYDNS.toString().c_str());
        
  if (connected) {
      // Start the camera web server
      Serial.printf("Connected to %s. Signal: %d dBm.\r\n", SSID.c_str(), WiFi.RSSI());
      startCameraServer();
      Serial.println("Web Server Ready! (STA) Use 'http://" + WiFi.localIP().toString() + "' to connect");
#ifndef DISABLE_AP      
      Serial.println("Web Server Ready! (AP) Use 'http://" + WiFi.softAPIP().toString() + "' to connect");
#endif      
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

camera_fb_t* TakePhoto(bool flash) {
  if (flash) {
    flashOn();
    delay(100);
  }

  camera_fb_t* fb = esp_camera_fb_get();  
  if (flash) {
    delay(100);
    flashOff();
  }
  
  Serial.printf("Image size: %d bytes\r\n", fb->len);

  return fb;
}

camera_fb_t* TakePhoto() {
  return TakePhoto(PARAMS.flash);
}

// If forget==true, this function will be much faster but will not wait for the server response
String CaptureAndSend(bool isMotionDetected, bool forget) {
  if (isMotionDetected) {
    Serial.println("-- MOTION CAPTURE, STORE IN CLOUD & SEND TO TELEGRAM --");  
  } else {
    Serial.println("-- CAPTURE & STORE IN CLOUD --");  
  }
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
    
    String url = String(SCRIPT_URL_SEND_IMAGE) + "?device=" + String(DEVICE_NAME) + "&telegram=" + (isMotionDetected ? "1" : "0");
    url = url + GetStatusQueryParams();

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

    if (isMotionDetected) {
      pics_count_motion++;
    } else {
      pics_count_cloud_gs++;
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
  Serial.println("-- CAPTURE & STORE IN SD CARD --");

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
      pics_count_sd++;
    } else {
      Serial.println("Saving failed");
    }

    esp_camera_fb_return(fb);
  }

  return "";
}

String GetStatusQueryParams() {
  return FormatConfigValues(EXTRA_PARAMS_FORMAT);
}

String GetStatusMessage() {
  return FormatConfigValues(STATUS_PARAMS_FORMAT);
}

String FormatConfigValues(String format) {
  format.replace("{mins_up}", String((unsigned long)(esp_timer_get_time() / 1000000 / 60)));
  format.replace("{wifi_signal}", String(WiFi.RSSI()));
  format.replace("{counter_cycles}", String(cycle_count));
  format.replace("{counter_pics_motion}", String(pics_count_motion));
  format.replace("{counter_pics_gs}", String(pics_count_cloud_gs));
  format.replace("{counter_pics_sd}", String(pics_count_sd));
  format.replace("{bytes_free}", String(ESP.getFreeHeap()));
  format.replace("{temperature_celsius}", String((long)temperatureRead()));
  format.replace("{used_size_mb}", String((unsigned long)GetUsedSizeMB()));

  return format;
}

// Sets the configuration from the /config.txt file in the SD card, if present
void setConfigFromFile() {
  String tmp;
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

        Serial.println("Read from config file: " + DEVICE_NAME + ", " + SSID + ", " + PASSWORD);
        
        if(file.available()) { tmp = file.readStringUntil('\n'); tmp.trim(); if (tmp.length() > 5) { LOCAL_IP.fromString(tmp); } }
        if(file.available()) { tmp = file.readStringUntil('\n'); tmp.trim(); if (tmp.length() > 5) { GATEWAY.fromString(tmp); } }
        if(file.available()) { tmp = file.readStringUntil('\n'); tmp.trim(); if (tmp.length() > 5) { SUBNET.fromString(tmp); } }
        if(file.available()) { tmp = file.readStringUntil('\n'); tmp.trim(); if (tmp.length() > 5) { PRIMARYDNS.fromString(tmp); } }
        if(file.available()) { tmp = file.readStringUntil('\n'); tmp.trim(); if (tmp.length() > 5) { SECONDARYDNS.fromString(tmp); } }
      }
      file.close();
    }
  }
}

// Refreshes the clock, the cycles configuration and camera configuration from gs web
String refreshConfigFromWeb() {
  Serial.println("-- CONFIG REFRESH --");

  // Call getConfig google script and store in PARAMS
  String url = String(SCRIPT_URL_GET_CONFIG) + "?device=" + String(DEVICE_NAME);
  HttpResponse response = GetHttpGetResponseBody(SCRIPT_DOMAIN, 443, url.c_str());
  if (response.location.length() > 4) {
      response = GetHttpGetResponseBody(SCRIPT_DOMAIN, 443, response.location.c_str());
  }

  String responseBody = response.body;

  if (response.status == 200 && responseBody.indexOf(":") > 0) {
    String configString = responseBody.substring(responseBody.indexOf(":") + 1, responseBody.length());
    unsigned long epoch;

    if (lastConfigString != configString) {
      Serial.println(String(lastConfigString.length() == 0 ? "Initial" : "New") + " params: " + configString);
      lastConfigString = configString;
      cycle_count = 1;

      //${unixDate}:${minCycleSeconds},${period_gs},${period_sd},${period_conf},${flash},${frame_size},${v_flip},${quality},${telegram_chat_id},${re}
      sscanf(responseBody.c_str(), "%lu:%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", &epoch, &PARAMS.min_cycle_seconds, &PARAMS.period_telegram, &PARAMS.period_gs_cloud, &PARAMS.period_sd_card,
      &PARAMS.period_config_refresh, &PARAMS.period_restart, &PARAMS.flash, &PARAMS.frame_size, &PARAMS.vflip, &PARAMS.brigthness, &PARAMS.saturation, &PARAMS.quality, &PARAMS.motion,	&PARAMS.min_motion_cycle_seconds);

      Serial.printf("New values\r\nepoch: %lu, min_cycle: %d, period_tlgm: %d, period_gs: %d, period sd: %d, period conf: %d, period restart: %d, flash: %d, framesize: %d, vflip: %d, bright: %d. sat: %d. quality: %d. motion: %d. min motion: %d\r\n", 
        epoch, PARAMS.min_cycle_seconds, PARAMS.period_telegram, PARAMS.period_gs_cloud, PARAMS.period_sd_card,
        PARAMS.period_config_refresh, PARAMS.period_restart, PARAMS.flash, PARAMS.frame_size, PARAMS.vflip, PARAMS.brigthness, PARAMS.saturation, PARAMS.quality,
        PARAMS.motion, PARAMS.min_motion_cycle_seconds);

      // Set cam settings
      setCamConfigFromParams();

    } else {
      Serial.println("Params didn't change");
      sscanf(responseBody.c_str(), "%U:%*s", &epoch);
    }

    // Set clock time
    rtc.setTime(epoch);
    Serial.println(rtc.getTime("Set clock to: %A, %B %d %Y %H:%M:%S"));

  }
  
  return responseBody;
}

void setCamConfigFromParams() {
  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, (framesize_t)PARAMS.frame_size); 
  s->set_vflip(s, PARAMS.vflip); 
  s->set_quality(s, PARAMS.quality); 
  s->set_brightness(s, PARAMS.brigthness);
  s->set_saturation(s, PARAMS.saturation);  
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

// Telegram
void HandleTelegramMessage(const String& text, const String& chat_id, const String& from)
{
  if (text == "/start")
    {
      const String commands = F("["
                        "{\"command\":\"pic\", \"description\":\"Take a photo\"},"
                        "{\"command\":\"picflash\", \"description\":\"Take a photo with flash\"},"
                        "{\"command\":\"status\", \"description\":\"Get the current status\"},"
                        "{\"command\":\"reconfig\", \"description\":\"Reconfigure the device from web\"},"
                        "{\"command\":\"options\",\"description\":\"Show the options menu\"}"
                        "]");
      telegramBot.SetCommands(commands);
    }
    else if (text == "/help" || text == "/options") 
    {
      String keyboardJson = "[[\"/pic\", \"/picflash\", \"/status\"]]";
      telegramBot.SendMessageWithReplyKeyboard("Choose from one of the following options", keyboardJson);
    }    
    else if (text == "/status") 
    {
        telegramBot.SendMessage(GetStatusMessage());
    }
    else if (text.startsWith("/pic")) 
    {
        bool flash = text.indexOf("flash") > 0;
        camera_fb_t* fb = TakePhoto(flash);
        telegramBot.SendImage(fb->buf, fb->len);
    }
    else if (text == "/reconfig") 
    {
        reconfigOnNextCycle = true;
    }
    else if (text == "/restart") 
    {
        ESP.restart();
    }
}

