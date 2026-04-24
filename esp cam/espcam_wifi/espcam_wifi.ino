#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiUdp.h>

// AI-Thinker ESP32-CAM Pin Definition
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

#define FLASH_GPIO_NUM    33

// --- NETWORK CONFIG ---
const char* ssid = "RedeTeste";
const char* password = "rubibot123";

WiFiServer server(80);
WiFiUDP udp;
const int udpPort = 5005;

void setup() {
  Serial.begin(115200); 
  Serial.println("\n\n--- ESP32-CAM Starting ---");
  
  pinMode(FLASH_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_GPIO_NUM, HIGH); 
  
  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("[WIFI] Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\n[WIFI] Connected!");
  Serial.print("[WIFI] Camera IP: ");
  Serial.println(WiFi.localIP());

  // Initialize UDP for broadcast
  udp.begin(udpPort);
  Serial.println("[UDP] Service started.");

  server.begin();
  Serial.println("[TCP] Server started on port 80.");

  // Camera Configuration
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
  
  // OPTIMIZATION: Reduced clock speed to 10MHz for lower power and heat.
  config.xclk_freq_hz = 10000000; 
  
  // CHANGED: Use raw Grayscale instead of JPEG and 96x96 resolution
  config.pixel_format = PIXFORMAT_GRAYSCALE; 
  config.frame_size = FRAMESIZE_96X96; 
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_DRAM;

  Serial.println("[CAM] Initializing camera...");
  esp_err_t err = esp_camera_init(&config);
  
  if (err != ESP_OK) {
    Serial.printf("[CAM] ERROR: Init failed with error 0x%x\n", err);
    Serial.println("[SYSTEM] Camera is broken or not initialized properly. Halting.");
    while (true) {
      delay(1000); 
    }
  }
  Serial.println("[CAM] Initialization successful.");
}

void loop() {
  WiFiClient client = server.available();

  if (client) {    
    while (client.connected()) {
      if (client.available() > 0) {
        char cmd = client.read();
        
        if (cmd == 'C') {
          camera_fb_t * fb = esp_camera_fb_get();

          if (!fb) {
            Serial.println("[CAM] ERROR: Camera capture failed!");
            continue;
          }

          char header[32];
          size_t headerLen = snprintf(header, sizeof(header), "<START>%u,", fb->len);
          
          client.write((const uint8_t*)header, headerLen);
          client.write(fb->buf, fb->len); // Sending raw grayscale bytes
          client.write((const uint8_t*)"<END>", 5);

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