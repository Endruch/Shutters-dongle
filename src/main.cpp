#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <DNSServer.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <new>
#include <stdarg.h>

#include "radio.h"
#include "web_pages.h"

#define FW_VERSION "0.1-esp32"
#define MAX_CH 128
#define LOG_LINES 60
#define REPEATS_NORMAL 2
#define REPEATS_AOK 8
#define REPEATS_TAP 3
#define REPEATS_LONG 18
#define LEARN_MS 20000

struct Config {
  String ssid, pass, ssid2, pass2;
  String apPass = "shutters";
  String authKey;
  bool pwSet = false;
  bool dhcp = true;
  String ip, mask, gw;
  String mqttHost = "0.0.0.0";
  uint16_t mqttPort = 1883;
  String mqttUser, mqttPass;
  String clientId;
  String deviceName;
  String topic;
  bool discovery = false;
  float freq = 433.42;
  float freqAok = 433.92;
  uint8_t pSck = 18, pMiso = 19, pMosi = 23, pCs = 5, pGdo0 = 4;
} cfg;

struct Shutter {
  bool en = false;
  uint32_t id = 0;
  uint16_t code = 1;
  uint8_t proto = 0;
  uint8_t pair = 0;
  int16_t state = -1;
  String name;
} sh[MAX_CH];

Preferences prefs;
#define IDLE_CLIENT_MS 300
#define IDLE_BUSY_MS 50
#define LISTEN_BACKLOG 8
class DongleServer : public WebServer {
 public:
  explicit DongleServer(int port) : WebServer(port) {
    _server.~WiFiServer();
    new (&_server) WiFiServer(port, LISTEN_BACKLOG);
  }
  void dropIdleClient() {
    if (_currentStatus != HC_WAIT_READ || _currentClient.available()) return;
    uint32_t idle = millis() - _statusChange;
    if (idle < IDLE_CLIENT_MS && (idle < IDLE_BUSY_MS || !_server.hasClient())) return;
    _currentClient.stop();
    _currentClient = WiFiClient();
    _currentStatus = HC_NONE;
  }
  void noDelay() { _server.setNoDelay(true); }
};
DongleServer server(80);
DNSServer dns;
WiFiClient net;
PubSubClient mqtt(net);

#define LED_PIN 2
#define BOOT_PIN 0
#define AP_WINDOW_MS 180000
#define AP_LOST_AFTER_MS 30000
#define AP_GRACE_MS 120000
#define AP_MAX_MS 600000
bool apOn = false;
uint32_t apUntil = 0, apStarted = 0, apGraceAt = 0;
bool bootPhase = true, staUp = false, lostAp = false, altToggle = false;
uint32_t downSince = 0, lastAttempt = 0;
bool radioOk = false;
uint8_t radioVer = 0;
uint32_t lastRadioCheck = 0;
uint32_t rebootAt = 0;
uint32_t lastMqttTry = 0;
bool haBirthPending = false;
bool otaFailed = false;

String logBuf[LOG_LINES];
uint32_t logCount = 0;

