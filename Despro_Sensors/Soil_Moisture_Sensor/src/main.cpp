#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <time.h>

#include "secrets.h" // Make the secrets file

#define WIFI_SSID SSID_WIFI
#define WIFI_PASSWORD PASSWORD_WIFI

#define API_KEY KEY_API
#define DATABASE_URL URL_DATABASE
#define USER_EMAIL EMAIL_USER
#define USER_PASSWORD PASSWORD_USER

// Pin definitions
#define SOIL_PIN1 32
#define SOIL_PIN2 33
#define SOIL_PIN3 34

// Firebase components
FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
RealtimeDatabase Database;

// Struct for passing parameters to tasks
struct SoilParams {
  int pin;          // Sensor Pin
  int* reading;     // Sensor Reading Value
  int valMin;       // Dry value (0%)
  int valMax;       // Wet value (100%)
  const char* sensorName; // Sensor Name
};

// Authentication
UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);

// Task handles
TaskHandle_t soil1_TaskHandle;
TaskHandle_t soil2_TaskHandle;
TaskHandle_t soil3_TaskHandle;
TaskHandle_t firebaseLoopTaskHandle;
TaskHandle_t firebaseSendTaskHandle;

// Mutex for protecting sensor data
SemaphoreHandle_t sensorDataMutex;

// Variables
int soil_moist1, soil_moist2, soil_moist3;
String uid;
String databasePath;
int sendFirebase = 15000;

// Functions Declaration
void readSoilHumid(void *pvParameters);
void firebaseLoopTask(void *pvParameters);
void firebaseSendTask(void *pvParameters);
void processData(AsyncResult &aResult);

// Parameters for each Soil sensor task
SoilParams p_soil1 = {SOIL_PIN1, &soil_moist1, 35, 163, "Soil Sensor 1"};
SoilParams p_soil2 = {SOIL_PIN2, &soil_moist2, 35, 163, "Soil Sensor 2"};
SoilParams p_soil3 = {SOIL_PIN3, &soil_moist3, 35, 163, "Soil Sensor 3"};

void setup(){
  Serial.begin(115200);

  // Pin setup
  pinMode(SOIL_PIN1, INPUT);
  pinMode(SOIL_PIN2, INPUT);
  pinMode(SOIL_PIN3, INPUT);
  
  // Create mutex before starting tasks
  sensorDataMutex = xSemaphoreCreateMutex();
  if (sensorDataMutex == NULL) {
    Serial.println("Failed to create mutex!");
    while(1);
  }

  // Connect to Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Setup Time (NTP)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Waiting for NTP time");
  while (time(nullptr) < 100000) {
    Serial.print(".");
    delay(100);
  }
  Serial.println("\nTime synchronized");

  ssl_client.setInsecure();
  ssl_client.setHandshakeTimeout(5);

  // Initialize Firebase
  initializeApp(aClient, app, getAuth(user_auth), processData, "🔐 authTask");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);

  // Create tasks for reading Soil sensors
  xTaskCreatePinnedToCore(readSoilHumid, "Task Soil 1", 3072, &p_soil1, 1, &soil1_TaskHandle, 1);
  xTaskCreatePinnedToCore(readSoilHumid, "Task Soil 2", 3072, &p_soil2, 1, &soil2_TaskHandle, 1);
  xTaskCreatePinnedToCore(readSoilHumid, "Task Soil 3", 3072, &p_soil3, 1, &soil3_TaskHandle, 1);
  
  // Create Firebase loop task (maintains connection) - Increased stack size
  xTaskCreatePinnedToCore(firebaseLoopTask, "Firebase Loop", 12000, NULL, 2, &firebaseLoopTaskHandle, 0);
  
  // Create Firebase send task (sends data every 8 seconds) - Increased stack size
  xTaskCreatePinnedToCore(firebaseSendTask, "Firebase Send", 12000, NULL, 1, &firebaseSendTaskHandle, 0);
}

void loop(){}

// Task: read a soil sensor, map values, and update shared variables safely
void readSoilHumid(void *pvParameters) {
  SoilParams* params = (SoilParams*)pvParameters;

  while (1) {
    int raw = analogRead(params->pin);
    int moisture = map(raw, params->valMin, params->valMax, 0, 100);
    moisture = constrain(moisture, 0, 100);

    // Lock mutex before updating shared variables
    if(xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE) {
      *(params->reading) = moisture;
      xSemaphoreGive(sensorDataMutex); // Unlock mutex
    }

    Serial.print(params->sensorName);
    Serial.print(": ");
    Serial.print(moisture);
    Serial.println(" %");

    vTaskDelay(pdMS_TO_TICKS(2000)); // Delay for 2 seconds
  }
}

// Maintains Firebase authentication and handles async operations
void firebaseLoopTask(void *pvParameters) {
  Serial.println("Firebase Loop Task started");
  
  while(1) {
    app.loop(); // Keep Firebase connection alive
    vTaskDelay(pdMS_TO_TICKS(10)); // Short delay to prevent watchdog timeout
  }
}

// Sends all sensor data to Firebase every 8 seconds
void firebaseSendTask(void *pvParameters) {
  Serial.println("Firebase Send Task started");
  
  while(1) {
    if (app.ready()) {
      int soil1, soil2, soil3;
      
      if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE) {
        soil1 = soil_moist1; 
        soil2 = soil_moist2; 
        soil3 = soil_moist3;
        xSemaphoreGive(sensorDataMutex); 
      }
      
      Serial.println("Sending Data...");
      
      unsigned long timestamp = getEpochTime();
      String timestampStr = String(timestamp);
      
      String latestPath = "/latest/session_001";
      String logPath = "/sensor_logs/session_001/" + timestampStr;
      
      // Sensor 1
      Database.set<int>(aClient, latestPath + "/sensor1/moisture", soil1, processData, "L_S1");
      Database.set<int>(aClient, logPath + "/sensor1/moisture", soil1, processData, "Log_S1");

      // Sensor 2
      Database.set<int>(aClient, latestPath + "/sensor2/moisture", soil2, processData, "L_S2");
      Database.set<int>(aClient, logPath + "/sensor2/moisture", soil2, processData, "Log_S2");
      // Sensor 3
      Database.set<int>(aClient, latestPath + "/sensor3/moisture", soil3, processData, "L_S3");
      Database.set<int>(aClient, logPath + "/sensor3/moisture", soil3, processData, "Log_S3");
      
      Database.set<int>(aClient, latestPath + "/timestamp", timestamp, processData, "Time");
      Database.set<int>(aClient, logPath + "/timestamp", timestamp, processData, "Time");
      
      Serial.println("Data sent!");
    
    }
    vTaskDelay(pdMS_TO_TICKS(sendFirebase)); 
  }
}
// Callback function for Firebase operations
void processData(AsyncResult &aResult) {
  if (!aResult.isResult())
    return;
    
  if (aResult.isEvent())
    Firebase.printf("Event task: %s, msg: %s, code: %d\n", 
                    aResult.uid().c_str(), 
                    aResult.eventLog().message().c_str(), 
                    aResult.eventLog().code());

  if (aResult.isError())
    Firebase.printf("Error task: %s, msg: %s, code: %d\n", 
                    aResult.uid().c_str(), 
                    aResult.error().message().c_str(), 
                    aResult.error().code());
}

unsigned long getEpochTime() {
  time_t now;
  time(&now);
  return now;
}