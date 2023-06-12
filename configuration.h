/* ThePirat 2023 - Configuration constants and parameters handling */
#pragma once

#include "Arduino.h"


// Config file format: Device Name \n WiFi name \n WiFi password \n IP Address \n Gateway \n Subnet \n PrimatyDNS \n SecondaryDNS
const char* CONFIG_FILE = "/config.txt";

// Config. These configs can be overriden in the config.txt file
String DEVICE_NAME = "CAM1";
String SSID = "THEPIRAT_2.4G"; // "ThePirat++";
String PASSWORD = "333" + String(175 * 23029);
IPAddress LOCAL_IP(192, 168, 1, 103);
IPAddress GATEWAY(192, 168, 1, 1);
IPAddress SUBNET(255, 255, 255, 0);
IPAddress PRIMARYDNS(8, 8, 8, 8); // WiFiClientSecure.connect will fail if DNS is not set
IPAddress SECONDARYDNS(8, 8, 4, 4);

// AP config
const char *SOFT_AP_SSID          = "EspCam (1-9)";    
const char *SOFT_AP_PASSWORD      = "123456789";

// Google app scripts URLs
const char* SCRIPT_URL_SEND_IMAGE = "/macros/s/AKfycbx98K1CEm6J2UOru5oSj10g2O3X8aDDsXQDcugzOxXAo_Um1btoAK8wBxRF6a3NhbYH/exec";
const char* SCRIPT_URL_GET_CONFIG = "/macros/s/AKfycbx98K1CEm6J2UOru5oSj10g2O3X8aDDsXQDcugzOxXAo_Um1btoAK8wBxRF6a3NhbYH/exec";
const char* SCRIPT_DOMAIN = "script.google.com";

struct Params
{
    int min_cycle_seconds = 60;
    int period_gs_cloud = 1;
    int period_sd_card = 2;
    int period_config_refresh = 5;
    int period_restart = 100;
    bool flash = false;
    int frame_size = 12;
    bool vflip = 0;
    int brigthness = 0;
    int saturation = 0;
    int quality = 14;
    
    // Minimum time in ms between motion detection processing (i.e. to avoid quota limits)
    int min_motion_cycle_seconds = 5;
    bool motion = true;
} PARAMS;