void logf(const char *fmt, ...) {
  char msg[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  uint32_t s = millis() / 1000;
  char line[200];
  snprintf(line, sizeof(line), "[%02u:%02u:%02u] %s", (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60), msg);
  logBuf[logCount % LOG_LINES] = line;
  logCount++;
  Serial.println(line);
}

String deviceSuffix() {
  char b[8];
  snprintf(b, sizeof(b), "%06X", (uint32_t)(ESP.getEfuseMac() >> 24));
  return String(b);
}

String deviceId() {
  if (cfg.deviceName.length()) return "shutter-" + cfg.deviceName;
  return "ShuttersDongle-" + deviceSuffix();
}

String defaultTopic() {
  if (cfg.deviceName.length()) return "shutters-" + cfg.deviceName;
  return "shutters-" + deviceSuffix();
}

static String clean(String s) {
  String o;
  for (size_t i = 0; i < s.length() && o.length() < 32; i++) {
    char c = s[i];
    if (c >= 32 && c != '"' && c != '\\' && c != '<' && c != '>') o += c;
  }
  return o;
}

static String cleanDeviceName(String s) {
  String o;
  for (size_t i = 0; i < s.length() && o.length() < 20; i++) {
    char c = s[i];
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') o += c;
    else if (c >= 'A' && c <= 'Z') o += (char)(c - 'A' + 'a');
  }
  return o;
}

void loadConfig() {
  prefs.begin("cfg", true);
  cfg.ssid = prefs.getString("ssid", "");
  cfg.pass = prefs.getString("pass", "");
  cfg.ssid2 = prefs.getString("ssid2", "");
  cfg.pass2 = prefs.getString("pass2", "");
  cfg.apPass = prefs.getString("apass", "shutters");
  cfg.authKey = prefs.getString("akey", "");
  cfg.pwSet = prefs.getBool("pwset", false);
  cfg.dhcp = prefs.getBool("dhcp", true);
  cfg.ip = prefs.getString("ip", "");
  cfg.mask = prefs.getString("mask", "255.255.255.0");
  cfg.gw = prefs.getString("gw", "");
  cfg.mqttHost = prefs.getString("mhost", "0.0.0.0");
  cfg.mqttPort = prefs.getUShort("mport", 1883);
  cfg.mqttUser = prefs.getString("muser", "");
  cfg.mqttPass = prefs.getString("mpass", "");
  char def[32];
  snprintf(def, sizeof(def), "ShuttersDongle-%s", deviceSuffix().c_str());
  cfg.clientId = prefs.getString("cid", def);
  if (cfg.clientId.startsWith("SomfyDongle-")) cfg.clientId = def;
  cfg.deviceName = prefs.getString("dname", "");
  cfg.topic = prefs.getString("topic", defaultTopic());
  cfg.discovery = prefs.getBool("disc", false);
  cfg.freq = prefs.getFloat("freq", 433.42);
  cfg.freqAok = prefs.getFloat("freq_aok", 433.92);
  cfg.pSck = prefs.getUChar("p_sck", 18);
  cfg.pMiso = prefs.getUChar("p_miso", 19);
  cfg.pMosi = prefs.getUChar("p_mosi", 23);
  cfg.pCs = prefs.getUChar("p_cs", 5);
  cfg.pGdo0 = prefs.getUChar("p_gdo0", 4);
  prefs.end();
}

void saveConfig() {
  prefs.begin("cfg", false);
  prefs.putString("ssid", cfg.ssid);
  prefs.putString("pass", cfg.pass);
  prefs.putString("ssid2", cfg.ssid2);
  prefs.putString("pass2", cfg.pass2);
  prefs.putString("apass", cfg.apPass);
  prefs.putString("akey", cfg.authKey);
  prefs.putBool("pwset", cfg.pwSet);
  prefs.putBool("dhcp", cfg.dhcp);
  prefs.putString("ip", cfg.ip);
  prefs.putString("mask", cfg.mask);
  prefs.putString("gw", cfg.gw);
  prefs.putString("mhost", cfg.mqttHost);
  prefs.putUShort("mport", cfg.mqttPort);
  prefs.putString("muser", cfg.mqttUser);
  prefs.putString("mpass", cfg.mqttPass);
  prefs.putString("cid", cfg.clientId);
  prefs.putString("dname", cfg.deviceName);
  prefs.putString("topic", cfg.topic);
  prefs.putBool("disc", cfg.discovery);
  prefs.putFloat("freq", cfg.freq);
  prefs.putFloat("freq_aok", cfg.freqAok);
  prefs.putUChar("p_sck", cfg.pSck);
  prefs.putUChar("p_miso", cfg.pMiso);
  prefs.putUChar("p_mosi", cfg.pMosi);
  prefs.putUChar("p_cs", cfg.pCs);
  prefs.putUChar("p_gdo0", cfg.pGdo0);
  prefs.end();
}

void loadShutters() {
  uint32_t chip = (uint32_t)(ESP.getEfuseMac() >> 24) & 0xFFFF;
  prefs.begin("sh", true);
  for (int i = 0; i < MAX_CH; i++) {
    char k[8];
    snprintf(k, sizeof(k), "e%d", i);
    sh[i].en = prefs.getBool(k, false);
    snprintf(k, sizeof(k), "i%d", i);
    sh[i].id = prefs.getUInt(k, (chip << 8) | i);
    snprintf(k, sizeof(k), "c%d", i);
    sh[i].code = prefs.getUShort(k, 1);
    snprintf(k, sizeof(k), "s%d", i);
    sh[i].state = prefs.getShort(k, -1);
    snprintf(k, sizeof(k), "p%d", i);
    sh[i].proto = prefs.getUChar(k, 0);
    snprintf(k, sizeof(k), "q%d", i);
    sh[i].pair = prefs.getUChar(k, 0);
    snprintf(k, sizeof(k), "n%d", i);
    sh[i].name = prefs.getString(k, String("Shutter ") + i);
  }
  prefs.end();
}

void saveShutter(int i, bool withIdentity) {
  prefs.begin("sh", false);
  char k[8];
  snprintf(k, sizeof(k), "c%d", i);
  prefs.putUShort(k, sh[i].code);
  snprintf(k, sizeof(k), "s%d", i);
  prefs.putShort(k, sh[i].state);
  if (withIdentity) {
    snprintf(k, sizeof(k), "e%d", i);
    prefs.putBool(k, sh[i].en);
    snprintf(k, sizeof(k), "i%d", i);
    prefs.putUInt(k, sh[i].id);
    snprintf(k, sizeof(k), "p%d", i);
    prefs.putUChar(k, sh[i].proto);
    snprintf(k, sizeof(k), "q%d", i);
    prefs.putUChar(k, sh[i].pair);
    snprintf(k, sizeof(k), "n%d", i);
    prefs.putString(k, sh[i].name);
  }
  prefs.end();
}

enum Cmd { C_UP, C_DOWN, C_STOP, C_SHADE, C_SETSHADE, C_LEARN, C_NONE };

Cmd parseCmd(String p) {
  p.trim();
  p.toUpperCase();
  if (p == "UP" || p == "OPEN") return C_UP;
  if (p == "DOWN" || p == "CLOSE") return C_DOWN;
  if (p == "STOP") return C_STOP;
  if (p == "SHADE") return C_SHADE;
  if (p == "SETSHADE") return C_SETSHADE;
  if (p == "LEARN") return C_LEARN;
  return C_NONE;
}

struct Job {
  uint8_t ch;
  Cmd cmd;
};
#define QUEUE_LEN 32
Job queue[QUEUE_LEN];
uint8_t qHead = 0, qTail = 0;

bool enqueue(uint8_t ch, Cmd cmd) {
  uint8_t next = (qHead + 1) % QUEUE_LEN;
  if (next == qTail) return false;
  queue[qHead] = {ch, cmd};
  qHead = next;
  return true;
}

void publishState(int ch) {
  if (!mqtt.connected() || sh[ch].state < 0) return;
  char t[64];
  snprintf(t, sizeof(t), "stat/%s/shutter/%d", cfg.topic.c_str(), ch);
  mqtt.publish(t, String(sh[ch].state).c_str(), true);
}

void execute(uint8_t ch, Cmd cmd) {
  if (!radioOk) {
    logf("ch%u: radio not available, command dropped", ch);
    return;
  }
  Shutter &s = sh[ch];
  if (s.proto == 1) {
    uint8_t ac;
    const char *name;
    switch (cmd) {
      case C_UP: ac = Radio::AOK_UP; name = "UP"; s.state = 0; break;
      case C_DOWN: ac = Radio::AOK_DOWN; name = "DOWN"; s.state = 100; break;
      case C_STOP: ac = Radio::AOK_STOP; name = "STOP"; break;
      case C_LEARN: ac = s.pair ? Radio::AOK_PROGRAM : Radio::AOK_UP; name = "LEARN"; break;
      default: logf("ch%u: command not supported by A-OK", ch); return;
    }
    Radio::transmitAOK(s.id, s.code, ac, REPEATS_AOK);
    logf("ch%u A-OK %s id=%06X addr=%04X", ch, name, (unsigned)s.id, s.code);
    saveShutter(ch, false);
    publishState(ch);
    return;
  }
  uint8_t code = CMD_MY;
  uint8_t repeats = REPEATS_NORMAL;
  const char *name = "";
  switch (cmd) {
    case C_UP: code = CMD_UP; name = "UP"; s.state = 0; break;
    case C_DOWN: code = CMD_DOWN; name = "DOWN"; s.state = 100; break;
    case C_STOP: code = CMD_MY; name = "STOP"; break;
    case C_SHADE: code = CMD_MY; name = "SHADE"; s.state = 90; break;
    case C_SETSHADE: code = CMD_MY; name = "SETSHADE"; repeats = REPEATS_LONG; break;
    case C_LEARN: code = CMD_PROG; name = "LEARN"; repeats = REPEATS_TAP; break;
    default: return;
  }
  uint8_t frame[7];
  Radio::buildFrame(frame, s.id, s.code, code);
  Radio::transmit(frame, repeats);
  logf("ch%u %s id=%06X code=%u", ch, name, (unsigned)s.id, s.code);
  s.code++;
  saveShutter(ch, false);
  publishState(ch);
}

enum SeqKind { SQ_NONE, SQ_LEARN, SQ_UNPAIR };
struct Seq {
  SeqKind kind = SQ_NONE;
  uint8_t ch = 0, step = 0;
  uint32_t start = 0, stepStart = 0, lastHeard = 0;
  bool heard = false, sendNow = false;
  uint8_t frame[7];
} seq;

uint32_t seqTotalMs() { return seq.kind == SQ_LEARN ? LEARN_MS : (sh[seq.ch].proto == 1 ? 8500 : 6000); }

void publishDiscovery(int only = -1);

uint32_t freshRemoteId(int forCh) {
  uint32_t id;
  bool clash;
  do {
    id = esp_random() & 0xFFFFFF;
    clash = id == 0;
    for (int i = 0; !clash && i < MAX_CH; i++)
      if (i != forCh && sh[i].id == id) clash = true;
  } while (clash);
  return id;
}

void wipeChannel(int ch) {
  Shutter &s = sh[ch];
  s.en = false;
  s.proto = 0;
  s.pair = 0;
  s.id = freshRemoteId(ch);
  s.code = 1;
  s.state = -1;
  s.name = String("Shutter ") + ch;
  saveShutter(ch, true);
  if (mqtt.connected()) {
    char t[64];
    snprintf(t, sizeof(t), "stat/%s/shutter/%d", cfg.topic.c_str(), ch);
    mqtt.publish(t, "", true);
    publishDiscovery(ch);
  }
  logf("ch%d deleted", ch);
}

void somfyTap(uint8_t ch, uint8_t cmd, uint8_t repeats) {
  Shutter &s = sh[ch];
  uint8_t f[7];
  Radio::buildFrame(f, s.id, s.code, cmd);
  Radio::transmit(f, repeats);
  s.code++;
  saveShutter(ch, false);
}

bool startLearn(uint8_t ch) {
  if (seq.kind != SQ_NONE || !radioOk) return false;
  seq = Seq();
  seq.kind = SQ_LEARN;
  seq.ch = ch;
  seq.start = millis();
  Radio::rxStart();
  logf("ch%u learn: listening 20 s for PROG on a paired remote", ch);
  return true;
}

bool startUnpair(uint8_t ch) {
  if (seq.kind != SQ_NONE || !radioOk) return false;
  seq = Seq();
  seq.kind = SQ_UNPAIR;
  seq.ch = ch;
  seq.start = seq.stepStart = millis();
  logf("ch%u unpair started", ch);
  return true;
}

void seqLoop() {
  if (seq.kind == SQ_NONE) return;
  uint32_t now = millis();
  Shutter &s = sh[seq.ch];

  if (seq.kind == SQ_LEARN) {
    uint8_t c;
    uint32_t id;
    uint16_t code;
    while (Radio::rxPoll(c, id, code)) {
      if (c == CMD_PROG) {
        if (!seq.heard) logf("ch%u learn: PROG heard from remote %06X", seq.ch, (unsigned)id);
        seq.heard = true;
        seq.lastHeard = now;
      }
    }
    bool released = seq.heard && now - seq.lastHeard > 600;
    bool timeout = now - seq.start > LEARN_MS;
    if (released || timeout || seq.sendNow) {
      Radio::rxStop();
      somfyTap(seq.ch, CMD_PROG, REPEATS_TAP);
      logf("ch%u learn: PROG sent (%s)", seq.ch, released ? "after remote" : seq.sendNow ? "on request" : "timeout");
      seq.kind = SQ_NONE;
    }
    return;
  }

  if (s.proto == 0) {
    switch (seq.step) {
      case 0:
        Radio::buildFrame(seq.frame, s.id, s.code, CMD_PROG);
        Radio::transmit(seq.frame, 19, true);
        s.code++;
        seq.step = 1;
        seq.stepStart = now;
        break;
      case 1:
        if (now - seq.stepStart > 1500) {
          somfyTap(seq.ch, CMD_PROG, REPEATS_TAP);
          seq.step = 2;
          seq.stepStart = millis();
        }
        break;
      default:
        if (now - seq.stepStart > 800) {
          wipeChannel(seq.ch);
          seq.kind = SQ_NONE;
        }
    }
  } else {
    switch (seq.step) {
      case 0:
        Radio::transmitAOK(s.id, s.code, Radio::AOK_STOP, 8);
        if (millis() - seq.stepStart >= 5000) {
          seq.step = 1;
          seq.stepStart = millis();
        }
        break;
      case 1:
        if (now - seq.stepStart > 1500) {
          seq.step = 2;
          seq.stepStart = now;
        }
        break;
      case 2:
        Radio::transmitAOK(s.id, s.code, Radio::AOK_PROGRAM, 8);
        if (millis() - seq.stepStart >= 1000) {
          seq.step = 3;
          seq.stepStart = millis();
        }
        break;
      default:
        if (now - seq.stepStart > 500) {
          wipeChannel(seq.ch);
          seq.kind = SQ_NONE;
        }
    }
  }
}

String topicOf(const char *kind, const char *tail) { return String(kind) + "/" + cfg.topic + "/" + tail; }

void publishDiscoveryFor(int i, const String &mac) {
  String base = "homeassistant/cover/shuttersdongle_" + mac + "_" + i + "/config";
  if (!sh[i].en) {
    mqtt.publish(base.c_str(), "", true);
    return;
  }
  String cmd = topicOf("cmd", (String("shutter/") + i).c_str());
  String st = topicOf("stat", (String("shutter/") + i).c_str());
  String p = "{\"name\":\"" + sh[i].name + "\",\"unique_id\":\"shuttersdongle_" + mac + "_" + i + "\"";
  p += ",\"command_topic\":\"" + cmd + "\",\"state_topic\":\"" + st + "\"";
  p += ",\"payload_open\":\"UP\",\"payload_close\":\"DOWN\",\"payload_stop\":\"STOP\"";
  p += ",\"position_topic\":\"" + st + "\",\"position_open\":0,\"position_closed\":100";
  p += ",\"state_open\":\"0\",\"state_closed\":\"100\"";
  p += ",\"availability_topic\":\"" + topicOf("tele", "LWT") + "\"";
  p += ",\"payload_available\":\"Online\",\"payload_not_available\":\"Offline\"";
  p += ",\"device\":{\"identifiers\":[\"shuttersdongle_" + mac + "\"],\"name\":\"Shutters Dongle\",\"manufacturer\":\"DIY\",\"model\":\"ESP32+CC1101\"}}";
  mqtt.publish(base.c_str(), p.c_str(), true);
}

void publishDiscovery(int only) {
  if (!cfg.discovery) return;
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  if (only >= 0) {
    publishDiscoveryFor(only, mac);
    return;
  }
  for (int i = 0; i < MAX_CH; i++)
    if (sh[i].en) publishDiscoveryFor(i, mac);
}

void clearDiscovery() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  for (int i = 0; i < MAX_CH; i++) {
    if (!sh[i].en) continue;
    String base = "homeassistant/cover/shuttersdongle_" + mac + "_" + i + "/config";
    mqtt.publish(base.c_str(), "", true);
  }
}

