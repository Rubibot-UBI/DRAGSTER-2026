#include <QTRSensors.h>
#include <Preferences.h> // <-- CORREÇÃO 1: Biblioteca adicionada!

QTRSensors qtr;
Preferences memoria;     // <-- CORREÇÃO 1: Objeto criado!

// ==========================================
// PINOS FISICOS
// ==========================================
// Sensores de Linha (QTR-8A)
const uint8_t SensorCount = 6;
uint16_t sensorValues[SensorCount];
const uint8_t qtrPins[] = {36, 39, 34, 35, 32, 33};

// Motores
#define M1_PWM 19 
#define M1_DIR 21 
#define M2_PWM 5 
#define M2_DIR 18

// Semáforo (LDR Digital)
#define PINO_SEMAFORO 14 

// ==========================================
// VARIÁVEIS DO PID E ESTADO
// ==========================================
float Kp = 0.05; 
float Kd = 0.60; 
float Ki = 0.00;
int baseSpeed = 100; // Velocidade de cruzeiro

unsigned long tempoArranque = 0;

int lastError = 0;
float I = 0;

String estadoCarro = "espera_semaforo"; // O estado inicial correto

void setMotor(int speed, int pinPWM, int pinDIR);

void setup() {
  Serial.begin(115200);

  pinMode(PINO_SEMAFORO, INPUT);
  
  qtr.setTypeAnalog();
  qtr.setSensorPins(qtrPins, SensorCount);

  pinMode(M1_PWM, OUTPUT); pinMode(M1_DIR, OUTPUT);
  pinMode(M2_PWM, OUTPUT); pinMode(M2_DIR, OUTPUT);

  // ==========================================
  // CARREGAR CALIBRAÇÃO DA MEMÓRIA
  // ==========================================
  Serial.println("A carregar calibração guardada...");
  
  // 1. Fazemos uma leitura falsa rápida só para a biblioteca QTR criar as variáveis internas
  qtr.calibrate(); 
  
  // 2. Lemos o "disco rígido" do ESP32 e substituímos os valores
  memoria.begin("calibracao", true); // "true" significa modo apenas de leitura
  memoria.getBytes("minimos", qtr.calibrationOn.minimum, SensorCount * sizeof(uint16_t));
  memoria.getBytes("maximos", qtr.calibrationOn.maximum, SensorCount * sizeof(uint16_t));
  memoria.end();
  
  Serial.println("Calibração Carregada! Pronto a arrancar.");
  // ==========================================
}

void loop() {
  
  // CORREÇÃO 2: Trocado 'else if' por 'if'
  if (estadoCarro == "espera_semaforo") {
    setMotor(0, M1_PWM, M1_DIR);
    setMotor(0, M2_PWM, M2_DIR);

    if (digitalRead(PINO_SEMAFORO) == HIGH) {
      Serial.println("LUZ DETETADA! GO GO GO!");
      estadoCarro = "correr"; 
      tempoArranque = millis(); // Regista o milissegundo exato da partida!
    }
  }
  
  else if (estadoCarro == "correr") {
    uint16_t position = qtr.readLineBlack(sensorValues);
    
    // ==========================================
    // DETEÇÃO DA META (IGNORADA NO 1º SEGUNDO!)
    // ==========================================
    // Só tenta detetar a meta se já tiver passado 1000 milissegundos (1 segundo)
    if (millis() - tempoArranque > 1000) { 
      
      int sensoresNoPreto = 0;
      for (int i = 0; i < SensorCount; i++) {
        if (sensorValues[i] > 600) { 
          sensoresNoPreto++;
        }
      }

      if (sensoresNoPreto >= 5) {
        setMotor(0, M1_PWM, M1_DIR);
        setMotor(0, M2_PWM, M2_DIR);
        estadoCarro = "parar"; 
        Serial.println("META DETETADA! FIM DE CORRIDA!");
        return; 
      }
    }
    // ==========================================

    // 2. O PID normal para seguir a linha
    int error = 2500 - position;

    int P = error;
    I = I + error;
    int D = error - lastError;
    lastError = error;

    float motorSpeedCorrection = (P * Kp) + (I * Ki) + (D * Kd);

    // Ajusta a velocidade de cada motor
    int motorSpeedA = baseSpeed - motorSpeedCorrection; 
    int motorSpeedB = baseSpeed + motorSpeedCorrection;

    setMotor(motorSpeedA, M1_PWM, M1_DIR);
    setMotor(motorSpeedB, M2_PWM, M2_DIR);
  }
  
  else if (estadoCarro == "parar") {
    setMotor(0, M1_PWM, M1_DIR);
    setMotor(0, M2_PWM, M2_DIR);
  }
}

// O vosso controlador de motores
void setMotor(int speed, int pinPWM, int pinDIR) {
  if (speed > 0) { digitalWrite(pinDIR, HIGH); } 
  else if (speed < 0) { digitalWrite(pinDIR, LOW); } 
  else { speed = 0; }
  
  if (speed > 255) speed = 255;
  if (speed < -255) speed = -255;
  
  analogWrite(pinPWM, abs(speed)); 
}