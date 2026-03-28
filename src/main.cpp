/**
 * ESP32 RadioMaster Controller
 * 
 * Подключение MAX3421E USB Host:
 * --------------------------------
 * MAX3421E    ESP32
 * ------      -----
 * VBUS        5V
 * GND         GND
 * SS/CS       GPIO 5
 * INT         GPIO 4
 * RST         GPIO 15
 * MOSI        GPIO 23 (VSPI)
 * MISO        GPIO 19 (VSPI)
 * SCK         GPIO 18 (VSPI)
 * 
 * Подключение PWM выходов:
 * ------------------------
 * CH1 (Aileron)  - GPIO 25
 * CH2 (Elevator) - GPIO 26
 * CH3 (Throttle) - GPIO 27
 * CH4 (Rudder)   - GPIO 14
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <SPI.h>

// ==================== НАСТРОЙКИ ====================
// Пины для MAX3421E USB Host
#define MAX3421_SELECT 5   // SS/CS
#define MAX3421_INT      4   // INT
#define MAX3421_RESET    15  // RST

// Пины для PWM выходов (4 канала)
#define CHANNEL_1_PIN  25
#define CHANNEL_2_PIN  26
#define CHANNEL_3_PIN  27
#define CHANNEL_4_PIN  14

// LED индикатор
#define LED_PIN 2

// ==================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ====================
WebServer server(80);

// Структура для хранения настроек Wi-Fi
struct WiFiCredentials {
  char ssid[33];
  char password[65];
};

WiFiCredentials wifiCreds;
bool wifiConnected = false;
bool isAccessPointMode = true;

// Значения каналов (0-1023)
volatile int channelValues[4] = {512, 512, 512, 512};
volatile bool newDataReceived = false;
volatile bool usbConnected = false;

// ==================== HTML СТРАНИЦЫ ====================
const char HTML_HEADER[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 RadioMaster Controller</title>
  <style>
    body { font-family: Arial, sans-serif; max-width: 800px; margin: 0 auto; padding: 20px; background: #1a1a2e; color: #eee; }
    h1 { color: #00d9ff; text-align: center; }
    .card { background: #16213e; border-radius: 10px; padding: 20px; margin: 15px 0; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
    .form-group { margin: 15px 0; }
    label { display: block; margin-bottom: 5px; color: #00d9ff; }
    input[type="text"], input[type="password"] { width: 100%%; padding: 10px; border: none; border-radius: 5px; background: #0f3460; color: #fff; box-sizing: border-box; }
    button { background: #00d9ff; color: #1a1a2e; border: none; padding: 12px 25px; border-radius: 5px; cursor: pointer; font-size: 16px; width: 100%%; margin-top: 10px; }
    button:hover { background: #00b8d4; }
    .status { padding: 10px; border-radius: 5px; margin: 10px 0; }
    .status.connected { background: #00c853; color: #000; }
    .status.disconnected { background: #ff5252; color: #fff; }
    .channel-display { display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; margin-top: 15px; }
    .channel { background: #0f3460; padding: 15px; border-radius: 5px; text-align: center; }
    .channel h3 { margin: 0 0 10px 0; color: #00d9ff; }
    .channel-value { font-size: 24px; color: #fff; }
    .progress-bar { width: 100%%; height: 20px; background: #1a1a2e; border-radius: 10px; overflow: hidden; margin-top: 10px; }
    .progress-fill { height: 100%%; background: linear-gradient(90deg, #00d9ff, #00ff88); transition: width 0.1s; }
    .usb-status { display: inline-block; width: 12px; height: 12px; border-radius: 50%%; margin-right: 8px; }
    .usb-on { background: #00ff88; box-shadow: 0 0 10px #00ff88; }
    .usb-off { background: #ff5252; }
  </style>
</head>
<body>
  <h1>🎮 ESP32 RadioMaster</h1>
)rawliteral";

const char HTML_FOOTER[] PROGMEM = R"rawliteral(
  <div class="card" style="text-align: center; color: #888;">
    <p>ESP32 WROOM + MAX3421E Controller &copy; 2026</p>
  </div>
</body>
</html>
)rawliteral";

const char HTML_SETTINGS_PAGE[] PROGMEM = R"rawliteral(
  <div class="card">
    <h2>📡 Настройки Wi-Fi</h2>
    <div class="status %s">%s</div>
    <form method="POST" action="/save">
      <div class="form-group">
        <label for="ssid">SSID сети:</label>
        <input type="text" id="ssid" name="ssid" value="%s" required>
      </div>
      <div class="form-group">
        <label for="password">Пароль:</label>
        <input type="password" id="password" name="password" value="%s">
      </div>
      <button type="submit">💾 Сохранить и подключиться</button>
    </form>
    <form method="GET" action="/scan" style="margin-top: 15px;">
      <button type="submit" style="background: #0f3460;">🔍 Сканировать сети</button>
    </form>
  </div>
)rawliteral";

const char HTML_CHANNELS_PAGE[] PROGMEM = R"rawliteral(
  <div class="card">
    <h2>📊 Каналы управления</h2>
    <div class="status connected">
      <span class="usb-status %s"></span>%s<br>
      Wi-Fi: %s | IP: %s
    </div>
    <div class="channel-display">
      <div class="channel">
        <h3>CH1 (Aileron)</h3>
        <div class="channel-value" id="ch1-val">%d</div>
        <div class="progress-bar"><div class="progress-fill" style="width: %d%%;"></div></div>
      </div>
      <div class="channel">
        <h3>CH2 (Elevator)</h3>
        <div class="channel-value" id="ch2-val">%d</div>
        <div class="progress-bar"><div class="progress-fill" style="width: %d%%;"></div></div>
      </div>
      <div class="channel">
        <h3>CH3 (Throttle)</h3>
        <div class="channel-value" id="ch3-val">%d</div>
        <div class="progress-bar"><div class="progress-fill" style="width: %d%%;"></div></div>
      </div>
      <div class="channel">
        <h3>CH4 (Rudder)</h3>
        <div class="channel-value" id="ch4-val">%d</div>
        <div class="progress-bar"><div class="progress-fill" style="width: %d%%;"></div></div>
      </div>
    </div>
    <p style="font-size: 12px; color: #888; margin-top: 10px;">PWM: 50 Hz | Resolution: 10 bit | Range: 0-1023</p>
  </div>
  <div class="card">
    <h2>🔄 Управление</h2>
    <form method="GET" action="/settings">
      <button type="submit" style="background: #0f3460;">⚙️ Настройки Wi-Fi</button>
    </form>
    <form method="POST" action="/calibrate" style="margin-top: 15px;">
      <button type="submit" style="background: #ff9800;">🎯 Калибровка (центр)</button>
    </form>
  </div>
  <script>
    setTimeout(function(){ location.reload(); }, 3000);
  </script>
)rawliteral";

const char HTML_SCAN_PAGE[] PROGMEM = R"rawliteral(
  <div class="card">
    <h2>🔍 Найденные сети</h2>
    <table style="width: 100%%; border-collapse: collapse;">
      <tr style="background: #0f3460;">
        <th style="padding: 10px; text-align: left;">SSID</th>
        <th style="padding: 10px;">RSSI</th>
        <th style="padding: 10px;">Защита</th>
      </tr>
      %s
    </table>
    <form method="GET" action="/" style="margin-top: 15px;">
      <button type="submit" style="background: #0f3460;">⬅️ Назад</button>
    </form>
  </div>
)rawliteral";

// ==================== ФУНКЦИИ ====================

void loadWiFiCredentials() {
  EEPROM.begin(512);
  EEPROM.get(0, wifiCreds);
  EEPROM.end();
  
  if (wifiCreds.ssid[0] == '\0' || wifiCreds.ssid[0] == 0xFF) {
    strcpy(wifiCreds.ssid, "");
    strcpy(wifiCreds.password, "");
  }
}

void saveWiFiCredentials() {
  EEPROM.begin(512);
  EEPROM.put(0, wifiCreds);
  EEPROM.commit();
  EEPROM.end();
}

bool connectToWiFi(const char* ssid, const char* password) {
  Serial.print("Подключение к ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  for (int i = 0; i < 15; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Подключено! IP: ");
      Serial.println(WiFi.localIP());
      return true;
    }
    delay(1000);
    Serial.print(".");
  }
  
  Serial.println("\nНе удалось подключиться");
  return false;
}

void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-RadioMaster", "12345678");
  
  Serial.print("Точка доступа: ESP32-RadioMaster, пароль: 12345678, IP: ");
  Serial.println(WiFi.softAPIP());
  
  isAccessPointMode = true;
}

// ==================== MAX3421E USB HOST ====================
// Регистры MAX3421E
#define rRCVFIFO    0x08
#define rSNDFIFO    0x10
#define rUSBIRQ     0x14
#define rUSBIE      0x15
#define rHIRQ       0x16
#define rHIEN       0x17
#define rMODE       0x1A
#define rPERADDR    0x22
#define rHCTL       0x29
#define rHXFR       0x2A
#define rHRSL       0x2B

// Команды SPI
#define MAX3421_RD  (0<<7)
#define MAX3421_WR  (1<<7)

// Значения регистров
#define bmHIRQ_BUSEVENT     0x01
#define bmHIRQ_CONDET       0x02
#define bmHIRQ_XFRM         0x04

#define bmHIEN_BUSEVENT     0x01
#define bmHIEN_CONDET       0x02
#define bmHIEN_XFRM         0x04

#define bmMODE_HOST         0x01
#define bmMODE_LOWSPEED     0x02

#define bmHCTL_BUSRST       0x01
#define bmHCTL_FRMRST       0x02

#define hxfrSETUP           0x00
#define hxfrIN              0x10
#define hxfrOUT             0x20
#define hxfrIN0             0x00
#define hxfrOUT0            0x00

#define hrSLV               0x00
#define hrNAK               0x10
#define hrSTALL             0x20
#define hrTOGERR            0x30
#define hrCRCERR            0x40
#define hrKERR              0x50
#define hrJERR              0x60
#define hrERR               0x70

SPIClass* spi = nullptr;

uint8_t max3421_read(uint8_t reg) {
  digitalWrite(MAX3421_SELECT, LOW);
  spi->transfer(reg | MAX3421_RD);
  uint8_t data = spi->transfer(0);
  digitalWrite(MAX3421_SELECT, HIGH);
  return data;
}

void max3421_write(uint8_t reg, uint8_t data) {
  digitalWrite(MAX3421_SELECT, LOW);
  spi->transfer(reg | MAX3421_WR);
  spi->transfer(data);
  digitalWrite(MAX3421_SELECT, HIGH);
}

void max3421_init() {
  Serial.println("Инициализация MAX3421E...");
  
  // Сброс MAX3421E
  pinMode(MAX3421_RESET, OUTPUT);
  pinMode(MAX3421_SELECT, OUTPUT);
  pinMode(MAX3421_INT, INPUT);
  
  digitalWrite(MAX3421_RESET, LOW);
  delay(100);
  digitalWrite(MAX3421_RESET, HIGH);
  delay(100);
  
  // Инициализация SPI
  spi = new SPIClass(VSPI);
  spi->begin(18, 19, 23, MAX3421_SELECT);  // SCK, MISO, MOSI, SS
  
  // Проверка связи
  uint8_t revision = max3421_read(0x0D);  // rREVISION
  Serial.print("MAX3421E Revision: 0x");
  Serial.println(revision, HEX);
  
  if (revision != 0x12 && revision != 0x13) {
    Serial.println("Ошибка: MAX3421E не найден!");
    usbConnected = false;
    return;
  }
  
  // Настройка в режим хоста
  max3421_write(rMODE, bmMODE_HOST);
  max3421_write(rHIEN, bmHIEN_CONDET | bmHIEN_XFRM);
  
  // Сброс шины USB
  max3421_write(rHCTL, bmHCTL_BUSRST);
  delay(100);
  
  usbConnected = true;
  Serial.println("MAX3421E инициализирован успешно");
}

// Чтение данных из endpoint
int max3421_read_endpoint(uint8_t ep, uint8_t* buf, uint8_t len) {
  if (!usbConnected) return 0;
  
  // Установка адреса устройства (предполагаем устройство 1)
  max3421_write(rPERADDR, 0x01);
  
  // Запуск IN транзакции
  max3421_write(rHXFR, (ep & 0x07) | hxfrIN);
  
  // Ожидание завершения
  int timeout = 100;
  while (!(max3421_read(rHIRQ) & bmHIEN_XFRM) && timeout-- > 0) {
    delay(1);
  }
  
  // Чтение статуса
  uint8_t hrsl = max3421_read(rHRSL);
  
  if ((hrsl & 0x0F) == hrSLV) {
    // Успешная транзакция
    uint8_t bytes = max3421_read(rRCVFIFO);
    if (bytes > len) bytes = len;
    
    for (uint8_t i = 0; i < bytes; i++) {
      buf[i] = max3421_read(rRCVFIFO);
    }
    
    // Очистка флага
    max3421_write(rHIRQ, bmHIEN_XFRM);
    
    return bytes;
  }
  
  return 0;
}

void updateUSB() {
  if (!usbConnected) return;
  
  // Проверка прерывания
  if (digitalRead(MAX3421_INT) == LOW) {
    uint8_t hirq = max3421_read(rHIRQ);
    
    // Обнаружение подключения/отключения
    if (hirq & bmHIEN_CONDET) {
      Serial.println("Изменение подключения USB");
      max3421_write(rHIRQ, bmHIEN_CONDET);
      
      // Попытка enumeration
      max3421_write(rPERADDR, 0x01);
    }
    
    // Чтение данных от устройства
    if (hirq & bmHIEN_XFRM) {
      uint8_t buf[16];
      int len = max3421_read_endpoint(1, buf, sizeof(buf));
      
      if (len >= 4) {
        // Обработка данных HID
        // Формат зависит от отчёта устройства
        
        // Предполагаем 4 канала по 1 байту (0-255 -> 0-1020)
        channelValues[0] = buf[0] << 2;
        channelValues[1] = buf[1] << 2;
        channelValues[2] = buf[2] << 2;
        channelValues[3] = buf[3] << 2;
        
        // Ограничение диапазона
        for (int i = 0; i < 4; i++) {
          channelValues[i] = constrain(channelValues[i], 0, 1023);
        }
        
        newDataReceived = true;
      }
      
      max3421_write(rHIRQ, bmHIEN_XFRM);
    }
  }
}

// ==================== Обработчики веб-сервера ====================

void handleRoot() {
  String html = String(HTML_HEADER);
  
  if (isAccessPointMode || !wifiConnected) {
    const char* statusClass = isAccessPointMode ? "connected" : "disconnected";
    const char* statusText = isAccessPointMode ? "Режим настройки (AP)" : "Не подключено к Wi-Fi";
    
    char buf[1024];
    sprintf(buf, HTML_SETTINGS_PAGE, 
            statusClass, statusText,
            wifiCreds.ssid, wifiCreds.password);
    html += String(buf);
  } else {
    const char* usbStatusClass = usbConnected ? "usb-on" : "usb-off";
    const char* usbStatusText = usbConnected ? "USB готов" : "USB не подключено";
    const char* wifiStatusText = wifiConnected ? "Подключено" : "Отключено";
    
    char buf[2048];
    sprintf(buf, HTML_CHANNELS_PAGE,
            usbStatusClass, usbStatusText,
            wifiStatusText, WiFi.localIP().toString().c_str(),
            channelValues[0], map(channelValues[0], 0, 1023, 0, 100),
            channelValues[1], map(channelValues[1], 0, 1023, 0, 100),
            channelValues[2], map(channelValues[2], 0, 1023, 0, 100),
            channelValues[3], map(channelValues[3], 0, 1023, 0, 100));
    
    html += String(buf);
  }
  
  html += String(HTML_FOOTER);
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("ssid")) {
    strncpy(wifiCreds.ssid, server.arg("ssid").c_str(), 32);
    wifiCreds.ssid[32] = '\0';
  }
  if (server.hasArg("password")) {
    strncpy(wifiCreds.password, server.arg("password").c_str(), 64);
    wifiCreds.password[64] = '\0';
  }
  
  saveWiFiCredentials();
  
  if (strlen(wifiCreds.ssid) > 0) {
    if (connectToWiFi(wifiCreds.ssid, wifiCreds.password)) {
      wifiConnected = true;
      isAccessPointMode = false;
    }
  }
  
  server.sendHeader("Location", "/");
  server.sendHeader("Cache-Control", "no-cache");
  server.send(303);
}

void handleScan() {
  int n = WiFi.scanNetworks();
  
  String rows = "";
  for (int i = 0; i < n; i++) {
    String encryption = "";
    switch (WiFi.encryptionType(i)) {
      case WIFI_AUTH_OPEN: encryption = "Открытая"; break;
      case WIFI_AUTH_WEP: encryption = "WEP"; break;
      case WIFI_AUTH_WPA_PSK: encryption = "WPA"; break;
      case WIFI_AUTH_WPA2_PSK: encryption = "WPA2"; break;
      case WIFI_AUTH_WPA_WPA2_PSK: encryption = "WPA/WPA2"; break;
      default: encryption = "Unknown";
    }
    
    rows += "<tr><td style='padding: 8px;'>" + WiFi.SSID(i) + "</td>";
    rows += "<td style='padding: 8px; text-align: center;'>" + String(WiFi.RSSI(i)) + "</td>";
    rows += "<td style='padding: 8px; text-align: center;'>" + encryption + "</td></tr>";
  }
  
  if (n == 0) rows = "<tr><td colspan='3' style='padding: 20px; text-align: center;'>Сети не найдены</td></tr>";
  
  char buf[4096];
  sprintf(buf, HTML_SCAN_PAGE, rows.c_str());
  
  String html = String(HTML_HEADER) + String(buf) + String(HTML_FOOTER);
  server.send(200, "text/html", html);
}

void handleSettings() {
  server.sendHeader("Location", "/");
  server.sendHeader("Cache-Control", "no-cache");
  server.send(303);
}

void handleCalibrate() {
  for (int i = 0; i < 4; i++) {
    channelValues[i] = 512;
  }
  server.sendHeader("Location", "/");
  server.sendHeader("Cache-Control", "no-cache");
  server.send(303);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not Found");
}

// ==================== PWM (LEDC) ====================

void setupPWM() {
  ledcSetup(0, 50, 10);
  ledcSetup(1, 50, 10);
  ledcSetup(2, 50, 10);
  ledcSetup(3, 50, 10);
  
  ledcAttachPin(CHANNEL_1_PIN, 0);
  ledcAttachPin(CHANNEL_2_PIN, 1);
  ledcAttachPin(CHANNEL_3_PIN, 2);
  ledcAttachPin(CHANNEL_4_PIN, 3);
}

void updatePWM() {
  for (int i = 0; i < 4; i++) {
    int pwmValue = map(channelValues[i], 0, 1023, 51, 256);
    pwmValue = constrain(pwmValue, 0, 1023);
    ledcWrite(i, pwmValue);
  }
}

// ==================== SETUP & LOOP ====================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== ESP32 RadioMaster Controller ===");
  Serial.println("MAX3421E USB Host + 4x PWM выходы");
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  
  // Загрузка настроек Wi-Fi
  loadWiFiCredentials();
  
  // Попытка подключения к сохранённой сети
  if (strlen(wifiCreds.ssid) > 0) {
    Serial.println("Подключение к сохранённой сети...");
    if (connectToWiFi(wifiCreds.ssid, wifiCreds.password)) {
      wifiConnected = true;
      isAccessPointMode = false;
    }
  }
  
  if (!wifiConnected) {
    Serial.println("Запуск точки доступа...");
    startAccessPoint();
  }
  
  // Настройка PWM
  setupPWM();
  
  // Настройка USB Host
  max3421_init();
  
  // Настройка веб-сервера
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/scan", handleScan);
  server.on("/settings", handleSettings);
  server.on("/calibrate", handleCalibrate);
  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println("Веб-сервер запущен");
  
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  server.handleClient();
  
  // Обновление USB
  updateUSB();
  
  // Обновление PWM
  updatePWM();
  
  // Индикация
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 500) {
    lastBlink = millis();
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }
  
  delay(10);
}
