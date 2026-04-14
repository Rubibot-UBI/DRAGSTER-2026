#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESP32Encoder.h>
#include <ArduinoOTA.h> // Biblioteca para o Upload sem fios

// ==========================================
// PÁGINA WEB COM COMANDOS + SLIDERS PID
// ==========================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { text-align:center; font-family: Arial; background-color: #222; color: white; padding: 20px;}
    .btn { padding: 20px 30px; font-size: 18px; margin: 5px; border-radius: 10px; cursor: pointer; border: none; font-weight: bold;}
    .btn-dir { background-color: #3498db; color: white; }
    .btn-stop { background-color: #e74c3c; color: white; }
    .slider-container { background: #333; padding: 15px; border-radius: 10px; margin: 15px 0; }
    input[type=range] { width: 80%; margin: 10px; }
    .val-display { font-weight: bold; color: #2ecc71; }
  </style>
</head>
<body>
  <h1>Controlo & Tuning PID</h1>
  
  <div style="margin-bottom: 30px;">
    <button class="btn btn-dir" onclick="send('frente')">▲</button><br>
    <button class="btn btn-dir" onclick="send('esquerda')">◀</button>
    <button class="btn btn-stop" onclick="send('parar')">■</button>
    <button class="btn btn-dir" onclick="send('direita')">▶</button><br>
    <button class="btn btn-dir" onclick="send('tras')">▼</button>
  </div>

  <div class="slider-container">
    <label>Kp: <span id="kp_val" class="val-display">0.40</span></label><br>
    <input type="range" min="0" max="2" step="0.01" value="0.40" onchange="updatePID('kp', this.value)">
  </div>
  <div class="slider-container">
    <label>Ki: <span id="ki_val" class="val-display">0.50</span></label><br>
    <input type="range" min="0" max="2" step="0.01" value="0.50" onchange="updatePID('ki', this.value)">
  </div>
  <div class="slider-container">
    <label>Kd: <span id="kd_val" class="val-display">0.03</span></label><br>
    <input type="range" min="0" max="0.5" step="0.005" value="0.03" onchange="updatePID('kd', this.value)">
  </div>

  <script>
    function send(cmd) { fetch('/' + cmd); }
    function updatePID(param, val) {
      document.getElementById(param + '_val').innerHTML = val;
      fetch('/update?param=' + param + '&value=' + val);
    }
  </script>
</body>
</html>
)rawliteral";

AsyncWebServer server(80);
ESP32Encoder enc1, enc2;

// Pinos (Conforme a tua última montagem física)
#define M1_ENCA 4  
#define M1_ENCB 5 
#define M2_ENCA 18 
#define M2_ENCB 19 
#define M1_PWM 26 
#define M1_DIR 27 
#define M2_PWM 33
#define M2_DIR 25

// Variáveis de controlo
float kp = 0.4, kd = 0.03, ki = 0.5;
long target1 = 0, target2 = 0;
long prevT = 0;
float eprev1 = 0, eintegral1 = 0;
float eprev2 = 0, eintegral2 = 0;

void setup() {
  Serial.begin(115200);

  // 1. WiFi e OTA
  WiFi.softAP("Robo_ESP32", "12345678");
  ArduinoOTA.setHostname("MeuRoboPID");
  ArduinoOTA.begin();

  // 2. Rotas do Servidor
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("param") && request->hasParam("value")) {
      String param = request->getParam("param")->value();
      float val = request->getParam("value")->value().toFloat();
      if(param == "kp") kp = val;
      if(param == "ki") ki = val;
      if(param == "kd") kd = val;
      Serial.printf("Novo PID: Kp=%.2f Ki=%.2f Kd=%.3f\n", kp, ki, kd);
    }
    request->send(200);
  });

  // (Rotas de direção iguais ao código anterior: /frente, /parar, etc.)
  server.on("/frente", HTTP_GET, [](AsyncWebServerRequest *request){ target1 += 1600; target2 += 1600; request->send(200); });
  server.on("/parar", HTTP_GET, [](AsyncWebServerRequest *request){ target1 = enc1.getCount(); target2 = enc2.getCount(); request->send(200); });

  server.begin();

  // 3. Hardware
  enc1.attachFullQuad(M1_ENCA, M1_ENCB);
  enc2.attachFullQuad(M2_ENCA, M2_ENCB);
  pinMode(M1_PWM, OUTPUT); pinMode(M1_DIR, OUTPUT);
  pinMode(M2_PWM, OUTPUT); pinMode(M2_DIR, OUTPUT);
}

void loop() {
  ArduinoOTA.handle(); // <--- OBRIGATÓRIO para o upload sem fios funcionar

  long currT = micros();
  float deltaT = ((float) (currT - prevT)) / 1.0e6;
  if (deltaT < 0.01) return;
  prevT = currT;

long p1 = enc1.getCount();
  long p2 = enc2.getCount();

  // PID M1
  int e1 = target1 - p1;
  if (abs(e1) < 10) { e1 = 0; eintegral1 = 0; } // Limpa a memória quando estaciona
  float dedt1 = (e1 - eprev1) / deltaT;
  eintegral1 = eintegral1 + e1 * deltaT;
  float u1 = (kp * e1) + (kd * dedt1) + (ki * eintegral1);
  eprev1 = e1;

  // PID M2
  int e2 = target2 - p2;
  if (abs(e2) < 10) { e2 = 0; eintegral2 = 0; }
  float dedt2 = (e2 - eprev2) / deltaT;
  eintegral2 = eintegral2 + e2 * deltaT;
  float u2 = (kp * e2) + (kd * dedt2) + (ki * eintegral2);
  eprev2 = e2;

  setMotor((int)u1, M1_PWM, M1_DIR);
  setMotor((int)u2, M2_PWM, M2_DIR);
}

// ==========================================
// FUNÇÃO DE ENERGIA (O TEU LIMITE DE 150)
// ==========================================
void setMotor(int speed, int pinPWM, int pinDIR) {
  int minPWM = 15; // O micro-empurrão anti-fricção
  
  if (speed > 0) {
    digitalWrite(pinDIR, HIGH); 
    if (speed < minPWM) speed = minPWM; 
  } else if (speed < 0) {
    digitalWrite(pinDIR, LOW); 
    if (speed > -minPWM) speed = -minPWM; 
  } else {
    speed = 0; 
  }
  
  // O LIMITE AMIGO DAS PILHAS!
  if (speed > 150) speed = 150;
  if (speed < -150) speed = -150;
  
  analogWrite(pinPWM, abs(speed)); 
}