void onMqtt(char *topic, byte *payload, unsigned int len) {
  String t = topic;
  if (t == "homeassistant/status") {
    if (len == 6 && memcmp(payload, "online", 6) == 0) haBirthPending = true;
    return;
  }
  int pos = t.lastIndexOf('/');
  if (pos < 0 || t.substring(0, pos) != "cmd/" + cfg.topic + "/shutter") return;
  String chStr = t.substring(pos + 1);
  bool numeric = chStr.length() > 0;
  for (size_t i = 0; numeric && i < chStr.length(); i++)
    if (!isDigit(chStr[i])) numeric = false;
  int ch = numeric ? chStr.toInt() : -1;
  String body;
  for (unsigned i = 0; i < len; i++) body += (char)payload[i];
  Cmd c = parseCmd(body);
  if (ch < 0 || ch >= MAX_CH || !sh[ch].en || c == C_NONE) {
    logf("MQTT ignored: %s = %s", topic, body.c_str());
    return;
  }
  if (!enqueue(ch, c)) logf("queue full, dropped ch%d", ch);
}

void mqttLoop() {
  if (cfg.mqttHost == "0.0.0.0" || cfg.mqttHost.length() == 0 || WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) {
    mqtt.loop();
    if (haBirthPending) {
      haBirthPending = false;
      logf("HA online, resending discovery");
      publishDiscovery();
      for (int i = 0; i < MAX_CH; i++)
        if (sh[i].en) publishState(i);
    }
    return;
  }
  if (millis() - lastMqttTry < 5000 && lastMqttTry != 0) return;
  lastMqttTry = millis();
  logf("MQTT connecting to %s:%u", cfg.mqttHost.c_str(), cfg.mqttPort);
  mqtt.setServer(cfg.mqttHost.c_str(), cfg.mqttPort);
  String lwt = topicOf("tele", "LWT");
  bool ok = cfg.mqttUser.length()
                ? mqtt.connect(cfg.clientId.c_str(), cfg.mqttUser.c_str(), cfg.mqttPass.c_str(), lwt.c_str(), 0, true, "Offline")
                : mqtt.connect(cfg.clientId.c_str(), nullptr, nullptr, lwt.c_str(), 0, true, "Offline");
  if (!ok) {
    logf("MQTT failed, rc=%d", mqtt.state());
    return;
  }
  mqtt.publish(lwt.c_str(), "Online", true);
  mqtt.subscribe(topicOf("cmd", "shutter/+").c_str());
  mqtt.subscribe("homeassistant/status");
  if (cfg.discovery) publishDiscovery();
  else clearDiscovery();
  for (int i = 0; i < MAX_CH; i++)
    if (sh[i].en) publishState(i);
  logf("MQTT connected");
}

