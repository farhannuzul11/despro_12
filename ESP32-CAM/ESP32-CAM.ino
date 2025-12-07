#define ENABLE_USER_AUTH
#define ENABLE_STORAGE
#define ENABLE_FS

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <FS.h>
#include <LittleFS.h>
#include "esp_camera.h"

#include "secrets.h" // Make the secrets.h file

#define WIFI_SSID SSID_WIFI
#define WIFI_PASSWORD PASSWORD_WIFI

#define API_KEY KEY_API
#define USER_EMAIL EMAIL_USER
#define USER_PASSWORD PASSWORD_USER
#define STORAGE_BUCKET_ID ID_BUCKET

// Photo File Configuration
#define FILE_PHOTO_PATH "/photo.jpg"
#define STORAGE_PATH "/images/session_001/"

// LED Flash Pin
#define LED_PIN 4

// Firebase components
FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
Storage storage;

// Authentication
UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD, 3000);

// Status Flags
bool uploadInProgress = false;
String uploadFileName = "";

// Timer Variables
unsigned long lastCaptureTime = 0;
const unsigned long captureInterval = 30000; 

// Functions declarations
void processData(AsyncResult &aResult);
void file_operation_callback(File &file, const char *filename, file_operating_mode mode);
bool capturePhotoSaveLittleFS();
void initCamera();
void initLittleFS();

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

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== ESP32-CAM ===");

  // Setup LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // File System Initialization
  initLittleFS();

  // Camera Initialization and Power Cycle
  pinMode(PWDN_GPIO_NUM, OUTPUT);
  digitalWrite(PWDN_GPIO_NUM, HIGH); delay(10);
  digitalWrite(PWDN_GPIO_NUM, LOW); delay(10);
  delay(1000);
  
  initCamera();

  // Connect WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("IP: "); Serial.println(WiFi.localIP());

  // Firebase Initialization
  ssl_client.setInsecure();
  ssl_client.setHandshakeTimeout(15);

  initializeApp(aClient, app, getAuth(user_auth), processData, "authTask");
  app.getApp<Storage>(storage);
  
  Serial.println("Waiting for Firebase Auth...");
  while (!app.ready()) {
    app.loop();
    delay(100);
  }
  Serial.println("Firebase Ready!");
}

void loop() {
  app.loop();

  unsigned long currentMillis = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi Lost. Reconnecting...");
    WiFi.reconnect();
    return;
  }

  // Check if:
  // a. Capture interval reached (OR first startup/lastCaptureTime=0)
  // b. Firebase is ready (authenticated)
  // c. Not currently uploading previous photo
  if ((currentMillis - lastCaptureTime >= captureInterval || lastCaptureTime == 0) 
      && app.ready() 
      && !uploadInProgress) {
    
    // Reset timer
    lastCaptureTime = currentMillis;

    Serial.println("\n--- Starting Sequence ---");

    // Take photo & Save
    if (capturePhotoSaveLittleFS()) {
      
      // Prepare unique filename
      uploadFileName = STORAGE_PATH + String(millis()) + ".jpg";
      
      Serial.printf("Uploading to: %s\n", uploadFileName.c_str());

      // Start upload (Asynchronous)
      storage.upload(
        aClient, 
        FirebaseStorage::Parent(STORAGE_BUCKET_ID, uploadFileName), 
        getFile(media_file), 
        "image/jpeg",
        processData, 
        "uploadTask"
      );

      // Set flag to prevent taking photos again until upload completes
      uploadInProgress = true; 
    }
  }
}

// Process Data Callback
void processData(AsyncResult &aResult) {
  if (aResult.isEvent()) {
    Serial.printf("[Event] %s\n", aResult.appEvent().message().c_str());
  }

  if (aResult.isError()) {
    Serial.printf("[Error] %s (Code: %d)\n", aResult.error().message().c_str(), aResult.error().code());
    uploadInProgress = false; 
  }

  if (aResult.uploadProgress()) {
    Serial.printf("[Progress] %d%%\n", aResult.uploadInfo().progress);

    if (aResult.uploadInfo().total == aResult.uploadInfo().uploaded) {
      Serial.println("[Done] Upload Complete!");
      uploadInProgress = false; 
    }
  }
}

// Capture Photo and Save to LittleFS
bool capturePhotoSaveLittleFS() {
  Serial.println("[Cam] Taking photo...");
  
  digitalWrite(LED_PIN, HIGH);
  delay(150);

  camera_fb_t* fb = NULL;
  
  // Discard initial frames (optional, for better quality)
  for(int i=0; i<3; i++){
    fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    delay(10);
  }

  // Capture actual photo
  fb = esp_camera_fb_get();
  digitalWrite(LED_PIN, LOW); // Flash OFF

  if (!fb) {
    Serial.println("[Cam] Capture failed!");
    return false;
  }

  Serial.printf("[Cam] Captured %d bytes\n", fb->len);

  // Save to LittleFS
  File file = LittleFS.open(FILE_PHOTO_PATH, FILE_WRITE);
  if (!file) {
    Serial.println("[FS] Failed to open file for writing");
    esp_camera_fb_return(fb);
    return false;
  }

  file.write(fb->buf, fb->len);
  file.close();
  
  esp_camera_fb_return(fb);
  Serial.println("[FS] Photo saved to LittleFS");
  return true;
}

// LittleFS Initialization
void initLittleFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed, restarting...");
    ESP.restart();
  }
  Serial.println("LittleFS Mounted");
}

void initCamera() {
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
  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera Init Failed 0x%x\n", err);
    ESP.restart();
  }
  Serial.println("Camera Initialized");
}

// Function Callback for File Operations
void file_operation_callback(File &file, const char *filename, file_operating_mode mode) {
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