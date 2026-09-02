/*
 * ИБП-контроллер на ESP32 — ntfy + веб-панель + WiFi recovery
 * ==================================================================
 *  - замер АКБ и 24В через ADC1
 *  - детект наличия сети с гистерезисом и антидребезгом
 *  - два независимых выхода: ROUTER GPIO32 и ONT GPIO26 (P-MOSFET через NPN)
 *  - защита АКБ от глубокого разряда
 *  - ntfy-уведомления
 *  - веб-панель
 *  - captive portal DC-UPS-Setup, если WiFi не поднялся
 *  - повторная попытка подключения к WiFi из портала БЕЗ перезагрузки
 *  - при каждом успешном подключении к WiFi в ntfy отправляется IP панели
 *  - кнопка GPIO14: короткое = restart ROUTER, двойное = restart ONT, удержание 5с = setup AP
 *
 * Делители 100к/10к -> x11. Замеры строго на ADC1 (GPIO32-39).
 * Внешних библиотек не требуется.
 *
 * ЖЕЛЕЗО:
 *  - P-MOSFET high-side управляется через 2N2222/NPN.
 *  - GPIO HIGH -> NPN открыт -> gate P-MOSFET вниз -> НАГРУЗКА ВКЛ.
 *  - Подтяжка gate P-MOSFET к source: 10 кОм.
 * ==================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include "esp_task_wdt.h"
#include "esp_sleep.h"

// -------------------- ПИНЫ --------------------
#define PIN_VBATT    35
#define PIN_VGRID    34
#define PIN_LOAD_ROUTER 32
#define PIN_LOAD_ONT    25
#define PIN_BUTTON      14
#define PIN_LED_GRID    13
#define PIN_LED_BATT    12

// -------------------- ADC --------------------
const float DIV_VBATT = (100.0 + 10.0) / 10.0;   // 11.0
const float DIV_VGRID = (100.0 + 10.0) / 10.0;   // 11.0

// -------------------- WiFi / WDT --------------------
#define AP_SSID "DC-UPS-Setup"
#define AP_DEFAULT_PASSWORD "dc-ups-setup"
#define MDNS_HOST "dc-ups"
#define FW_VERSION "2026.09.02-v5"
#define WDT_TIMEOUT 30

// Время, которое даём роутеру/ONT поднять WiFi после включения нагрузки.
// Если твой роутер стартует быстрее, можно уменьшить.
const uint32_t ROUTER_BOOT_MS           = 25000UL;
const uint32_t WIFI_CONNECT_TIMEOUT_MS  = 30000UL;
const uint32_t WIFI_RECONNECT_MS        = 20000UL;
const uint32_t PORTAL_STOP_DELAY_MS     = 15000UL;
const uint32_t NTFY_RETRY_MS            = 10000UL;
const uint32_t LOAD_RESTART_OFF_MS       = 5000UL;
const uint32_t ACTION_RESPONSE_DELAY_MS  = 750UL;

// Тактовая кнопка GPIO14, замыкает вход на GND (INPUT_PULLUP).
const uint32_t BUTTON_DEBOUNCE_MS        = 40UL;
const uint32_t BUTTON_DOUBLE_MS          = 450UL;
const uint32_t BUTTON_LONG_MS            = 5000UL;
const uint32_t BUTTON_PORTAL_MS          = 15UL*60UL*1000UL;

// Автовосстановление связи.
const uint32_t INTERNET_CHECK_MS          = 60000UL;      // проверка раз в минуту
const uint32_t HEALTHY_RESET_MS           = 10UL*60UL*1000UL;
const uint8_t  AUTO_MAX_POWER_CYCLES      = 2;
const uint8_t  AUTO_MAX_ESP_REBOOTS       = 1;
const uint32_t PORTAL_REOPEN_DELAY_MS     = 5UL*60UL*1000UL;

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

Config cfg;
Preferences prefs;
WebServer server(80);
DNSServer dns;

enum Mode      { MODE_RUN, MODE_PORTAL };
enum BattState { BATT_OK, BATT_WARN, BATT_CRIT };
enum LoadMode  { LM_AUTO, LM_ON, LM_OFF };
enum WifiRetryState { WIFI_RETRY_IDLE, WIFI_RETRY_WAIT_ROUTER, WIFI_RETRY_CONNECTING };
enum PowerCycleState { PC_IDLE, PC_WAIT_OFF, PC_OFF_WAIT };
enum PowerTarget { TARGET_NONE, TARGET_ROUTER, TARGET_ONT, TARGET_BOTH };

Mode mode = MODE_RUN;
BattState battState = BATT_OK;
LoadMode routerMode = LM_AUTO;
LoadMode ontMode = LM_AUTO;
WifiRetryState wifiRetryState = WIFI_RETRY_IDLE;
PowerCycleState powerCycleState = PC_IDLE;
PowerTarget powerCycleTarget = TARGET_NONE;

bool routerOn = false;
bool ontOn = false;
bool gridPresent = true;
bool gridCandidate = true;
bool warnSent = false;
bool lvdTripped = false;

bool portalActive = false;
bool webStarted = false;
bool wifiWasConnected = false;
bool everWifiConnected = false;
bool mdnsStarted = false;

bool ntfyIpPending = false;
String ntfyIpReason;

// RTC-память переживает deep sleep и software reset.
// Нужна, чтобы аварийный сон и лимиты autorecovery не превращались в бесконечный цикл.
RTC_DATA_ATTR bool rtcEmergencySleep = false;
RTC_DATA_ATTR uint32_t rtcSleepWakeCount = 0;
RTC_DATA_ATTR uint8_t rtcAutoPowerCycles = 0;
RTC_DATA_ATTR uint8_t rtcNetReboots = 0;

unsigned long gridChangeSince = 0;
unsigned long lastTick = 0;
unsigned long lastReconnect = 0;
unsigned long routerTurnedOnAt = 0;
unsigned long ontTurnedOnAt = 0;
unsigned long wifiRetryStarted = 0;
unsigned long wifiRetryWaitUntil = 0;
unsigned long portalStopAt = 0;
unsigned long ntfyNextTry = 0;
unsigned long powerCycleAt = 0;
unsigned long rebootAt = 0;
unsigned long outageStartedAt = 0;
unsigned long lastOutageDurationMs = 0;
uint32_t outageCount = 0;

unsigned long sleepLowSince = 0;
unsigned long lastInternetCheck = 0;
unsigned long networkBadSince = 0;
unsigned long internetHealthySince = 0;
unsigned long portalLastActivity = 0;
unsigned long portalReopenAt = 0;
unsigned long manualPortalUntil = 0;
bool manualPortalSession = false;

// Состояние тактовой кнопки.
bool buttonRaw = HIGH;
bool buttonStable = HIGH;
bool buttonLongHandled = false;
uint8_t buttonClickCount = 0;
unsigned long buttonRawChangedAt = 0;
unsigned long buttonPressedAt = 0;
unsigned long buttonFirstClickAt = 0;

bool internetKnown = false;
bool internetReachable = false;
String recoveryStatus = "ожидание";
bool powerCycleAutomatic = false;
String powerCycleReason;

float lastVbatt = 0.0f;
float lastVgrid = 0.0f;
String wifiStatusText = "инициализация";
String nextWifiConnectReason;

// Небольшой журнал событий только в RAM. После перезагрузки очищается.
const uint8_t EVENT_LOG_SIZE = 60;
String eventLog[EVENT_LOG_SIZE];
uint8_t eventLogHead = 0;
uint8_t eventLogCount = 0;

// ============================================================
// CONFIG
// ============================================================

void loadConfig()
{
    prefs.begin("ups", true);

    cfg.ssid = prefs.getString("ssid", "");
    cfg.pass = prefs.getString("pass", "");
    cfg.ntfy = prefs.getString("ntfy", "");
    cfg.apPass = prefs.getString("apPass", AP_DEFAULT_PASSWORD);
    cfg.adminPass = prefs.getString("adminPass", "");

    cfg.calVbatt = prefs.getFloat("calVbatt", 1.0f);
    cfg.calVgrid = prefs.getFloat("calVgrid", 1.0f);

    cfg.battCutoff  = prefs.getFloat("cutoff",  11.0f);
    cfg.battRestore = prefs.getFloat("restore", 12.5f);
    cfg.battWarn    = prefs.getFloat("warn",    11.5f);
    cfg.battSleep   = prefs.getFloat("sleepV",  10.8f);

    cfg.gridOn  = prefs.getFloat("gridOn",  15.0f);
    cfg.gridOff = prefs.getFloat("gridOff", 10.0f);
    cfg.gridDebounce = prefs.getUInt("deb", 3000);

    cfg.deepSleepEnabled = prefs.getBool("deepSleep", true);
    cfg.sleepCheckSec = prefs.getUInt("sleepChk", 30);
    cfg.sleepDelaySec = prefs.getUInt("sleepDelay", 30);

    cfg.autoRecoveryEnabled = prefs.getBool("autoRec", true);
    cfg.autoEspRebootEnabled = prefs.getBool("autoEsp", false);
    cfg.netBadSec = prefs.getUInt("netBadSec", 180);
    cfg.portalIdleSec = prefs.getUInt("portalIdle", 900);

    // Эти два счётчика пишутся крайне редко и переживают software reset.
    rtcAutoPowerCycles = prefs.getUChar("recCycles", rtcAutoPowerCycles);
    rtcNetReboots = prefs.getUChar("recReboots", rtcNetReboots);

    // Защита от случайно сохранённых абсурдных значений.
    if (cfg.battSleep > cfg.battCutoff - 0.1f) cfg.battSleep = cfg.battCutoff - 0.1f;
    if (cfg.battSleep < 9.0f) cfg.battSleep = 9.0f;
    if (cfg.sleepCheckSec < 10) cfg.sleepCheckSec = 10;
    if (cfg.sleepCheckSec > 3600) cfg.sleepCheckSec = 3600;
    if (cfg.sleepDelaySec > 600) cfg.sleepDelaySec = 600;
    if (cfg.netBadSec < 60) cfg.netBadSec = 60;
    if (cfg.netBadSec > 3600) cfg.netBadSec = 3600;
    if (cfg.portalIdleSec < 60) cfg.portalIdleSec = 60;
    if (cfg.portalIdleSec > 86400) cfg.portalIdleSec = 86400;

    prefs.end();
}

void saveConfig()
{
    prefs.begin("ups", false);

    prefs.putString("ssid", cfg.ssid);
    prefs.putString("pass", cfg.pass);
    prefs.putString("ntfy", cfg.ntfy);
    prefs.putString("apPass", cfg.apPass);
    prefs.putString("adminPass", cfg.adminPass);

    prefs.putFloat("calVbatt", cfg.calVbatt);
    prefs.putFloat("calVgrid", cfg.calVgrid);

    prefs.putFloat("cutoff",  cfg.battCutoff);
    prefs.putFloat("restore", cfg.battRestore);
    prefs.putFloat("warn",    cfg.battWarn);
    prefs.putFloat("sleepV",  cfg.battSleep);

    prefs.putFloat("gridOn",  cfg.gridOn);
    prefs.putFloat("gridOff", cfg.gridOff);
    prefs.putUInt("deb",      cfg.gridDebounce);

    prefs.putBool("deepSleep", cfg.deepSleepEnabled);
    prefs.putUInt("sleepChk", cfg.sleepCheckSec);
    prefs.putUInt("sleepDelay", cfg.sleepDelaySec);

    prefs.putBool("autoRec", cfg.autoRecoveryEnabled);
    prefs.putBool("autoEsp", cfg.autoEspRebootEnabled);
    prefs.putUInt("netBadSec", cfg.netBadSec);
    prefs.putUInt("portalIdle", cfg.portalIdleSec);

    prefs.end();
}

void saveRecoveryCounters()
{
    prefs.begin("ups", false);
    prefs.putUChar("recCycles", rtcAutoPowerCycles);
    prefs.putUChar("recReboots", rtcNetReboots);
    prefs.end();
}

// ============================================================
// HELPERS
// ============================================================

String formatDuration(uint32_t ms)
{
    uint32_t total = ms / 1000UL;
    uint32_t days = total / 86400UL;
    total %= 86400UL;
    uint32_t hours = total / 3600UL;
    total %= 3600UL;
    uint32_t minutes = total / 60UL;
    uint32_t seconds = total % 60UL;

    char buf[40];
    if (days > 0)
        snprintf(buf, sizeof(buf), "%lu д %02lu:%02lu:%02lu",
                 (unsigned long)days,
                 (unsigned long)hours,
                 (unsigned long)minutes,
                 (unsigned long)seconds);
    else
        snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
                 (unsigned long)hours,
                 (unsigned long)minutes,
                 (unsigned long)seconds);
    return String(buf);
}

void logEvent(const String &message)
{
    String line = "[" + formatDuration(millis()) + "] " + message;
    eventLog[eventLogHead] = line;
    eventLogHead = (eventLogHead + 1) % EVENT_LOG_SIZE;
    if (eventLogCount < EVENT_LOG_SIZE)
        eventLogCount++;

    Serial.println(line);
}

String getEventLogText()
{
    String out;
    out.reserve(5000);

    uint8_t first = (eventLogHead + EVENT_LOG_SIZE - eventLogCount) % EVENT_LOG_SIZE;
    for (uint8_t i = 0; i < eventLogCount; ++i)
    {
        uint8_t idx = (first + i) % EVENT_LOG_SIZE;
        out += eventLog[idx];
        out += "\n";
    }

    if (eventLogCount == 0)
        out = "Журнал пуст.\n";

    return out;
}

void clearEventLog()
{
    for (uint8_t i = 0; i < EVENT_LOG_SIZE; ++i)
        eventLog[i] = "";
    eventLogHead = 0;
    eventLogCount = 0;
    logEvent("Журнал очищен");
}

void waitWithWatchdog(uint32_t ms)
{
    unsigned long started = millis();
    while (millis() - started < ms)
    {
        esp_task_wdt_reset();
        delay(100);
    }
}

float readVoltage(int pin, float div, float cal)
{
    uint32_t acc = 0;
    for (uint8_t i = 0; i < 32; ++i)
    {
        acc += analogReadMilliVolts(pin);
        delay(2);
    }
    return ((acc / 32.0f) / 1000.0f) * div * cal;
}

BattState getBattState(float vb)
{
    if (vb <= cfg.battCutoff) return BATT_CRIT;
    if (vb <= cfg.battWarn)   return BATT_WARN;
    return BATT_OK;
}

// Используется на timer-wakeup ДО инициализации WiFi/lwIP.
// Никаких WiFi.* здесь вызывать нельзя.
[[noreturn]] void emergencySleepTimerOnly()
{
    digitalWrite(PIN_LOAD_ROUTER, LOW);
    digitalWrite(PIN_LOAD_ONT, LOW);
    routerOn = false;
    ontOn = false;
    digitalWrite(PIN_LED_GRID, LOW);
    digitalWrite(PIN_LED_BATT, LOW);

    rtcEmergencySleep = true;
    esp_sleep_enable_timer_wakeup((uint64_t)cfg.sleepCheckSec * 1000000ULL);

    Serial.printf("EMERGENCY SLEEP: wake in %lu s\n",
                  (unsigned long)cfg.sleepCheckSec);
    Serial.flush();
    delay(20);
    esp_deep_sleep_start();

    while (true) delay(1000);
}

bool anyLoadOn()
{
    return routerOn || ontOn;
}

bool networkLoadsOn()
{
    return routerOn && ontOn;
}

String modeText(LoadMode m)
{
    if (m == LM_ON) return "ручн.ВКЛ";
    if (m == LM_OFF) return "ручн.ВЫКЛ";
    return "авто";
}

String targetText(PowerTarget t)
{
    if (t == TARGET_ROUTER) return "ROUTER";
    if (t == TARGET_ONT) return "ONT";
    if (t == TARGET_BOTH) return "ROUTER+ONT";
    return "—";
}

bool targetIncludesRouter(PowerTarget t)
{
    return t == TARGET_ROUTER || t == TARGET_BOTH;
}

bool targetIncludesOnt(PowerTarget t)
{
    return t == TARGET_ONT || t == TARGET_BOTH;
}

void setRouter(bool on)
{
    if (routerOn == on) return;
    routerOn = on;
    digitalWrite(PIN_LOAD_ROUTER, on ? HIGH : LOW);
    if (on)
    {
        routerTurnedOnAt = millis();
        logEvent("ROUTER -> ВКЛ");
    }
    else
        logEvent("ROUTER -> ВЫКЛ");
}

void setOnt(bool on)
{
    if (ontOn == on) return;
    ontOn = on;
    digitalWrite(PIN_LOAD_ONT, on ? HIGH : LOW);
    if (on)
    {
        ontTurnedOnAt = millis();
        logEvent("ONT -> ВКЛ");
    }
    else
        logEvent("ONT -> ВЫКЛ");
}

void setBothLoads(bool on)
{
    setRouter(on);
    setOnt(on);
}

void setTargetPower(PowerTarget target, bool on)
{
    if (targetIncludesRouter(target)) setRouter(on);
    if (targetIncludesOnt(target)) setOnt(on);
}

unsigned long equipmentTurnedOnAt()
{
    return routerTurnedOnAt > ontTurnedOnAt ? routerTurnedOnAt : ontTurnedOnAt;
}

void updateLeds()
{
    digitalWrite(PIN_LED_GRID, gridPresent ? HIGH : LOW);

    bool red;
    if (battState == BATT_CRIT || (!gridPresent && !anyLoadOn()))
    {
        red = true;
    }
    else if (gridPresent && battState == BATT_OK)
    {
        red = false;
    }
    else
    {
        const int period = (battState == BATT_WARN) ? 250 : 1000;
        red = ((millis() / period) % 2) != 0;
    }

    digitalWrite(PIN_LED_BATT, red ? HIGH : LOW);
}

String htmlEscape(String s)
{
    s.replace("&", "&amp;");
    s.replace("\"", "&quot;");
    s.replace("'", "&#39;");
    s.replace("<", "&lt;");
    s.replace(">", "&gt;");
    return s;
}

String jsonEscape(String s)
{
    s.replace("\\", "\\\\");
    s.replace("\"", "\\\"");
    s.replace("\r", "\\r");
    s.replace("\n", "\\n");
    return s;
}

String panelUrl()
{
    if (WiFi.status() != WL_CONNECTED)
        return "";

    return "http://" + WiFi.localIP().toString() + "/";
}

String mdnsUrl()
{
    return String("http://") + MDNS_HOST + ".local/";
}

bool requireAuth()
{
    if (cfg.adminPass == "")
        return true;

    if (server.authenticate("admin", cfg.adminPass.c_str()))
        return true;

    server.requestAuthentication();
    return false;
}

void touchWebActivity()
{
    portalLastActivity = millis();
}

// ============================================================
// NTFY
// ============================================================

bool sendNtfy(const String &text)
{
    if (WiFi.status() != WL_CONNECTED || cfg.ntfy == "")
        return false;

    WiFiClientSecure client;
    HTTPClient http;

    client.setInsecure();
    client.setTimeout(5000);

    String url = "https://ntfy.sh/" + cfg.ntfy;

    if (!http.begin(client, url))
        return false;

    http.setTimeout(5000);
    http.addHeader("Title", "Дача Роутер ИБП");
    http.addHeader("Content-Type", "text/plain; charset=utf-8");

    int code = http.POST(text);

    if (code < 200 || code >= 300)
    {
        Serial.printf("[ntfy] -> %d", code);
        if (code > 0)
            Serial.printf(": %s", http.getString().c_str());
        Serial.println();
    }

    http.end();
    return code >= 200 && code < 300;
}

[[noreturn]] void enterEmergencySleep(const String &reason)
{
    // К этому моменту основная нагрузка уже должна быть снята LVD.
    // Если WiFi ещё каким-то образом доступен — пробуем отправить последнее сообщение,
    // но не задерживаем сон надолго.
    logEvent("Аварийный deep sleep: " + reason);

    if (WiFi.status() == WL_CONNECTED && cfg.ntfy != "")
    {
        sendNtfy("ИБП уходит в аварийный сон.\nПричина: " + reason +
                 "\nАКБ: " + String(lastVbatt, 2) + " В" +
                 "\nПроверка сети каждые " + String(cfg.sleepCheckSec) + " с.");
    }

    setBothLoads(false);
    digitalWrite(PIN_LED_GRID, LOW);
    digitalWrite(PIN_LED_BATT, LOW);

    if (mdnsStarted)
    {
        MDNS.end();
        mdnsStarted = false;
    }

    if (portalActive)
    {
        dns.stop();
        WiFi.softAPdisconnect(true);
        portalActive = false;
    }

    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    delay(100);

    rtcEmergencySleep = true;
    rtcSleepWakeCount = 0;
    esp_sleep_enable_timer_wakeup((uint64_t)cfg.sleepCheckSec * 1000000ULL);

    Serial.printf("Deep sleep: Vbat=%.2f, check every %lu s\n",
                  lastVbatt,
                  (unsigned long)cfg.sleepCheckSec);
    Serial.flush();
    delay(20);
    esp_deep_sleep_start();

    while (true) delay(1000);
}

void queueIpNotification(const String &reason)
{
    if (cfg.ntfy == "")
        return;

    ntfyIpReason = reason;
    ntfyIpPending = true;
    ntfyNextTry = millis() + 1200;
}

void ntfyTick()
{
    if (!ntfyIpPending)
        return;

    if (WiFi.status() != WL_CONNECTED)
        return;

    if ((long)(millis() - ntfyNextTry) < 0)
        return;

    String msg = ntfyIpReason +
                 "\nIP: " + panelUrl() +
                 "\nmDNS: " + mdnsUrl() +
                 "\nАКБ: " + String(lastVbatt, 1) + " В" +
                 "\nСеть: " + String(gridPresent ? "есть" : "нет");

    if (sendNtfy(msg))
    {
        ntfyIpPending = false;
        Serial.println("[ntfy] IP sent: " + panelUrl());
    }
    else
    {
        ntfyNextTry = millis() + NTFY_RETRY_MS;
    }
}

// ============================================================
// WIFI
// ============================================================

void handleWifiConnected()
{
    String ip = WiFi.localIP().toString();

    wifiStatusText = "подключено: " + ip;
    mode = MODE_RUN;

    logEvent("WiFi подключён: " + cfg.ssid + ", IP " + ip);
    Serial.println("[ntfy] topic: " + (cfg.ntfy == "" ? String("NOT SET") : cfg.ntfy));

    if (!mdnsStarted)
    {
        if (MDNS.begin(MDNS_HOST))
        {
            MDNS.addService("http", "tcp", 80);
            mdnsStarted = true;
            logEvent("mDNS запущен: " + mdnsUrl());
        }
        else
        {
            logEvent("mDNS: не удалось запустить");
        }
    }

    String reason;
    if (nextWifiConnectReason != "")
    {
        reason = nextWifiConnectReason;
        nextWifiConnectReason = "";
    }
    else if (!everWifiConnected)
        reason = "ИБП запущен и подключён к WiFi.";
    else
        reason = "WiFi снова подключён.";

    everWifiConnected = true;
    queueIpNotification(reason);

    // Если подключились из captive portal — оставляем AP ещё на 15 секунд,
    // чтобы браузер успел увидеть новый IP, затем выключаем setup-AP.
    if (portalActive && !manualPortalSession)
        portalStopAt = millis() + PORTAL_STOP_DELAY_MS;
}

void stopPortal()
{
    if (!portalActive)
        return;

    dns.stop();
    WiFi.softAPdisconnect(true);
    portalActive = false;
    portalStopAt = 0;
    manualPortalSession = false;
    manualPortalUntil = 0;

    if (WiFi.status() == WL_CONNECTED)
        WiFi.mode(WIFI_STA);

    logEvent("Setup portal остановлен");
}

void startPortal()
{
    if (!portalActive)
    {
        // AP+STA: портал остаётся доступен, пока ESP параллельно пробует
        // подключиться к домашней сети.
        WiFi.mode(WIFI_AP_STA);

        bool apOk;
        if (cfg.apPass.length() >= 8)
            apOk = WiFi.softAP(AP_SSID, cfg.apPass.c_str());
        else
            apOk = WiFi.softAP(AP_SSID);

        if (apOk)
        {
            dns.start(53, "*", WiFi.softAPIP());
            portalActive = true;
            portalLastActivity = millis();
            portalReopenAt = 0;
            logEvent("Setup AP " + String(AP_SSID) +
                     " запущен, IP " + WiFi.softAPIP().toString() +
                     (cfg.apPass.length() >= 8 ? " (WPA2)" : " (ОТКРЫТЫЙ)"));
        }
        else
        {
            logEvent("ОШИБКА: setup AP не запустился");
        }
    }

    mode = MODE_PORTAL;
    portalStopAt = 0;

    if (WiFi.status() != WL_CONNECTED)
        wifiStatusText = "портал настройки";
}

bool connectSTABlocking(uint32_t timeoutMs)
{
    if (cfg.ssid == "")
        return false;

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());

    wifiStatusText = "подключение к " + cfg.ssid;
    logEvent("Подключение к WiFi: " + cfg.ssid);

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs)
    {
        esp_task_wdt_reset();
        delay(200);
    }

    return WiFi.status() == WL_CONNECTED;
}

void beginPortalStaAttempt()
{
    if (cfg.ssid == "")
    {
        wifiRetryState = WIFI_RETRY_IDLE;
        wifiStatusText = "SSID не задан";
        return;
    }

    // Портал не выключаем: остаёмся AP+STA.
    if (!portalActive)
        startPortal();
    else
        WiFi.mode(WIFI_AP_STA);

    WiFi.disconnect(false, false);
    delay(50);
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());

    wifiRetryState = WIFI_RETRY_CONNECTING;
    wifiRetryStarted = millis();
    wifiStatusText = "подключение к " + cfg.ssid;

    logEvent("Повторное подключение к WiFi: " + cfg.ssid);
}

bool requestPortalWifiRetry(String &answer)
{
    if (cfg.ssid == "")
    {
        answer = "SSID не сохранён. Сначала укажи WiFi в настройках и сохрани.";
        return false;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        answer = "WiFi уже подключён. Адрес: " + panelUrl();
        return true;
    }

    if (wifiRetryState != WIFI_RETRY_IDLE)
    {
        answer = "Попытка подключения уже выполняется.";
        return true;
    }

    // ESP подключается к WiFi ROUTER-канала, поэтому он должен быть включён.
    if (!routerOn)
    {
        if (routerMode == LM_OFF)
        {
            answer = "ROUTER выключен вручную. Сначала включи его или поставь Авто.";
            return false;
        }

        if (!gridPresent && lastVbatt <= cfg.battCutoff)
        {
            answer = "Нельзя включить ROUTER: АКБ ниже порога отсечки.";
            return false;
        }

        setRouter(true);
    }

    unsigned long readyAt = routerTurnedOnAt + ROUTER_BOOT_MS;
    if ((long)(millis() - readyAt) < 0)
    {
        wifiRetryState = WIFI_RETRY_WAIT_ROUTER;
        wifiRetryWaitUntil = readyAt;
        wifiStatusText = "ждём загрузку роутера";
        answer = "Роутер включён. Ждём его загрузку, затем ESP подключится автоматически.";
    }
    else
    {
        beginPortalStaAttempt();
        answer = "Повторная попытка подключения к WiFi запущена.";
    }

    return true;
}

void wifiRetryTick()
{
    if (wifiRetryState == WIFI_RETRY_WAIT_ROUTER)
    {
        if ((long)(millis() - wifiRetryWaitUntil) >= 0)
            beginPortalStaAttempt();
        return;
    }

    if (wifiRetryState == WIFI_RETRY_CONNECTING)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            wifiRetryState = WIFI_RETRY_IDLE;
            wifiStatusText = "подключено: " + WiFi.localIP().toString();
            return; // переход обработает wifiTick()
        }

        if (millis() - wifiRetryStarted >= WIFI_CONNECT_TIMEOUT_MS)
        {
            wifiRetryState = WIFI_RETRY_IDLE;
            wifiStatusText = "не удалось подключиться; портал активен";
            mode = MODE_PORTAL;
            logEvent("Повторное подключение к WiFi не удалось");
        }
    }
}

void wifiTick()
{
    bool connected = (WiFi.status() == WL_CONNECTED);

    if (connected && !wifiWasConnected)
    {
        wifiWasConnected = true;
        handleWifiConnected();
    }
    else if (!connected && wifiWasConnected)
    {
        wifiWasConnected = false;
        wifiStatusText = portalActive ? "портал настройки" : "WiFi отключён";
        logEvent("WiFi отключён");
    }

    // Остановить setup AP через некоторое время после успешного подключения.
    if (portalActive && portalStopAt != 0 && connected &&
        (long)(millis() - portalStopAt) >= 0)
    {
        stopPortal();
    }

    // В штатном режиме пробуем восстановить STA автоматически.
    // Если нагрузка выключена, бессмысленно искать WiFi выключенного роутера.
    if (!connected && !portalActive && routerOn && cfg.ssid != "" &&
        wifiRetryState == WIFI_RETRY_IDLE)
    {
        // После включения нагрузки даём роутеру время загрузиться.
        bool routerHadTime = (millis() - routerTurnedOnAt >= ROUTER_BOOT_MS);

        if (routerHadTime && millis() - lastReconnect >= WIFI_RECONNECT_MS)
        {
            lastReconnect = millis();
            wifiStatusText = "автопереподключение";
            logEvent("WiFi: автопереподключение");
            WiFi.reconnect();
        }
    }
}

// ============================================================
// POWER CYCLE / REBOOT ACTIONS
// ============================================================

bool canPowerLoadNow()
{
    // После LVD не разрешаем ручным restart'ом обойти гистерезис на отскоке АКБ.
    bool batteryAllows = lvdTripped ? (lastVbatt >= cfg.battRestore)
                                    : (lastVbatt > cfg.battCutoff);
    return gridPresent || batteryAllows;
}

bool targetModeAllowsRestart(PowerTarget target, String &why)
{
    if (targetIncludesRouter(target) && routerMode == LM_OFF)
    {
        why = "ROUTER в ручном OFF.";
        return false;
    }
    if (targetIncludesOnt(target) && ontMode == LM_OFF)
    {
        why = "ONT в ручном OFF.";
        return false;
    }
    return true;
}

bool requestTargetRestart(PowerTarget target, String &answer, bool automatic = false,
                          const String &reason = "ручной перезапуск")
{
    if (target == TARGET_NONE)
    {
        answer = "Не выбран канал.";
        return false;
    }

    if (powerCycleState != PC_IDLE)
    {
        answer = "Перезапуск уже выполняется: " + targetText(powerCycleTarget) + ".";
        return false;
    }

    String why;
    if (!targetModeAllowsRestart(target, why))
    {
        answer = why + " Сначала выбери Авто или Вкл.";
        return false;
    }

    if (!canPowerLoadNow())
    {
        answer = "Перезапуск запрещён: сети нет, АКБ ниже отсечки.";
        return false;
    }

    powerCycleAutomatic = automatic;
    powerCycleReason = reason;
    powerCycleTarget = target;
    powerCycleState = PC_WAIT_OFF;
    powerCycleAt = millis() + (automatic ? 250UL : ACTION_RESPONSE_DELAY_MS);

    logEvent(String(automatic ? "AUTO RECOVERY: " : "Запрошен ручной restart: ") +
             targetText(target) + " — " + reason);

    answer = "ОК. " + targetText(target) + " будет отключён на 5 секунд.";
    return true;
}

bool requestRouterRestart(String &answer)
{
    return requestTargetRestart(TARGET_ROUTER, answer, false, "ручной перезапуск ROUTER");
}

bool requestOntRestart(String &answer)
{
    return requestTargetRestart(TARGET_ONT, answer, false, "ручной перезапуск ONT");
}

bool requestBothRestart(String &answer)
{
    return requestTargetRestart(TARGET_BOTH, answer, false, "ручной перезапуск обоих каналов");
}

void powerCycleTick()
{
    if (powerCycleState == PC_IDLE) return;
    if ((long)(millis() - powerCycleAt) < 0) return;

    if (powerCycleState == PC_WAIT_OFF)
    {
        logEvent("Restart " + targetText(powerCycleTarget) + ": снимаю питание");
        setTargetPower(powerCycleTarget, false);
        powerCycleState = PC_OFF_WAIT;
        powerCycleAt = millis() + LOAD_RESTART_OFF_MS;
        return;
    }

    if (powerCycleState == PC_OFF_WAIT)
    {
        PowerTarget finishedTarget = powerCycleTarget;
        bool wasAutomatic = powerCycleAutomatic;
        String finishedReason = powerCycleReason;

        powerCycleState = PC_IDLE;
        powerCycleTarget = TARGET_NONE;
        powerCycleAutomatic = false;
        powerCycleReason = "";

        if (!canPowerLoadNow())
        {
            lvdTripped = true;
            logEvent("Restart " + targetText(finishedTarget) +
                     ": питание не возвращено — АКБ ниже отсечки");
            return;
        }

        if (targetIncludesRouter(finishedTarget) && routerMode != LM_OFF)
            setRouter(true);
        if (targetIncludesOnt(finishedTarget) && ontMode != LM_OFF)
            setOnt(true);
        lvdTripped = false;

        if (targetIncludesRouter(finishedTarget))
        {
            nextWifiConnectReason = wasAutomatic
                ? "Автовосстановление: после перезапуска ROUTER WiFi восстановлен."
                : "После перезапуска ROUTER WiFi восстановлен.";
            wifiStatusText = "ждём загрузку роутера";
            lastReconnect = millis();
        }

        networkBadSince = millis();
        lastInternetCheck = 0;
        internetHealthySince = 0;
        logEvent("Restart " + targetText(finishedTarget) +
                 ": питание возвращено (" + finishedReason + ")");
    }
}

bool tcpProbe(IPAddress ip, uint16_t port)
{
    WiFiClient c;
    c.setTimeout(1500);
    bool ok = c.connect(ip, port);
    c.stop();
    return ok;
}

bool checkInternetReachable()
{
    if (WiFi.status() != WL_CONNECTED)
        return false;

    // Две независимые точки. Ничего не отправляем наружу: только TCP connect.
    // Это меньше зависит от ntfy и не спамит HTTP-запросами.
    if (tcpProbe(IPAddress(1, 1, 1, 1), 443))
        return true;

    esp_task_wdt_reset();

    if (tcpProbe(IPAddress(8, 8, 8, 8), 53))
        return true;

    return false;
}

bool startAutomaticPowerCycle(PowerTarget target, const String &reason)
{
    if (powerCycleState != PC_IDLE || !canPowerLoadNow())
        return false;

    String why;
    if (!targetModeAllowsRestart(target, why))
        return false;

    String answer;
    if (!requestTargetRestart(target, answer, true, reason))
        return false;

    rtcAutoPowerCycles++;
    saveRecoveryCounters();
    recoveryStatus = "автоперезапуск " + targetText(target) + " " +
                     String(rtcAutoPowerCycles) + "/" + String(AUTO_MAX_POWER_CYCLES);

    logEvent("AUTO RECOVERY: " + reason + ", цикл " +
             String(rtcAutoPowerCycles) + "/" + String(AUTO_MAX_POWER_CYCLES) +
             ", цель " + targetText(target));

    networkBadSince = 0;
    internetHealthySince = 0;
    return true;
}

void networkRecoveryTick()
{
    if (!cfg.autoRecoveryEnabled)
    {
        recoveryStatus = "автовосстановление выключено";
        return;
    }

    if (rebootAt != 0 || powerCycleState != PC_IDLE)
        return;

    if (!networkLoadsOn() || routerMode == LM_OFF || ontMode == LM_OFF)
    {
        recoveryStatus = "ROUTER/ONT выключен";
        networkBadSince = 0;
        internetHealthySince = 0;
        return;
    }

    if (portalActive)
    {
        recoveryStatus = "setup portal";
        return;
    }

    // Не тратим последние проценты АКБ на циклические перезапуски.
    if (!gridPresent && lastVbatt <= cfg.battWarn)
    {
        recoveryStatus = "пауза: низкий АКБ";
        return;
    }

    // После подачи питания роутеру/ONT ничего не диагностируем, пока они грузятся.
    if (millis() - equipmentTurnedOnAt() < ROUTER_BOOT_MS + 10000UL)
    {
        recoveryStatus = "ждём загрузку оборудования";
        return;
    }

    bool badNow = false;
    String badReason;

    if (WiFi.status() != WL_CONNECTED)
    {
        badNow = true;
        internetKnown = false;
        internetReachable = false;
        badReason = "ESP не подключена к WiFi";
    }
    else if (lastInternetCheck == 0 || millis() - lastInternetCheck >= INTERNET_CHECK_MS)
    {
        lastInternetCheck = millis();
        bool ok = checkInternetReachable();

        if (ok)
        {
            if (!internetKnown || !internetReachable)
                logEvent("Internet probe: связь есть");

            internetKnown = true;
            internetReachable = true;
            networkBadSince = 0;
            recoveryStatus = "интернет есть";

            if (internetHealthySince == 0)
                internetHealthySince = millis();

            if (millis() - internetHealthySince >= HEALTHY_RESET_MS &&
                (rtcAutoPowerCycles != 0 || rtcNetReboots != 0))
            {
                logEvent("AUTO RECOVERY: счётчики сброшены после 10 мин стабильной связи");
                rtcAutoPowerCycles = 0;
                rtcNetReboots = 0;
                saveRecoveryCounters();
            }

            return;
        }

        if (!internetKnown || internetReachable)
            logEvent("Internet probe: интернет не отвечает");

        internetKnown = true;
        internetReachable = false;
        badNow = true;
        badReason = "WiFi есть, но интернет не отвечает";
    }
    else
    {
        // Между probe-запросами используем последнее состояние.
        if (internetKnown && !internetReachable)
        {
            badNow = true;
            badReason = "интернет не отвечает";
        }
        else
        {
            return;
        }
    }

    if (!badNow)
        return;

    internetHealthySince = 0;

    if (networkBadSince == 0)
    {
        networkBadSince = millis();
        recoveryStatus = "проблема связи: " + badReason;
        logEvent("AUTO RECOVERY: начался таймер — " + badReason);
        return;
    }

    uint32_t badFor = millis() - networkBadSince;
    recoveryStatus = "нет связи " + formatDuration(badFor);

    if (badFor < cfg.netBadSec * 1000UL)
        return;

    if (rtcAutoPowerCycles < AUTO_MAX_POWER_CYCLES)
    {
        // 1-й шаг: если пропал сам WiFi — перезапускаем ROUTER;
        // если WiFi жив, но WAN нет — сначала ONT. 2-й шаг — оба канала.
        PowerTarget target;
        if (rtcAutoPowerCycles == 0)
            target = (WiFi.status() == WL_CONNECTED) ? TARGET_ONT : TARGET_ROUTER;
        else
            target = TARGET_BOTH;

        startAutomaticPowerCycle(target, badReason);
        return;
    }

    if (cfg.autoEspRebootEnabled && rtcNetReboots < AUTO_MAX_ESP_REBOOTS)
    {
        rtcNetReboots++;
        saveRecoveryCounters();
        logEvent("AUTO RECOVERY: power-cycle не помог, запланирован reboot ESP");
        recoveryStatus = "перезагрузка ESP";
        rebootAt = millis() + 1500UL;
        networkBadSince = 0;
        return;
    }

    recoveryStatus = "лимит autorecovery исчерпан";
}

void portalMaintenanceTick()
{
    bool connected = WiFi.status() == WL_CONNECTED;

    if (portalActive && manualPortalSession && manualPortalUntil != 0 &&
        (long)(millis() - manualPortalUntil) >= 0)
    {
        logEvent("Setup portal закрыт: истекли 15 минут ручного режима");
        stopPortal();
        return;
    }

    if (portalActive)
    {
        // Пока к setup AP кто-то подключён, считаем портал используемым.
        if (WiFi.softAPgetStationNum() > 0)
            portalLastActivity = millis();

        // При пустом SSID портал нужен постоянно для первичной настройки.
        if (cfg.ssid == "" || connected)
            return;

        // При отсутствии клиентов не держим AP бесконечно.
        if (WiFi.softAPgetStationNum() == 0 &&
            portalLastActivity != 0 &&
            millis() - portalLastActivity >= cfg.portalIdleSec * 1000UL)
        {
            logEvent("Setup portal закрыт по таймауту бездействия");
            stopPortal();

            WiFi.mode(WIFI_STA);
            WiFi.setAutoReconnect(true);
            WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
            wifiStatusText = "STA retry; setup AP временно закрыт";
            portalReopenAt = millis() + PORTAL_REOPEN_DELAY_MS;
        }
        return;
    }

    // Если после закрытия портала домашняя сеть так и не появилась —
    // через несколько минут снова даём локальный способ настройки.
    if (!connected && cfg.ssid != "" && portalReopenAt != 0 &&
        (long)(millis() - portalReopenAt) >= 0)
    {
        portalReopenAt = 0;
        startPortal();
    }
}

void deepSleepTick()
{
    if (!cfg.deepSleepEnabled)
    {
        sleepLowSince = 0;
        return;
    }

    if (gridPresent || anyLoadOn() || powerCycleState != PC_IDLE || rebootAt != 0)
    {
        sleepLowSince = 0;
        return;
    }

    // Основной сценарий: LVD уже отключил оба выхода по battCutoff.
    // После снятия нагрузки напряжение АКБ отскакивает вверх, поэтому НЕ ждём,
    // пока оно снова упадёт до battSleep. battSleep остаётся аварийным резервным порогом.
    bool afterLvd = lvdTripped;
    bool emergencyVoltage = lastVbatt <= cfg.battSleep;

    if (!afterLvd && !emergencyVoltage)
    {
        sleepLowSince = 0;
        return;
    }

    if (sleepLowSince == 0)
    {
        sleepLowSince = millis();
        if (afterLvd)
            logEvent("Deep sleep: LVD снял нагрузку; ожидание " +
                     String(cfg.sleepDelaySec) + " с перед сном");
        else
            logEvent("АКБ ниже аварийного порога сна: " + String(lastVbatt, 2) +
                     " В <= " + String(cfg.battSleep, 2) + " В");
        return;
    }

    if (millis() - sleepLowSince < cfg.sleepDelaySec * 1000UL)
        return;

    String reason = afterLvd
        ? ("LVD отключил ROUTER+ONT; АКБ " + String(lastVbatt, 2) + " В")
        : ("АКБ " + String(lastVbatt, 2) + " В ниже аварийного порога");
    enterEmergencySleep(reason + ", сеть отсутствует");
}

void rebootTick()
{
    if (rebootAt != 0 && (long)(millis() - rebootAt) >= 0)
    {
        logEvent("Перезагрузка ESP");
        delay(100);
        ESP.restart();
    }
}

// ============================================================
// PHYSICAL BUTTON GPIO14
// ============================================================

void startManualPortalFromButton()
{
    manualPortalSession = true;
    manualPortalUntil = millis() + BUTTON_PORTAL_MS;
    startPortal();
    portalStopAt = 0;
    logEvent("Кнопка: setup AP включён на 15 минут");
}

void handleButtonShort()
{
    String answer;
    bool ok = requestRouterRestart(answer);
    logEvent("Кнопка: короткое -> " + answer);
}

void handleButtonDouble()
{
    String answer;
    bool ok = requestOntRestart(answer);
    logEvent("Кнопка: двойное -> " + answer);
}

void buttonTick()
{
    bool raw = digitalRead(PIN_BUTTON);
    unsigned long now = millis();

    if (raw != buttonRaw)
    {
        buttonRaw = raw;
        buttonRawChangedAt = now;
    }

    if (raw != buttonStable && now - buttonRawChangedAt >= BUTTON_DEBOUNCE_MS)
    {
        buttonStable = raw;

        if (buttonStable == LOW)
        {
            buttonPressedAt = now;
            buttonLongHandled = false;
        }
        else
        {
            if (!buttonLongHandled)
            {
                buttonClickCount++;
                if (buttonClickCount == 1)
                    buttonFirstClickAt = now;
                else if (buttonClickCount >= 2)
                {
                    buttonClickCount = 0;
                    handleButtonDouble();
                }
            }
        }
    }

    if (buttonStable == LOW && !buttonLongHandled &&
        now - buttonPressedAt >= BUTTON_LONG_MS)
    {
        buttonLongHandled = true;
        buttonClickCount = 0;
        startManualPortalFromButton();
    }

    if (buttonClickCount == 1 && now - buttonFirstClickAt >= BUTTON_DOUBLE_MS)
    {
        buttonClickCount = 0;
        handleButtonShort();
    }
}

// ============================================================
// PROTECTION / POWER LOGIC
// ============================================================

void applyChannelMode(bool isRouter, float vb)
{
    LoadMode modeRef = isRouter ? routerMode : ontMode;
    bool onNow = isRouter ? routerOn : ontOn;

    if (modeRef == LM_OFF)
    {
        if (onNow)
        {
            if (isRouter) setRouter(false); else setOnt(false);
        }
        return;
    }

    bool batteryAllows = lvdTripped ? (vb >= cfg.battRestore)
                                    : (vb > cfg.battCutoff);
    bool want = gridPresent || batteryAllows;

    if (want != onNow)
    {
        if (isRouter) setRouter(want); else setOnt(want);
    }
}

void protectTick()
{
    if (millis() - lastTick < 1000) return;
    lastTick = millis();

    float vb = readVoltage(PIN_VBATT, DIV_VBATT, cfg.calVbatt);
    float vg = readVoltage(PIN_VGRID, DIV_VGRID, cfg.calVgrid);

    lastVbatt = vb;
    lastVgrid = vg;
    battState = getBattState(vb);

    bool nowGrid = gridPresent ? (vg > cfg.gridOff) : (vg > cfg.gridOn);
    if (nowGrid != gridCandidate)
    {
        gridCandidate = nowGrid;
        gridChangeSince = millis();
    }

    if (nowGrid != gridPresent && millis() - gridChangeSince > cfg.gridDebounce)
    {
        gridPresent = nowGrid;

        if (gridPresent)
        {
            if (outageStartedAt != 0)
            {
                lastOutageDurationMs = millis() - outageStartedAt;
                outageStartedAt = 0;
            }

            logEvent("Сеть появилась. АКБ " + String(vb, 1) + " В" +
                     (lastOutageDurationMs > 0
                        ? ", отключение длилось " + formatDuration(lastOutageDurationMs)
                        : ""));

            lvdTripped = false;
            sleepLowSince = 0;

            if (powerCycleState == PC_IDLE)
            {
                if (routerMode != LM_OFF) setRouter(true);
                if (ontMode != LM_OFF) setOnt(true);
            }

            String msg = "Свет дали. АКБ " + String(vb, 1) + " В";
            if (lastOutageDurationMs > 0)
                msg += "\nБез сети: " + formatDuration(lastOutageDurationMs);
            sendNtfy(msg);
            warnSent = false;
        }
        else
        {
            outageStartedAt = millis();
            outageCount++;
            logEvent("Сеть пропала. Переход на АКБ " + String(vb, 1) + " В");
            sendNtfy("Свет отключили! Работа от АКБ, " + String(vb, 1) + " В");
        }
    }

    // LVD общий для обоих выходов. Сначала уведомляем, пока ROUTER ещё жив,
    // затем снимаем питание обоих DC-DC модулей.
    if (!gridPresent && !lvdTripped && vb <= cfg.battCutoff && anyLoadOn() &&
        powerCycleState == PC_IDLE)
    {
        sendNtfy("АКБ разряжена (" + String(vb, 1) +
                 " В). ROUTER и ONT отключаются; затем ESP уйдёт в deep sleep.");
        logEvent("LVD: АКБ " + String(vb, 2) + " В -> отключаю ROUTER+ONT");
        setBothLoads(false);
        lvdTripped = true;
        sleepLowSince = millis();
    }

    // При ручных OFF/ON/AUTO обслуживаем каналы независимо, но во время
    // power-cycle не вмешиваемся в состояние целевого канала.
    if (powerCycleState == PC_IDLE)
    {
        if (gridPresent)
        {
            lvdTripped = false;
            applyChannelMode(true, vb);
            applyChannelMode(false, vb);
        }
        else if (!lvdTripped)
        {
            applyChannelMode(true, vb);
            applyChannelMode(false, vb);
        }
        else if (vb >= cfg.battRestore)
        {
            // Гистерезис восстановления после LVD.
            lvdTripped = false;
            sleepLowSince = 0;
            applyChannelMode(true, vb);
            applyChannelMode(false, vb);
        }
    }

    if (!gridPresent && anyLoadOn() && vb <= cfg.battWarn && !warnSent)
    {
        sendNtfy("АКБ садится: " + String(vb, 1) + " В. Скоро отключение ROUTER+ONT.");
        warnSent = true;
    }
    if (vb > cfg.battWarn + 0.3f)
        warnSent = false;

    Serial.printf("Vbat=%.2f V24=%.2f grid=%d router=%d ont=%d rMode=%d oMode=%d state=%d wifi=%d\n",
                  vb, vg, gridPresent, routerOn, ontOn,
                  (int)routerMode, (int)ontMode, (int)battState,
                  WiFi.status() == WL_CONNECTED);
}

// ============================================================
// WEB UI
// ============================================================

String field(const String &label,
             const String &name,
             const String &value,
             const String &hint = "",
             const String &type = "text")
{
    String h = hint == "" ? "" : "<small>" + htmlEscape(hint) + "</small>";
    return "<label>" + htmlEscape(label) + h +
           "<input type='" + htmlEscape(type) +
           "' name='" + htmlEscape(name) +
           "' value='" + htmlEscape(value) + "'></label>";
}

String checkboxField(const String &label,
                     const String &name,
                     bool checked,
                     const String &hint = "")
{
    String h = hint == "" ? "" : "<small>" + htmlEscape(hint) + "</small>";
    return "<label style='display:flex;gap:8px;align-items:flex-start'>"
           "<input style='width:auto;margin-top:3px' type='checkbox' name='" +
           htmlEscape(name) + "' value='1'" + (checked ? " checked" : "") + ">"
           "<span>" + htmlEscape(label) + h + "</span></label>";
}

String page()
{
    String p = F(
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>DC-UPS</title><style>"
        "body{font-family:sans-serif;max-width:560px;margin:0 auto;padding:16px;background:#f4f4f0;color:#222}"
        "h2,h3{font-weight:500}label{display:block;margin:10px 0}small{display:block;color:#777;font-size:12px}"
        "input{width:100%;padding:8px;box-sizing:border-box;border:1px solid #ccc;border-radius:6px}"
        "fieldset{border:1px solid #ddd;border-radius:8px;margin:14px 0;background:#fff}legend{color:#555}"
        ".card{background:#fff;border:1px solid #ddd;border-radius:8px;padding:12px;margin:12px 0}"
        ".row{display:flex;gap:8px;margin:8px 0;flex-wrap:wrap}.row input{flex:1}"
        "button{padding:11px 14px;border:0;border-radius:8px;background:#1d6e56;color:#fff;font-size:15px;cursor:pointer}"
        ".b2{background:#555}.b3{background:#a33}.b4{background:#8a5a00}.big{width:100%;padding:12px;margin-top:14px}"
        "b{font-weight:600}.v{font-size:20px}.muted{font-size:13px;color:#666;word-break:break-all}"
        "pre{white-space:pre-wrap;word-break:break-word;max-height:300px;overflow:auto;background:#111;color:#ddd;padding:10px;border-radius:7px;font-size:12px}"
        "a{color:#1d6e56}"
        "</style></head><body>");

    p += "<h2>DC-UPS панель</h2><div class='muted'>FW " FW_VERSION "</div>";

    p += "<div class='card'><h3>Состояние</h3>"
         "<div class='v'>АКБ: <b id='vb'>—</b> В &nbsp; 24В: <b id='v24'>—</b> В</div>"
         "Сеть: <b id='gr'>—</b><br>"
         "ROUTER: <b id='routerst'>—</b> · ONT: <b id='ontst'>—</b><br>"
         "АКБ: <b id='st'>—</b> · Uptime: <b id='up'>—</b><br>"
         "Отключений сети с запуска: <b id='oc'>—</b> · <span id='otxt'>—</span><br>"
         "Internet: <b id='inet'>—</b> · Recovery: <b id='rec'>—</b><br>"
         "Deep sleep: <b id='ds'>—</b></div>";

    p += "<div class='card'><h3>WiFi</h3>"
         "Состояние: <b id='wf'>—</b><br>"
         "RSSI: <b id='rssi'>—</b> dBm<br>"
         "<div class='muted'>IP: <a id='iplink' href='#' target='_blank'>—</a></div>"
         "<div class='muted'>mDNS: <a href='http://" MDNS_HOST ".local/' target='_blank'>http://" MDNS_HOST ".local/</a></div>"
         "<div class='row'>"
         "<button onclick='wretry()'>Подключиться к WiFi снова</button>"
         "</div>"
         "<div class='muted' id='wmsg'></div>"
         "</div>";

    p += "<div class='card'><h3>Выход ROUTER — GPIO32</h3>"
         "Состояние: <b id='routercard'>—</b><div class='row'>"
         "<button onclick=\"chmode('router','on')\">Вкл</button>"
         "<button class='b2' onclick=\"if(confirm('Выключить ROUTER? Веб-сессия может пропасть.'))chmode('router','off')\">Выкл</button>"
         "<button class='b2' onclick=\"chmode('router','auto')\">Авто</button>"
         "<button class='b4' onclick=\"restartCh('router')\">Restart ROUTER</button>"
         "</div></div>";

    p += "<div class='card'><h3>Выход ONT — GPIO26</h3>"
         "Состояние: <b id='ontcard'>—</b><div class='row'>"
         "<button onclick=\"chmode('ont','on')\">Вкл</button>"
         "<button class='b2' onclick=\"chmode('ont','off')\">Выкл</button>"
         "<button class='b2' onclick=\"chmode('ont','auto')\">Авто</button>"
         "<button class='b4' onclick=\"restartCh('ont')\">Restart ONT</button>"
         "</div>"
         "<div class='row'><button class='b4' onclick=\"restartCh('both')\">Restart обоих</button></div>"
         "<div class='muted' id='lmsg'></div></div>";

    p += "<div class='card'><h3>Кнопка GPIO14</h3>"
         "Короткое нажатие — restart ROUTER<br>"
         "Двойное — restart ONT<br>"
         "Удержание 5 секунд — setup AP на 15 минут"
         "</div>";

    p += "<div class='card'><h3>Журнал событий</h3>"
         "<div class='row'><button class='b2' onclick='refreshLog()'>Обновить</button>"
         "<button class='b3' onclick='clearLog()'>Очистить</button></div>"
         "<pre id='elog'>загрузка...</pre></div>";

    p += "<div class='card'><h3>Система</h3><div class='row'>"
         "<button class='b3' onclick='rebootEsp()'>Перезагрузить ESP</button>"
         "</div></div>";

    p += "<div class='card'><h3>Калибровка (по мультиметру)</h3>"
         "<div class='row'><input id='rb' placeholder='реальное АКБ, В'><button onclick='calb()'>АКБ</button></div>"
         "<div class='row'><input id='r24' placeholder='реальное 24В, В'><button onclick='cal24()'>24В</button></div>"
         "</div>";

    p += "<div class='card'><h3>Уведомления</h3>"
         "<button onclick='testn()'>Отправить тест в ntfy</button>"
         "</div>";

    p += "<form method='POST' action='/save'>";
    p += "<fieldset><legend>WiFi</legend>" +
         field("SSID", "ssid", cfg.ssid) +
         field("Пароль", "pass", "", "пусто — не менять", "password") +
         field("Пароль setup AP", "apPass", "", "пусто — не менять; минимум 8 символов", "password") +
         field("Пароль веб-панели", "adminPass", "", "пусто — не менять; пользователь: admin; введи - чтобы отключить защиту", "password") +
         "<small>По умолчанию setup AP: " AP_DEFAULT_PASSWORD "</small>"
         "</fieldset>";

    p += "<fieldset><legend>ntfy</legend>" +
         field("Тема (topic)", "ntfy", cfg.ntfy, "придумай длинную и не угадываемую") +
         "</fieldset>";

    p += "<fieldset><legend>Калибровка (коэфф.)</legend>" +
         field("CAL АКБ", "calVbatt", String(cfg.calVbatt, 3)) +
         field("CAL GRID", "calVgrid", String(cfg.calVgrid, 3)) +
         "</fieldset>";

    p += "<fieldset><legend>Пороги, В</legend>" +
         field("Отсечка", "cutoff", String(cfg.battCutoff, 1)) +
         field("Возврат", "restore", String(cfg.battRestore, 1)) +
         field("Предупр.", "warn", String(cfg.battWarn, 1)) +
         field("Резервный аварийный порог сна <", "sleepV", String(cfg.battSleep, 1), "основной сон запускается после LVD; этот порог — запасной") +
         field("Сеть есть >", "gridOn", String(cfg.gridOn, 1)) +
         field("Сети нет <", "gridOff", String(cfg.gridOff, 1)) +
         field("Антидребезг, с", "deb", String(cfg.gridDebounce / 1000)) +
         "</fieldset>";

    p += "<fieldset><legend>Deep sleep</legend>" +
         checkboxField("Включить аварийный deep sleep", "deepSleep", cfg.deepSleepEnabled,
                       "после LVD ESP засыпает даже если напряжение АКБ отскочило вверх; battSleep — резервный аварийный порог") +
         field("Проверять сеть каждые, с", "sleepCheckSec", String(cfg.sleepCheckSec)) +
         field("Задержка перед сном, с", "sleepDelaySec", String(cfg.sleepDelaySec)) +
         "</fieldset>";

    p += "<fieldset><legend>Автовосстановление интернета</legend>" +
         checkboxField("Автоматически перезапускать роутер/ONT", "autoRec", cfg.autoRecoveryEnabled,
                       "не более 2 power-cycle подряд; после 10 минут нормальной связи счётчик сбрасывается") +
         checkboxField("Разрешить один reboot ESP после двух неудачных power-cycle", "autoEsp", cfg.autoEspRebootEnabled,
                       "по умолчанию выключено: reboot ESP может кратко дёрнуть питание нагрузки") +
         field("Сколько секунд связи не должно быть", "netBadSec", String(cfg.netBadSec)) +
         field("Таймаут setup AP без клиентов, с", "portalIdleSec", String(cfg.portalIdleSec),
               "если WiFi настроен; затем AP закроется и позже откроется снова") +
         "</fieldset>";

    p += "<button type='submit' class='big'>Сохранить и перезагрузить</button></form>";

    p += F(
        "<script>"
        "function upd(){fetch('/status').then(r=>r.json()).then(d=>{"
        "vb.textContent=d.vb;v24.textContent=d.v24;"
        "gr.textContent=d.grid?'есть':'нет';"
        "routerst.textContent=(d.router_on?'вкл':'выкл')+' ('+['авто','ручн.ВКЛ','ручн.ВЫКЛ'][d.router_mode]+')';"
        "ontst.textContent=(d.ont_on?'вкл':'выкл')+' ('+['авто','ручн.ВКЛ','ручн.ВЫКЛ'][d.ont_mode]+')';"
        "routercard.textContent=routerst.textContent;ontcard.textContent=ontst.textContent;"
        "st.textContent=['норма','садится','критично'][d.state];"
        "rssi.textContent=d.wifi?d.rssi:'—';up.textContent=d.up_text;wf.textContent=d.wifi_status;"
        "oc.textContent=d.outages;"
        "otxt.textContent=d.grid?(d.last_outage_ms?('последнее: '+d.last_outage_text):'последних нет'):('на АКБ: '+d.outage_text);"
        "inet.textContent=d.internet_known?(d.internet?'есть':'нет/не отвечает'):'не проверен';"
        "rec.textContent=d.recovery_status;"
        "ds.textContent=d.deep_sleep?'включён ('+d.sleep_v+' В / '+d.sleep_check+' с)':'выключен';"
        "if(d.ip){iplink.textContent='http://'+d.ip+'/';iplink.href='http://'+d.ip+'/';}"
        "else{iplink.textContent='—';iplink.href='#';}"
        "}).catch(()=>{});}"
        "function q(u){return fetch(u).then(async r=>({ok:r.ok,text:await r.text()}));}"
        "function chmode(ch,m){q('/channel?ch='+ch+'&mode='+m).then(x=>{lmsg.textContent=x.text;setTimeout(upd,300);});}"
        "function post(u){return fetch(u,{method:'POST'}).then(async r=>({ok:r.ok,text:await r.text()}));}"
        "function restartCh(ch){if(!confirm('Отключить '+ch+' на 5 секунд?'))return;"
        "lmsg.textContent='Запрашиваю перезапуск '+ch+'...';post('/channel/restart?ch='+ch).then(x=>{lmsg.textContent=x.text;});}"
        "function rebootEsp(){if(!confirm('Перезагрузить ESP?'))return;post('/system/reboot').then(x=>alert(x.text));}"
        "function refreshLog(){fetch('/events').then(r=>r.text()).then(t=>{elog.textContent=t;elog.scrollTop=elog.scrollHeight;});}"
        "function clearLog(){if(!confirm('Очистить журнал?'))return;post('/events/clear').then(x=>refreshLog());}"
        "function calb(){q('/calbatt?v='+encodeURIComponent(document.getElementById('rb').value)).then(x=>alert(x.text));}"
        "function cal24(){q('/calgrid?v='+encodeURIComponent(document.getElementById('r24').value)).then(x=>alert(x.text));}"
        "function testn(){q('/testntfy').then(x=>alert(x.text));}"
        "function wretry(){wmsg.textContent='Запускаю...';q('/wifi/retry').then(x=>{wmsg.textContent=x.text;setTimeout(upd,500);});}"
        "setInterval(upd,2000);setInterval(refreshLog,10000);upd();refreshLog();"
        "</script></body></html>");

    return p;
}

void hRoot()
{
    touchWebActivity();
    if (!requireAuth()) return;
    server.send(200, "text/html; charset=utf-8", page());
}

void hStatus()
{
    touchWebActivity();
    if (!requireAuth()) return;
    bool connected = WiFi.status() == WL_CONNECTED;
    int rssi = connected ? WiFi.RSSI() : -127;
    String ip = connected ? WiFi.localIP().toString() : "";

    uint32_t currentOutageMs = (!gridPresent && outageStartedAt != 0)
                                  ? (millis() - outageStartedAt) : 0;

    String j = "{\"vb\":" + String(lastVbatt, 2) +
               ",\"v24\":" + String(lastVgrid, 2) +
               ",\"grid\":" + String(gridPresent ? 1 : 0) +
               ",\"router_on\":" + String(routerOn ? 1 : 0) +
               ",\"router_mode\":" + String((int)routerMode) +
               ",\"ont_on\":" + String(ontOn ? 1 : 0) +
               ",\"ont_mode\":" + String((int)ontMode) +
               ",\"state\":" + String((int)battState) +
               ",\"wifi\":" + String(connected ? 1 : 0) +
               ",\"rssi\":" + String(rssi) +
               ",\"ip\":\"" + jsonEscape(ip) + "\"" +
               ",\"wifi_status\":\"" + jsonEscape(wifiStatusText) + "\"" +
               ",\"portal\":" + String(portalActive ? 1 : 0) +
               ",\"power_cycle\":" + String(powerCycleState != PC_IDLE ? 1 : 0) +
               ",\"internet_known\":" + String(internetKnown ? 1 : 0) +
               ",\"internet\":" + String(internetReachable ? 1 : 0) +
               ",\"recovery_status\":\"" + jsonEscape(recoveryStatus) + "\"" +
               ",\"auto_cycles\":" + String(rtcAutoPowerCycles) +
               ",\"auto_reboots\":" + String(rtcNetReboots) +
               ",\"deep_sleep\":" + String(cfg.deepSleepEnabled ? 1 : 0) +
               ",\"sleep_v\":" + String(cfg.battSleep, 2) +
               ",\"sleep_check\":" + String(cfg.sleepCheckSec) +
               ",\"outages\":" + String(outageCount) +
               ",\"outage_ms\":" + String(currentOutageMs) +
               ",\"outage_text\":\"" + jsonEscape(formatDuration(currentOutageMs)) + "\"" +
               ",\"last_outage_ms\":" + String(lastOutageDurationMs) +
               ",\"last_outage_text\":\"" + jsonEscape(formatDuration(lastOutageDurationMs)) + "\"" +
               ",\"up\":" + String(millis() / 1000) +
               ",\"up_text\":\"" + jsonEscape(formatDuration(millis())) + "\"}";

    server.send(200, "application/json", j);
}

void hChannel()
{
    touchWebActivity();
    if (!requireAuth()) return;

    String ch = server.arg("ch");
    String m = server.arg("mode");
    LoadMode newMode = (m == "on") ? LM_ON : (m == "off" ? LM_OFF : LM_AUTO);

    if (ch == "router")
    {
        routerMode = newMode;
        logEvent("ROUTER режим -> " + m);
    }
    else if (ch == "ont")
    {
        ontMode = newMode;
        logEvent("ONT режим -> " + m);
    }
    else
    {
        server.send(400, "text/plain; charset=utf-8", "Неизвестный канал.");
        return;
    }

    // Фактическое переключение произойдёт в protectTick(), чтобы соблюдался LVD.
    server.send(200, "text/plain; charset=utf-8", "OK: " + ch + " -> " + m);
}

void hCalBatt()
{
    touchWebActivity();
    if (!requireAuth()) return;
    float real = server.arg("v").toFloat();

    if (real > 0.5f && lastVbatt > 0.5f)
    {
        cfg.calVbatt = cfg.calVbatt * real / lastVbatt;
        saveConfig();
        server.send(200, "text/plain; charset=utf-8",
                    "АКБ откалибрована: CAL=" + String(cfg.calVbatt, 3));
    }
    else
    {
        server.send(400, "text/plain; charset=utf-8",
                    "Нужно реальное напряжение > 0,5 В");
    }
}

void hCalGrid()
{
    touchWebActivity();
    if (!requireAuth()) return;
    float real = server.arg("v").toFloat();

    if (real > 0.5f && lastVgrid > 0.5f)
    {
        cfg.calVgrid = cfg.calVgrid * real / lastVgrid;
        saveConfig();
        server.send(200, "text/plain; charset=utf-8",
                    "24В откалибрована: CAL=" + String(cfg.calVgrid, 3));
    }
    else
    {
        server.send(400, "text/plain; charset=utf-8",
                    "Нужно реальное напряжение > 0,5 В");
    }
}

void hTestNtfy()
{
    touchWebActivity();
    if (!requireAuth()) return;
    String msg = "Тест уведомления DC-UPS";
    if (WiFi.status() == WL_CONNECTED)
        msg += "\nIP: " + panelUrl() + "\nmDNS: " + mdnsUrl();

    bool ok = sendNtfy(msg);
    server.send(ok ? 200 : 503,
                "text/plain; charset=utf-8",
                ok ? "Отправлено в ntfy"
                   : "Не ушло (проверь WiFi, интернет и тему ntfy)");
}

void hWifiRetry()
{
    touchWebActivity();
    if (!requireAuth()) return;
    String answer;
    bool ok = requestPortalWifiRetry(answer);

    server.send(ok ? 202 : 409,
                "text/plain; charset=utf-8",
                answer);
}

void hChannelRestart()
{
    touchWebActivity();
    if (!requireAuth()) return;

    String ch = server.arg("ch");
    String answer;
    bool ok = false;

    if (ch == "router") ok = requestRouterRestart(answer);
    else if (ch == "ont") ok = requestOntRestart(answer);
    else if (ch == "both") ok = requestBothRestart(answer);
    else
    {
        server.send(400, "text/plain; charset=utf-8", "Неизвестный канал.");
        return;
    }

    server.send(ok ? 202 : 409, "text/plain; charset=utf-8", answer);
}

void hEvents()
{
    touchWebActivity();
    if (!requireAuth()) return;
    server.send(200, "text/plain; charset=utf-8", getEventLogText());
}

void hEventsClear()
{
    touchWebActivity();
    if (!requireAuth()) return;
    clearEventLog();
    server.send(200, "text/plain; charset=utf-8", "Журнал очищен.");
}

void hSystemReboot()
{
    touchWebActivity();
    if (!requireAuth()) return;
    if (rebootAt != 0)
    {
        server.send(409, "text/plain; charset=utf-8", "Перезагрузка уже запланирована.");
        return;
    }

    logEvent("Запрошена перезагрузка ESP из веб-панели");
    rebootAt = millis() + 1200UL;
    server.send(202, "text/plain; charset=utf-8", "ESP перезагрузится через секунду.");
}

void hSave()
{
    touchWebActivity();
    if (!requireAuth()) return;
    if (server.hasArg("ssid"))
        cfg.ssid = server.arg("ssid");

    if (server.hasArg("pass") && server.arg("pass") != "")
        cfg.pass = server.arg("pass");

    if (server.hasArg("ntfy"))
        cfg.ntfy = server.arg("ntfy");

    if (server.hasArg("apPass") && server.arg("apPass") != "")
    {
        String newApPass = server.arg("apPass");
        if (newApPass.length() >= 8)
            cfg.apPass = newApPass;
    }

    if (server.hasArg("adminPass") && server.arg("adminPass") != "")
    {
        if (server.arg("adminPass") == "-")
            cfg.adminPass = "";
        else
            cfg.adminPass = server.arg("adminPass");
    }

    if (server.hasArg("calVbatt"))
        cfg.calVbatt = server.arg("calVbatt").toFloat();

    if (server.hasArg("calVgrid"))
        cfg.calVgrid = server.arg("calVgrid").toFloat();

    if (server.hasArg("cutoff"))
        cfg.battCutoff = server.arg("cutoff").toFloat();

    if (server.hasArg("restore"))
        cfg.battRestore = server.arg("restore").toFloat();

    if (server.hasArg("warn"))
        cfg.battWarn = server.arg("warn").toFloat();

    if (server.hasArg("sleepV"))
        cfg.battSleep = server.arg("sleepV").toFloat();

    if (server.hasArg("gridOn"))
        cfg.gridOn = server.arg("gridOn").toFloat();

    if (server.hasArg("gridOff"))
        cfg.gridOff = server.arg("gridOff").toFloat();

    if (server.hasArg("deb"))
    {
        long sec = server.arg("deb").toInt();
        if (sec < 0) sec = 0;
        if (sec > 600) sec = 600;
        cfg.gridDebounce = (uint32_t)sec * 1000UL;
    }

    cfg.deepSleepEnabled = server.hasArg("deepSleep");
    cfg.autoRecoveryEnabled = server.hasArg("autoRec");
    cfg.autoEspRebootEnabled = server.hasArg("autoEsp");

    if (server.hasArg("sleepCheckSec"))
    {
        long v = server.arg("sleepCheckSec").toInt();
        if (v < 10) v = 10;
        if (v > 3600) v = 3600;
        cfg.sleepCheckSec = (uint32_t)v;
    }

    if (server.hasArg("sleepDelaySec"))
    {
        long v = server.arg("sleepDelaySec").toInt();
        if (v < 0) v = 0;
        if (v > 600) v = 600;
        cfg.sleepDelaySec = (uint32_t)v;
    }

    if (server.hasArg("netBadSec"))
    {
        long v = server.arg("netBadSec").toInt();
        if (v < 60) v = 60;
        if (v > 3600) v = 3600;
        cfg.netBadSec = (uint32_t)v;
    }

    if (server.hasArg("portalIdleSec"))
    {
        long v = server.arg("portalIdleSec").toInt();
        if (v < 60) v = 60;
        if (v > 86400) v = 86400;
        cfg.portalIdleSec = (uint32_t)v;
    }

    // Порог сна всегда держим ниже LVD.
    if (cfg.battSleep > cfg.battCutoff - 0.1f)
        cfg.battSleep = cfg.battCutoff - 0.1f;
    if (cfg.battSleep < 9.0f)
        cfg.battSleep = 9.0f;

    saveConfig();

    server.send(200,
                "text/html; charset=utf-8",
                "<meta charset='utf-8'><body style='font-family:sans-serif;text-align:center;padding-top:40px'>"
                "<h3>Сохранено. Перезагрузка…</h3></body>");

    delay(1500);
    ESP.restart();
}

void hNotFound()
{
    if (portalActive)
    {
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302, "text/plain", "");
    }
    else
    {
        server.send(404, "text/plain; charset=utf-8", "Not found");
    }
}

void beginWeb()
{
    if (webStarted)
        return;

    server.on("/", hRoot);
    server.on("/status", hStatus);
    server.on("/save", HTTP_POST, hSave);
    server.on("/channel", hChannel);
    server.on("/calbatt", hCalBatt);
    server.on("/calgrid", hCalGrid);
    server.on("/testntfy", hTestNtfy);
    server.on("/wifi/retry", hWifiRetry);
    server.on("/channel/restart", HTTP_POST, hChannelRestart);
    server.on("/events", hEvents);
    server.on("/events/clear", HTTP_POST, hEventsClear);
    server.on("/system/reboot", HTTP_POST, hSystemReboot);
    server.onNotFound(hNotFound);

    server.begin();
    webStarted = true;
}

// ============================================================
// SETUP / LOOP
// ============================================================

void setup()
{
    Serial.begin(115200);

    pinMode(PIN_LED_GRID, OUTPUT);
    pinMode(PIN_LED_BATT, OUTPUT);
    pinMode(PIN_LOAD_ROUTER, OUTPUT);
    pinMode(PIN_LOAD_ONT, OUTPUT);
    pinMode(PIN_BUTTON, INPUT_PULLUP);

    // При старте оба силовых ключа гарантированно выключены.
    digitalWrite(PIN_LOAD_ROUTER, LOW);
    digitalWrite(PIN_LOAD_ONT, LOW);
    routerOn = false;
    ontOn = false;
    buttonRaw = digitalRead(PIN_BUTTON);
    buttonStable = buttonRaw;

    // Короткий self-test LED.
    digitalWrite(PIN_LED_GRID, HIGH);
    digitalWrite(PIN_LED_BATT, HIGH);
    delay(400);
    digitalWrite(PIN_LED_GRID, LOW);
    digitalWrite(PIN_LED_BATT, LOW);

    analogReadResolution(12);
    analogSetPinAttenuation(PIN_VBATT, ADC_11db);
    analogSetPinAttenuation(PIN_VGRID, ADC_11db);

    esp_task_wdt_init(WDT_TIMEOUT, true);
    esp_task_wdt_add(NULL);

    loadConfig();

    // ВАЖНО: WebServer нельзя запускать до инициализации TCP/IP стека.
    // WiFi.mode()/WiFi.begin()/WiFi.softAP() ниже сначала поднимут lwIP,
    // и только после этого вызываем beginWeb().

    lastVbatt = readVoltage(PIN_VBATT, DIV_VBATT, cfg.calVbatt);
    lastVgrid = readVoltage(PIN_VGRID, DIV_VGRID, cfg.calVgrid);

    battState = getBattState(lastVbatt);
    gridPresent = lastVgrid > cfg.gridOn;
    gridCandidate = gridPresent;
    gridChangeSince = millis();

    esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
    bool wokeFromEmergencySleep =
        (wakeCause == ESP_SLEEP_WAKEUP_TIMER && rtcEmergencySleep);

    if (wokeFromEmergencySleep)
    {
        rtcSleepWakeCount++;

        // В аварийном режиме не поднимаем WiFi каждые 30 секунд.
        // Только быстро меряем АКБ/24В. Полный запуск — когда вернулась сеть
        // или аккумулятор действительно восстановился.
        if (!gridPresent && lastVbatt < cfg.battRestore)
        {
            Serial.printf("Emergency wake #%lu: Vbat=%.2f V24=%.2f -> sleep again\n",
                          (unsigned long)rtcSleepWakeCount,
                          lastVbatt,
                          lastVgrid);
            emergencySleepTimerOnly();
        }

        uint32_t sleptChecks = rtcSleepWakeCount;
        rtcEmergencySleep = false;
        rtcSleepWakeCount = 0;

        nextWifiConnectReason =
            "ИБП проснулся после аварийного сна. Проверок во сне: " +
            String(sleptChecks) + ".";
    }
    else if (cfg.deepSleepEnabled && !gridPresent && lastVbatt <= cfg.battSleep)
    {
        // Если устройство включили уже с глубоко разряженным АКБ —
        // не поднимаем радио и не добиваем батарею.
        rtcEmergencySleep = true;
        rtcSleepWakeCount = 0;
        Serial.printf("Cold boot with low battery %.2f V -> emergency sleep\n", lastVbatt);
        emergencySleepTimerOnly();
    }

    logEvent("BOOT FW " FW_VERSION +
             String(" | АКБ ") + String(lastVbatt, 2) +
             " В | 24В " + String(lastVgrid, 2) +
             " В | сеть " + (gridPresent ? "есть" : "нет"));

    if (!gridPresent)
    {
        outageStartedAt = millis();
        outageCount = 1;
    }

    // КЛЮЧЕВОЕ ИСПРАВЛЕНИЕ:
    // ESP управляет питанием роутера, к WiFi которого сама должна подключиться.
    // Поэтому сначала включаем нагрузку, потом ждём загрузку роутера,
    // и только затем пытаемся подключиться к WiFi.
    bool canPowerRouter = gridPresent || (lastVbatt > cfg.battCutoff);
    if (canPowerRouter)
    {
        setRouter(true);
        setOnt(true);
    }
    else
    {
        setBothLoads(false);
        // Если ESP загрузилась уже ниже LVD (но чуть выше резервного sleepV),
        // считаем LVD сработавшим: через sleepDelaySec она тоже уйдёт в сон.
        if (!gridPresent && lastVbatt <= cfg.battCutoff)
        {
            lvdTripped = true;
            sleepLowSince = millis();
        }
    }

    if (cfg.ssid == "")
    {
        wifiStatusText = "SSID не задан";
        startPortal();
    }
    else if (!routerOn)
    {
        wifiStatusText = "роутер отключён: АКБ ниже отсечки";
        startPortal();
    }
    else
    {
        Serial.printf("Waiting %lu ms for ROUTER/ONT boot...\n", (unsigned long)ROUTER_BOOT_MS);
        waitWithWatchdog(ROUTER_BOOT_MS);

        if (connectSTABlocking(WIFI_CONNECT_TIMEOUT_MS))
        {
            wifiWasConnected = true;
            handleWifiConnected();
        }
        else
        {
            wifiWasConnected = false;
            wifiStatusText = "WiFi не найден; портал активен";
            logEvent("WiFi не найден -> запускаю setup portal");
            startPortal();
        }
    }

    // К этому моменту WiFi уже инициализирован:
    // либо STA через connectSTABlocking(), либо AP+STA через startPortal().
    // Теперь безопасно открывать TCP-сокет WebServer.
    beginWeb();

    updateLeds();
}

void loop()
{
    esp_task_wdt_reset();

    protectTick();
    powerCycleTick();
    buttonTick();
    deepSleepTick();
    rebootTick();
    updateLeds();

    // WebServer работает и через домашний WiFi, и через setup AP.
    server.handleClient();

    if (portalActive)
        dns.processNextRequest();

    wifiRetryTick();
    wifiTick();
    portalMaintenanceTick();
    networkRecoveryTick();
    ntfyTick();
}
