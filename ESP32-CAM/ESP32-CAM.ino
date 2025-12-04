#define ENABLE_USER_AUTH
#define ENABLE_STORAGE
#define ENABLE_FS

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <FS.h>
#include <LittleFS.h>
#include "esp_camera.h"

#include "secrets.h" 

#define WIFI_SSID SSID_WIFI
#define WIFI_PASSWORD PASSWORD_WIFI

#define API_KEY KEY_API
#define USER_EMAIL EMAIL_USER
#define USER_PASSWORD PASSWORD_USER
#define STORAGE_BUCKET_ID ID_BUCKET

// Photo configuration
#define FILE_PHOTO_PATH "/photo.jpg"
#define STORAGE_PATH "/images/" 

// Define Flash LED Pin
#define LED_PIN 4

// Firebase components
FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
Storage storage;

// Authentication
UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD, 3000);

// Task Handles
TaskHandle_t cameraTaskHandle;
TaskHandle_t firebaseTaskHandle;

// Semaphore untuk sinkronisasi
SemaphoreHandle_t uploadSemaphore;

// Flags for communication between tasks
volatile bool photoReadyToUpload = false;
volatile bool uploadInProgress = false;
String uploadFileName = "";

// User functions
void processData(AsyncResult &aResult);
void file_operation_callback(File &file, const char *filename, file_operating_mode mode);

FileConfig media_file(FILE_PHOTO_PATH, file_operation_callback); 
File myFile;

// Camera Pins (AI-Thinker ESP32-CAM)
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

// Function Declarations
void cameraTask(void *pvParameters);
void firebaseTask(void *pvParameters);
void capturePhotoSaveLittleFS();
void initCamera();
void initLittleFS();

