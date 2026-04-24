// Pinos ligados aos pinos 12 e 13 da ESP32-CAM
#define PIN_INPUT_LEFT  12 
#define PIN_INPUT_RIGHT 13

void setup() {
  Serial.begin(115200);

  pinMode(PIN_INPUT_LEFT, INPUT_PULLDOWN);
  pinMode(PIN_INPUT_RIGHT, INPUT_PULLDOWN);

  Serial.println("A aguardar sinal da ESP32-CAM...");
}

void loop() {
  bool isLeft = digitalRead(PIN_INPUT_LEFT);
  bool isRight = digitalRead(PIN_INPUT_RIGHT);

  if (isLeft && !isRight) {
    Serial.println("Recebido: ESQUERDA");
    // Coloca a tua lógica de virar o robô/carrinho para a esquerda aqui
  } 
  else if (isRight && !isLeft) {
    Serial.println("Recebido: DIREITA");
    // Coloca a tua lógica de virar para a direita aqui
  } 
  else if (!isLeft && !isRight) {
    // Ambos em baixo (Pode acontecer entre leituras ou se a câmara travar)
    // Serial.println("Parado / Sem Sinal"); 
  }
  else {
    // Erro: Ambos a HIGH não deveria acontecer pela lógica da câmara
    Serial.println("Erro de leitura: Ambos os pinos em HIGH");
  }

  delay(100); // Lemos a cada 100ms
}