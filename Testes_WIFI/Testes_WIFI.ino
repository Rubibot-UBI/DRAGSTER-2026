#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <QTRSensors.h>
#include <ArduinoOTA.h> 
#include <Preferences.h>

Preferences memoria;

// ==========================================
// PÁGINA WEB (A TUA BOX DE AFINAÇÃO)
// ==========================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { text-align:center; font-family: Arial; background-color: #222; color: white; padding: 10px; margin: 0;}
    .btn { padding: 15px 20px; font-size: 18px; margin: 5px; border-radius: 10px; cursor: pointer; border: none; font-weight: bold; width: 90%;}
    .btn-cal { background-color: #f39c12; color: white; }
    .btn-start { background-color: #2ecc71; color: white; }
    .btn-stop { background-color: #e74c3c; color: white; }
    .slider-container { background: #333; padding: 10px; border-radius: 10px; margin: 10px 0; }
    input[type=range] { width: 85%; margin: 10px; }
    .val-display { font-weight: bold; color: #3498db; }
  </style>
</head>
<body>
  <h2>Robô Dragster Tuning</h2>
  
  <button class="btn btn-cal" onclick="send('calibrar')">1. CALIBRAR SENSORES</button>
  <button class="btn btn-start" onclick="send('correr')">2. ARRANCAR (RACE!)</button>
  <button class="btn btn-stop" onclick="send('parar')">PARAR EMERGÊNCIA</button>

  <div class="slider-container">
    <label>Velocidade Base: <span id="vel_val" class="val-display">100</span></label><br>
    <input type="range" min="0" max="250" step="5" value="100" onchange="updateVal('vel', this.value)">
  </div>
  <div class="slider-container">
    <label>Kp (Curvar): <span id="kp_val" class="val-display">0.05</span></label><br>
    <input type="range" min="0" max="0.5" step="0.01" value="0.05" onchange="updateVal('kp', this.value)">
  </div>
  <div class="slider-container">
    <label>Kd (Travar Oscilação): <span id="kd_val" class="val-display">0.80</span></label><br>
    <input type="range" min="0" max="3" step="0.05" value="0.80" onchange="updateVal('kd', this.value)">
  </div>
  <div class="slider-container">
    <label>Ki (Erro Constante): <span id="ki_val" class="val-display">0.000</span></label><br>
    <input type="range" min="0" max="0.01" step="0.0001" value="0.000" onchange="updateVal('ki', this.value)">
  </div>

  <script>
    function send(cmd) { fetch('/' + cmd); }
    function updateVal(param, val) {
      document.getElementById(param + '_val').innerHTML = val;
      fetch('/update?param=' + param + '&value=' + val);
    }
  </script>
</body>
</html>
)rawliteral";

AsyncWebServer server(80);
QTRSensors qtr;

// ==========================================
// PINOS FISICOS
// ==========================================
// OS PINOS DE OURO (ADC1)
const uint8_t SensorCount = 5; // <--- Passa de 6 para 5
uint16_t sensorValues[SensorCount];

// Coloca aqui a ordem exata dos pinos analógicos do ESP32 onde ligaste o D3 ao D7
const uint8_t qtrPins[] = {39, 34, 35, 32, 33};

// Os pinos dos teus Motores
#define M1_PWM 19 
#define M1_DIR 21 
#define M2_PWM 5 
#define M2_DIR 18

// ==========================================
// VARIÁVEIS DO PID E ESTADO
// ==========================================
float Kp = 0.03, Kd = 0.80, Ki = 0.00;
int baseSpeed = 100;
int lastError = 0;
float I = 0;

String estadoCarro = "parar";

void setMotor(int speed, int pinPWM, int pinDIR);

void setup() {
  Serial.begin(115200);

  // 1. Configurar QTR como ANALÓGICO
  qtr.setTypeAnalog();
  qtr.setSensorPins(qtrPins, SensorCount);

  // 2. Configurar Motores
  pinMode(M1_PWM, OUTPUT); pinMode(M1_DIR, OUTPUT);
  pinMode(M2_PWM, OUTPUT); pinMode(M2_DIR, OUTPUT);

  // 3. WiFi e Servidor
  WiFi.softAP("Dragster_ESP32", "12345678");
  ArduinoOTA.setHostname("DragsterPID");
  ArduinoOTA.begin();
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("param") && request->hasParam("value")) {
      String param = request->getParam("param")->value();
      float val = request->getParam("value")->value().toFloat();
      if(param == "kp") Kp = val;
      if(param == "ki") Ki = val;
      if(param == "kd") Kd = val;
      if(param == "vel") baseSpeed = (int)val;
    }
    request->send(200);
  });

  server.on("/calibrar", HTTP_GET, [](AsyncWebServerRequest *request){ estadoCarro = "calibrar"; request->send(200); });
  server.on("/correr", HTTP_GET, [](AsyncWebServerRequest *request){ estadoCarro = "correr"; request->send(200); });
  server.on("/parar", HTTP_GET, [](AsyncWebServerRequest *request){ estadoCarro = "parar"; request->send(200); });

  server.begin();
}

void loop() {
  ArduinoOTA.handle(); // Atualizações via rede ligadas!

  if (estadoCarro == "parar") {
    setMotor(0, M1_PWM, M1_DIR);
    setMotor(0, M2_PWM, M2_DIR);
  } 
  
  else if (estadoCarro == "calibrar") {
    Serial.println("A calibrar (Dança Curta)...");
    
    // Repete o "abanar a cabeça" 2 vezes para garantir uma leitura perfeita
    for (int ciclo = 0; ciclo < 2; ciclo++) {
      if (estadoCarro == "parar") break; 
      
      setMotor(50, M1_PWM, M1_DIR);
      setMotor(-50, M2_PWM, M2_DIR);
      for (int i = 0; i < 10; i++) { 
        if (estadoCarro == "parar") break; 
        qtr.calibrate(); 
        delay(10); 
      }
      
      if (estadoCarro == "parar") break; 

      setMotor(-50, M1_PWM, M1_DIR);
      setMotor(50, M2_PWM, M2_DIR);
      for (int i = 0; i < 20; i++) { 
        if (estadoCarro == "parar") break; 
        qtr.calibrate(); 
        delay(10); 
      }
      
      if (estadoCarro == "parar") break; 

      setMotor(50, M1_PWM, M1_DIR);
      setMotor(-50, M2_PWM, M2_DIR);
      for (int i = 0; i < 10; i++) { 
        if (estadoCarro == "parar") break; 
        qtr.calibrate(); 
        delay(10); 
      }
    }

    setMotor(0, M1_PWM, M1_DIR);
    setMotor(0, M2_PWM, M2_DIR);
    
    // ==========================================
    // GRAVAR CALIBRAÇÃO NA MEMÓRIA DO ESP32
    // ==========================================
    memoria.begin("calibracao", false); 
    memoria.putBytes("minimos", qtr.calibrationOn.minimum, SensorCount * sizeof(uint16_t));
    memoria.putBytes("maximos", qtr.calibrationOn.maximum, SensorCount * sizeof(uint16_t));
    memoria.end();
    
    Serial.println("Calibração guardada na memória permanente!");

    if (estadoCarro != "parar") {
      estadoCarro = "parar"; 
    }
  }
  
  else if (estadoCarro == "correr") {
    uint16_t position = qtr.readLineBlack(sensorValues);
    
// ==========================================
    // DETEÇÃO DA META 
    // ==========================================
    int sensoresNoPreto = 0;
    for (int i = 0; i < SensorCount; i++) {
      if (sensorValues[i] > 600) { 
        sensoresNoPreto++;
      }
    }

    // Passa de >= 5 para >= 4
    if (sensoresNoPreto >= 4) {
      setMotor(0, M1_PWM, M1_DIR);
      setMotor(0, M2_PWM, M2_DIR);
      estadoCarro = "parar"; 
      Serial.println("META DETETADA!");
      return; 
    }

    // Cálculo do PID
    int error = 2000 - position;

    int P = error;
    I = I + error;
    int D = error - lastError;
    lastError = error;

    float motorSpeedCorrection = (P * Kp) + (I * Ki) + (D * Kd);

    int motorSpeedA = baseSpeed - motorSpeedCorrection; 
    int motorSpeedB = baseSpeed + motorSpeedCorrection;

    setMotor(motorSpeedA, M1_PWM, M1_DIR);
    setMotor(motorSpeedB, M2_PWM, M2_DIR);
  }
}

// O teu controlador de motores
void setMotor(int speed, int pinPWM, int pinDIR) {
  if (speed > 0) { digitalWrite(pinDIR, HIGH); } 
  else if (speed < 0) { digitalWrite(pinDIR, LOW); } 
  else { speed = 0; }
  
  if (speed > 255) speed = 255;
  if (speed < -255) speed = -255;
  
  analogWrite(pinPWM, abs(speed)); 
}