void sendPage(const char *body) {
  String page = FPSTR(PAGE_HEAD);
  page += FPSTR(body);
  page += FPSTR(PAGE_TAIL);
  server.send(200, "text/html", page);
}

String jsonEsc(const String &s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') {
      o += '\\';
      o += c;
    } else if ((uint8_t)c >= 32) {
      o += c;
    }
  }
  return o;
}

#define MAX_SESS 1
#define SESSION_MS (3600UL * 1000)
String sessions[MAX_SESS];
uint32_t sessionAt[MAX_SESS], sessionIp[MAX_SESS];
uint8_t sessNext = 0;
#define MAX_TOKENS 2
#define TOKEN_MS (24UL * 3600 * 1000)
String apiTokens[MAX_TOKENS];
uint32_t apiTokenAt[MAX_TOKENS];
uint8_t tokNext = 0;
#define TICKET_MS 120000
String updTicket;
uint32_t updTicketAt = 0;
bool otaByTicket = false;
#define MAX_NONCES 8
#define NONCE_MS 30000
String nonces[MAX_NONCES];
uint32_t nonceAt[MAX_NONCES];
uint8_t nonceNext = 0;
#define FAIL_SLOTS 12
#define GLOBAL_FAILS 20
#define GLOBAL_WINDOW_MS 60000
struct LoginFail {
  uint32_t ip = 0;
  uint8_t n = 0;
  uint32_t at = 0;
  bool trusted = false;
} loginFails[FAIL_SLOTS];
uint32_t failWindowAt = 0;
uint8_t failsInWindow = 0;

void redirect(const char *to) {
  server.sendHeader("Location", to, true);
  server.send(302, "text/plain", "");
}

bool viaAp() { return apOn && server.client().localIP() == WiFi.softAPIP(); }

static bool safeEq(const String &a, const String &b) {
  uint8_t diff = a.length() != b.length();
  for (size_t i = 0; i < a.length() && i < b.length(); i++) diff |= a[i] ^ b[i];
  return diff == 0;
}

String randomHex(uint8_t bytes) {
  String o;
  for (uint8_t i = 0; i < bytes; i++) {
    char b[3];
    snprintf(b, sizeof(b), "%02x", (unsigned)(esp_random() & 0xFF));
    o += b;
  }
  return o;
}

String hmacHex(const String &key, const String &msg) {
  uint8_t out[32];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (const uint8_t *)key.c_str(), key.length(),
                  (const uint8_t *)msg.c_str(), msg.length(), out);
  String o;
  for (uint8_t b : out) {
    char h[3];
    snprintf(h, sizeof(h), "%02x", b);
    o += h;
  }
  return o;
}

String macHex();

#define KDF_ROUNDS 50000

String deriveKey(const String &pw) {
  String salt = "shuttersdongle:" + macHex();
  uint8_t out[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_pkcs5_pbkdf2_hmac(&ctx, (const uint8_t *)pw.c_str(), pw.length(), (const uint8_t *)salt.c_str(), salt.length(), KDF_ROUNDS,
                            sizeof(out), out);
  mbedtls_md_free(&ctx);
  String o;
  for (uint8_t b : out) {
    char h[3];
    snprintf(h, sizeof(h), "%02x", b);
    o += h;
  }
  return o;
}

void ensureAuthKey() {
  if (!cfg.pwSet || cfg.authKey.length() == 64) return;
  uint32_t t = millis();
  cfg.authKey = deriveKey(cfg.apPass);
  saveConfig();
  logf("API key derived in %u ms", (unsigned)(millis() - t));
}

bool hostOk() {
  if (viaAp()) return true;
  String h = server.hostHeader();
  int colon = h.lastIndexOf(':');
  if (colon >= 0) h = h.substring(0, colon);
  h.toLowerCase();
  if (!h.length()) return true;
  IPAddress ip;
  if (ip.fromString(h)) return true;
  String id = deviceId();
  id.toLowerCase();
  return h == id || h.startsWith(id + ".");
}

void secHeaders() {
  server.sendHeader("X-Frame-Options", "DENY");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.sendHeader("Referrer-Policy", "no-referrer");
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Content-Security-Policy",
                    "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; "
                    "frame-ancestors 'none'; form-action 'self'");
}

bool cookieOk() {
  if (!server.hasHeader("Cookie")) return false;
  String c = server.header("Cookie");
  int p = -1, from = 0;
  while (true) {
    p = c.indexOf("sid=", from);
    if (p < 0) break;
    if (p == 0 || c[p - 1] == ' ' || c[p - 1] == ';') break;
    from = p + 4;
  }
  if (p < 0) return false;
  String tok = c.substring(p + 4);
  int e = tok.indexOf(';');
  if (e >= 0) tok = tok.substring(0, e);
  tok.trim();
  if (tok.length() != 32) return false;
  for (uint8_t i = 0; i < MAX_SESS; i++) {
    if (sessions[i].length() != 32 || !safeEq(sessions[i], tok)) continue;
    if ((uint32_t)(millis() - sessionAt[i]) > SESSION_MS) return false;
    if (sessionIp[i] != (uint32_t)server.client().remoteIP()) return false;
    sessionAt[i] = millis();
    return true;
  }
  return false;
}

String bearer() {
  String h = server.header("Authorization");
  if (!h.startsWith("Bearer ")) return "";
  String tok = h.substring(7);
  tok.trim();
  return tok.length() == 32 ? tok : "";
}

bool ticketOk() {
  String tok = bearer();
  if (!tok.length() || updTicket.length() != 32 || !safeEq(updTicket, tok)) return false;
  updTicket = "";
  return millis() - updTicketAt < TICKET_MS;
}

bool tokenOk() {
  String tok = bearer();
  if (!tok.length()) return false;
  for (uint8_t i = 0; i < MAX_TOKENS; i++) {
    if (apiTokens[i].length() == 32 && safeEq(apiTokens[i], tok)) return (uint32_t)(millis() - apiTokenAt[i]) <= TOKEN_MS;
  }
  return false;
}

uint32_t lockMs(uint8_t n) { return n < 5 ? 1000 : min<uint32_t>(1000UL << min(n - 4, 9), 300000); }

bool slotLocked(const LoginFail &f) { return f.n && millis() - f.at < lockMs(f.n); }

LoginFail *failSlot(bool create) {
  uint32_t ip = server.client().remoteIP();
  LoginFail *pick = nullptr;
  uint32_t pickAge = 0;
  for (LoginFail &f : loginFails) {
    bool used = f.n || f.trusted;
    if (used && f.ip == ip) return &f;
    if (f.trusted || slotLocked(f)) continue;
    uint32_t age = used ? millis() - f.at : UINT32_MAX;
    if (!pick || age > pickAge) {
      pick = &f;
      pickAge = age;
    }
  }
  if (!create || !pick) return nullptr;
  *pick = LoginFail();
  pick->ip = ip;
  return pick;
}

