#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>

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
int soil_moist1 = 0, soil_moist2 = 0, soil_moist3 = 0;
String uid;
String databasePath;

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
    // Wait for Firebase to be ready
    if (app.ready()) {
      
      // Get User UID (only once or when changed)
      if (uid.isEmpty()) {
        uid = app.getUid().c_str();
        Serial.printf("User UID: %s\n", uid.c_str());
        databasePath = "UsersData/" + uid;
      }
      
      // Lock mutex to read all sensor data safely
      if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE) {
        // Copy sensor data to local variables
        int m1 = soil_moist1;
        int m2 = soil_moist2;
        int m3 = soil_moist3;
        xSemaphoreGive(sensorDataMutex); // Release mutex immediately
        
        // Send data to Firebase
        Serial.println("Sending data to Firebase...");
        
        // Soil Sensor 1 data
        String soil1Path = databasePath + "/soil_sensor1/moisture";
        Database.set<int>(aClient, soil1Path, m1, processData, "Soil1_Data");

        // Soil Sensor 2 data
        String soil2Path = databasePath + "/soil_sensor2/moisture";
        Database.set<int>(aClient, soil2Path, m2, processData, "Soil2_Data");

        // Soil Sensor 3 data
        String soil3Path = databasePath + "/soil_sensor3/moisture";
        Database.set<int>(aClient, soil3Path, m3, processData, "Soil3_Data");
        
        Serial.println("Data sent successfully!");
      }
    } else {
      Serial.println("Waiting for Firebase authentication...");
    }
    
    vTaskDelay(pdMS_TO_TICKS(8000)); // Send every 8 seconds
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