/**
 * ESP32 RC Controller - Web Joystick Version
 * 
 * Управление через веб-интерфейс (виртуальные джойстики в браузере)
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

// ==================== НАСТРОЙКИ ====================

// Пины для PWM выходов (4 канала)
#define CHANNEL_1_PIN  25
#define CHANNEL_2_PIN  26
#define CHANNEL_3_PIN  27
#define CHANNEL_4_PIN  14

// LED индикатор
#define LED_PIN 2

// ==================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ====================

struct WiFiSettings {
  char ssid[33];
  char password[65];
};

WiFiSettings settings;
WebServer server(80);

// Значения каналов (0-1023)
volatile int channelValues[4] = {512, 512, 512, 512};
volatile unsigned long lastUpdateTime = 0;

bool wifiConnected = false;
bool isAccessPointMode = true;

// ==================== ВЕБ ИНТЕРФЕЙС ====================

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
  <title>ESP32 RC Controller</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; touch-action: none; user-select: none; }
    body { 
      font-family: 'Segoe UI', Arial, sans-serif; 
      background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%); 
      color: #eee; 
      min-height: 100vh;
      padding: 10px;
    }
    h1 { 
      text-align: center; 
      color: #00d9ff; 
      margin-bottom: 10px;
      font-size: 24px;
    }
    .status-bar {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 10px 15px;
      background: rgba(0,0,0,0.3);
      border-radius: 10px;
      margin-bottom: 15px;
      font-size: 13px;
    }
    .status-indicator {
      display: flex;
      align-items: center;
      gap: 8px;
    }
    .led {
      width: 10px;
      height: 10px;
      border-radius: 50%;
      background: #ff5252;
      animation: pulse 2s infinite;
    }
    .led.on { background: #00ff88; box-shadow: 0 0 10px #00ff88; }
    @keyframes pulse { 0%,100% { opacity: 1; } 50% { opacity: 0.5; } }
    
    .main-container {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 15px;
      margin-bottom: 15px;
    }
    
    .joystick-area {
      position: relative;
      width: 100%;
      padding-bottom: 100%;
      background: rgba(15,52,96,0.5);
      border-radius: 50%;
      border: 3px solid #00d9ff;
      overflow: hidden;
    }
    .joystick-center {
      position: absolute;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      width: 20%;
      height: 20%;
      background: rgba(0,217,255,0.3);
      border-radius: 50%;
    }
    .joystick-knob {
      position: absolute;
      width: 25%;
      height: 25%;
      background: linear-gradient(135deg, #00d9ff, #00ff88);
      border-radius: 50%;
      transform: translate(-50%, -50%);
      top: 50%;
      left: 50%;
      box-shadow: 0 4px 15px rgba(0,217,255,0.5);
      transition: transform 0.05s;
    }
    
    .sliders-container {
      display: flex;
      flex-direction: column;
      gap: 10px;
    }
    
    .slider-box {
      flex: 1;
      background: rgba(15,52,96,0.5);
      border-radius: 10px;
      padding: 10px;
      display: flex;
      align-items: center;
      gap: 10px;
    }
    .slider-label {
      width: 70px;
      font-size: 12px;
      font-weight: 600;
      color: #00d9ff;
      text-align: center;
    }
    .slider-wrapper {
      flex: 1;
      position: relative;
    }
    input[type="range"] {
      width: 100%;
      height: 30px;
      -webkit-appearance: none;
      background: #1a1a2e;
      border-radius: 15px;
      outline: none;
    }
    input[type="range"]::-webkit-slider-thumb {
      -webkit-appearance: none;
      width: 30px;
      height: 30px;
      background: linear-gradient(135deg, #00d9ff, #00ff88);
      border-radius: 50%;
      cursor: pointer;
      box-shadow: 0 2px 10px rgba(0,217,255,0.5);
    }
    .slider-value {
      width: 45px;
      text-align: right;
      font-size: 14px;
      font-weight: bold;
      color: #fff;
    }
    
    .channels-display {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 10px;
      margin-bottom: 15px;
    }
    .channel-box {
      background: rgba(15,52,96,0.5);
      border-radius: 10px;
      padding: 10px;
      text-align: center;
    }
    .channel-name {
      font-size: 11px;
      color: #888;
      margin-bottom: 5px;
    }
    .channel-value {
      font-size: 20px;
      font-weight: bold;
      color: #00d9ff;
    }
    .progress-bar {
      height: 8px;
      background: #1a1a2e;
      border-radius: 4px;
      margin-top: 8px;
      overflow: hidden;
    }
    .progress-fill {
      height: 100%;
      background: linear-gradient(90deg, #00d9ff, #00ff88);
      transition: width 0.1s;
    }
    
    .buttons-row {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 10px;
    }
    button {
      padding: 12px;
      border: none;
      border-radius: 8px;
      font-size: 13px;
      font-weight: 600;
      cursor: pointer;
      transition: transform 0.1s;
    }
    button:active { transform: scale(0.95); }
    .btn-center { background: linear-gradient(135deg, #ff9800, #ffb74d); color: #000; }
    .btn-zero { background: linear-gradient(135deg, #f44336, #ef5350); color: #fff; }
    .btn-settings { background: linear-gradient(135deg, #0f3460, #1a4a7a); color: #fff; }
    
    .settings-panel {
      display: none;
      background: rgba(15,52,96,0.8);
      border-radius: 10px;
      padding: 15px;
      margin-top: 15px;
    }
    .settings-panel.show { display: block; }
    .form-group {
      margin-bottom: 12px;
    }
    .form-group label {
      display: block;
      margin-bottom: 5px;
      color: #00d9ff;
      font-size: 13px;
    }
    .form-group input {
      width: 100%;
      padding: 10px;
      border: 2px solid #0f3460;
      border-radius: 8px;
      background: #0f3460;
      color: #fff;
      font-size: 14px;
    }
    .btn-save {
      background: linear-gradient(135deg, #00d9ff, #00b8d4);
      color: #000;
      width: 100%;
      padding: 12px;
    }
    
    @media (max-width: 600px) {
      .main-container { grid-template-columns: 1fr; }
      .channels-display { grid-template-columns: repeat(2, 1fr); }
      .buttons-row { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <h1>🎮 ESP32 RC Controller</h1>
  
  <div class="status-bar">
    <div class="status-indicator">
      <div class="led" id="conn-led"></div>
      <span id="status-text">Ожидание...</span>
    </div>
    <span id="ip-display"></span>
  </div>
  
  <div class="main-container">
    <div class="joystick-area" id="stick1">
      <div class="joystick-center"></div>
      <div class="joystick-knob" id="knob1"></div>
    </div>
    <div class="joystick-area" id="stick2">
      <div class="joystick-center"></div>
      <div class="joystick-knob" id="knob2"></div>
    </div>
  </div>
  
  <div class="sliders-container">
    <div class="slider-box">
      <div class="slider-label">CH3<br>Throttle</div>
      <div class="slider-wrapper">
        <input type="range" id="slider3" min="0" max="1023" value="512">
      </div>
      <div class="slider-value" id="val3">512</div>
    </div>
    <div class="slider-box">
      <div class="slider-label">CH4<br>Rudder</div>
      <div class="slider-wrapper">
        <input type="range" id="slider4" min="0" max="1023" value="512">
      </div>
      <div class="slider-value" id="val4">512</div>
    </div>
  </div>
  
  <div class="channels-display">
    <div class="channel-box">
      <div class="channel-name">CH1</div>
      <div class="channel-value" id="ch1-val">512</div>
      <div class="progress-bar"><div class="progress-fill" id="ch1-bar" style="width:50%"></div></div>
    </div>
    <div class="channel-box">
      <div class="channel-name">CH2</div>
      <div class="channel-value" id="ch2-val">512</div>
      <div class="progress-bar"><div class="progress-fill" id="ch2-bar" style="width:50%"></div></div>
    </div>
    <div class="channel-box">
      <div class="channel-name">CH3</div>
      <div class="channel-value" id="ch3-val">512</div>
      <div class="progress-bar"><div class="progress-fill" id="ch3-bar" style="width:50%"></div></div>
    </div>
    <div class="channel-box">
      <div class="channel-name">CH4</div>
      <div class="channel-value" id="ch4-val">512</div>
      <div class="progress-bar"><div class="progress-fill" id="ch4-bar" style="width:50%"></div></div>
    </div>
  </div>
  
  <div class="buttons-row">
    <button class="btn-center" onclick="centerSticks()">🎯 Центр</button>
    <button class="btn-zero" onclick="zeroSticks()">⬆️ Минимум</button>
    <button class="btn-settings" onclick="toggleSettings()">⚙️ Настройки</button>
  </div>
  
  <div class="settings-panel" id="settings">
    <h3 style="color:#00d9ff; margin-bottom:15px;">Настройки Wi-Fi</h3>
    <div class="form-group">
      <label>SSID:</label>
      <input type="text" id="wifi-ssid" placeholder="Имя сети">
    </div>
    <div class="form-group">
      <label>Пароль:</label>
      <input type="password" id="wifi-pass" placeholder="Пароль">
    </div>
    <button class="btn-save" onclick="saveWiFi()">💾 Сохранить и перезагрузить</button>
  </div>
  
  <script>
    // Состояние каналов
    let channels = [512, 512, 512, 512];
    let stick1Pos = {x: 0, y: 0};
    let stick2Pos = {x: 0, y: 0};
    let gamepadIndex = -1;

    // Элементы
    const knob1 = document.getElementById('knob1');
    const knob2 = document.getElementById('knob2');
    const stick1Area = document.getElementById('stick1');
    const stick2Area = document.getElementById('stick2');
    const slider3 = document.getElementById('slider3');
    const slider4 = document.getElementById('slider4');
    const connLed = document.getElementById('conn-led');
    const statusText = document.getElementById('status-text');
    
    // Индикатор подключения геймпада
    const gpIndicator = document.createElement('div');
    gpIndicator.style.cssText = 'position:fixed;top:10px;right:10px;background:#00d9ff;color:#000;padding:8px 15px;border-radius:20px;font-size:12px;font-weight:600;z-index:1000;display:none;';
    gpIndicator.textContent = '🎮 Геймпад подключён';
    document.body.appendChild(gpIndicator);

    // Инициализация джойстиков (мышь/тач)
    function initJoystick(area, knob, callback) {
      let isDragging = false;
      const maxDist = 37.5;
      
      function handleMove(clientX, clientY) {
        const rect = area.getBoundingClientRect();
        const centerX = rect.left + rect.width / 2;
        const centerY = rect.top + rect.height / 2;
        
        let dx = clientX - centerX;
        let dy = clientY - centerY;
        const dist = Math.sqrt(dx*dx + dy*dy);
        
        if (dist > maxDist) {
          dx = (dx / dist) * maxDist;
          dy = (dy / dist) * maxDist;
        }
        
        knob.style.transform = `translate(calc(-50% + ${dx}px), calc(-50% + ${dy}px))`;
        callback(dx / maxDist, dy / maxDist);
      }
      
      function handleEnd() {
        isDragging = false;
        knob.style.transform = 'translate(-50%, -50%)';
        callback(0, 0);
      }
      
      area.addEventListener('mousedown', e => {
        isDragging = true;
        handleMove(e.clientX, e.clientY);
      });
      document.addEventListener('mousemove', e => {
        if (isDragging) handleMove(e.clientX, e.clientY);
      });
      document.addEventListener('mouseup', handleEnd);
      
      area.addEventListener('touchstart', e => {
        isDragging = true;
        handleMove(e.touches[0].clientX, e.touches[0].clientY);
        e.preventDefault();
      }, {passive: false});
      document.addEventListener('touchmove', e => {
        if (isDragging) handleMove(e.touches[0].clientX, e.touches[0].clientY);
      }, {passive: false});
      document.addEventListener('touchend', handleEnd);
    }
    
    // Обновление позиции джойстика
    function updateStickPosition(knob, x, y) {
      const maxDist = 37.5;
      const dx = x * maxDist;
      const dy = y * maxDist;
      knob.style.transform = `translate(calc(-50% + ${dx}px), calc(-50% + ${dy}px))`;
    }
    
    // Обработка геймпада
    function handleGamepad() {
      const gamepads = navigator.getGamepads();
      
      if (!gamepads[0]) {
        if (gamepadIndex >= 0) {
          gamepadIndex = -1;
          gpIndicator.style.display = 'none';
        }
        return;
      }
      
      const gp = gamepads[0];
      if (!gp.connected) {
        if (gamepadIndex >= 0) {
          gamepadIndex = -1;
          gpIndicator.style.display = 'none';
        }
        return;
      }
      
      // Подключён геймпад
      if (gamepadIndex < 0) {
        gamepadIndex = gp.index;
        gpIndicator.style.display = 'block';
        console.log('Геймпад подключён:', gp.id);
      }
      
      // Оси: 0,1 - левый стик, 2,3 - правый стик
      // Мёртвая зона
      const deadzone = 0.15;
      
      let ax1 = Math.abs(gp.axes[0]) < deadzone ? 0 : gp.axes[0];
      let ay1 = Math.abs(gp.axes[1]) < deadzone ? 0 : gp.axes[1];
      let ax2 = Math.abs(gp.axes[2]) < deadzone ? 0 : gp.axes[2];
      let ay2 = Math.abs(gp.axes[3]) < deadzone ? 0 : gp.axes[3];
      
      // Обновляем визуальные джойстики
      updateStickPosition(knob1, ax1, ay1);
      updateStickPosition(knob2, ax2, ay2);
      
      // Преобразуем в значения каналов (0-1023)
      channels[0] = Math.round((ax1 + 1) / 2 * 1023);  // CH1 - левый стик X
      channels[1] = Math.round((-ay1 + 1) / 2 * 1023); // CH2 - левый стик Y (инверсия)
      channels[2] = Math.round((-ay2 + 1) / 2 * 1023); // CH3 - правый стик Y (газ, инверсия)
      channels[3] = Math.round((ax2 + 1) / 2 * 1023);  // CH4 - правый стик X
      
      // Обновляем слайдеры
      slider3.value = channels[2];
      slider4.value = channels[3];
      document.getElementById('val3').textContent = channels[2];
      document.getElementById('val4').textContent = channels[3];
      
      updateDisplay();
      sendValues();
    }
    
    // Слушатели подключения геймпада
    window.addEventListener('gamepadconnected', e => {
      gamepadIndex = e.gamepad.index;
      gpIndicator.style.display = 'block';
      gpIndicator.textContent = '🎮 ' + e.gamepad.id;
      console.log('Геймпад подключён:', e.gamepad);
    });
    
    window.addEventListener('gamepaddisconnected', e => {
      if (gamepadIndex === e.gamepad.index) {
        gamepadIndex = -1;
        gpIndicator.style.display = 'none';
      }
      console.log('Геймпад отключён');
    });
    
    // Цикл опроса геймпада (60 FPS)
    setInterval(handleGamepad, 16);
    
    // Джойстик 1 (CH1, CH2)
    initJoystick(stick1Area, knob1, (x, y) => {
      stick1Pos = {x, y};
      channels[0] = Math.round((x + 1) / 2 * 1023);
      channels[1] = Math.round((-y + 1) / 2 * 1023);
      updateDisplay();
      sendValues();
    });
    
    // Джойстик 2 (CH3, CH4)
    initJoystick(stick2Area, knob2, (x, y) => {
      stick2Pos = {x, y};
      channels[2] = Math.round((-y + 1) / 2 * 1023);
      channels[3] = Math.round((x + 1) / 2 * 1023);
      updateDisplay();
      sendValues();
    });
    
    // Слайдеры
    slider3.addEventListener('input', e => {
      channels[2] = parseInt(e.target.value);
      document.getElementById('val3').textContent = channels[2];
      updateDisplay();
      sendValues();
    });
    
    slider4.addEventListener('input', e => {
      channels[3] = parseInt(e.target.value);
      document.getElementById('val4').textContent = channels[3];
      updateDisplay();
      sendValues();
    });
    
    // Обновление отображения
    function updateDisplay() {
      for (let i = 0; i < 4; i++) {
        document.getElementById(`ch${i+1}-val`).textContent = channels[i];
        document.getElementById(`ch${i+1}-bar`).style.width = (channels[i] / 1023 * 100) + '%';
      }
    }
    
    // Отправка на ESP32
    let lastSend = 0;
    function sendValues() {
      const now = Date.now();
      if (now - lastSend < 50) return; // Не чаще 20 раз в секунду
      lastSend = now;
      
      const data = channels.map(v => Math.round(v / 4)); // 0-255 для экономии
      fetch('/update', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ch: data})
      }).then(() => {
        connLed.classList.add('on');
        statusText.textContent = 'Подключено';
      }).catch(() => {
        connLed.classList.remove('on');
        statusText.textContent = 'Нет связи';
      });
    }
    
    // Кнопки
    function centerSticks() {
      channels = [512, 512, 512, 512];
      knob1.style.transform = 'translate(-50%, -50%)';
      knob2.style.transform = 'translate(-50%, -50%)';
      slider3.value = 512;
      slider4.value = 512;
      document.getElementById('val3').textContent = 512;
      document.getElementById('val4').textContent = 512;
      updateDisplay();
      sendValues();
    }
    
    function zeroSticks() {
      channels = [0, 0, 0, 0];
      updateDisplay();
      sendValues();
    }
    
    function toggleSettings() {
      document.getElementById('settings').classList.toggle('show');
    }
    
    function saveWiFi() {
      const ssid = document.getElementById('wifi-ssid').value;
      const pass = document.getElementById('wifi-pass').value;
      fetch('/wifi?ssid=' + encodeURIComponent(ssid) + '&pass=' + encodeURIComponent(pass))
        .then(() => alert('Настройки сохранены! Перезагрузка...'))
        .catch(() => alert('Ошибка сохранения'));
    }
    
    // Опрос статуса
    setInterval(() => {
      fetch('/status')
        .then(r => r.json())
        .then(d => {
          document.getElementById('ip-display').textContent = d.ip || '';
        })
        .catch(() => {});
    }, 2000);
    
    // Старт
    updateDisplay();
  </script>
</body>
</html>
)rawliteral";

// ==================== ФУНКЦИИ ====================

void loadSettings() {
  EEPROM.begin(512);
  EEPROM.get(0, settings);
  EEPROM.end();

  if (settings.ssid[0] == '\0' || settings.ssid[0] == 0xFF) {
    strcpy(settings.ssid, "");
    strcpy(settings.password, "");
  }
}

void saveSettings(const char* ssid, const char* password) {
  strncpy(settings.ssid, ssid, 32);
  settings.ssid[32] = '\0';
  strncpy(settings.password, password, 64);
  settings.password[64] = '\0';
  
  EEPROM.begin(512);
  EEPROM.put(0, settings);
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
    delay(500);
    Serial.print(".");
  }

  return false;
}

void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-RC-Controller", "");
  Serial.print("AP запущен: ESP32-RC-Controller, IP: ");
  Serial.println(WiFi.softAPIP());
  isAccessPointMode = true;
}

// ==================== PWM ====================

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

// ==================== ВЕБ СЕРВЕР ====================

void handleRoot() {
  server.send(200, "text/html", HTML_PAGE);
}

void handleUpdate() {
  if (server.hasArg("plain")) {
    String json = server.arg("plain");
    
    // Парсим JSON (простой формат: {"ch":[v1,v2,v3,v4]})
    int start = json.indexOf('[');
    int end = json.indexOf(']');
    if (start > 0 && end > start) {
      String values = json.substring(start + 1, end);
      int idx = 0;
      int pos = 0;
      while (idx < 4 && pos < values.length()) {
        int comma = values.indexOf(',', pos);
        if (comma < 0) comma = values.length();
        String val = values.substring(pos, comma);
        channelValues[idx] = val.toInt() * 4; // 0-255 -> 0-1020
        pos = comma + 1;
        idx++;
      }
      lastUpdateTime = millis();
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleStatus() {
  String json = "{\"ip\":\"";
  json += WiFi.localIP().toString();
  json += "\",\"ssid\":\"";
  json += WiFi.SSID();
  json += "\",\"rssi\":";
  json += WiFi.RSSI();
  json += "}";
  server.send(200, "application/json", json);
}

void handleWiFi() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    saveSettings(ssid.c_str(), pass.c_str());
    
    server.send(200, "text/plain", "OK");
    delay(1000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Missing args");
  }
}

void handleNotFound() {
  server.send(404, "text/plain", "Not Found");
}

// ==================== SETUP & LOOP ====================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== ESP32 RC Controller ===");
  Serial.println("Веб-интерфейс + PWM выходы");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  loadSettings();

  if (strlen(settings.ssid) > 0) {
    Serial.println("Подключение к сохранённой сети...");
    if (connectToWiFi(settings.ssid, settings.password)) {
      wifiConnected = true;
      isAccessPointMode = false;
    }
  }

  if (!wifiConnected) {
    Serial.println("Запуск точки доступа...");
    startAccessPoint();
  }

  setupPWM();

  server.on("/", handleRoot);
  server.on("/update", handleUpdate);
  server.on("/status", handleStatus);
  server.on("/wifi", handleWiFi);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("Веб-сервер запущен");

  digitalWrite(LED_PIN, LOW);
  
  Serial.println("\n=== Готов ===");
  Serial.print("Откройте в браузере: http://");
  if (isAccessPointMode) {
    Serial.println("192.168.4.1");
  } else {
    Serial.println(WiFi.localIP());
  }
}

void loop() {
  server.handleClient();
  updatePWM();

  // Индикация
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 500) {
    lastBlink = millis();
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }
  
  delay(10);
}