bool deviceUnderAttack() {
  if (millis() - failWindowAt > GLOBAL_WINDOW_MS) failsInWindow = 0;
  return failsInWindow >= GLOBAL_FAILS;
}

bool loginLocked() {
  LoginFail *f = failSlot(false);
  if (f && slotLocked(*f)) return true;
  return deviceUnderAttack() && !(f && f->trusted);
}

void loginFailed(const char *what) {
  if (millis() - failWindowAt > GLOBAL_WINDOW_MS) {
    failWindowAt = millis();
    failsInWindow = 0;
  }
  if (failsInWindow < 255) failsInWindow++;
  uint8_t n = 0;
  if (LoginFail *f = failSlot(true)) {
    if (f->n < 255) f->n++;
    f->at = millis();
    n = f->n;
  }
  logf("%s login failed from %s (%u in a row)", what, server.client().remoteIP().toString().c_str(), n);
}

void loginOk() {
  LoginFail *f = failSlot(true);
  if (!f) return;
  f->n = 0;
  f->trusted = true;
  f->at = millis();
}

bool authed() { return cfg.pwSet && (viaAp() || cookieOk()); }

bool allowed(bool apiToken = false) {
  if (!hostOk()) {
    server.send(403, "text/plain", "bad host");
    return false;
  }
  String u = server.uri();
  if (!cfg.pwSet) {
    if (u == "/setpw.html" || u == "/api/setpw") return true;
    redirect("/setpw.html");
    return false;
  }
  if (authed() || (apiToken && tokenOk())) return true;
  if (u.startsWith("/api/"))
    server.send(401, "text/plain", "login required");
  else
    redirect("/login.html");
  return false;
}

void route(const char *uri, HTTPMethod m, WebServer::THandlerFunction fn) {
  server.on(uri, m, [fn]() {
    secHeaders();
    if (allowed()) fn();
  });
}

void apiRoute(const char *uri, HTTPMethod m, WebServer::THandlerFunction fn) {
  server.on(uri, m, [fn]() {
    secHeaders();
    if (allowed(true)) fn();
  });
}

void openRoute(const char *uri, HTTPMethod m, WebServer::THandlerFunction fn) {
  server.on(uri, m, [fn]() {
    secHeaders();
    if (hostOk()) fn();
    else server.send(403, "text/plain", "bad host");
  });
}

void handleChallenge() {
  String n = randomHex(16);
  uint8_t slot = nonceNext++ % MAX_NONCES;
  nonces[slot] = n;
  nonceAt[slot] = millis();
  server.send(200, "application/json", "{\"n\":\"" + n + "\"}");
}

void handleToken() {
  if (!cfg.pwSet) return server.send(401, "text/plain", "password not set");
  if (loginLocked()) return server.send(429, "text/plain", "too many attempts, wait a moment");
  String n = server.arg("n"), c = server.arg("c"), h = server.arg("h");
  bool fresh = false;
  for (uint8_t i = 0; i < MAX_NONCES; i++) {
    if (n.length() != 32 || nonces[i].length() != 32 || !safeEq(nonces[i], n)) continue;
    fresh = millis() - nonceAt[i] < NONCE_MS;
    nonces[i] = "";
  }
  if (!fresh) return server.send(400, "text/plain", "challenge expired");
  ensureAuthKey();
  if (c.length() != 32 || !safeEq(h, hmacHex(cfg.authKey, "shuttersdongle-auth:" + n + ":" + c))) {
    loginFailed("API");
    return server.send(401, "text/plain", "wrong password");
  }
  loginOk();
  String t = randomHex(16);
  if (server.arg("scope") == "update") {
    updTicket = t;
    updTicketAt = millis();
  } else {
    uint8_t slot = tokNext++ % MAX_TOKENS;
    apiTokens[slot] = t;
    apiTokenAt[slot] = millis();
  }
  String proof = hmacHex(cfg.authKey, "shuttersdongle-dongle:" + n + ":" + c + ":" + t);
  server.send(200, "application/json", "{\"t\":\"" + t + "\",\"p\":\"" + proof + "\"}");
}

void handleLogin() {
  if (!cfg.pwSet) return redirect("/setpw.html");
  if (loginLocked()) return server.send(429, "text/plain", "too many attempts, wait a moment");
  if (!safeEq(server.arg("password"), cfg.apPass)) {
    loginFailed("web");
    return server.send(403, "text/plain", "wrong password");
  }
  loginOk();
  String tok = randomHex(16);
  uint8_t slot = sessNext++ % MAX_SESS;
  sessions[slot] = tok;
  sessionAt[slot] = millis();
  sessionIp[slot] = server.client().remoteIP();
  server.sendHeader("Set-Cookie", "sid=" + tok + "; Path=/; HttpOnly; SameSite=Strict");
  server.send(200, "text/plain", "OK");
}

void handleSetPw() {
  if (cfg.pwSet) {
    if (loginLocked()) return server.send(429, "text/plain", "Too many attempts, wait a moment");
    if (!safeEq(server.arg("old"), cfg.apPass)) {
      loginFailed("password change");
      return server.send(403, "text/plain", "Current password is wrong");
    }
  }
  String a = server.arg("pw"), b = server.arg("pw2");
  if (a.length() < 10 || a.length() > 63) return server.send(400, "text/plain", "Password must be 10 to 63 characters");
  if (a != b) return server.send(400, "text/plain", "Passwords do not match");
  if (a == "shutters") return server.send(400, "text/plain", "Choose a different password than the default one");
  cfg.apPass = a;
  cfg.authKey = deriveKey(a);
  cfg.pwSet = true;
  saveConfig();
  for (uint8_t i = 0; i < MAX_SESS; i++) sessions[i] = "";
  for (uint8_t i = 0; i < MAX_TOKENS; i++) apiTokens[i] = "";
  updTicket = "";
  logf("password changed, rebooting");
  server.send(200, "text/plain", "OK");
  rebootAt = millis() + 1500;
}

void handleScanStart() {
  WiFi.scanDelete();
  WiFi.scanNetworks(true, false, false, 300);
  server.send(200, "text/plain", "OK");
}

void handleScanGet() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return server.send(200, "application/json", "{\"status\":\"run\"}");
  if (n < 0) return server.send(200, "application/json", "{\"status\":\"idle\",\"nets\":[]}");
  String j = "{\"status\":\"done\",\"nets\":[";
  bool first = true;
  for (int i = 0; i < n; i++) {
    String ss = WiFi.SSID(i);
    if (!ss.length()) continue;
    bool dup = false;
    for (int k = 0; k < n; k++)
      if (k != i && WiFi.SSID(k) == ss && (WiFi.RSSI(k) > WiFi.RSSI(i) || (WiFi.RSSI(k) == WiFi.RSSI(i) && k < i))) dup = true;
    if (dup) continue;
    if (!first) j += ",";
    first = false;
    j += "{\"ssid\":\"" + jsonEsc(ss) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"lock\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? 0 : 1) + "}";
  }
  WiFi.scanDelete();
  server.send(200, "application/json", j + "]}");
}

