#include "esp_camera.h"
#include "esp_http_server.h"
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

// ====== Configura qui la tua rete Wi-Fi ======
const char *WIFI_SSID = "NOME_WIFI";
const char *WIFI_PASSWORD = "PASSWORD_WIFI";

// Nome visibile in rete: http://esp32cam.local/ se il router supporta mDNS.
const char *HOSTNAME = "esp32cam";

// Credenziali per la pagina di aggiornamento firmware.
const char *UPDATE_USER = "admin";
const char *UPDATE_PASSWORD = "admin";

// ====== Pin ESP32-CAM AI Thinker ======
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#define FLASH_LED_PIN 4

// GPIO disponibili se non usi la microSD.
#define RELAY1_PIN 13
#define RELAY2_PIN 14
#define RELAY_ACTIVE_HIGH true

// Ingresso batteria: usa un partitore resistivo, mai collegare la batteria diretta al pin.
// Esempio Li-ion 1S: 100k tra BAT+ e GPIO33, 100k tra GPIO33 e GND -> fattore 2.0.
#define BATTERY_ADC_PIN 33
const float BATTERY_DIVIDER_RATIO = 2.0;
const float BATTERY_EMPTY_V = 3.20;
const float BATTERY_FULL_V = 4.20;

WebServer server(80);
httpd_handle_t streamServer = NULL;
Preferences prefs;

