// SPDX-License-Identifier: GPL-3.0-or-later
#include "wifi_mgr.h"

#include "ups_common.h"
#include "event_log.h"
#include "ntfy.h"
#include "power.h"
#include "hardware.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include "esp_task_wdt.h"
#include "web_ui.h"

bool          portalActive        = false;
bool          wifiWasConnected    = false;
bool          everWifiConnected   = false;
bool          mdnsStarted         = false;
String        wifiStatusText      = "инициализация";
String        nextWifiConnectReason;
bool          manualPortalSession = false;

unsigned long portalStopAt        = 0;
unsigned long portalLastActivity  = 0;
unsigned long portalReopenAt      = 0;
unsigned long manualPortalUntil   = 0;
unsigned long wifiRetryStarted    = 0;
unsigned long wifiRetryWaitUntil  = 0;
unsigned long lastReconnect       = 0;

Mode          mode           = MODE_RUN;
WifiRetryState wifiRetryState = WIFI_RETRY_IDLE;

void touchWebActivity()
{
    portalLastActivity = millis();
}

void stopPortal()
{
    if (!portalActive)
        return;

    dns.stop();
    WiFi.softAPdisconnect(true);
    portalActive        = false;
    portalStopAt        = 0;
    manualPortalSession = false;
    manualPortalUntil   = 0;

    if (WiFi.status() == WL_CONNECTED)
        WiFi.mode(WIFI_STA);

    logEvent("Setup portal остановлен");
}

void startPortal()
{
    if (!portalActive)
    {
        // AP+STA: keep the portal reachable while STA tries the home WiFi.
        WiFi.mode(WIFI_AP_STA);

        bool apOk;
        if (cfg.apPass.length() >= 8)
            apOk = WiFi.softAP(AP_SSID, cfg.apPass.c_str());
        else
            apOk = WiFi.softAP(AP_SSID);

        if (apOk)
        {
            dns.start(53, "*", WiFi.softAPIP());
            portalActive       = true;
            portalLastActivity = millis();
            portalReopenAt     = 0;
            logEvent("Setup AP " + String(AP_SSID) +
                     " запущен, IP " + WiFi.softAPIP().toString() +
                     (cfg.apPass.length() >= 8 ? " (WPA2)" : " (ОТКРЫТЫЙ)"));
        }
        else
        {
            logEvent("ОШИБКА: setup AP не запустился");
        }
    }

    mode         = MODE_PORTAL;
    portalStopAt = 0;

    if (WiFi.status() != WL_CONNECTED)
        wifiStatusText = "портал настройки";
}

void handleWifiConnected()
{
    String ip = WiFi.localIP().toString();

    wifiStatusText = "подключено: " + ip;
    mode           = MODE_RUN;

    // Auto-reconnect can re-enable modem sleep, which drops multicast (mDNS).
    // Force it off on every (re)connect.
    WiFi.setSleep(false);

    logEvent("WiFi подключён: " + cfg.ssid + ", IP " + ip);
    Serial.println("[ntfy] topic: " + (cfg.ntfy == "" ? String("NOT SET") : cfg.ntfy));

    // Restart mDNS on every connect — the responder gets flaky after a WiFi drop.
    if (mdnsStarted)
    {
        MDNS.end();
        mdnsStarted = false;
    }
    if (MDNS.begin(MDNS_HOST))
    {
        MDNS.addService("http", "tcp", 80);
        mdnsStarted = true;
        logEvent("mDNS запущен: " + mdnsUrl());
    }
    else
    {
        logEvent("mDNS: begin() не сработал");
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

    // If we came in through the captive portal, keep the AP alive briefly
    // so the browser sees the new IP, then shut it down.
    if (portalActive && !manualPortalSession)
        portalStopAt = millis() + PORTAL_STOP_DELAY_MS;
}

bool connectSTABlocking(uint32_t timeoutMs)
{
    if (cfg.ssid == "")
        return false;

    WiFi.mode(WIFI_STA);
    // Hostname must be set before begin() so DHCP announces it to the router.
    WiFi.setHostname(MDNS_HOST);
    WiFi.setAutoReconnect(true);
    // Modem sleep drops incoming multicast (mDNS). Off = reliable mDNS.
    WiFi.setSleep(false);
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());

    wifiStatusText = "подключение к " + cfg.ssid;
    logEvent("Подключение к WiFi: " + cfg.ssid);

    // Blink the green LED while associating so the user sees the ESP is alive
    // and busy joining WiFi (typical case: cold boot or wake from deep sleep,
    // when updateLeds() hasn't started running yet). ~2 Hz square wave.
    unsigned long t0        = millis();
    unsigned long nextBlink = 0;
    bool          ledOn     = false;
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs)
    {
        esp_task_wdt_reset();
        if ((long)(millis() - nextBlink) >= 0)
        {
            ledOn     = !ledOn;
            digitalWrite(PIN_LED_GRID, ledOn ? HIGH : LOW);
            nextBlink = millis() + 250;
        }
        delay(50);
    }

    // Leave the LED in a defined state; updateLeds() at the end of setup()
    // will re-derive it from gridPresent.
    digitalWrite(PIN_LED_GRID, LOW);

    return WiFi.status() == WL_CONNECTED;
}

