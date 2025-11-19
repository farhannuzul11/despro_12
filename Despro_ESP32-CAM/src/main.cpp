#define ENABLE_USER_AUTH
#define ENABLE_STORAGE
#define ENABLE_FS

#include <Arduino.h>
#include <FirebaseClient.h>
#include <FS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_camera.h"

#define WIFI_SSID "Kura"
#define WIFI_PASSWORD "Supacabra12"

#define FIREBASE_API "AIzaSyDJ_vVRBcNu9mBHeJY9L42rjH20IlxTLgw"
#define FIREBASE_EMAIL "REPLACE_WITH_FIREBASE_PROJECT_EMAIL_USER"
#define FIREBASE_PASSWORD "REPLACE_WITH_FIREBASE_PROJECT_USER_PASS"
#define FIREBASE_BUCKET "compost-box.firebasestorage.app"

// OV2640 camera module pins (CAMERA_MODEL_AI_THINKER)
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

// Photo path in filesystem and Firebase bucket
#define FILE_PATH "/photo.jpg"
#define BUCKET_PATH "/data/photo.jpg"

// Firebase components
FirebaseApp app;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
WiFiClientSecure ssl_client;
Storage storage;

// Authentication
UserAuth user_auth(FIREBASE_API, FIREBASE_EMAIL, FIREBASE_PASSWORD, 3000);

FileConfig media_file(FILE_PATH, file_operation_callback);
File myFile;

AsyncResult storageResult;

// Variables
int lastCaptureTime = 0;
int count = 0;
bool isUploading = false;

void initCamera();
void captureSaveLittleFS();
void processData(AsyncResult &aResult);
void file_operation_callback(File &file, const char *filename, file_operating_mode mode);

void setup(){
  Serial.begin(115200);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  
  while(!LittleFS.begin(true)){
    Serial.println("LittleFS mount failed");
    ESP.restart();
  }
  delay(1000);
  Serial.println("LittleFS mounted successfully");

  initCamera();
  
  // Configure SSL client
  ssl_client.setInsecure();
  ssl_client.setTimeout(1000);
  ssl_client.setHandshakeTimeout(5);
  Serial.println("Initializing app...");
  initializeApp(aClient, app, getAuth(user_auth), processData, "🔐 authTask");
  
  app.getApp<Storage>(storage);
  
  Serial.println("System ready! Will capture photo every 3 seconds...");
}

void loop(){
  // To maintain the authentication process.
  app.loop();
  
  int currentTime = millis();
  
  // Take picture every 3 seconds and upload
  if (app.ready() && !isUploading && (currentTime - lastCaptureTime >= 3000)){
    lastCaptureTime = currentTime;
    count++;
    
    Serial.printf("\n📸 Capturing photo #%d...\n", count);
    
    captureSaveLittleFS();
    
    Serial.printf("⬆️  Uploading to: %s (akan menimpa foto sebelumnya)\n", BUCKET_PATH);
    
    isUploading = true;
    
    // Upload ke Firebase - selalu ke path yang sama (menimpa foto lama)
    storage.upload(aClient, FirebaseStorage::Parent(FIREBASE_BUCKET, BUCKET_PATH), getFile(media_file), "image/jpg", processData, "⬆️  uploadTask");
  }
}

void initCamera(){
  // OV2640 camera module
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
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 1;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera failed: 0x%x", err);
    ESP.restart();
  }
  Serial.println("Camera init success");
}

// Capture Photo and Save it to LittleFS
void captureSaveLittleFS() {
  camera_fb_t* fb = NULL;
  
  // Take a new photo
  fb = esp_camera_fb_get();  
  if(!fb) {
    Serial.println("Camera capture failed");
    return;
  }  

  // Photo file name
  Serial.printf("Picture file name: %s\n", FILE_PATH);
  File file = LittleFS.open(FILE_PATH, FILE_WRITE);

  // Insert the data in the photo file
  if (!file) {
    Serial.println("Failed to open file in writing mode");
  }
  else {
    file.write(fb->buf, fb->len);
    Serial.print("The picture has been saved in ");
    Serial.print(FILE_PATH);
    Serial.print(" - Size: ");
    Serial.print(fb->len);
    Serial.println(" bytes");
  }
  // Close the file
  file.close();
  esp_camera_fb_return(fb);
}

void processData(AsyncResult &aResult)
{
    // Exits when no result available when calling from the loop.
    if (!aResult.isResult()){
        return;
    }

    if (aResult.isEvent()){
        Firebase.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.appEvent().message().c_str(), aResult.appEvent().code());
    }

    if (aResult.isDebug()){
        Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());
    }

    if (aResult.isError()){
        Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());
        isUploading = false; // Reset flag kalau error
    }

    if (aResult.uploadProgress()){
        Firebase.printf("Uploaded, task: %s, %d%s (%d of %d)\n", aResult.uid().c_str(), aResult.uploadInfo().progress, "%", aResult.uploadInfo().uploaded, aResult.uploadInfo().total);
        if (aResult.uploadInfo().total == aResult.uploadInfo().uploaded)
        {
            Firebase.printf("Upload task: %s, complete!✅️\n", aResult.uid().c_str());
            Serial.print("Download URL: ");
            Serial.println(aResult.uploadInfo().downloadUrl);
            isUploading = false; // Reset flag setelah upload selesai
        }
    }
}

void file_operation_callback(File &file, const char *filename, file_operating_mode mode){
    switch (mode)    {
    case file_mode_open_read:
        myFile = LittleFS.open(filename, "r");
        if (!myFile || !myFile.available()) {
            Serial.println("[ERROR] Failed to open file for reading");
        }
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
    default:
        break;
    }
    file = myFile;
}