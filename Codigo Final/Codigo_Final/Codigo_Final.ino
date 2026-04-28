#include <QTRSensors.h>
#include <Preferences.h>

QTRSensors qtr;
Preferences memoria;

// ==========================================
// PINOS FISICOS - ATUALIZADO PARA 6 SENSORES
// ==========================================
const uint8_t SensorCount = 6; // <-- Passa para 6
uint16_t sensorValues[SensorCount];
const uint8_t qtrPins[] = {36, 39, 34, 35, 32, 33}; // <-- Só 6 pinos

#define M1_PWM 19 
#define M1_DIR 21 
#define M2_PWM 5 
#define M2_DIR 18

#define PINO_SEMAFORO 27 // Mudámos para o 27 para libertar o 25/26
#define LED_INTERNO 2 

// VARIÁVEIS DO PID E ESTADO
float Kp = 0.03; // Ajustem conforme a nova velocidade
float Kd = 1; 
float Ki = 0; 
int baseSpeed = 250;  // 230 kp=0.03 kd=1 tava bom 

unsigned long tempoArranque = 0;
int thresholdLuz = 0;
int lastError = 0;
float I = 0;  

String estadoCarro = "espera_semaforo"; 

void setMotor(int speed, int pinPWM, int pinDIR);

void setup() {
  Serial.begin(115200);
  pinMode(PINO_SEMAFORO, INPUT);
  pinMode(LED_INTERNO, OUTPUT);

  // Configurar a biblioteca QTR para Analógico
  qtr.setTypeAnalog();
  qtr.setSensorPins(qtrPins, SensorCount);

  pinMode(M1_PWM, OUTPUT); pinMode(M1_DIR, OUTPUT);
  pinMode(M2_PWM, OUTPUT); pinMode(M2_DIR, OUTPUT);

  Serial.println("Sistemas Iniciados - QTR-8A Armada");
  delay(1000); 

  // ==========================================
  // 1. CALIBRAÇÃO MANUAL (5 SEGUNDOS)
  // ==========================================
  Serial.println("--- CALIBRAÇÃO: MOVE O CARRO NA LINHA ---");
  digitalWrite(LED_INTERNO, HIGH); 

  unsigned long tempoCalibracao = millis();
  while (millis() - tempoCalibracao < 2000) {
    qtr.calibrate();
  }
  digitalWrite(LED_INTERNO, LOW); 
  Serial.println("--- CALIBRAÇÃO OK ---");

  // Gravar na memória para segurança
  memoria.begin("calibracao", false);
  memoria.putBytes("minimos", qtr.calibrationOn.minimum, SensorCount * sizeof(uint16_t));
  memoria.putBytes("maximos", qtr.calibrationOn.maximum, SensorCount * sizeof(uint16_t));
  memoria.end();

  // Tempo para alinhar
  Serial.println("ALINHA NA PARTIDA!");
  for(int i=0; i<5; i++) {
    digitalWrite(LED_INTERNO, HIGH); delay(500);
    digitalWrite(LED_INTERNO, LOW); delay(500);
  }

  // ==========================================
  // 2. LER LUZ AMBIENTE
  // ==========================================
  long somaLuz = 0;
  for(int i = 0; i < 20; i++) {
    somaLuz += analogRead(PINO_SEMAFORO);
    delay(20);
  }
  
  thresholdLuz = (somaLuz / 20) + 200; 
  if (thresholdLuz > 3800) thresholdLuz = 3800;

  Serial.println("ARMADO! À espera do Semáforo...");
  estadoCarro = "espera_semaforo";
}

void loop() {
  
  if (estadoCarro == "espera_semaforo") {
    setMotor(0, M1_PWM, M1_DIR);
    setMotor(0, M2_PWM, M2_DIR);

    if (analogRead(PINO_SEMAFORO) > thresholdLuz) {
      estadoCarro = "correr"; 
      tempoArranque = millis(); 
    }
  }
  
 else if (estadoCarro == "correr") {
    
    // ==========================================
    // CONTROLO DE TRAÇÃO (RAMPA DE ACELERAÇÃO)
    // ==========================================
    unsigned long tempoNaCorrida = millis() - tempoArranque;
    
    // Durante os primeiros 800 milissegundos (0.8 segundos) após o arranque
    if (tempoNaCorrida < 800) { 
      // Sobe a velocidade progressivamente de 220 até 255
      baseSpeed = map(tempoNaCorrida, 0, 800, 210, 250); 
    } else {
      // Passaram os 800ms? O carro já estabilizou no chão, prego a fundo!
      baseSpeed = 250; 
    }
    
    // 1. LEITURA DA POSIÇÃO (Lógica QTR)
    // Para 8 sensores, o valor vai de 0 a 7000.
    uint16_t position = qtr.readLineBlack(sensorValues);

    // 2. DETEÇÃO DA META (6 ou mais sensores no preto)
    if (millis() - tempoArranque > 1500) { 
      int sensoresNoPreto = 0;
      for (int i = 0; i < SensorCount; i++) {
        if (sensorValues[i] > 700) sensoresNoPreto++;
      }

      if (sensoresNoPreto >= 5) { // <-- Trava se 5 dos 6 virem a linha
        setMotor(0, M1_PWM, M1_DIR);
        setMotor(0, M2_PWM, M2_DIR);
        estadoCarro = "parar"; 
        Serial.println("VITÓRIA! META DETETADA.");
        return; 
      }
    }

   // 3. CÁLCULO DO PID (Centro de 6 sensores é 2500)
    int error = 2500 - (int)position; // <-- Passa de 3500 para 2500

    float motorSpeedCorrection = (error * Kp) + (I * Ki) + ((error - lastError) * Kd);
    lastError = error;
    I += error;

    setMotor(baseSpeed - motorSpeedCorrection, M1_PWM, M1_DIR);
    setMotor(baseSpeed + motorSpeedCorrection, M2_PWM, M2_DIR);
  }
  
  else if (estadoCarro == "parar") {
    setMotor(0, M1_PWM, M1_DIR);
    setMotor(0, M2_PWM, M2_DIR);
  }
}

void setMotor(int speed, int pinPWM, int pinDIR) {
  if (speed > 0) digitalWrite(pinDIR, HIGH);
  else if (speed < 0) digitalWrite(pinDIR, LOW);
  else speed = 0;
  
  if (speed > 250) speed = 250;
  if (speed < -250) speed = -250;
  analogWrite(pinPWM, abs(speed)); 
}