void setup(){
  Serial.begin(115200);
  delay(1000); 
  
  Serial.println("\n\n=== ESP32-CAM Firebase Upload ===");
  Serial.println("=== OV2640 Module ===\n");
  
  // 1. Setup LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); 
  Serial.println("[SETUP] LED Pin configured");

  // 2. Create semaphore
  uploadSemaphore = xSemaphoreCreateBinary();
  if (uploadSemaphore == NULL) {
    Serial.println("[ERROR] Failed to create semaphore!");
    while(1);
  }
  xSemaphoreGive(uploadSemaphore);
  Serial.println("[SETUP] Semaphore created");

  // 3. Init LittleFS first
  Serial.println("[SETUP] Initializing LittleFS...");
  initLittleFS();
  
  // 4. CRITICAL: Power stabilization for OV2640
  Serial.println("[SETUP] Preparing camera power...");
  
  // Power cycle the camera module
  if (PWDN_GPIO_NUM != -1) {
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    digitalWrite(PWDN_GPIO_NUM, HIGH); // Power down
    delay(10);
    digitalWrite(PWDN_GPIO_NUM, LOW);  // Power up
    delay(10);
  }
  
  // Wait for OV2640 to stabilize (CRITICAL for I2C communication)
  Serial.println("[SETUP] Waiting for OV2640 stabilization (3 seconds)...");
  delay(3000); // OV2640 needs time to boot up
  
  // 5. Initialize Camera
  Serial.println("[SETUP] Initializing Camera...");
  initCamera();

  // 6. Setup WiFi
  Serial.println("[SETUP] Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int wifi_retry = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_retry < 30) {
    Serial.print(".");
    delay(500);
    wifi_retry++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[ERROR] WiFi connection failed!");
    ESP.restart();
  }
  
  Serial.println("\n[SETUP] WiFi connected!");
  Serial.print("[SETUP] IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("[SETUP] Signal strength: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");

  // 7. Init Firebase
  Serial.println("[SETUP] Initializing Firebase...");
  ssl_client.setInsecure();
  ssl_client.setHandshakeTimeout(15);

  initializeApp(aClient, app, getAuth(user_auth), processData, "authTask");
  app.getApp<Storage>(storage);
  
  Serial.println("[SETUP] Waiting for Firebase authentication...");
  unsigned long authStart = millis();
  while (!app.ready() && (millis() - authStart) < 30000) {
    app.loop();
    delay(100);
  }
  
  if (app.ready()) {
    Serial.println("[SETUP] Firebase authenticated successfully!");
  } else {
    Serial.println("[ERROR] Firebase authentication timeout!");
  }

  // 8. Create Tasks
  Serial.println("[SETUP] Creating tasks...");
  
  xTaskCreatePinnedToCore(
    cameraTask, 
    "CameraTask", 
    8192, 
    NULL, 
    1, 
    &cameraTaskHandle, 
    1  // Core 1
  );
  
  xTaskCreatePinnedToCore(
    firebaseTask, 
    "FirebaseTask", 
    20480, 
    NULL, 
    2, 
    &firebaseTaskHandle, 
    0  // Core 0
  );
  
  Serial.println("[SETUP] Setup complete! Starting operations...\n");
}

void loop(){
  vTaskDelay(pdMS_TO_TICKS(1000));
}

// ==========================================
// TASK 1: Camera Logic
// ==========================================
void cameraTask(void *pvParameters) {
  Serial.println("[CAM] Camera task started");
  
  unsigned long previousMillis = 0;
  const unsigned long interval = 10000; // 10 seconds for testing
  
  bool firstRun = true;

  while(1) {
    unsigned long currentMillis = millis();
    
    if ((currentMillis - previousMillis >= interval) || firstRun) {
      previousMillis = currentMillis;
      firstRun = false;

      if (!uploadInProgress) {
        Serial.println("\n[CAM] ===== Taking Photo =====");
        
        if (xSemaphoreTake(uploadSemaphore, pdMS_TO_TICKS(1000)) == pdTRUE) {
          
          capturePhotoSaveLittleFS();
          
          if (LittleFS.exists(FILE_PHOTO_PATH)) {
            File testFile = LittleFS.open(FILE_PHOTO_PATH, "r");
            if (testFile) {
              size_t fileSize = testFile.size();
              testFile.close();
              
              if (fileSize > 1000) {
                uploadFileName = STORAGE_PATH + String(millis()) + ".jpg";
                
                Serial.printf("[CAM] Photo ready: %d bytes\n", fileSize);
                Serial.printf("[CAM] Upload target: %s\n", uploadFileName.c_str());
                
                photoReadyToUpload = true;
                uploadInProgress = true;
              } else {
                Serial.println("[CAM] ERROR: File too small, retrying...");
                xSemaphoreGive(uploadSemaphore);
              }
            } else {
              Serial.println("[CAM] ERROR: Cannot open file");
              xSemaphoreGive(uploadSemaphore);
            }
          } else {
            Serial.println("[CAM] ERROR: File not found");
            xSemaphoreGive(uploadSemaphore);
          }
        } else {
          Serial.println("[CAM] Upload in progress, skipping...");
        }
      } else {
        Serial.println("[CAM] Previous upload not finished. Skipping.");
      }
    }
    
    if (uploadInProgress && (millis() % 10000 == 0)) {
      Serial.println("[CAM] Waiting for upload to complete...");
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ==========================================
// TASK 2: Firebase Logic
// ==========================================
void firebaseTask(void *pvParameters) {
  Serial.println("[FB] Firebase task started");
  
  while(1) {
    app.loop();
    
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[FB] WiFi disconnected! Reconnecting...");
      WiFi.reconnect();
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    if (app.ready() && photoReadyToUpload) {
      
      Serial.println("\n[FB] ===== Starting Upload =====");
      Serial.printf("[FB] Target: %s\n", uploadFileName.c_str());
      Serial.printf("[FB] Bucket: %s\n", STORAGE_BUCKET_ID);
      Serial.printf("[FB] Free heap: %d bytes\n", ESP.getFreeHeap());
      
      photoReadyToUpload = false;
      
      storage.upload(
        aClient, 
        FirebaseStorage::Parent(STORAGE_BUCKET_ID, uploadFileName), 
        getFile(media_file), 
        "image/jpeg",
        processData, 
        "uploadTask"
      );
      
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ==========================================
// Helper Functions
// ==========================================

void capturePhotoSaveLittleFS() {
  Serial.println("[CAM] Starting capture...");
  
  digitalWrite(LED_PIN, HIGH);
  vTaskDelay(pdMS_TO_TICKS(200));

  camera_fb_t* fb = NULL;
  
  // Buang frame pertama untuk OV2640
  Serial.println("[CAM] Disposing initial frames...");
  for (int i = 0; i < 4; i++) {
    fb = esp_camera_fb_get();
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
    
  Serial.println("[CAM] Capturing photo...");
  fb = esp_camera_fb_get();  
  
  digitalWrite(LED_PIN, LOW);

  if(!fb) {
    Serial.println("[CAM] ERROR: Camera capture failed!");
    return;
  }
  
  Serial.printf("[CAM] Capture OK: %d bytes, Format: %d\n", fb->len, fb->format);

  File file = LittleFS.open(FILE_PHOTO_PATH, FILE_WRITE);
  if (!file) {
    Serial.println("[CAM] ERROR: Failed to open file for writing");
    esp_camera_fb_return(fb);
    return;
  }
  
  size_t written = file.write(fb->buf, fb->len);
  file.close();
  
  if (written == fb->len) {
    Serial.printf("[CAM] ✓ Saved successfully: %s (%d bytes)\n", FILE_PHOTO_PATH, written);
  } else {
    Serial.printf("[CAM] ERROR: Write incomplete: %d/%d bytes\n", written, fb->len);
  }
  
  esp_camera_fb_return(fb);
}

void initCamera(){
  Serial.println("[CAM] Configuring OV2640 camera...");
  
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  
  // CRITICAL: Lower clock frequency for OV2640 stability
  config.xclk_freq_hz = 10000000; // 10MHz (was 20MHz)
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (psramFound()) {
    Serial.println("[CAM] PSRAM found, using high quality");
    config.frame_size = FRAMESIZE_UXGA; // 1600x1200
    config.jpeg_quality = 10;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    Serial.println("[CAM] PSRAM not found, using lower quality");
    config.frame_size = FRAMESIZE_SVGA; // 800x600
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }
  
  // Try initialization with retry logic
  esp_err_t err = ESP_FAIL;
  int retry_count = 0;
  const int max_retries = 5; // More retries for OV2640
  
  while (err != ESP_OK && retry_count < max_retries) {
    if (retry_count > 0) {
      Serial.printf("[CAM] Retry attempt %d/%d...\n", retry_count + 1, max_retries);
      
      // Power cycle for retry
      if (PWDN_GPIO_NUM != -1) {
        digitalWrite(PWDN_GPIO_NUM, HIGH);
        delay(100);
        digitalWrite(PWDN_GPIO_NUM, LOW);
        delay(500);
      } else {
        delay(1000);
      }
    }
    
    err = esp_camera_init(&config);
    
    if (err != ESP_OK) {
      Serial.printf("[CAM] Init failed with error 0x%x ", err);
      
      // Decode error
      switch(err) {
        case ESP_ERR_NOT_FOUND:
          Serial.println("(Camera not found - check I2C connections)");
          break;
        case ESP_ERR_TIMEOUT:
          Serial.println("(I2C timeout - check power supply)");
          break;
        case ESP_ERR_INVALID_STATE:
          Serial.println("(Invalid state)");
          break;
        default:
          Serial.println("(Unknown error)");
      }
      
      if (retry_count < max_retries - 1) {
        esp_camera_deinit();
      }
    }
    retry_count++;
  }
  
  if (err != ESP_OK) {
    Serial.printf("[CAM] ❌ FATAL: Camera init failed after %d attempts\n", max_retries);
    Serial.println("[CAM] Troubleshooting:");
    Serial.println("  1. Check power supply (needs stable 5V/2A)");
    Serial.println("  2. Verify camera ribbon cable is firmly connected");
    Serial.println("  3. Try a different ESP32-CAM board");
    Serial.println("  4. Measure voltage on 3.3V pin (should be 3.2-3.4V)");
    
    while(1) {
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PIN, LOW);
      delay(200);
    }
  }
  
  // OV2640 sensor settings
  sensor_t * s = esp_camera_sensor_get();
  if (s) {
    // These settings are optimized for OV2640
    s->set_brightness(s, 0);     // -2 to 2
    s->set_contrast(s, 0);       // -2 to 2
    s->set_saturation(s, 0);     // -2 to 2
    s->set_special_effect(s, 0); // 0 to 6 (0 - No Effect)
    s->set_whitebal(s, 1);       // 0 = disable , 1 = enable
    s->set_awb_gain(s, 1);       // 0 = disable , 1 = enable
    s->set_wb_mode(s, 0);        // 0 to 4
    s->set_exposure_ctrl(s, 1);  // 0 = disable , 1 = enable
    s->set_aec2(s, 0);           // 0 = disable , 1 = enable
    s->set_gain_ctrl(s, 1);      // 0 = disable , 1 = enable
    s->set_agc_gain(s, 0);       // 0 to 30
    s->set_gainceiling(s, (gainceiling_t)0);  // 0 to 6
    s->set_bpc(s, 0);            // 0 = disable , 1 = enable
    s->set_wpc(s, 1);            // 0 = disable , 1 = enable
    s->set_raw_gma(s, 1);        // 0 = disable , 1 = enable
    s->set_lenc(s, 1);           // 0 = disable , 1 = enable
    s->set_hmirror(s, 0);        // 0 = disable , 1 = enable
    s->set_vflip(s, 0);          // 0 = disable , 1 = enable
    s->set_dcw(s, 1);            // 0 = disable , 1 = enable
    s->set_colorbar(s, 0);       // 0 = disable , 1 = enable
    
    Serial.println("[CAM] OV2640 sensor settings applied");
  }
  
  Serial.println("[CAM] ✓ OV2640 initialized successfully");
}

void initLittleFS(){
  if (!LittleFS.begin(true)) {
    Serial.println("[FS] ERROR: Mount failed!");
    ESP.restart();
  }
  
  Serial.println("[FS] ✓ LittleFS mounted");
  Serial.printf("[FS] Total: %d bytes\n", LittleFS.totalBytes());
  Serial.printf("[FS] Used: %d bytes\n", LittleFS.usedBytes());
}

void processData(AsyncResult &aResult) {
  if (!aResult.isResult()) return;

  if (aResult.isEvent()) {
    Serial.printf("[FB] Event: %s\n", aResult.appEvent().message().c_str());
  }
  
  if (aResult.isError()) {
    Serial.printf("[FB] ❌ ERROR: %s (Code: %d)\n", 
                  aResult.error().message().c_str(), 
                  aResult.error().code());
    
    uploadInProgress = false;
    xSemaphoreGive(uploadSemaphore);
  }
  
  if (aResult.uploadProgress()) {
    int progress = aResult.uploadInfo().progress;
    Serial.printf("[FB] Upload progress: %d%%\n", progress);
    
    if (aResult.uploadInfo().total == aResult.uploadInfo().uploaded) {
      Serial.println("[FB] ✅ Upload Complete!");
      Serial.println("[FB] Check Firebase Console for download URL");
      
      uploadInProgress = false;
      xSemaphoreGive(uploadSemaphore);
    }
  }
}

void file_operation_callback(File &file, const char *filename, file_operating_mode mode){
  switch (mode) {
    case file_mode_open_read:
      myFile = LittleFS.open(filename, "r");
      Serial.printf("[FS] Opening for read: %s\n", filename);
      break;
    case file_mode_open_write:
      myFile = LittleFS.open(filename, "w");
      Serial.printf("[FS] Opening for write: %s\n", filename);
      break;
    case file_mode_open_append:
      myFile = LittleFS.open(filename, "a");
      Serial.printf("[FS] Opening for append: %s\n", filename);
      break;
    case file_mode_remove:
      LittleFS.remove(filename);
      Serial.printf("[FS] Removing: %s\n", filename);
      break;
    default: 
      break;
  }
  file = myFile;
}