void handleConfigGet() {
  String j = "{";
  j += "\"fw\":\"" FW_VERSION "\",";
  j += "\"ssid\":\"" + jsonEsc(cfg.ssid) + "\",\"ssid2\":\"" + jsonEsc(cfg.ssid2) + "\",\"dhcp\":" + (cfg.dhcp ? 1 : 0) + ",";
  j += "\"ip\":\"" + cfg.ip + "\",\"mask\":\"" + cfg.mask + "\",\"gw\":\"" + cfg.gw + "\",";
  j += "\"mqtt_host\":\"" + cfg.mqttHost + "\",\"mqtt_port\":" + cfg.mqttPort + ",";
  j += "\"mqtt_user\":\"" + cfg.mqttUser + "\",\"client_id\":\"" + cfg.clientId + "\",";
  j += "\"device_name\":\"" + jsonEsc(cfg.deviceName) + "\",\"ap_name\":\"" + jsonEsc(deviceId()) + "\",";
  j += "\"topic\":\"" + cfg.topic + "\",\"discovery\":" + (cfg.discovery ? 1 : 0) + ",";
  j += "\"freq\":\"" + String(cfg.freq, 2) + "\",\"freq_aok\":\"" + String(cfg.freqAok, 2) + "\",";
  j += "\"pins\":\"" + String(cfg.pSck) + "," + cfg.pMiso + "," + cfg.pMosi + "," + cfg.pCs + "," + cfg.pGdo0 + "\"}";
  server.send(200, "application/json", j);
}

void handleConfigPost() {
  if (server.arg("pass").length() || server.arg("ssid") != cfg.ssid) cfg.pass = server.arg("pass");
  cfg.ssid = server.arg("ssid");
  if (!server.arg("ssid2").length()) {
    cfg.ssid2 = "";
    cfg.pass2 = "";
  } else {
    if (server.arg("pass2").length() || server.arg("ssid2") != cfg.ssid2) cfg.pass2 = server.arg("pass2");
    cfg.ssid2 = server.arg("ssid2");
  }
  cfg.dhcp = server.hasArg("dhcp");
  cfg.ip = clean(server.arg("ip"));
  cfg.mask = clean(server.arg("mask"));
  cfg.gw = clean(server.arg("gw"));
  String host = clean(server.arg("mqtt_host"));
  if (!host.length()) host = "0.0.0.0";
  uint16_t port = server.arg("mqtt_port").toInt() ? server.arg("mqtt_port").toInt() : 1883;
  if (server.arg("mqtt_pass").length()) cfg.mqttPass = server.arg("mqtt_pass");
  else if (host != cfg.mqttHost || port != cfg.mqttPort) cfg.mqttPass = "";
  cfg.mqttHost = host;
  cfg.mqttPort = port;
  cfg.mqttUser = clean(server.arg("mqtt_user"));
  String cid = clean(server.arg("client_id"));
  if (cid.length()) cfg.clientId = cid;
  cfg.deviceName = cleanDeviceName(server.arg("device_name"));
  String tp = clean(server.arg("topic"));
  tp.replace("/", "");
  cfg.topic = tp.length() ? tp : defaultTopic();
  bool discoveryWas = cfg.discovery;
  cfg.discovery = server.hasArg("discovery");
  if (discoveryWas && !cfg.discovery && mqtt.connected()) clearDiscovery();
  float f = server.arg("freq").toFloat();
  if (f > 300 && f < 928) cfg.freq = f;
  f = server.arg("freq_aok").toFloat();
  if (f > 300 && f < 928) cfg.freqAok = f;
  int a[5];
  if (sscanf(server.arg("pins").c_str(), "%d,%d,%d,%d,%d", &a[0], &a[1], &a[2], &a[3], &a[4]) == 5) {
    bool ok = true;
    for (int i = 0; i < 5; i++) {
      int p = a[i];
      bool inputOnlyAllowed = (i == 1);
      if (p < 0 || p > 39 || (p >= 6 && p <= 11) || p == 0 || p == 2 || (!inputOnlyAllowed && p >= 34 && p <= 39)) ok = false;
    }
    if (ok) {
      cfg.pSck = a[0]; cfg.pMiso = a[1]; cfg.pMosi = a[2]; cfg.pCs = a[3]; cfg.pGdo0 = a[4];
    } else {
      logf("config: rejected invalid radio pins \"%s\"", server.arg("pins").c_str());
    }
  }
  saveConfig();
  server.send(200, "text/plain", "OK");
  logf("config saved, rebooting");
  rebootAt = millis() + 1500;
}

void handleShuttersGet() {
  String out;
  out.reserve(MAX_CH * 110);
  out += '[';
  char buf[200];
  for (int i = 0; i < MAX_CH; i++) {
    snprintf(buf, sizeof(buf), "%s{\"ch\":%d,\"en\":%d,\"name\":\"%s\",\"proto\":%d,\"pair\":%d,\"id\":\"%06X\",\"code\":%u,\"state\":%d}", i ? "," : "", i, sh[i].en ? 1 : 0,
             sh[i].name.c_str(), sh[i].proto, sh[i].pair, (unsigned)sh[i].id, sh[i].code, sh[i].state);
    out += buf;
  }
  out += ']';
  server.send(200, "application/json", out);
}

void handleShutterSave() {
  int ch = server.arg("ch").toInt();
  if (ch < 0 || ch >= MAX_CH) return server.send(400, "text/plain", "bad channel");
  if (seq.kind != SQ_NONE && seq.ch == ch) return server.send(409, "text/plain", "busy");
  sh[ch].en = server.arg("en") == "1";
  sh[ch].name = clean(server.arg("name"));
  sh[ch].id = strtoul(server.arg("id").c_str(), nullptr, 16) & 0xFFFFFF;
  sh[ch].proto = server.arg("proto") == "1" ? 1 : 0;
  sh[ch].pair = server.arg("pair") == "1" ? 1 : 0;
  sh[ch].code = (uint16_t)strtoul(server.arg("code").c_str(), nullptr, 10);
  saveShutter(ch, true);
  logf("ch%d saved: %s id=%06X code=%u en=%d", ch, sh[ch].proto ? "A-OK" : "Somfy", (unsigned)sh[ch].id, sh[ch].code, sh[ch].en);
  if (mqtt.connected()) publishDiscovery(ch);
  server.send(200, "text/plain", "OK");
}

void handleShutterCmd() {
  int ch = server.arg("ch").toInt();
  Cmd c = parseCmd(server.arg("cmd"));
  if (ch < 0 || ch >= MAX_CH || c == C_NONE) return server.send(400, "text/plain", "bad request");
  if (c == C_LEARN && sh[ch].proto == 0) {
    if (!radioOk) return server.send(400, "text/plain", "CC1101 not found");
    if (seq.kind == SQ_LEARN && seq.ch == ch) {
      seq.sendNow = true;
      return server.send(200, "text/plain", "OK");
    }
    if (!startLearn(ch)) return server.send(409, "text/plain", "busy");
    return server.send(200, "text/plain", "OK");
  }
  if (!enqueue(ch, c)) return server.send(503, "text/plain", "busy, try again");
  server.send(200, "text/plain", "OK");
}

void handleUnpair() {
  int ch = server.arg("ch").toInt();
  if (ch < 0 || ch >= MAX_CH) return server.send(400, "text/plain", "bad channel");
  if (!radioOk) return server.send(400, "text/plain", "CC1101 not found");
  if (!startUnpair(ch)) return server.send(409, "text/plain", "busy");
  server.send(200, "text/plain", "OK");
}

void handleDelete() {
  int ch = server.arg("ch").toInt();
  if (ch < 0 || ch >= MAX_CH) return server.send(400, "text/plain", "bad channel");
  if (seq.kind != SQ_NONE && seq.ch == ch) return server.send(409, "text/plain", "busy");
  wipeChannel(ch);
  server.send(200, "text/plain", "OK");
}

