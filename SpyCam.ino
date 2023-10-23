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
#include <esp_wifi.h>
#include <Preferences.h>
#include "configuration.h"
#include "esp_camera.h"
#include "camera_pins.h"
#include "sd_card.h"
#include "http_client.h"
#include "app_httpd.h"
#include "telegram.h"

#include "base64.h"

#ifdef DISABLE_BROWNOUT
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#endif

unsigned long wifi_prev_ms = 0;
unsigned long wifi_interval = 30000;
bool offline = true;
ESP32Time rtc;
String lastConfigString = "";
String publicIpAddress = "";
unsigned int cycle_count = 1;
esp_reset_reason_t reset_reason;
Preferences preferences;

void HandleTelegramMessage(const String& text, const String& chat_id, const String& from, long message_id);
Telegram *telegramBot;

// Pics counters since restart
unsigned long pics_count_cloud_gs;    //Cloud gs pics count
unsigned long pics_count_sd;          //SD card pics count
unsigned long pics_count_motion;      //Motion pics count 

// Config query paramaters for gs script POST call
const String EXTRA_PARAMS_FORMAT = "&su={mins_up}&ws={wifi_signal}&cc={counter_cycles}&cm={counter_pics_motion}&cg={counter_pics_gs}&cs={counter_pics_sd}&bf={bytes_free}&tc={temperature_celsius}&us={used_size_mb}";
// Status values format
const String STATUS_PARAMS_FORMAT = "{mins_up} mins up - WiFi: {wifi_signal} dBm\r\nCounters: Cycle={counter_cycles} - MO={counter_pics_motion} GS={counter_pics_gs} SD={counter_pics_sd}\r\n{bytes_free} bytes free - Temp: {temperature_celsius}ºC - Used: {used_size_mb}MB";

bool reconfigOnNextCycle = false;

