/*
  Wemos D1 mini (ESP8266)
  - AP (Setup) + STA (Normal)
  - Simpan SSID/PASS AP & STA + API URL ke EEPROM
  - HTTP/HTTPS GET + POST JSON
  - Tampilkan JSON dan hasil parse "name/title"
  - Scan Wi-Fi sekitar & daftar klien DHCP (AP)
  - Tanpa OLED
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

extern "C" {
  #include <user_interface.h>
}

// ---------- CONFIG ----------
struct Config {
  uint32_t magic;
  char ap_ssid[32];
  char ap_pass[32];
  char sta_ssid[32];
  char sta_pass[32];
  char apiGet[128];
  char apiPost[128];
};
Config config;
const uint32_t CFG_MAGIC = 0x43464732;

void saveConfig() { EEPROM.put(0, config); EEPROM.commit(); }
void loadConfig() {
  EEPROM.get(0, config);
  if (config.magic != CFG_MAGIC) {
    memset(&config, 0, sizeof(config));
    config.magic = CFG_MAGIC;
    strcpy(config.ap_ssid, "SetupD1Mini");
    strcpy(config.ap_pass, "12345678");
    strcpy(config.apiGet,  "https://jsonplaceholder.typicode.com/users/1");
    saveConfig();
  }
}
bool hasSTA() { return strlen(config.sta_ssid) > 0; }

// ---------- STATE ----------
String lastJsonRaw, lastParsedName;
int lastHttpCode = 0;
unsigned long lastFetchMs = 0;

// ---------- SERVER ----------
ESP8266WebServer server(80);

String htmlHeader() {
  return F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<style>body{font-family:Arial;padding:16px;max-width:900px;margin:auto}"
           "input,button{width:100%;padding:8px;margin:6px 0}"
           "code,pre{background:#f5f5f5;padding:8px;overflow:auto;display:block}"
           "table{border-collapse:collapse;width:100%}th,td{border:1px solid #ddd;padding:6px;text-align:left}"
           ".row{display:grid;grid-template-columns:1fr 1fr;gap:16px}@media(max-width:720px){.row{grid-template-columns:1fr}}"
           "a.btn{display:inline-block;padding:6px 12px;border:1px solid #999;border-radius:6px;text-decoration:none}</style>"
           "</head><body>");
}
String htmlFooter() { return F("</body></html>"); }

String htmlStationTable() {
  String out; out += F("<h3>Klien Terhubung (AP)</h3>");
  struct station_info *si = wifi_softap_get_station_info();
  if (!si) return out + "<p>Tidak ada klien terhubung.</p>";
  out += F("<table><tr><th>#</th><th>IP</th><th>MAC</th></tr>");
  int i = 0;
  for (; si; si = STAILQ_NEXT(si, next)) {
    i++;
    IPAddress ip((uint32_t)si->ip.addr);
    char mac[18];
    sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X",
            si->bssid[0], si->bssid[1], si->bssid[2],
            si->bssid[3], si->bssid[4], si->bssid[5]);
    out += "<tr><td>" + String(i) + "</td><td>" + ip.toString() + "</td><td>" + mac + "</td></tr>";
  }
  out += F("</table>");
  wifi_softap_free_station_info();
  return out;
}

String htmlRoot() {
  String h = htmlHeader();
  h += F("<h2>Setup Wemos D1 mini</h2><form action='/save' method='GET'>");
  h += "<h3>Access Point (AP)</h3>";
  h += "AP SSID:<input name='apssid' value='" + String(config.ap_ssid) + "'>";
  h += "AP Password:<input name='appass' type='password' value='" + String(config.ap_pass) + "'>";

  h += "<h3>Wi-Fi Client (STA)</h3>";
  h += "STA SSID:<input name='stassid' value='" + String(config.sta_ssid) + "'>";
  h += "STA Password:<input name='stapass' type='password' value='" + String(config.sta_pass) + "'>";

  h += "<h3>API Configuration</h3>";
  h += "API GET:<input name='get' value='" + String(config.apiGet) + "'>";
  h += "API POST:<input name='post' value='" + String(config.apiPost) + "'>";

  h += "<input type='submit' value='Save & Restart'></form>";

  h += "<div class='row'><div><h3>Status Jaringan</h3>";
  h += "<p><b>STA IP:</b> " + WiFi.localIP().toString() + "<br>";
  h += "<b>AP IP:</b> " + WiFi.softAPIP().toString() + "<br>";
  h += "<b>AP SSID:</b> " + String(config.ap_ssid) + "</p>";
  h += "<a class='btn' href='/fetch'>Fetch API</a> ";
  h += "<a class='btn' href='/scan'>Scan Wi-Fi</a></div>";

  h += "<div><h3>JSON & Parse</h3>";
  h += "<p><b>Code:</b> " + String(lastHttpCode) + "<br>";
  h += "<b>Name:</b> " + (lastParsedName.length() ? lastParsedName : "-") + "</p>";
  if (lastJsonRaw.length()) {
    String s = lastJsonRaw.length() > 4000 ? lastJsonRaw.substring(0, 4000) + "\n...(truncated)" : lastJsonRaw;
    s.replace("<", "&lt;");
    h += "<pre><code>" + s + "</code></pre>";
  }
  h += "</div></div>";

  h += htmlStationTable();
  h += htmlFooter();
  return h;
}

void handleRoot() { server.send(200, "text/html", htmlRoot()); }

void handleSave() {
  if (server.hasArg("apssid")) server.arg("apssid").toCharArray(config.ap_ssid, sizeof(config.ap_ssid));
  if (server.hasArg("appass")) server.arg("appass").toCharArray(config.ap_pass, sizeof(config.ap_pass));
  if (server.hasArg("stassid")) server.arg("stassid").toCharArray(config.sta_ssid, sizeof(config.sta_ssid));
  if (server.hasArg("stapass")) server.arg("stapass").toCharArray(config.sta_pass, sizeof(config.sta_pass));
  if (server.hasArg("get")) server.arg("get").toCharArray(config.apiGet, sizeof(config.apiGet));
  if (server.hasArg("post")) server.arg("post").toCharArray(config.apiPost, sizeof(config.apiPost));
  config.magic = CFG_MAGIC; saveConfig();
  server.send(200, "text/html", "Saved! Restarting...");
  delay(700); ESP.restart();
}

// ---------- Scan Wi-Fi ----------
void handleScan() {
  int n = WiFi.scanNetworks();
  String h = htmlHeader();
  h += "<h2>Wi-Fi Sekitar (" + String(n) + ")</h2><a class='btn' href='/'>Kembali</a>";
  if (n > 0) {
    h += "<table><tr><th>#</th><th>SSID</th><th>RSSI</th><th>Enc</th><th>Ch</th></tr>";
    for (int i = 0; i < n; i++) {
      h += "<tr><td>" + String(i + 1) + "</td><td>" + WiFi.SSID(i) + "</td><td>" +
           String(WiFi.RSSI(i)) + "</td><td>" + String(WiFi.encryptionType(i)) +
           "</td><td>" + String(WiFi.channel(i)) + "</td></tr>";
    }
    h += "</table>";
  }
  h += htmlFooter();
  server.send(200, "text/html", h);
  WiFi.scanDelete();
}

// ---------- Fetch API ----------
void handleFetch() {
  if (strlen(config.apiGet) == 0) {
    server.sendHeader("Location", "/"); server.send(302);
    return;
  }

  HTTPClient http;
  String payload; int code = 0;
  bool ok = false;
  if (WiFi.status() == WL_CONNECTED) {
    if (String(config.apiGet).startsWith("https://")) {
      WiFiClientSecure cli; cli.setInsecure();
      if (http.begin(cli, config.apiGet)) { code = http.GET(); if (code > 0) { payload = http.getString(); ok = true; } http.end(); }
    } else {
      WiFiClient cli;
      if (http.begin(cli, config.apiGet)) { code = http.GET(); if (code > 0) { payload = http.getString(); ok = true; } http.end(); }
    }
  }
  lastHttpCode = code;
  lastJsonRaw = ok ? payload : "Error code=" + String(code);

  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (!err) {
    JsonVariant root = doc.as<JsonVariant>();
    String name = "-";
    if (root.is<JsonObject>()) {
      if (root["name"].is<const char*>()) name = root["name"].as<const char*>();
      else if (root["title"].is<const char*>()) name = root["title"].as<const char*>();
    }
    if (name == "-" && root.is<JsonArray>()) {
      JsonArray a = root.as<JsonArray>();
      if (a.size() > 0 && a[0]["name"].is<const char*>()) name = a[0]["name"].as<const char*>();
    }
    lastParsedName = name;
  } else lastParsedName = String("Parse error: ") + err.c_str();

  server.sendHeader("Location", "/"); server.send(302);
}

// ---------- Wi-Fi setup ----------
void startAP_STA() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(config.ap_ssid, config.ap_pass);
  delay(150);

  if (hasSTA()) {
    WiFi.begin(config.sta_ssid, config.sta_pass);
    unsigned long t0 = millis();
    Serial.printf("[STA] Connecting to %s\n", config.sta_ssid);
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) { delay(300); Serial.print("."); }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED)
      Serial.printf("[STA] Connected, IP: %s\n", WiFi.localIP().toString().c_str());
    else
      Serial.println("[STA] Failed. Stay in AP mode.");
  } else Serial.println("[STA] No creds, AP only.");

  Serial.printf("[AP] SSID:%s PASS:%s IP:%s\n",
                config.ap_ssid, config.ap_pass, WiFi.softAPIP().toString().c_str());

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/scan", handleScan);
  server.on("/fetch", handleFetch);
  server.begin();
  Serial.println("HTTP server: http://192.168.4.1/");
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(1024);
  loadConfig();
  startAP_STA();
}

void loop() {
  server.handleClient();
}