void handleSeq() {
  String j = "{\"active\":" + String(seq.kind != SQ_NONE ? 1 : 0);
  if (seq.kind != SQ_NONE) {
    uint32_t total = seqTotalMs(), el = millis() - seq.start;
    j += ",\"kind\":\"" + String(seq.kind == SQ_LEARN ? "learn" : "unpair") + "\",\"ch\":" + seq.ch + ",\"left\":" + String(el >= total ? 0 : (total - el + 999) / 1000) +
         ",\"heard\":" + (seq.heard ? 1 : 0);
  }
  server.send(200, "application/json", j + "}");
}

void handleRadio() {
  server.send(200, "application/json", String("{\"ok\":") + (radioOk ? 1 : 0) + ",\"ver\":" + radioVer + "}");
}

void radioCheck() {
  if (seq.kind != SQ_NONE || millis() - lastRadioCheck < 3000) return;
  lastRadioCheck = millis();
  uint8_t v = Radio::chipVersion();
  if (v && !radioOk) {
    radioOk = Radio::begin(cfg.pSck, cfg.pMiso, cfg.pMosi, cfg.pCs, cfg.pGdo0, cfg.freq, cfg.freqAok);
    if (radioOk) logf("CC1101 connected (version 0x%02X)", v);
    else logf("CC1101 answered (version 0x%02X) but begin() failed", v);
  } else if (!v && radioOk) {
    radioOk = false;
    logf("CC1101 lost");
  }
  radioVer = radioOk ? v : 0;
}

String macHex() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  return mac;
}

void handleInfo() {
  String j = "{\"model\":\"ESP32+CC1101\",\"fw\":\"" FW_VERSION "\",\"mac\":\"" + macHex() + "\",\"name\":\"" + jsonEsc(deviceId()) + "\",\"pw_set\":" + (cfg.pwSet ? 1 : 0) + "}";
  server.send(200, "application/json", j);
}

void handleState() {
  String out;
  out.reserve(512);
  char buf[120];
  snprintf(buf, sizeof(buf), "{\"radio\":%d,\"ch\":[", radioOk ? 1 : 0);
  out += buf;
  bool first = true;
  for (int i = 0; i < MAX_CH; i++) {
    if (!sh[i].en) continue;
    snprintf(buf, sizeof(buf), "%s{\"ch\":%d,\"name\":\"%s\",\"proto\":%d,\"state\":%d}", first ? "" : ",", i, sh[i].name.c_str(), sh[i].proto, sh[i].state);
    out += buf;
    first = false;
  }
  out += "]}";
  server.send(200, "application/json", out);
}

void handleHaLink() {
  if (cfg.discovery) {
    cfg.discovery = false;
    saveConfig();
    if (mqtt.connected()) clearDiscovery();
    logf("Home Assistant integration linked: MQTT discovery turned off");
  }
  server.send(200, "text/plain", "OK");
}

void handleLog() {
  uint32_t n = min<uint32_t>(logCount, LOG_LINES);
  String out;
  out.reserve(n * 80);
  for (uint32_t i = logCount - n; i < logCount; i++) {
    out += logBuf[i % LOG_LINES];
    out += '\n';
  }
  server.send(200, "text/plain", out);
}

void setupWeb() {
  static const char *hdrs[] = {"Cookie", "Authorization"};
  server.collectHeaders(hdrs, 2);

  openRoute("/login.html", HTTP_GET, []() {
    if (!cfg.pwSet) return redirect("/setpw.html");
    sendPage(PAGE_LOGIN);
  });
  openRoute("/api/login", HTTP_POST, handleLogin);
  openRoute("/api/auth/challenge", HTTP_GET, handleChallenge);
  openRoute("/api/auth/token", HTTP_POST, handleToken);
  route("/setpw.html", HTTP_GET, []() { sendPage(PAGE_SETPW); });
  route("/api/setpw", HTTP_POST, handleSetPw);

  route("/", HTTP_GET, []() { sendPage(PAGE_HOME); });
  route("/index.html", HTTP_GET, []() { sendPage(PAGE_HOME); });
  route("/system.html", HTTP_GET, []() { sendPage(PAGE_SYSTEM); });
  route("/shutter.html", HTTP_GET, []() { sendPage(PAGE_SHUTTER); });
  route("/log.html", HTTP_GET, []() { sendPage(PAGE_LOG); });
  route("/api/config", HTTP_GET, handleConfigGet);
  route("/api/config", HTTP_POST, handleConfigPost);
  route("/api/scan/start", HTTP_POST, handleScanStart);
  route("/api/scan", HTTP_GET, handleScanGet);
  route("/api/shutters", HTTP_GET, handleShuttersGet);
  route("/api/shutter/save", HTTP_POST, handleShutterSave);
  apiRoute("/api/shutter/cmd", HTTP_POST, handleShutterCmd);
  route("/api/shutter/unpair", HTTP_POST, handleUnpair);
  route("/api/shutter/delete", HTTP_POST, handleDelete);
  route("/api/seq", HTTP_GET, handleSeq);
  route("/api/log", HTTP_GET, handleLog);
  route("/api/radio", HTTP_GET, handleRadio);
  openRoute("/api/info", HTTP_GET, handleInfo);
  apiRoute("/api/state", HTTP_GET, handleState);
  apiRoute("/api/ha/link", HTTP_POST, handleHaLink);
  server.on(
      "/update", HTTP_POST,
      []() {
        bool ok = otaByTicket || authed();
        otaByTicket = false;
        if (!hostOk() || !ok) return server.send(401, "text/plain", "login required");
        server.sendHeader("Connection", "close");
        bool written = !otaFailed && !Update.hasError();
        server.send(200, "text/plain", written ? "OK, rebooting" : "FAIL");
        if (written) rebootAt = millis() + 1000;
      },
      []() {
        HTTPUpload &u = server.upload();
        if (u.status == UPLOAD_FILE_START) otaByTicket = hostOk() && ticketOk();
        if (!hostOk() || !(otaByTicket || authed())) return;
        if (u.status == UPLOAD_FILE_START) {
          otaFailed = false;
          logf("OTA start: %s", u.filename.c_str());
          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            logf("OTA begin failed: %s", Update.errorString());
            otaFailed = true;
          }
        } else if (u.status == UPLOAD_FILE_WRITE) {
          if (!otaFailed && Update.write(u.buf, u.currentSize) != u.currentSize) {
            logf("OTA write failed: %s", Update.errorString());
            Update.abort();
            otaFailed = true;
          }
        } else if (u.status == UPLOAD_FILE_END) {
          if (!otaFailed && Update.end(true)) {
            logf("OTA done, %u bytes", (unsigned)u.totalSize);
          } else {
            logf("OTA failed: %s", Update.errorString());
            Update.abort();
            otaFailed = true;
          }
        } else if (u.status == UPLOAD_FILE_ABORTED) {
          otaByTicket = false;
          logf("OTA aborted");
          Update.abort();
          otaFailed = true;
        }
      });
  server.onNotFound([]() {
    if (viaAp()) {
      redirect("http://192.168.4.1/");
    } else {
      server.send(404, "text/plain", "not found");
    }
  });
  server.begin();
  server.noDelay();
}

void startAp(uint32_t until) {
  if (apOn) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  String apName = deviceId();
  WiFi.softAP(apName.c_str(), cfg.apPass.c_str());
  dns.start(53, "*", WiFi.softAPIP());
  apOn = true;
  apUntil = until;
  apStarted = millis();
  apGraceAt = 0;
  digitalWrite(LED_PIN, HIGH);
  logf("access point \"%s\" on at %s%s", apName.c_str(), WiFi.softAPIP().toString().c_str(), until ? " (3 min)" : "");
}

