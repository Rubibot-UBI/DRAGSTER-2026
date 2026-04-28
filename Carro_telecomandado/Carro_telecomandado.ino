// ==========================================
// CÓDIGO - MODO RÁDIO COMANDO WEB + ENCODERS
// Hardware: ESP32-WROOM + Keyestudio Shield
// ==========================================

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
  <h2>Robô Rádio Comando + PID</h2>
  
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

// ==========================================
// PINOS DA KEYESTUDIO SHIELD
// ==========================================
#define M1_ENCA 16  
#define M1_ENCB 4 
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

String estadoCarro = "parar";

void setMotor(int speed, int pinPWM, int pinDIR);

void setup() {
  Serial.begin(115200);

  // 1. Configuração do Ponto de Acesso Wi-Fi
  Serial.println("A iniciar Wi-Fi...");
  WiFi.softAP("Robo_ESP32", "12345678");
  Serial.print("Endereço IP para ligar no telemóvel: ");
  Serial.println(WiFi.softAPIP()); // Normalmente é 192.168.4.1

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

  server.on("/frente", HTTP_GET, [](AsyncWebServerRequest *request){ estadoCarro = "frente"; request->send(200); });
  server.on("/tras", HTTP_GET, [](AsyncWebServerRequest *request){ estadoCarro = "tras"; request->send(200); });
  server.on("/esquerda", HTTP_GET, [](AsyncWebServerRequest *request){ estadoCarro = "esquerda"; request->send(200); });
  server.on("/direita", HTTP_GET, [](AsyncWebServerRequest *request){ estadoCarro = "direita"; request->send(200); });
  
 server.on("/parar", HTTP_GET, [](AsyncWebServerRequest *request){ 
    estadoCarro = "parar"; 
    
    // 1. O alvo passa a ser o exato sítio onde a roda está
    target1 = enc1.getCount(); 
    target2 = enc2.getCount(); 
    
    // 2. MATAR A INÉRCIA DO PID (O Segredo para travar a fundo)
    eintegral1 = 0; 
    eintegral2 = 0; 
    eprev1 = 0;
    eprev2 = 0;
    
    request->send(200); 
  });

  server.begin();

  // 3. Configuração de Hardware
  // Os pinos dos encoders precisam de resistências de PULLUP para leituras limpas
  pinMode(M1_ENCA, INPUT_PULLUP); pinMode(M1_ENCB, INPUT_PULLUP);
  pinMode(M2_ENCA, INPUT_PULLUP); pinMode(M2_ENCB, INPUT_PULLUP);
  
  // Associa os pinos à biblioteca dos encoders
  enc1.attachFullQuad(M1_ENCA, M1_ENCB);
  enc2.attachFullQuad(M2_ENCA, M2_ENCB);
  enc1.clearCount(); 
  enc2.clearCount();

  pinMode(M1_PWM, OUTPUT); pinMode(M1_DIR, OUTPUT);
  pinMode(M2_PWM, OUTPUT); pinMode(M2_DIR, OUTPUT);
  
  target1 = 0; target2 = 0;
  Serial.println("Sistema Pronto! Liga-te à rede Robo_ESP32");
}

void loop() {
  ArduinoOTA.handle();

  long currT = micros();
  float deltaT = ((float) (currT - prevT)) / 1.0e6;
  if (deltaT < 0.01) return; // Corre o PID rigorosamente a cada 10ms
  prevT = currT;

  // ==========================================
  // O GERADOR DE TRAJETÓRIA CONTÍNUA
  // ==========================================
  // "vel_alvo" define a velocidade. Se achares muito lento, aumenta para 80 ou 100.
  int vel_alvo = 80; 
  
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
  // PID (Motor 1)
  // ==========================================
  long p1 = enc1.getCount();
  int e1 = target1 - p1;
  if (abs(e1) < 10) { e1 = 0; eintegral1 = 0; }
  float dedt1 = (e1 - eprev1) / deltaT;
  eintegral1 += e1 * deltaT;
  float u1 = (kp * e1) + (kd * dedt1) + (ki * eintegral1);
  eprev1 = e1;

  // ==========================================
  // PID (Motor 2)
  // ==========================================
  long p2 = enc2.getCount();
  int e2 = target2 - p2;
  if (abs(e2) < 10) { e2 = 0; eintegral2 = 0; }
  float dedt2 = (e2 - eprev2) / deltaT;
  eintegral2 += e2 * deltaT;
  float u2 = (kp * e2) + (kd * dedt2) + (ki * eintegral2);
  eprev2 = e2;

  // Envia energia para os motores
  setMotor((int)u1, M1_PWM, M1_DIR);
  setMotor((int)u2, M2_PWM, M2_DIR);
}

void setMotor(int speed, int pinPWM, int pinDIR) {
  int minPWM = 15; // Energia mínima para vencer a inércia do motor parado
  
  if (speed > 0) { 
    digitalWrite(pinDIR, HIGH); 
    if (speed < minPWM) speed = minPWM; 
  } 
  else if (speed < 0) { 
    digitalWrite(pinDIR, LOW); 
    if (speed > -minPWM) speed = -minPWM; 
  } 
  else { 
    speed = 0; 
  }
  
  // Capamos a velocidade máxima a 150 (para não ser violento a conduzir pela web)
  if (speed > 200) speed = 200;
  if (speed < -200) speed = -200;
  
  analogWrite(pinPWM, abs(speed)); 
}