uint32_t snapshotIntervalMs = 5000;
bool flashEnabled = false;
bool relay1Enabled = false;
bool relay2Enabled = false;

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="it">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-CAM</title>
  <style>
    :root { color-scheme: light dark; font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; }
    body { margin: 0; background: #101418; color: #eef3f8; }
    main { max-width: 1100px; margin: 0 auto; padding: 20px; }
    header { display: flex; align-items: center; justify-content: space-between; gap: 16px; flex-wrap: wrap; }
    h1 { margin: 0; font-size: 1.55rem; }
    a { color: #8ed1ff; }
    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 16px; margin-top: 18px; }
    section { background: #18212b; border: 1px solid #2c3a47; border-radius: 8px; padding: 14px; }
    img { width: 100%; height: auto; display: block; border-radius: 6px; background: #080b0e; }
    .controls { display: flex; align-items: end; gap: 10px; flex-wrap: wrap; margin-top: 12px; }
    label { display: grid; gap: 5px; font-size: .9rem; color: #c9d6e2; }
    input { min-height: 38px; border-radius: 6px; border: 1px solid #435365; padding: 0 10px; background: #101820; color: #eef3f8; }
    button { min-height: 40px; border: 0; border-radius: 6px; padding: 0 14px; background: #2f9e44; color: white; font-weight: 700; cursor: pointer; }
    button.secondary { background: #3164c8; }
    .toggle { display: flex; align-items: center; justify-content: space-between; gap: 12px; min-width: 160px; padding: 10px 12px; border: 1px solid #304252; border-radius: 6px; background: #111a22; }
    .toggle input { min-height: auto; width: 22px; height: 22px; }
    .meter { margin-top: 14px; height: 16px; border-radius: 999px; overflow: hidden; background: #0b1117; border: 1px solid #304252; }
    .meter span { display: block; height: 100%; width: 0%; background: #2f9e44; transition: width .2s ease; }
    .readout { display: grid; gap: 4px; margin-top: 10px; color: #c9d6e2; }
    .status { color: #b6c4d2; font-size: .92rem; margin-top: 10px; min-height: 1.2em; }
    @media (max-width: 800px) { .grid { grid-template-columns: 1fr; } }
  </style>
</head>
<body>
<main>
  <header>
    <h1>ESP32-CAM</h1>
    <nav><a href="/update">Aggiorna firmware</a></nav>
  </header>

  <div class="grid">
    <section>
      <h2>Streaming live</h2>
      <img id="stream" alt="Streaming live">
      <div class="controls">
        <button class="secondary" onclick="restartStream()">Riavvia stream</button>
      </div>
    </section>

    <section>
      <h2>Foto periodica</h2>
      <img id="snapshot" src="/capture.jpg" alt="Ultima foto">
      <div class="controls">
        <label>Intervallo foto (secondi)
          <input id="interval" type="number" min="1" max="3600" step="1">
        </label>
        <button onclick="saveInterval()">Salva</button>
        <button class="secondary" onclick="captureNow()">Scatta ora</button>
      </div>
      <div class="controls">
        <label>
          Flash LED
          <input id="flash" type="checkbox" onchange="setFlash(this.checked)">
        </label>
      </div>
      <p class="status" id="status"></p>
    </section>

    <section>
      <h2>Uscite</h2>
      <div class="controls">
        <label class="toggle">Relay 1 / GPIO13
          <input id="relay1" type="checkbox" onchange="setRelay(1, this.checked)">
        </label>
        <label class="toggle">Relay 2 / GPIO14
          <input id="relay2" type="checkbox" onchange="setRelay(2, this.checked)">
        </label>
      </div>
      <p class="status" id="ioStatus"></p>
    </section>

    <section>
      <h2>Carica</h2>
      <div class="meter"><span id="batteryBar"></span></div>
      <div class="readout">
        <span id="batteryVoltage">-- V</span>
        <span id="batteryPercent">-- %</span>
      </div>
      <div class="controls">
        <button class="secondary" onclick="refreshStatus()">Aggiorna</button>
      </div>
    </section>
  </div>
</main>
<script>
let timer = null;
let statusTimer = null;

async function loadStatus() {
  const res = await fetch('/status');
  const data = await res.json();
  updateUi(data);
  schedule(data.intervalSeconds);
  setStatus('Connesso a ' + data.ip);
}

function updateUi(data) {
  document.getElementById('interval').value = data.intervalSeconds;
  document.getElementById('flash').checked = data.flash;
  document.getElementById('relay1').checked = data.relay1;
  document.getElementById('relay2').checked = data.relay2;
  document.getElementById('batteryBar').style.width = data.batteryPercent + '%';
  document.getElementById('batteryVoltage').textContent = data.batteryVoltage.toFixed(2) + ' V';
  document.getElementById('batteryPercent').textContent = data.batteryPercent + ' % stimato';
}

async function refreshStatus() {
  const res = await fetch('/status');
  const data = await res.json();
  updateUi(data);
}

function setStatus(text) {
  document.getElementById('status').textContent = text;
}

function schedule(seconds) {
  if (timer) clearInterval(timer);
  timer = setInterval(captureNow, seconds * 1000);
}

function captureNow() {
  document.getElementById('snapshot').src = '/capture.jpg?t=' + Date.now();
}

function restartStream() {
  const stream = document.getElementById('stream');
  stream.src = '';
  setTimeout(() => stream.src = location.protocol + '//' + location.hostname + ':81/stream?t=' + Date.now(), 300);
}

async function saveInterval() {
  const seconds = Math.max(1, Number(document.getElementById('interval').value || 1));
  const res = await fetch('/set?interval=' + encodeURIComponent(seconds));
  const data = await res.json();
  schedule(data.intervalSeconds);
  setStatus('Intervallo salvato: ' + data.intervalSeconds + ' s');
}

async function setFlash(enabled) {
  const res = await fetch('/set?flash=' + (enabled ? '1' : '0'));
  const data = await res.json();
  updateUi(data);
  setStatus('Flash ' + (data.flash ? 'attivo' : 'spento'));
}

async function setRelay(index, enabled) {
  const res = await fetch('/set?relay' + index + '=' + (enabled ? '1' : '0'));
  const data = await res.json();
  updateUi(data);
  document.getElementById('ioStatus').textContent = 'Relay ' + index + ' ' + (enabled ? 'attivo' : 'spento');
}

loadStatus().catch(() => setStatus('Errore nel caricamento stato'));
restartStream();
statusTimer = setInterval(refreshStatus, 10000);
</script>
</body>
</html>
)rawliteral";

static const char UPDATE_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="it">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Aggiorna ESP32-CAM</title>
  <style>
    body { font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; max-width: 620px; margin: 40px auto; padding: 0 18px; }
    form { display: grid; gap: 14px; }
    button { min-height: 42px; border: 0; border-radius: 6px; background: #3164c8; color: white; font-weight: 700; }
  </style>
</head>
<body>
  <h1>Aggiorna firmware</h1>
  <p>Carica il file <strong>.bin</strong> compilato dall'IDE Arduino o da PlatformIO.</p>
  <form method="POST" action="/update" enctype="multipart/form-data">
    <input type="file" name="firmware" accept=".bin" required>
    <button type="submit">Carica e riavvia</button>
  </form>
  <p><a href="/">Torna alla camera</a></p>
</body>
</html>
)rawliteral";

void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Errore camera: 0x%x\n", err);
    delay(5000);
    ESP.restart();
  }

  sensor_t *sensor = esp_camera_sensor_get();
  sensor->set_vflip(sensor, 1);
  sensor->set_brightness(sensor, 0);
  sensor->set_saturation(sensor, 0);
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connessione Wi-Fi");
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi non connesso, riavvio.");
    delay(3000);
    ESP.restart();
  }

  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void setupOTA() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(UPDATE_PASSWORD);
  ArduinoOTA
    .onStart([]() {
      Serial.println("OTA avviato");
    })
    .onEnd([]() {
      Serial.println("\nOTA completato");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("OTA: %u%%\r", (progress * 100) / total);
    })
    .onError([](ota_error_t error) {
      Serial.printf("Errore OTA[%u]\n", error);
    });
  ArduinoOTA.begin();
}

bool requireUpdateAuth() {
  if (server.authenticate(UPDATE_USER, UPDATE_PASSWORD)) {
    return true;
  }
  server.requestAuthentication();
  return false;
}

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void writeRelay(uint8_t pin, bool enabled) {
  digitalWrite(pin, enabled == RELAY_ACTIVE_HIGH ? HIGH : LOW);
}

float readBatteryVoltage() {
  uint32_t totalMv = 0;
  const uint8_t samples = 16;

  for (uint8_t i = 0; i < samples; i++) {
    totalMv += analogReadMilliVolts(BATTERY_ADC_PIN);
    delay(2);
  }

  float pinVoltage = (totalMv / (float)samples) / 1000.0;
  return pinVoltage * BATTERY_DIVIDER_RATIO;
}

uint8_t batteryPercent(float voltage) {
  float percent = (voltage - BATTERY_EMPTY_V) * 100.0 / (BATTERY_FULL_V - BATTERY_EMPTY_V);
  return constrain((int)round(percent), 0, 100);
}

void handleStatus() {
  float batteryVoltage = readBatteryVoltage();
  String json = "{";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"intervalSeconds\":" + String(snapshotIntervalMs / 1000) + ",";
  json += "\"flash\":" + String(flashEnabled ? "true" : "false") + ",";
  json += "\"relay1\":" + String(relay1Enabled ? "true" : "false") + ",";
  json += "\"relay2\":" + String(relay2Enabled ? "true" : "false") + ",";
  json += "\"batteryVoltage\":" + String(batteryVoltage, 2) + ",";
  json += "\"batteryPercent\":" + String(batteryPercent(batteryVoltage));
  json += "}";
  server.send(200, "application/json", json);
}

void handleSet() {
  if (server.hasArg("interval")) {
    uint32_t seconds = constrain(server.arg("interval").toInt(), 1, 3600);
    snapshotIntervalMs = seconds * 1000UL;
    prefs.putUInt("interval", snapshotIntervalMs);
  }

  if (server.hasArg("flash")) {
    flashEnabled = server.arg("flash") == "1";
    prefs.putBool("flash", flashEnabled);
    digitalWrite(FLASH_LED_PIN, flashEnabled ? HIGH : LOW);
  }

  if (server.hasArg("relay1")) {
    relay1Enabled = server.arg("relay1") == "1";
    prefs.putBool("relay1", relay1Enabled);
    writeRelay(RELAY1_PIN, relay1Enabled);
  }

  if (server.hasArg("relay2")) {
    relay2Enabled = server.arg("relay2") == "1";
    prefs.putBool("relay2", relay2Enabled);
    writeRelay(RELAY2_PIN, relay2Enabled);
  }

  handleStatus();
}

void handleCapture() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Errore acquisizione immagine");
    return;
  }

  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

esp_err_t streamHandler(httpd_req_t *req) {
  esp_err_t res = httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
  if (res != ESP_OK) {
    return res;
  }

  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "Connection", "close");

  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Frame non disponibile");
      return ESP_FAIL;
    }

    char header[96];
    size_t headerLen = snprintf(header, sizeof(header),
                                "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                                (unsigned int)fb->len);

    res = httpd_resp_send_chunk(req, header, headerLen);
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, "\r\n", 2);
    }

    esp_camera_fb_return(fb);

    if (res != ESP_OK) {
      break;
    }
    delay(45);
  }

  return res;
}

void handleUpdatePage() {
  if (!requireUpdateAuth()) {
    return;
  }
  server.send_P(200, "text/html", UPDATE_HTML);
}

void handleUpdateUpload() {
  if (!requireUpdateAuth()) {
    return;
  }

  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("Update: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("Update completato: %u byte\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  }
}

void handleUpdateDone() {
  if (Update.hasError()) {
    server.send(500, "text/plain", "Aggiornamento fallito");
    return;
  }
  server.send(200, "text/plain", "Aggiornamento completato. Riavvio...");
  delay(800);
  ESP.restart();
}

void setupServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/set", HTTP_GET, handleSet);
  server.on("/capture.jpg", HTTP_GET, handleCapture);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Non trovato");
  });
  server.begin();
}

void setupStreamServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 81;
  config.ctrl_port = 32769;

  httpd_uri_t streamUri = {};
  streamUri.uri = "/stream";
  streamUri.method = HTTP_GET;
  streamUri.handler = streamHandler;
  streamUri.user_ctx = NULL;

  if (httpd_start(&streamServer, &config) == ESP_OK) {
    if (httpd_register_uri_handler(streamServer, &streamUri) == ESP_OK) {
      Serial.println("Stream pronto su porta 81");
    } else {
      Serial.println("Errore registrazione stream");
    }
  } else {
    Serial.println("Errore avvio stream server");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  writeRelay(RELAY1_PIN, false);
  writeRelay(RELAY2_PIN, false);
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);

  prefs.begin("esp32cam", false);
  snapshotIntervalMs = prefs.getUInt("interval", 5000);
  flashEnabled = prefs.getBool("flash", false);
  relay1Enabled = prefs.getBool("relay1", false);
  relay2Enabled = prefs.getBool("relay2", false);
  digitalWrite(FLASH_LED_PIN, flashEnabled ? HIGH : LOW);
  writeRelay(RELAY1_PIN, relay1Enabled);
  writeRelay(RELAY2_PIN, relay2Enabled);

  setupCamera();
  connectWiFi();
  setupOTA();
  setupServer();
  setupStreamServer();

  Serial.println("Server pronto");
  Serial.print("Apri: http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
}
