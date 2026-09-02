// SPDX-License-Identifier: MIT
#include "web_ui.h"

#include "ups_common.h"
#include "event_log.h"
#include "power.h"
#include "wifi_mgr.h"
#include "ntfy.h"
#include "sleep_modes.h"
#include "recovery.h"
#include "config_store.h"

#include <WiFi.h>

WebServer server(80);
DNSServer dns;
bool      webStarted = false;

// ============================================================
// Хелперы для формы
// ============================================================
static String field(const String &label,
                    const String &name,
                    const String &value,
                    const String &hint = "",
                    const String &type = "text")
{
    String h = hint == "" ? "" : "<small>" + htmlEscape(hint) + "</small>";
    return "<label>" + htmlEscape(label) + h +
           "<input type='" + htmlEscape(type) +
           "' name='"      + htmlEscape(name) +
           "' value='"     + htmlEscape(value) + "'></label>";
}

static String checkboxField(const String &label,
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

static bool requireAuth()
{
    if (cfg.adminPass == "")
        return true;

    if (server.authenticate("admin", cfg.adminPass.c_str()))
        return true;

    server.requestAuthentication();
    return false;
}

// ============================================================
// Страница
// ============================================================
static String page()
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
         "Короткое нажатие — отправить полный статус в ntfy<br>"
         "Двойное — setup AP на 15 минут<br>"
         "Удержание 10 секунд — режим хранения: всё OFF, wake только кнопкой"
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

    p += "<div class='card'><h3>Уведомления / ntfy command</h3>"
         "<button onclick='testn()'>Отправить тест в ntfy</button>"
         "<div class='muted' style='margin-top:8px'>В этот же topic можно отправить сообщение <b>!ups status</b> или <b>!ups ping</b>. Устройство опрашивает ntfy примерно раз в 15 секунд и отвечает полным статусом.</div>"
         "</div>";

    p += "<form method='POST' action='/save'>";
    p += "<fieldset><legend>WiFi</legend>" +
         field("SSID",              "ssid",      cfg.ssid) +
         field("Пароль",            "pass",      "", "пусто — не менять", "password") +
         field("Пароль setup AP",   "apPass",    "", "пусто — не менять; минимум 8 символов", "password") +
         field("Пароль веб-панели", "adminPass", "", "пусто — не менять; пользователь: admin; введи - чтобы отключить защиту", "password") +
         "<small>По умолчанию setup AP: " AP_DEFAULT_PASSWORD "</small>"
         "</fieldset>";

    p += "<fieldset><legend>ntfy</legend>" +
         field("Тема (topic)", "ntfy", cfg.ntfy, "придумай длинную и не угадываемую") +
         "</fieldset>";

    p += "<fieldset><legend>Калибровка (коэфф.)</legend>" +
         field("CAL АКБ",  "calVbatt", String(cfg.calVbatt, 3)) +
         field("CAL GRID", "calVgrid", String(cfg.calVgrid, 3)) +
         "</fieldset>";

    p += "<fieldset><legend>Пороги, В</legend>" +
         field("Отсечка",                                "cutoff",  String(cfg.battCutoff, 1)) +
         field("Возврат",                                "restore", String(cfg.battRestore, 1)) +
         field("Предупр.",                               "warn",    String(cfg.battWarn, 1)) +
         field("Резервный аварийный порог сна <",        "sleepV",  String(cfg.battSleep, 1),
               "основной сон запускается после LVD; этот порог — запасной") +
         field("Сеть есть >",                            "gridOn",  String(cfg.gridOn, 1)) +
         field("Сети нет <",                             "gridOff", String(cfg.gridOff, 1)) +
         field("Антидребезг, с",                         "deb",     String(cfg.gridDebounce / 1000)) +
         "</fieldset>";

    p += "<fieldset><legend>Deep sleep</legend>" +
         checkboxField("Включить аварийный deep sleep", "deepSleep", cfg.deepSleepEnabled,
                       "после LVD ESP засыпает по таймеру проверки сети; отдельный режим хранения включается 10-секундным удержанием кнопки и просыпается только кнопкой") +
         field("Проверять сеть каждые, с", "sleepCheckSec", String(cfg.sleepCheckSec)) +
         field("Задержка перед сном, с",   "sleepDelaySec", String(cfg.sleepDelaySec)) +
         "</fieldset>";

    p += "<fieldset><legend>Автовосстановление интернета</legend>" +
         checkboxField("Автоматически перезапускать роутер/ONT", "autoRec", cfg.autoRecoveryEnabled,
                       "не более 2 power-cycle подряд; после 10 минут нормальной связи счётчик сбрасывается") +
         checkboxField("Разрешить один reboot ESP после двух неудачных power-cycle", "autoEsp", cfg.autoEspRebootEnabled,
                       "по умолчанию выключено: reboot ESP может кратко дёрнуть питание нагрузки") +
         field("Сколько секунд связи не должно быть", "netBadSec",     String(cfg.netBadSec)) +
         field("Таймаут setup AP без клиентов, с",    "portalIdleSec", String(cfg.portalIdleSec),
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

// ============================================================
// Handlers
// ============================================================
static void hRoot()
{
    touchWebActivity();
    if (!requireAuth()) return;
    server.send(200, "text/html; charset=utf-8", page());
}

static void hStatus()
{
    touchWebActivity();
    if (!requireAuth()) return;
    bool connected = WiFi.status() == WL_CONNECTED;
    int  rssi = connected ? WiFi.RSSI() : -127;
    String ip = connected ? WiFi.localIP().toString() : "";

    uint32_t currentOutageMs = (!gridPresent && outageStartedAt != 0)
                                  ? (millis() - outageStartedAt) : 0;

    String j = "{\"vb\":"          + String(lastVbatt, 2) +
               ",\"v24\":"         + String(lastVgrid, 2) +
               ",\"grid\":"        + String(gridPresent ? 1 : 0) +
               ",\"router_on\":"   + String(routerOn ? 1 : 0) +
               ",\"router_mode\":" + String((int)routerMode) +
               ",\"ont_on\":"      + String(ontOn ? 1 : 0) +
               ",\"ont_mode\":"    + String((int)ontMode) +
               ",\"state\":"       + String((int)battState) +
               ",\"wifi\":"        + String(connected ? 1 : 0) +
               ",\"rssi\":"        + String(rssi) +
               ",\"ip\":\""        + jsonEscape(ip) + "\"" +
               ",\"wifi_status\":\"" + jsonEscape(wifiStatusText) + "\"" +
               ",\"portal\":"      + String(portalActive ? 1 : 0) +
               ",\"power_cycle\":" + String(powerCycleState != PC_IDLE ? 1 : 0) +
               ",\"internet_known\":" + String(internetKnown ? 1 : 0) +
               ",\"internet\":"    + String(internetReachable ? 1 : 0) +
               ",\"recovery_status\":\"" + jsonEscape(recoveryStatus) + "\"" +
               ",\"auto_cycles\":"  + String(rtcAutoPowerCycles) +
               ",\"auto_reboots\":" + String(rtcNetReboots) +
               ",\"deep_sleep\":"  + String(cfg.deepSleepEnabled ? 1 : 0) +
               ",\"sleep_v\":"     + String(cfg.battSleep, 2) +
               ",\"sleep_check\":" + String(cfg.sleepCheckSec) +
               ",\"outages\":"     + String(outageCount) +
               ",\"outage_ms\":"   + String(currentOutageMs) +
               ",\"outage_text\":\""      + jsonEscape(formatDuration(currentOutageMs)) + "\"" +
               ",\"last_outage_ms\":"     + String(lastOutageDurationMs) +
               ",\"last_outage_text\":\"" + jsonEscape(formatDuration(lastOutageDurationMs)) + "\"" +
               ",\"up\":"          + String(millis() / 1000) +
               ",\"up_text\":\""   + jsonEscape(formatDuration(millis())) + "\"}";

    server.send(200, "application/json", j);
}

static void hChannel()
{
    touchWebActivity();
    if (!requireAuth()) return;

    String ch = server.arg("ch");
    String m  = server.arg("mode");
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

static void hCalBatt()
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

static void hCalGrid()
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

static void hTestNtfy()
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

static void hWifiRetry()
{
    touchWebActivity();
    if (!requireAuth()) return;
    String answer;
    bool ok = requestPortalWifiRetry(answer);

    server.send(ok ? 202 : 409, "text/plain; charset=utf-8", answer);
}

static void hChannelRestart()
{
    touchWebActivity();
    if (!requireAuth()) return;

    String ch = server.arg("ch");
    String answer;
    bool ok = false;

    if      (ch == "router") ok = requestRouterRestart(answer);
    else if (ch == "ont")    ok = requestOntRestart(answer);
    else if (ch == "both")   ok = requestBothRestart(answer);
    else
    {
        server.send(400, "text/plain; charset=utf-8", "Неизвестный канал.");
        return;
    }

    server.send(ok ? 202 : 409, "text/plain; charset=utf-8", answer);
}

static void hEvents()
{
    touchWebActivity();
    if (!requireAuth()) return;
    server.send(200, "text/plain; charset=utf-8", getEventLogText());
}

static void hEventsClear()
{
    touchWebActivity();
    if (!requireAuth()) return;
    clearEventLog();
    server.send(200, "text/plain; charset=utf-8", "Журнал очищен.");
}

static void hSystemReboot()
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

static void hSave()
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

    if (server.hasArg("calVbatt")) cfg.calVbatt   = server.arg("calVbatt").toFloat();
    if (server.hasArg("calVgrid")) cfg.calVgrid   = server.arg("calVgrid").toFloat();
    if (server.hasArg("cutoff"))   cfg.battCutoff = server.arg("cutoff").toFloat();
    if (server.hasArg("restore"))  cfg.battRestore= server.arg("restore").toFloat();
    if (server.hasArg("warn"))     cfg.battWarn   = server.arg("warn").toFloat();
    if (server.hasArg("sleepV"))   cfg.battSleep  = server.arg("sleepV").toFloat();
    if (server.hasArg("gridOn"))   cfg.gridOn     = server.arg("gridOn").toFloat();
    if (server.hasArg("gridOff"))  cfg.gridOff    = server.arg("gridOff").toFloat();

    if (server.hasArg("deb"))
    {
        long sec = server.arg("deb").toInt();
        if (sec < 0)   sec = 0;
        if (sec > 600) sec = 600;
        cfg.gridDebounce = (uint32_t)sec * 1000UL;
    }

    cfg.deepSleepEnabled     = server.hasArg("deepSleep");
    cfg.autoRecoveryEnabled  = server.hasArg("autoRec");
    cfg.autoEspRebootEnabled = server.hasArg("autoEsp");

    if (server.hasArg("sleepCheckSec"))
    {
        long v = server.arg("sleepCheckSec").toInt();
        if (v < 10)   v = 10;
        if (v > 3600) v = 3600;
        cfg.sleepCheckSec = (uint32_t)v;
    }

    if (server.hasArg("sleepDelaySec"))
    {
        long v = server.arg("sleepDelaySec").toInt();
        if (v < 0)   v = 0;
        if (v > 600) v = 600;
        cfg.sleepDelaySec = (uint32_t)v;
    }

    if (server.hasArg("netBadSec"))
    {
        long v = server.arg("netBadSec").toInt();
        if (v < 60)   v = 60;
        if (v > 3600) v = 3600;
        cfg.netBadSec = (uint32_t)v;
    }

    if (server.hasArg("portalIdleSec"))
    {
        long v = server.arg("portalIdleSec").toInt();
        if (v < 60)    v = 60;
        if (v > 86400) v = 86400;
        cfg.portalIdleSec = (uint32_t)v;
    }

    // Порог сна всегда держим ниже LVD.
    if (cfg.battSleep > cfg.battCutoff - 0.1f) cfg.battSleep = cfg.battCutoff - 0.1f;
    if (cfg.battSleep < 9.0f)                  cfg.battSleep = 9.0f;

    saveConfig();

    server.send(200, "text/html; charset=utf-8",
                "<meta charset='utf-8'><body style='font-family:sans-serif;text-align:center;padding-top:40px'>"
                "<h3>Сохранено. Перезагрузка…</h3></body>");

    delay(1500);
    ESP.restart();
}

static void hNotFound()
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
    if (webStarted) return;

    server.on("/",                  hRoot);
    server.on("/status",            hStatus);
    server.on("/save",              HTTP_POST, hSave);
    server.on("/channel",           hChannel);
    server.on("/calbatt",           hCalBatt);
    server.on("/calgrid",           hCalGrid);
    server.on("/testntfy",          hTestNtfy);
    server.on("/wifi/retry",        hWifiRetry);
    server.on("/channel/restart",   HTTP_POST, hChannelRestart);
    server.on("/events",            hEvents);
    server.on("/events/clear",      HTTP_POST, hEventsClear);
    server.on("/system/reboot",     HTTP_POST, hSystemReboot);
    server.onNotFound(hNotFound);

    server.begin();
    webStarted = true;
}
