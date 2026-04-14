#define LIGHT_SENSOR_PIN  34

#define ALPHA           0.05f
#define FLASH_MARGIN    200
#define COOLDOWN_MS     2000
#define CALIB_SAMPLES   50

bool a_andar = false;
float baseline = 0;
unsigned long lastFlash = 0;

void calibrar() {
  Serial.println("A calibrar...");
  long soma = 0;
  for (int i = 0; i < CALIB_SAMPLES; i++) {
    soma += analogRead(LIGHT_SENSOR_PIN);
    delay(20);
  }
  baseline = soma / CALIB_SAMPLES;
  Serial.print("Baseline: ");
  Serial.println(baseline);
}

void setup() {
  Serial.begin(9600);
  analogSetAttenuation(ADC_11db);
  calibrar();
}

void loop() {
  int luz = analogRead(LIGHT_SENSOR_PIN);
  float threshold = baseline + FLASH_MARGIN;
  bool flash = false;
  unsigned long agora = millis();

  if (luz > threshold && (agora - lastFlash) > COOLDOWN_MS) {
    flash     = true;
    lastFlash = agora;
    a_andar   = true;
  }

  if (!flash) {
    baseline = ALPHA * luz + (1.0f - ALPHA) * baseline;
  }

  if (a_andar) {
    Serial.println("A ANDAR");
    // motoresOn();
  } else {
    Serial.println("À espera de flash...");
    // motoresOff();
  }

  Serial.print("Luz:");        Serial.print(luz);
  Serial.print(",Baseline:");  Serial.print(baseline);
  Serial.print(",Threshold:"); Serial.print(threshold);
  Serial.print(",Flash:");     Serial.println(flash ? 1000 : 0);

  delay(50);
}
