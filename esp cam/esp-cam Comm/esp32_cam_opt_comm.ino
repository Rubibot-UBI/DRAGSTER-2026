#include "esp_camera.h"
#include "model_opt.h"

// --- TFLite Includes ---
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

// --- TFLite Config ---
#define NUM_INPUTS 9216

constexpr int kTensorArenaSize = 180 * 1024; 
uint8_t* tensor_arena = nullptr;

const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;
TfLiteTensor* output = nullptr;

// --- AI-Thinker ESP32-CAM Pin Definition ---
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// --- Pinos de Comunicação ---
#define PIN_LEFT  12 // Pino Esquerda (Classe 0)
#define PIN_RIGHT 13 // Pino Direita (Classe 1)

void setup() {
  Serial.begin(115200);
  delay(2000); 
  Serial.println("\n\n--- ESP32-CAM Starting (Offline Mode) ---");

  // Configurar Pinos de Saída
  pinMode(PIN_LEFT, OUTPUT);
  pinMode(PIN_RIGHT, OUTPUT);
  
  // Garantir que começam desligados
  digitalWrite(PIN_LEFT, LOW);
  digitalWrite(PIN_RIGHT, LOW);

  // --- Camera Configuration ---
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 10000000; 
  config.pixel_format = PIXFORMAT_GRAYSCALE;
  config.frame_size = FRAMESIZE_96X96;
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_PSRAM; 

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed!");
    while(true);
  }

  tensor_arena = (uint8_t*) heap_caps_malloc(kTensorArenaSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (tensor_arena == NULL) {
    Serial.println("Failed to allocate tensor arena in PSRAM!");
    while(true);
  }
  
  model = tflite::GetModel(model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.println("Model schema version mismatch!");
    while(true);
  }

  static tflite::MicroMutableOpResolver<8> resolver;
  resolver.AddConv2D();
  resolver.AddMaxPool2D();
  resolver.AddReshape();
  resolver.AddFullyConnected();
  resolver.AddLogistic(); 
  resolver.AddRelu();
  resolver.AddAveragePool2D();
  resolver.AddMean();

  static tflite::MicroInterpreter static_interpreter(
      model, resolver, tensor_arena, kTensorArenaSize);
  interpreter = &static_interpreter;

  if (interpreter->AllocateTensors() != kTfLiteOk) {
    Serial.println("AllocateTensors failed! Increase kTensorArenaSize.");
    while(true);
  }

  input = interpreter->input(0);
  output = interpreter->output(0);
  
  Serial.println("[ML] Model initialized successfully! Starting inference loop...");
}

void loop() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Falha ao capturar imagem");
    delay(500);
    return;
  }

  // Preprocess
  for (int i = 0; i < NUM_INPUTS; i++) {
      input->data.int8[i] = (int8_t)(fb->buf[i] - 128);
  }

  if (interpreter->Invoke() != kTfLiteOk) {
    Serial.println("Inference failed!");
    esp_camera_fb_return(fb);
    return;
  }

  int8_t out_val = output->data.int8[0];
  float scale = output->params.scale;
  int zero_point = output->params.zero_point;
  
  float probability = (out_val - zero_point) * scale;
  int predicted_class = (probability > 0.5f) ? 1 : 0;

  // Atualizar pinos com base na previsão
  if (predicted_class == 0) {
    digitalWrite(PIN_LEFT, HIGH);
    digitalWrite(PIN_RIGHT, LOW);
    Serial.println("Direção: ESQUERDA (0)");
  } else {
    digitalWrite(PIN_LEFT, LOW);
    digitalWrite(PIN_RIGHT, HIGH);
    Serial.println("Direção: DIREITA (1)");
  }

  esp_camera_fb_return(fb);
  
  // Pequeno delay para estabilidade do sistema e para a outra placa conseguir ler a tempo
  delay(5000); 
}