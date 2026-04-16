#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESP32Encoder.h>
#include <ArduinoOTA.h> 

// ==========================================
// PÁGINA WEB (COM BOTÕES "HOLD TO MOVE")
// ==========================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
  <style>
    body { text-align:center; font-family: Arial; background-color: #222; color: white; padding: 10px; margin: 0;}
    /* touch-action: none é CRÍTICO para o telemóvel não tentar fazer scroll enquanto conduzes! */
    .btn { padding: 25px 30px; font-size: 20px; margin: 5px; border-radius: 10px; cursor: pointer; border: none; font-weight: bold; touch-action: none; user-select: none; -webkit-user-select: none;}
    .btn-dir { background-color: #3498db; color: white; }
    .btn-dir:active { background-color: #2980b9; }
    .btn-stop { background-color: #e74c3c; color: white; }
    .slider-container { background: #333; padding: 10px; border-radius: 10px; margin: 10px 0; }
    input[type=range] { width: 85%; margin: 10px; }
    .val-display { font-weight: bold; color: #2ecc71; }
    .controls { display: inline-block; margin-top: 20px; }
  </style>
</head>
<body>
  <h2>Controlo Contínuo + Tuning</h2>
  
  <div class="controls">
    <button class="btn btn-dir" onpointerdown="send('frente')" onpointerup="send('parar')" onpointerleave="send('parar')">FRENTE</button><br>
    <button class="btn btn-dir" onpointerdown="send('esquerda')" onpointerup="send('parar')" onpointerleave="send('parar')">ESQ</button>
    <button class="btn btn-stop" onpointerdown="send('parar')">PARAR</button>
    <button class="btn btn-dir" onpointerdown="send('direita')" onpointerup="send('parar')" onpointerleave="send('parar')">DIR</button><br>
    <button class="btn btn-dir" onpointerdown="send('tras')" onpointerup="send('parar')" onpointerleave="send('parar')">TRAS</button>
  </div>

  <div class="slider-container" style="margin-top:30px;">
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

// PINOS (A tua montagem)
#define M1_ENCA 4  
#define M1_ENCB 16 
#define M2_ENCA 22 
#define M2_ENCB 23 
#define M1_PWM 19 
#define M1_DIR 21 
#define M2_PWM 5
#define M2_DIR 18

// VARIÁVEIS DO SISTEMA
float kp = 0.4, kd = 0.03, ki = 0.5;
long target1 = 0, target2 = 0;
long prevT = 0;
float eprev1 = 0, eintegral1 = 0;
float eprev2 = 0, eintegral2 = 0;

// O ESTADO DO CARRO
String estadoCarro = "parar";

void setMotor(int speed, int pinPWM, int pinDIR);

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
    }
    request->send(200);
  });

  // As rotas de direção agora SÓ mudam o estado!
  server.on("/frente", HTTP_GET, [](AsyncWebServerRequest *request){ estadoCarro = "frente"; request->send(200); });
  server.on("/tras", HTTP_GET, [](AsyncWebServerRequest *request){ estadoCarro = "tras"; request->send(200); });
  server.on("/esquerda", HTTP_GET, [](AsyncWebServerRequest *request){ estadoCarro = "esquerda"; request->send(200); });
  server.on("/direita", HTTP_GET, [](AsyncWebServerRequest *request){ estadoCarro = "direita"; request->send(200); });
  
  server.on("/parar", HTTP_GET, [](AsyncWebServerRequest *request){ 
    estadoCarro = "parar"; 
    // Trava instantaneamente na posição em que as rodas estão agora
    target1 = enc1.getCount(); 
    target2 = enc2.getCount(); 
    request->send(200); 
  });

  server.begin();

  // 3. Hardware
  pinMode(M1_ENCA, INPUT_PULLUP); pinMode(M1_ENCB, INPUT_PULLUP);
  pinMode(M2_ENCA, INPUT_PULLUP); pinMode(M2_ENCB, INPUT_PULLUP);
  enc1.attachFullQuad(M1_ENCA, M1_ENCB);
  enc2.attachFullQuad(M2_ENCA, M2_ENCB);
  enc1.clearCount(); enc2.clearCount();

  pinMode(M1_PWM, OUTPUT); pinMode(M1_DIR, OUTPUT);
  pinMode(M2_PWM, OUTPUT); pinMode(M2_DIR, OUTPUT);
  
  target1 = 0; target2 = 0;
}

void loop() {
  ArduinoOTA.handle(); // Mantém o ESP à escuta para receber código novo!

  long currT = micros();
  float deltaT = ((float) (currT - prevT)) / 1.0e6;
  if (deltaT < 0.01) return; // Corre a cada 10ms
  prevT = currT;

  // ==========================================
  // O GERADOR DE TRAJETÓRIA CONTÍNUA (MÁGICA!)
  // ==========================================
  int vel_alvo = 50; // Passos adicionados a cada 10ms (Aumenta para ir mais rápido)
  
  if (estadoCarro == "frente") {
    target1 += vel_alvo; target2 += vel_alvo;
  } else if (estadoCarro == "tras") {
    target1 -= vel_alvo; target2 -= vel_alvo;
  } else if (estadoCarro == "esquerda") {
    target1 += vel_alvo/2; target2 -= vel_alvo/2;
  } else if (estadoCarro == "direita") {
    target1 -= vel_alvo/2; target2 += vel_alvo/2;
  }

  // ==========================================
  // PID (Correção)
  // ==========================================
  long p1 = enc1.getCount();
  long p2 = enc2.getCount();

  int e1 = target1 - p1;
  if (abs(e1) < 10) { e1 = 0; eintegral1 = 0; }
  float dedt1 = (e1 - eprev1) / deltaT;
  eintegral1 += e1 * deltaT;
  float u1 = (kp * e1) + (kd * dedt1) + (ki * eintegral1);
  eprev1 = e1;

  int e2 = target2 - p2;
  if (abs(e2) < 10) { e2 = 0; eintegral2 = 0; }
  float dedt2 = (e2 - eprev2) / deltaT;
  eintegral2 += e2 * deltaT;
  float u2 = (kp * e2) + (kd * dedt2) + (ki * eintegral2);
  eprev2 = e2;

  setMotor((int)u1, M1_PWM, M1_DIR);
  setMotor((int)u2, M2_PWM, M2_DIR);
}

void setMotor(int speed, int pinPWM, int pinDIR) {
  int minPWM = 15; 
  if (speed > 0) { digitalWrite(pinDIR, HIGH); if (speed < minPWM) speed = minPWM; } 
  else if (speed < 0) { digitalWrite(pinDIR, LOW); if (speed > -minPWM) speed = -minPWM; } 
  else { speed = 0; }
  
  if (speed > 150) speed = 150;
  if (speed < -150) speed = -150;
  analogWrite(pinPWM, abs(speed)); 
}