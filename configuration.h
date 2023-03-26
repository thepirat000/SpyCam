/* ThePirat 2023 - Configuration constants and parameters handling */
#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include "Arduino.h"

const String DEVICE_NAME = "CAM1";

// STA config
const char* SSID = "THEPIRAT"; // "ThePirat++";
const String PASSWORD = "333" + String(175 * 23029);
IPAddress LOCAL_IP(192, 168, 15, 3);
IPAddress GATEWAY(192, 168, 15, 1);
IPAddress SUBNET(255, 255, 255, 0);
IPAddress PRIMARYDNS(8, 8, 8, 8); // WiFiClientSecure.connect will fail if DNS is not set
IPAddress SECONDARYDNS(8, 8, 4, 4);

// Google app scripts URLs
const char* SCRIPT_URL_SEND_IMAGE = "/macros/s/AKfycbx98K1CEm6J2UOru5oSj10g2O3X8aDDsXQDcugzOxXAo_Um1btoAK8wBxRF6a3NhbYH/exec";
const char* SCRIPT_URL_GET_CONFIG = "/macros/s/AKfycbx98K1CEm6J2UOru5oSj10g2O3X8aDDsXQDcugzOxXAo_Um1btoAK8wBxRF6a3NhbYH/exec";
const char* SCRIPT_DOMAIN = "script.google.com";

// AP config
const char *SOFT_AP_SSID          = "EspCam (1-9)";    
const char *SOFT_AP_PASSWORD      = "123456789";

struct Params
{
    int min_cycle_seconds = 60;
    int period_gs_cloud = 1;
    int period_sd_card = 2;
    int period_config_refresh = 5;
    int period_restart = 100;
    bool flash = false;
    int frame_size = 13;
    bool vflip = 0;
    int quality = 10;
} PARAMS;

void setClock();
void getConfigFromWeb();

#endif // CONFIGURATION_H