// **** SETUP **** //
void setup() 
{
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  esp_reset_reason_t reason = esp_reset_reason();
  
  preferences.begin("SpyCam", false);
  telegramBot = new Telegram(TLGRM_BOT_TOKEN, TLGRM_CHAT_ID, preferences.getLong("message_id"), HandleTelegramMessage);

  GetCounters();
  
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
void loop() 
{
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
    telegramBot->SendMessage(DEVICE_NAME + " is online. Reset reason: " + String(reset_reason) + "\r\n" + GetStatusMessage());
  }

  // Check for telegram new message commands
  if (!isStreaming && !offline && PARAMS.period_telegram > 0 && (cycle_count % PARAMS.period_telegram == 0)) {
    telegramBot->ProcessInputMessages();
  }

  // Capture image and send to google drive
  if (!isStreaming && !offline && PARAMS.period_gs_cloud > 0 && (cycle_count % PARAMS.period_gs_cloud == 0)) {
    CaptureAndSend(false, true);
  }

  // Capture and store to SD_CARD
  if (!isStreaming && PARAMS.period_sd_card > 0 && (cycle_count % PARAMS.period_sd_card == 0)) {
    CaptureAndStore(1);
  }

  // Reconfigure from web
  if (!offline && ((PARAMS.period_config_refresh > 0 && (cycle_count % PARAMS.period_config_refresh == 0)) || reconfigOnNextCycle)) {
    String newConfig = refreshConfigFromWeb();
    if (reconfigOnNextCycle) {
      telegramBot->SendMessage("Reconfigured with: " + newConfig);
      reconfigOnNextCycle = false;
    }
  }
  
  // Restart cycle
  if (PARAMS.period_restart > 0 && (cycle_count % PARAMS.period_restart == 0)) {
    Restart();
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

void motionDetection(unsigned long sleep_end_time) 
{
  int motion_detected = digitalRead(PIR_SENSOR_NUM);
  if (!isStreaming && motion_detected == HIGH) {
    Serial.println("Motion detected !");
    const unsigned long motion_start_time = millis();

    // Capture image and store to SD card 
    CaptureAndStore(1);

    // Upload image and notify
    if (!offline) {
      CaptureAndSend(true, true);
    }

    const unsigned long motion_elapsed_ms = millis() - motion_start_time;
    const unsigned long motion_next_cycle_ms = (PARAMS.min_motion_cycle_seconds * 1000UL) > motion_elapsed_ms ? (PARAMS.min_motion_cycle_seconds * 1000) - motion_elapsed_ms : 0; 
    const unsigned long motion_sleep_ms = min(sleep_end_time - millis()/*ms for the next main cycle*/, motion_next_cycle_ms);
    Serial.println("Motion elapsed " + String(motion_elapsed_ms) + " ms. Will delay on motion for " + String(motion_sleep_ms) + " ms");
    delay(motion_sleep_ms);
  }
}

void DiscardImages(int count)
{
    for(int i = 0; i < count; i++) {
      camera_fb_t* fb = esp_camera_fb_get();  
      if (!fb) {
        return;
      }
      esp_camera_fb_return(fb);
      delay(1);
  }
}

bool initCamera() 
{
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
  
  // Discard first images
  DiscardImages(5);

  return true;
}

bool startWiFi() 
{
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
  if (!WiFi.config(LOCAL_IP, GATEWAY, SUBNET, PRIMARYDNS, SECONDARYDNS)) 
  {
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

  // Get the public IP 
  publicIpAddress = connected ? GetPublicIp() : "Disconnected";

  Serial.println("");
  Serial.println(connected ? "WiFi connected" : "Offline mode");
  Serial.printf("Public IP: %s\r\n", publicIpAddress.c_str());
  Serial.printf("Private IP: %s\r\nGateway: %s\r\nSubnet: %s\r\nPrimary DNS: %s\r\nSecondary DNS: %s\r\n", LOCAL_IP.toString().c_str(),
        GATEWAY.toString().c_str(), SUBNET.toString().c_str(), PRIMARYDNS.toString().c_str(), SECONDARYDNS.toString().c_str());
        
  if (connected) 
  {
      // Start the camera web server
      Serial.printf("Connected to %s. Signal: %d dBm.\r\n", SSID.c_str(), WiFi.RSSI());
      startCameraServer(WEB_SERVER_USER, WEB_SERVER_PASSWORD, SERVER_PORT);
      Serial.printf("Web Server Ready! (STA) Use 'http://%s' to connect or 'http://%s'\r\n%s:%s\r\n", WiFi.localIP().toString().c_str(), publicIpAddress.c_str(), WEB_SERVER_USER, WEB_SERVER_PASSWORD);
      #ifndef DISABLE_AP      
      Serial.println("Web Server Ready! (AP) Use 'http://" + WiFi.softAPIP().toString() + "' to connect");
      #endif      
  }

  return connected;  
}

void reconnectWifi() 
{
  unsigned long current_ms = millis();

  if ((WiFi.status() != WL_CONNECTED) && (current_ms - wifi_prev_ms >= wifi_interval)) {
    Serial.println("Reconnecting to WiFi... Status: " + String(WiFi.status()));
    WiFi.disconnect();
    startWiFi();
    wifi_prev_ms = current_ms;
  }
}

camera_fb_t* TakePhoto(bool flash) 
{
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

camera_fb_t* TakePhoto() 
{
  return TakePhoto(PARAMS.flash);
}

// If forget==true, this function will be much faster but will not wait for the server response
String CaptureAndSend(bool isMotionDetected, bool forget) 
{
  if (isMotionDetected) {
    Serial.println("-- MOTION CAPTURE, STORE IN CLOUD & SEND TO TELEGRAM --");  
  } else {
    Serial.println("-- CAPTURE & STORE IN CLOUD --");  
  }
  camera_fb_t* fb = TakePhoto();
  if(!fb) {
    Serial.println("Camera capture failed");
    return "Camera capture failed";
  }  

  ledOn();

  String queryString = "?device=" + String(DEVICE_NAME) + "&telegram=" + (isMotionDetected ? "1" : "0") + FormatConfigValues(EXTRA_PARAMS_FORMAT);
  String url = SCRIPT_URL_SEND_IMAGE + queryString;
  String body = "{\"image\": \"data:image/png;base64," + base64::encode(fb->buf, fb->len) + "\"}"; 
  esp_camera_fb_return(fb);
  HttpResponse response = HttpPost(url, body, false);
  if (!forget && response.status == 302) {
    response = HttpGet(response.location);
  }

  ledOff();
  return response.body;
}

String CaptureAndStore(int count) 
{
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
      SaveCounters();
    } else {
      Serial.println("Saving failed");
    }

    esp_camera_fb_return(fb);
  }

  return "";
}

String GetStatusMessage() 
{
  String status = DEVICE_NAME + " IP: " + WiFi.localIP().toString() + " / " + publicIpAddress + "\r\n"  ;
  status.concat(FormatConfigValues(STATUS_PARAMS_FORMAT));

  return status;
}

String FormatConfigValues(String format) 
{
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
void setConfigFromFile() 
{
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
        if(file.available()) { tmp = file.readStringUntil('\n'); tmp.trim(); if (tmp.length() > 1) { SERVER_PORT = tmp.toInt(); } }
      }
      file.close();
    }
  }
}

