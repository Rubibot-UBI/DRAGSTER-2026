#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include "model_opt.h"

// --- TFLite Includes ---
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

// --- TFLite Config ---
#define NUM_INPUTS 9216

// Tensor arena: memory for TFLite to compute the layers. 
// 60KB is a safe starting point for a tiny CNN. Adjust if it fails to allocate.
// Increase the arena size to 180KB to satisfy the 176720 byte request
constexpr int kTensorArenaSize = 180 * 1024; // Set this to what your model actually needs
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

// --- NETWORK CONFIG ---
const char* ssid = "RedeTeste";
const char* password = "rubibot123";

WiFiServer server(80);
WiFiUDP udp;
const int udpPort = 5005;

void setup() {
  Serial.begin(115200);
  delay(2000); 
  Serial.println("\n\n--- ESP32-CAM Starting ---");


  
  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("[WIFI] Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println(WiFi.status());
  }
  Serial.println("\n[WIFI] Connected!");
  Serial.print("[WIFI] Camera IP: ");
  Serial.println(WiFi.localIP());

  // Initialize UDP & TCP
  udp.begin(udpPort);
  server.begin();
  Serial.println("[NET] Services started.");

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

  // Allocate tensor arena in external PSRAM
  tensor_arena = (uint8_t*) heap_caps_malloc(kTensorArenaSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (tensor_arena == NULL) {
    Serial.println("Failed to allocate tensor arena in PSRAM!");
    while(true);
  }
  
  // --- Initialize TFLite Micro ---
  model = tflite::GetModel(model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.println("Model schema version mismatch!");
    while(true);
  }

  // Load the specific operations...

  // Load the specific operations your CNN uses. 
  // If your model uses an operation not listed here, it will fail at allocation.
  static tflite::MicroMutableOpResolver<8> resolver;
  resolver.AddConv2D();
  resolver.AddMaxPool2D();
  resolver.AddReshape();
  resolver.AddFullyConnected();
  resolver.AddLogistic(); 
  resolver.AddRelu();
  resolver.AddAveragePool2D();
  resolver.AddMean();

  // Build interpreter
  static tflite::MicroInterpreter static_interpreter(
      model, resolver, tensor_arena, kTensorArenaSize);
  interpreter = &static_interpreter;

  // Allocate memory from the tensor_arena for the model's tensors
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    Serial.println("AllocateTensors failed! Increase kTensorArenaSize.");
    while(true);
  }

  // Assign pointers to input and output tensors
  input = interpreter->input(0);
  output = interpreter->output(0);
  
  Serial.println("[ML] Model initialized successfully!");
}

void loop() {
  WiFiClient client = server.available();
  if (client) {    
    while (client.connected()) {
      if (client.available() > 0) {
        char cmd = client.read();
        
        if (cmd == 'C') {
          camera_fb_t * fb = esp_camera_fb_get();
          if (!fb) continue;

          // Preprocess: The model's internal Rescaling layer expects 0-255.
          // Because the input type is int8, we map 0...255 to -128...127.
          for (int i = 0; i < NUM_INPUTS; i++) {
              // Direct mapping: 0 becomes -128, 255 becomes 127
              input->data.int8[i] = (int8_t)(fb->buf[i] - 128);
          }
          
          // DO NOT RETURN THE FB HERE YET! We still need it for the network stream.

          if (interpreter->Invoke() != kTfLiteOk) {
            Serial.println("Inference failed!");
            esp_camera_fb_return(fb); // Only return if we fail and abort
            continue;
          }

          // Sigmoid Output (1 unit)
          int8_t out_val = output->data.int8[0];
          float scale = output->params.scale;
          int zero_point = output->params.zero_point;
          
          // Convert quantized int8 back to a 0.0-1.0 probability
          float probability = (out_val - zero_point) * scale;
          int predicted_class = (probability > 0.5f) ? 1 : 0;

          // --- SEND TO PYTHON SCRIPT ---
          // Protocol: <START>length,prediction,image_bytes<END>
          client.print("<START>");
          client.print(fb->len);
          client.print(",");
          client.print(predicted_class); // Will send '0' or '1'
          client.print(",");
          client.write(fb->buf, fb->len);
          client.print("<END>");

          // NOW WE ARE DONE WITH THE IMAGE, FREE THE MEMORY
          esp_camera_fb_return(fb);
        }
      }
      yield();
    }    
  } else {
    static unsigned long lastBroadcast = 0;
    if (millis() - lastBroadcast > 2000) {
      udp.beginPacket(IPAddress(255, 255, 255, 255), udpPort);
      udp.write((const uint8_t *)"ESP32_CAM", 9);
      udp.endPacket();
      lastBroadcast = millis();
    }
  }
}