void stopAp() {
  if (!apOn) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  apOn = false;
  digitalWrite(LED_PIN, LOW);
  logf("access point off");
}

void beginJoin(int pick) {
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  if (pick == 1 && !cfg.dhcp) {
    IPAddress ip, mask, gw;
    if (ip.fromString(cfg.ip) && mask.fromString(cfg.mask) && gw.fromString(cfg.gw)) WiFi.config(ip, gw, mask, gw);
  }
  const String &ss = pick == 1 ? cfg.ssid : cfg.ssid2;
  const String &pw = pick == 1 ? cfg.pass : cfg.pass2;
  WiFi.begin(ss.c_str(), pw.c_str());
  logf("connecting to \"%s\"", ss.c_str());
}

bool wifiPicking = false;
uint32_t wifiPickStart = 0;

void connectBest() {
  bool h1 = cfg.ssid.length(), h2 = cfg.ssid2.length();
  if (!h1 && !h2) return;
  if (h1 && h2) {
    if (WiFi.softAPgetStationNum() == 0) {
      WiFi.scanDelete();
      WiFi.scanNetworks(true, false, false, 300);
      wifiPicking = true;
      wifiPickStart = millis();
      return;
    }
    altToggle = !altToggle;
    beginJoin(altToggle ? 2 : 1);
    return;
  }
  beginJoin(h1 ? 1 : 2);
}

void connectBestPoll() {
  if (!wifiPicking) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    if (millis() - wifiPickStart > 6000) {
      WiFi.scanDelete();
      wifiPicking = false;
      altToggle = !altToggle;
      beginJoin(altToggle ? 2 : 1);
    }
    return;
  }
  wifiPicking = false;
  int r1 = -999, r2 = -999;
  for (int i = 0; i < n; i++) {
    String ss = WiFi.SSID(i);
    int r = WiFi.RSSI(i);
    if (ss == cfg.ssid && r > r1) r1 = r;
    if (ss == cfg.ssid2 && r > r2) r2 = r;
  }
  WiFi.scanDelete();
  int pick;
  if (r1 == -999 && r2 == -999) {
    altToggle = !altToggle;
    pick = altToggle ? 2 : 1;
  } else {
    pick = r2 > r1 ? 2 : 1;
  }
  logf("signal: \"%s\" %d dBm, \"%s\" %d dBm", cfg.ssid.c_str(), r1, cfg.ssid2.c_str(), r2);
  beginJoin(pick);
}

void setupWifi() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(BOOT_PIN, INPUT_PULLUP);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(deviceId().c_str());
  if (!cfg.ssid.length() && !cfg.ssid2.length()) {
    startAp(0);
  } else {
    connectBest();
    lastAttempt = millis();
  }
}

void wifiLoop() {
  connectBestPoll();
  bool conn = WiFi.status() == WL_CONNECTED;
  if (conn && !staUp) {
    staUp = true;
    lostAp = false;
    bootPhase = false;
    logf("WiFi up: %s, IP %s, %d dBm", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
    MDNS.end();
    MDNS.begin(deviceId().c_str());
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("shuttersdongle", "tcp", 80);
    MDNS.addServiceTxt("shuttersdongle", "tcp", "mac", macHex());
    if (cfg.pwSet) {
      MDNS.addService("arduino", "tcp", 3232);
      MDNS.addServiceTxt("arduino", "tcp", "auth_upload", "yes");
    }
  } else if (!conn && staUp) {
    staUp = false;
    downSince = millis();
    lastAttempt = millis() - 15000;
    logf("WiFi lost");
  }

  if (conn) {
    if (!apOn) return;
    if (WiFi.softAPgetStationNum() == 0) {
      stopAp();
    } else if (!apGraceAt) {
      apGraceAt = millis();
    } else if (millis() - apGraceAt >= AP_GRACE_MS) {
      logf("WiFi is back, closing the access point");
      stopAp();
    }
    return;
  }
  apGraceAt = 0;

  if ((cfg.ssid.length() || cfg.ssid2.length()) && millis() - lastAttempt > 15000) {
    lastAttempt = millis();
    connectBest();
  }

  bool configured = cfg.ssid.length() || cfg.ssid2.length();
  if (!apOn) {
    if (bootPhase && !lostAp && millis() > 20000) {
      lostAp = configured;
      startAp(0);
    } else if (!bootPhase && !lostAp && millis() - downSince > AP_LOST_AFTER_MS) {
      lostAp = true;
      startAp(millis() + AP_WINDOW_MS);
    }
  } else if ((apUntil && (int32_t)(millis() - apUntil) >= 0 && WiFi.softAPgetStationNum() == 0) || (configured && millis() - apStarted >= AP_MAX_MS)) {
    stopAp();
  }
}

void otaLoop() {
  static bool started = false;
  if (!started) {
    if (!cfg.pwSet || (!staUp && !apOn)) return;
    ArduinoOTA.setHostname(deviceId().c_str());
    ArduinoOTA.setPassword(cfg.apPass.c_str());
    ArduinoOTA.setMdnsEnabled(false);
    ArduinoOTA.onStart([]() { logf("OTA from PlatformIO: receiving firmware"); });
    ArduinoOTA.onEnd([]() { logf("OTA from PlatformIO: done, rebooting"); });
    ArduinoOTA.onError([](ota_error_t e) { logf("OTA from PlatformIO failed (error %u)", (unsigned)e); });
    ArduinoOTA.begin();
    started = true;
    logf("OTA from PlatformIO enabled on port 3232");
  }
  ArduinoOTA.handle();
}

void serviceButton() {
  static uint32_t holdStart = 0;
  if (digitalRead(BOOT_PIN) == LOW) {
    if (!holdStart) holdStart = millis();
    if (millis() - holdStart < 10000) return;
    logf("BOOT held: resetting Wi-Fi and password");
    prefs.begin("cfg", false);
    prefs.remove("ssid");
    prefs.remove("pass");
    prefs.remove("ssid2");
    prefs.remove("pass2");
    prefs.remove("apass");
    prefs.remove("pwset");
    prefs.remove("akey");
    prefs.remove("p_sck");
    prefs.remove("p_miso");
    prefs.remove("p_mosi");
    prefs.remove("p_cs");
    prefs.remove("p_gdo0");
    prefs.end();
    while (digitalRead(BOOT_PIN) == LOW) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(100);
    }
    ESP.restart();
  } else {
    holdStart = 0;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  loadConfig();
  loadShutters();
  radioOk = Radio::begin(cfg.pSck, cfg.pMiso, cfg.pMosi, cfg.pCs, cfg.pGdo0, cfg.freq, cfg.freqAok);
  radioVer = radioOk ? Radio::chipVersion() : 0;
  logf("Shutters dongle %s, CC1101 %s @ %.2f MHz", FW_VERSION, radioOk ? "OK" : "NOT FOUND", cfg.freq);
  setupWifi();
  ensureAuthKey();
  mqtt.setBufferSize(1024);
  mqtt.setKeepAlive(30);
  mqtt.setCallback(onMqtt);
  setupWeb();
}

void loop() {
  if (apOn) dns.processNextRequest();
  server.dropIdleClient();
  server.handleClient();
  mqttLoop();
  seqLoop();
  while (seq.kind == SQ_NONE && qTail != qHead) {
    Job j = queue[qTail];
    qTail = (qTail + 1) % QUEUE_LEN;
    execute(j.ch, j.cmd);
  }
  if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) ESP.restart();
  radioCheck();
  wifiLoop();
  otaLoop();
  serviceButton();
}