// Refreshes the clock, the cycles configuration and camera configuration from gs web
String refreshConfigFromWeb() 
{
  Serial.println("-- CONFIG REFRESH --");

  // Call getConfig google script and store in PARAMS
  String url = String(SCRIPT_URL_GET_CONFIG);
  url.replace("{name}", String(DEVICE_NAME));

  HttpResponse response = HttpGet(url);
  String responseBody = response.body;
  int statusCode = response.status;

  if (statusCode == 200 && responseBody.indexOf(":") > 0) {
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
  } else {
    Serial.println("Status code received: " + String(statusCode) + " -> " + responseBody);
  }
  
  return responseBody;
}

void setCamConfigFromParams() 
{
  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, (framesize_t)PARAMS.frame_size); 
  s->set_vflip(s, PARAMS.vflip); 
  s->set_quality(s, PARAMS.quality); 
  s->set_brightness(s, PARAMS.brigthness);
  s->set_saturation(s, PARAMS.saturation);  
}

// Serial commands (for debug)
void serialInput() 
{
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
void HandleTelegramMessage(const String& text, const String& chat_id, const String& from, long message_id)
{
  Serial.println("Message ID: " + String(message_id));
  preferences.putLong("message_id", message_id);

  if (text == "/start")
    {
      const String commands = F("["
                        "{\"command\":\"pic\", \"description\":\"Take a photo\"},"
                        "{\"command\":\"gs\", \"description\":\"Take a photo and send to google\"},"
                        "{\"command\":\"sd\", \"description\":\"Take a photo and store in SD\"},"
                        "{\"command\":\"picflash\", \"description\":\"Take a photo with flash\"},"
                        "{\"command\":\"status\", \"description\":\"Get the current status\"},"
                        "{\"command\":\"reconfig\", \"description\":\"Reconfigure the device from web\"},"
                        "{\"command\":\"options\",\"description\":\"Show the options menu\"}"
                        "]");
      telegramBot->SetCommands(commands);
    }
    else if (text == "/help" || text == "/options") 
    {
      String keyboardJson = "[[\"/pic\", \"/picflash\", \"/status\", \"/gs\", \"/sd\"]]";
      telegramBot->SendMessageWithReplyKeyboard("Choose from one of the following options", keyboardJson);
    }    
    else if (text == "/status") 
    {
        telegramBot->SendMessage(GetStatusMessage());
    }
    else if (text.startsWith("/pic")) 
    {
        bool flash = text.indexOf("flash") > 0;
        camera_fb_t* fb = TakePhoto(flash);
        if(fb) {
          telegramBot->SendImage(fb->buf, fb->len);
          
          esp_camera_fb_return(fb);
        } else {
          Serial.println("Camera capture failed");
          telegramBot->SendMessage("Camera capture failed");
        }
    }
    else if (text == "/gs") {
      // Send to google
      telegramBot->SendMessage("Send to google");
      CaptureAndSend(false, true);
    }
    else if (text == "/sd") {
      // Capture to SD
      telegramBot->SendMessage("Capture to SD");
      CaptureAndStore(1);
    }    
    else if (text == "/reconfig") 
    {
      reconfigOnNextCycle = true;
    }
    else if (text == "/restart") 
    {
        Restart();
    }
}

void Restart() 
{
  Serial.println("Will restart...");
  SaveCounters();
  preferences.end();
  ESP.restart();
}

void GetCounters() 
{
  pics_count_cloud_gs = preferences.getULong("count_gs");
  pics_count_sd = preferences.getULong("count_sd");
  pics_count_motion = preferences.getULong("count_mo");
}

void SaveCounters() 
{
  preferences.putULong("count_gs", pics_count_cloud_gs);
  preferences.putULong("count_sd", pics_count_sd);
  preferences.putULong("count_mo", pics_count_motion);
}