static void beginPortalStaAttempt()
{
    if (cfg.ssid == "")
    {
        wifiRetryState = WIFI_RETRY_IDLE;
        wifiStatusText = "SSID не задан";
        return;
    }

    // Keep the portal alive: stay in AP+STA.
    if (!portalActive)
        startPortal();
    else
        WiFi.mode(WIFI_AP_STA);

    WiFi.setHostname(MDNS_HOST);
    WiFi.setSleep(false);
    WiFi.disconnect(false, false);
    delay(50);
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());

    wifiRetryState   = WIFI_RETRY_CONNECTING;
    wifiRetryStarted = millis();
    wifiStatusText   = "подключение к " + cfg.ssid;

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

    // The ESP joins the router's WiFi, so the ROUTER output must be on first.
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
        wifiRetryState     = WIFI_RETRY_WAIT_ROUTER;
        wifiRetryWaitUntil = readyAt;
        wifiStatusText     = "ждём загрузку роутера";
        answer = "Роутер включён. Ждём его загрузку, затем ESP подключится автоматически.";
    }
    else
    {
        beginPortalStaAttempt();
        answer = "Повторная попытка подключения к WiFi запущена.";
    }

    return true;
}

void startManualPortalFromButton()
{
    manualPortalSession = true;
    manualPortalUntil   = millis() + BUTTON_PORTAL_MS;
    startPortal();
    portalStopAt = 0;
    logEvent("Кнопка: setup AP включён на 15 минут");

    // Double-tap acknowledged: both LEDs blink once.
    digitalWrite(PIN_LED_GRID, HIGH);
    digitalWrite(PIN_LED_BATT, HIGH);
    delay(120);
    digitalWrite(PIN_LED_GRID, LOW);
    digitalWrite(PIN_LED_BATT, LOW);
    delay(80);
    updateLeds();
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
            return; // transition handled by wifiTick()
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
        wifiStatusText   = portalActive ? "портал настройки" : "WiFi отключён";
        logEvent("WiFi отключён");
        // Stop mDNS so the next reconnect gets a fresh responder.
        if (mdnsStarted)
        {
            MDNS.end();
            mdnsStarted = false;
        }
    }

    // Close the setup AP a little after a successful connect.
    if (portalActive && portalStopAt != 0 && connected &&
        (long)(millis() - portalStopAt) >= 0)
    {
        stopPortal();
    }

    // Auto-reconnect STA in the run mode. If the ROUTER output is off, there
    // is no point in scanning for a WiFi that isn't being powered.
    if (!connected && !portalActive && routerOn && cfg.ssid != "" &&
        wifiRetryState == WIFI_RETRY_IDLE)
    {
        // Give the router time to boot after being switched on.
        bool routerHadTime = (millis() - routerTurnedOnAt >= ROUTER_BOOT_MS);

        if (routerHadTime && millis() - lastReconnect >= WIFI_RECONNECT_MS)
        {
            lastReconnect  = millis();
            wifiStatusText = "автопереподключение";
            logEvent("WiFi: автопереподключение");
            WiFi.reconnect();
        }
    }
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
        // Any AP client keeps the portal alive.
        if (WiFi.softAPgetStationNum() > 0)
            portalLastActivity = millis();

        // With no SSID configured the portal must stay open indefinitely.
        if (cfg.ssid == "" || connected)
            return;

        // Otherwise, don't keep an empty AP running forever.
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

    // If home WiFi never came back after we closed the portal, reopen it
    // after a few minutes so the user has a local way in again.
    if (!connected && cfg.ssid != "" && portalReopenAt != 0 &&
        (long)(millis() - portalReopenAt) >= 0)
    {
        portalReopenAt = 0;
        startPortal();
    }
}
