#include <QTRSensors.h>

QTRSensors qtr;

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
#define PINO_SEMAFORO 14 // <-- Podes mudar para o pino digital livre que usares!

// ==========================================
// VARIÁVEIS DO PID E ESTADO (COLOCA AQUI OS TEUS VALORES FINAIS!)
// ==========================================
float Kp = 0.05; 
float Kd = 0.60; 
float Ki = 0.00;
int baseSpeed = 100; // A vossa velocidade de cruzeiro

int lastError = 0;
float I = 0;

String estadoCarro = "calibrar"; // Arranca logo a calibrar mal ligas a bateria!

void setMotor(int speed, int pinPWM, int pinDIR);

void setup() {
  Serial.begin(115200);

  // 1. Configurar Pinos e Sensores
  pinMode(PINO_SEMAFORO, INPUT);
  
  qtr.setTypeAnalog();
  qtr.setSensorPins(qtrPins, SensorCount);

  pinMode(M1_PWM, OUTPUT); pinMode(M1_DIR, OUTPUT);
  pinMode(M2_PWM, OUTPUT); pinMode(M2_DIR, OUTPUT);

  // Pequena pausa de 2 segundos antes de começar a calibrar 
  // para dar tempo de tirares as mãos do carro ao ligar a bateria
  delay(2000); 
}

void loop() {
  
  else if (estadoCarro == "calibrar") {
    Serial.println("A calibrar (Dança Curta)...");
    
    // Repete o "abanar a cabeça" 2 vezes para garantir uma leitura perfeita
    for (int ciclo = 0; ciclo < 2; ciclo++) {
      
      // 1. Vira ligeiramente para a Direita (Ex: 30 graus)
      setMotor(70, M1_PWM, M1_DIR);
      setMotor(-70, M2_PWM, M2_DIR);
      for (int i = 0; i < 30; i++) { // Dura ~300ms
        qtr.calibrate(); 
        delay(10); 
      }
      
      // 2. Vira para a Esquerda (Varre os 60 graus, passando pelo centro)
      setMotor(-70, M1_PWM, M1_DIR);
      setMotor(70, M2_PWM, M2_DIR);
      for (int i = 0; i < 60; i++) { // Dura ~600ms
        qtr.calibrate(); 
        delay(10); 
      }
      
      // 3. Volta a virar para a Direita (Ex: 30 graus, regressando ao centro)
      setMotor(70, M1_PWM, M1_DIR);
      setMotor(-70, M2_PWM, M2_DIR);
      for (int i = 0; i < 30; i++) { // Dura ~300ms
        qtr.calibrate(); 
        delay(10); 
      }
    }

    // Para os motores e avança para a próxima fase
    setMotor(0, M1_PWM, M1_DIR);
    setMotor(0, M2_PWM, M2_DIR);
    Serial.println("Calibração concluída! À espera do Semáforo...");
    estadoCarro = "espera_semaforo"; 
  }

  else if (estadoCarro == "espera_semaforo") {
    // Fica a ler o pino da LDR constantemente
    if (digitalRead(PINO_SEMAFORO) == HIGH) {
      Serial.println("LUZ DETETADA! ARRANCAR!!!");
      estadoCarro = "correr"; 
    }
  }
  
  else if (estadoCarro == "correr") {
    // 1. Lê a posição
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

    // Se 5 ou mais sensores virem a linha preta... TRAVA!
    if (sensoresNoPreto >= 5) {
      setMotor(0, M1_PWM, M1_DIR);
      setMotor(0, M2_PWM, M2_DIR);
      estadoCarro = "parar"; // Bloqueia o robô para sempre
      Serial.println("META DETETADA! FIM DE CORRIDA!");
      return; 
    }
    // ==========================================

    // 2. O PID normal para seguir a linha
    int error = 2500 - position;

    int P = error;
    I = I + error;
    int D = error - lastError;
    lastError = error;

    float motorSpeedCorrection = (P * Kp) + (I * Ki) + (D * Kd);

    // Velocidade dos motores (Com os sinais corretos do vosso teste)
    int motorSpeedA = baseSpeed - motorSpeedCorrection; 
    int motorSpeedB = baseSpeed + motorSpeedCorrection;

    setMotor(motorSpeedA, M1_PWM, M1_DIR);
    setMotor(motorSpeedB, M2_PWM, M2_DIR);
  }

  else if (estadoCarro == "parar") {
    // Estado final após cortar a meta. O carro não faz mais nada até ser reiniciado.
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