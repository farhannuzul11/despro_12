#define ENABLE_USER_AUTH
#define ENABLE_STORAGE
#define ENABLE_FS

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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
// Folder di Firebase Storage (sesuai screenshot Anda sebelumnya)
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

// Flags for communication between tasks
volatile bool photoReadyToUpload = false;
String uploadFileName = "";

// User functions
void processData(AsyncResult &aResult);
void file_operation_callback(File &file, const char *filename, file_operating_mode mode);

FileConfig media_file(FILE_PHOTO_PATH, file_operation_callback); 
File myFile;

// Camera Pins (AI-Thinker)
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
  
  // 1. Setup LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); 

  // 2. Setup WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi connected!");

  // 3. Init Hardware
  initLittleFS();
  initCamera();

  // 4. Init Firebase
  ssl_client.setInsecure();
  ssl_client.setHandshakeTimeout(10); // Increase timeout for heavy operations

  initializeApp(aClient, app, getAuth(user_auth), processData, "🔐 authTask");
  app.getApp<Storage>(storage);

  // 5. Create Tasks
  
  // Task Kamera: Menangani timer 1 jam & ambil foto
  // Stack 4096 cukup untuk sekadar ambil foto
  xTaskCreatePinnedToCore(cameraTask, "Camera Task", 4096, NULL, 1, &cameraTaskHandle, 1);
  
  // Task Firebase: Menangani Upload & Keep Alive
  // PENTING: Stack 16384 (16KB) agar upload gambar TIDAK CRASH (Stack Overflow)
  xTaskCreatePinnedToCore(firebaseTask, "Firebase Task", 16384, NULL, 2, &firebaseTaskHandle, 0);
}

void loop(){} // Loop kosong, semua di handle Task

// ==========================================
// TASK 1: Camera Logic (Timer & Capture)
// ==========================================
void cameraTask(void *pvParameters) {
  // Timer variables
  unsigned long previousMillis = 0;
  // Interval 1 Jam = 3600000 ms. 
  // Untuk tes, ubah jadi 60000 (1 menit)
  const unsigned long interval = 3600000; 
  
  // Ambil foto pertama kali saat nyala (optional)
  bool firstRun = true;

  while(1) {
    unsigned long currentMillis = millis();
    
    // Cek apakah sudah waktunya ambil foto
    if ((currentMillis - previousMillis >= interval) || firstRun) {
      previousMillis = currentMillis;
      firstRun = false;

      // Cek apakah upload sebelumnya sudah selesai
      if (!photoReadyToUpload) {
        Serial.println("[CAM] Time to take photo...");
        capturePhotoSaveLittleFS();
        
        // Generate nama file unik berdasarkan waktu
        uploadFileName = STORAGE_PATH + String(millis()) + ".jpg";
        
        // Beri sinyal ke Task Firebase untuk upload
        photoReadyToUpload = true; 
      } else {
        Serial.println("[CAM] Previous upload not finished yet. Skipping.");
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000)); // Cek setiap 1 detik
  }
}

// ==========================================
// TASK 2: Firebase Logic (Upload & Auth)
// ==========================================
void firebaseTask(void *pvParameters) {
  while(1) {
    // 1. Jaga koneksi tetap hidup
    app.loop();

    // 2. Cek apakah ada request upload dari Task Kamera
    if (app.ready() && photoReadyToUpload) {
      
      Serial.print("[FB] Uploading to: ");
      Serial.println(uploadFileName);

      // Lakukan Upload Asynchronous
      storage.upload(aClient, 
                     FirebaseStorage::Parent(STORAGE_BUCKET_ID, uploadFileName), 
                     getFile(media_file), 
                     "image/jpg", 
                     processData, 
                     "⬆️ uploadTask");
      
      // Reset flag, tunggu sampai proses selesai di callback atau anggap selesai setelah perintah dikirim
      // Note: Di library async, 'processData' akan menangani statusnya. 
      // Kita set false di sini agar Task Kamera tahu perintah sudah dikirim.
      photoReadyToUpload = false; 
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // Delay kecil untuk watchdog
  }
}

// ==========================================
// Helper Functions
// ==========================================

void capturePhotoSaveLittleFS( void ) {
  // Turn LED ON
  digitalWrite(LED_PIN, HIGH);
  vTaskDelay(pdMS_TO_TICKS(150)); // Allow sensor to adjust

  camera_fb_t* fb = NULL;
  // Dispose first few frames
  for (int i = 0; i < 4; i++) {
    fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = NULL;
  }
    
  // Take actual photo
  fb = esp_camera_fb_get();  
  
  // Turn LED OFF
  digitalWrite(LED_PIN, LOW);

  if(!fb) {
    Serial.println("Camera capture failed");
    return; // Jangan restart, coba lagi nanti
  }  

  File file = LittleFS.open(FILE_PHOTO_PATH, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file in writing mode");
  }
  else {
    file.write(fb->buf, fb->len); 
    Serial.printf("Saved: %s, Size: %d bytes\n", FILE_PHOTO_PATH, fb->len);
  }
  file.close();
  esp_camera_fb_return(fb);
}

void initCamera(){
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
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA; // Resolusi tinggi
    config.jpeg_quality = 10;
    config.fb_count = 1;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed 0x%x", err);
  }
}

void initLittleFS(){
  if (!LittleFS.begin(true)) {
    Serial.println("Mount LittleFS Failed");
  } else {
    Serial.println("LittleFS Mounted");
  }
}

void processData(AsyncResult &aResult) {
  if (!aResult.isResult()) return;

  if (aResult.isEvent()) {
      Serial.printf("Event: %s\n", aResult.appEvent().message().c_str());
  }
  if (aResult.isError()) {
      Serial.printf("Error: %s, Code: %d\n", aResult.error().message().c_str(), aResult.error().code());
  }
  if (aResult.uploadProgress()) {
      Serial.printf("Uploaded: %d%s\n", aResult.uploadInfo().progress, "%");
      if (aResult.uploadInfo().total == aResult.uploadInfo().uploaded) {
          Serial.println("Upload Complete! ✅");
      }
  }
}

void file_operation_callback(File &file, const char *filename, file_operating_mode mode){
    switch (mode) {
    case file_mode_open_read:
        myFile = LittleFS.open(filename, "r");
        break;
    case file_mode_open_write:
        myFile = LittleFS.open(filename, "w");
        break;
    case file_mode_open_append:
        myFile = LittleFS.open(filename, "a");
        break;
    case file_mode_remove:
        LittleFS.remove(filename);
        break;
    default: break;
    }
    file = myFile;
}