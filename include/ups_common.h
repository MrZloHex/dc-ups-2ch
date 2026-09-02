// SPDX-License-Identifier: MIT
// DC-UPS-2CH — общий заголовок / shared declarations
//
// Здесь только объявления. Определения глобального состояния — в src/ups_common.cpp,
// определения функций — в соответствующем модуле (см. include/*.h рядом).
#pragma once

#include <Arduino.h>
#include <Preferences.h>

// -------------------- ПРОШИВКА / FIRMWARE --------------------
#define FW_VERSION           "2026.09.02-v6"
#define MDNS_HOST            "dc-ups"
#define AP_SSID              "DC-UPS-Setup"
#define AP_DEFAULT_PASSWORD  "dc-ups-setup"

// -------------------- ПИНЫ --------------------
#define PIN_VBATT        35
#define PIN_VGRID        34
#define PIN_LOAD_ROUTER  32
#define PIN_LOAD_ONT     25
#define PIN_BUTTON       14
#define PIN_LED_GRID     13
#define PIN_LED_BATT     12

// Делители 100к/10к -> x11 (замеры строго на ADC1: GPIO32..39).
constexpr float DIV_VBATT = (100.0f + 10.0f) / 10.0f;
constexpr float DIV_VGRID = (100.0f + 10.0f) / 10.0f;

// -------------------- ТАЙМИНГИ --------------------
constexpr uint32_t WDT_TIMEOUT_SEC          = 30;
constexpr uint32_t ROUTER_BOOT_MS           = 25000UL;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS  = 30000UL;
constexpr uint32_t WIFI_RECONNECT_MS        = 20000UL;
constexpr uint32_t PORTAL_STOP_DELAY_MS     = 15000UL;
constexpr uint32_t NTFY_RETRY_MS            = 10000UL;
constexpr uint32_t LOAD_RESTART_OFF_MS      = 5000UL;
constexpr uint32_t ACTION_RESPONSE_DELAY_MS = 750UL;

constexpr uint32_t BUTTON_DEBOUNCE_MS       = 40UL;
constexpr uint32_t BUTTON_DOUBLE_MS         = 450UL;
constexpr uint32_t BUTTON_SHELF_HOLD_MS     = 10000UL;
constexpr uint32_t BUTTON_PORTAL_MS         = 15UL * 60UL * 1000UL;

constexpr uint32_t NTFY_COMMAND_POLL_MS     = 15000UL;

constexpr uint32_t INTERNET_CHECK_MS        = 60000UL;
constexpr uint32_t HEALTHY_RESET_MS         = 10UL * 60UL * 1000UL;
constexpr uint8_t  AUTO_MAX_POWER_CYCLES    = 2;
constexpr uint8_t  AUTO_MAX_ESP_REBOOTS     = 1;
constexpr uint32_t PORTAL_REOPEN_DELAY_MS   = 5UL * 60UL * 1000UL;

// -------------------- ПЕРЕЧИСЛЕНИЯ --------------------
enum Mode            { MODE_RUN, MODE_PORTAL };
enum BattState       { BATT_OK, BATT_WARN, BATT_CRIT };
enum LoadMode        { LM_AUTO, LM_ON, LM_OFF };
enum WifiRetryState  { WIFI_RETRY_IDLE, WIFI_RETRY_WAIT_ROUTER, WIFI_RETRY_CONNECTING };
enum PowerCycleState { PC_IDLE, PC_WAIT_OFF, PC_OFF_WAIT };
enum PowerTarget     { TARGET_NONE, TARGET_ROUTER, TARGET_ONT, TARGET_BOTH };

// -------------------- КОНФИГ --------------------
struct Config
{
    String ssid, pass, ntfy, apPass, adminPass;
    float calVbatt, calVgrid;
    float battCutoff, battRestore, battWarn, battSleep;
    float gridOn, gridOff;
    uint32_t gridDebounce;

    bool deepSleepEnabled;
    uint32_t sleepCheckSec;
    uint32_t sleepDelaySec;

    bool autoRecoveryEnabled;
    bool autoEspRebootEnabled;
    uint32_t netBadSec;
    uint32_t portalIdleSec;
};

extern Config      cfg;
extern Preferences prefs;

// -------------------- УТИЛИТЫ --------------------
String   formatDuration(uint32_t ms);
String   htmlEscape(String s);
String   jsonEscape(String s);
void     waitWithWatchdog(uint32_t ms);
float    readVoltage(int pin, float div, float cal);
String   panelUrl();
String   mdnsUrl();
String   modeText(LoadMode m);
String   targetText(PowerTarget t);
bool     targetIncludesRouter(PowerTarget t);
bool     targetIncludesOnt(PowerTarget t);
