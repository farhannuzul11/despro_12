#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <MQ135.h>

#include "secrets.h" // Make the secrets file

#define WIFI_SSID SSID_WIFI
#define WIFI_PASSWORD PASSWORD_WIFI

#define API_KEY KEY_API
#define DATABASE_URL URL_DATABASE
#define USER_EMAIL EMAIL_USER
#define USER_PASSWORD PASSWORD_USER

// Pins definition for MQ-4 and MQ-135 sensors
#define MQ4_PIN1   2  // ADC2_0
#define MQ4_PIN2   4  // ADC2_2
#define MQ4_PIN3   15 // ADC2_3

#define MQ135_PIN1 12 // ADC1_CH7
#define MQ135_PIN2 13 // ADC1_CH0
#define MQ135_PIN3 14 // ADC1_CH3

// Firebase components
FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
RealtimeDatabase Database;

// Authentication
UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);

// Task Handles
TaskHandle_t mq4_1_TaskHandle, mq4_2_TaskHandle, mq4_3_TaskHandle;
TaskHandle_t mq135_1_TaskHandle, mq135_2_TaskHandle, mq135_3_TaskHandle;
TaskHandle_t firebaseLoopTaskHandle;
TaskHandle_t firebaseSendTaskHandle;

// Mutex for protecting sensor data
SemaphoreHandle_t sensorDataMutex;

// Struct for MQ-4 sensors (analog reading)
struct MQ4SensorParams{
  int pin;                  // ADC Pin
  int* valueOut;            // Value Output
  const char* sensorName;   // Sensor name
};

// Struct for MQ-135 sensors (using library)
struct MQ135SensorParams{
  MQ135* gasSensor;         // MQ135 object pointer
  float* rzeroOut;          // RZero value output
  float* ppmOut;            // PPM value output
  const char* sensorName;   // Sensor name
};

// Variables for MQ-4 (analog values)
int mq4_value1, mq4_value2, mq4_value3;

// Variables for MQ-135 (RZero and PPM values)
float mq135_rzero1, mq135_rzero2, mq135_rzero3;
float mq135_ppm1, mq135_ppm2, mq135_ppm3;

// MQ135 sensor objects
MQ135 mq135_1(MQ135_PIN1);
MQ135 mq135_2(MQ135_PIN2);
MQ135 mq135_3(MQ135_PIN3);

String uid;
String databasePath;

// MQ-4 (Methane) Sensor Parameters
MQ4SensorParams p_mq4_1 = {MQ4_PIN1, &mq4_value1, "MQ-4 (1)"};
MQ4SensorParams p_mq4_2 = {MQ4_PIN2, &mq4_value2, "MQ-4 (2)"};
MQ4SensorParams p_mq4_3 = {MQ4_PIN3, &mq4_value3, "MQ-4 (3)"};

// MQ-135 (Air Quality) Sensor Parameters
MQ135SensorParams p_mq135_1 = {&mq135_1, &mq135_rzero1, &mq135_ppm1, "MQ-135 (1)"};
MQ135SensorParams p_mq135_2 = {&mq135_2, &mq135_rzero2, &mq135_ppm2, "MQ-135 (2)"};
MQ135SensorParams p_mq135_3 = {&mq135_3, &mq135_rzero3, &mq135_ppm3, "MQ-135 (3)"};

// Functions Declaration
void readMQ4Sensor(void *pvParameters);
void readMQ135Sensor(void *pvParameters);
void firebaseLoopTask(void *pvParameters);
void firebaseSendTask(void *pvParameters);
void processData(AsyncResult &aResult);

void setup(){
  Serial.begin(115200);

  // Create mutex before starting tasks
  sensorDataMutex = xSemaphoreCreateMutex();
  if (sensorDataMutex == NULL) {
    Serial.println("Failed to create mutex!");
    while(1);
  }

  pinMode(MQ4_PIN1, INPUT_PULLDOWN);
  pinMode(MQ4_PIN2, INPUT_PULLDOWN);
  pinMode(MQ4_PIN3, INPUT_PULLDOWN);

  pinMode(MQ135_PIN1, INPUT_PULLDOWN);
  pinMode(MQ135_PIN2, INPUT_PULLDOWN);
  pinMode(MQ135_PIN3, INPUT_PULLDOWN);

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

  // Create tasks for reading MQ-4 sensors (analog)
  xTaskCreatePinnedToCore(readMQ4Sensor, "Task MQ-4 (1)", 4096, &p_mq4_1, 1, &mq4_1_TaskHandle, 1);
  xTaskCreatePinnedToCore(readMQ4Sensor, "Task MQ-4 (2)", 4096, &p_mq4_2, 1, &mq4_2_TaskHandle, 1);
  xTaskCreatePinnedToCore(readMQ4Sensor, "Task MQ-4 (3)", 4096, &p_mq4_3, 1, &mq4_3_TaskHandle, 1);

  // Create tasks for reading MQ-135 sensors (using library)
  xTaskCreatePinnedToCore(readMQ135Sensor, "Task MQ-135 (1)", 4096, &p_mq135_1, 1, &mq135_1_TaskHandle, 1);
  xTaskCreatePinnedToCore(readMQ135Sensor, "Task MQ-135 (2)", 4096, &p_mq135_2, 1, &mq135_2_TaskHandle, 1);
  xTaskCreatePinnedToCore(readMQ135Sensor, "Task MQ-135 (3)", 4096, &p_mq135_3, 1, &mq135_3_TaskHandle, 1);

  // Create Firebase loop task (maintains connection)
  xTaskCreatePinnedToCore(firebaseLoopTask, "Firebase Loop", 4096, NULL, 2, &firebaseLoopTaskHandle, 0);
  
  // Create Firebase send task (sends data every 8 seconds)
  xTaskCreatePinnedToCore(firebaseSendTask, "Firebase Send", 8192, NULL, 1, &firebaseSendTaskHandle, 0);
}

void loop(){
}

// TASK: Read MQ-4 Sensor (analog reading)
// Runs every 2 seconds, updates shared variables with mutex protection
void readMQ4Sensor(void *pvParameters){
  MQ4SensorParams* params = (MQ4SensorParams*)pvParameters;

  while (1){
    int temp = analogRead(params->pin);

    // Lock mutex before updating shared variables
    if(xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE) {
      *(params->valueOut) = temp;
      xSemaphoreGive(sensorDataMutex); // Unlock mutex
    }

    // Print the reading to the Serial Monitor
    Serial.println(String(params->sensorName) + " reading: " + String(temp));

    vTaskDelay(pdMS_TO_TICKS(2000)); // Delay for 2 seconds
  }
}

// TASK: Read MQ-135 Sensor
void readMQ135Sensor(void *pvParameters){
  MQ135SensorParams* params = (MQ135SensorParams*)pvParameters;

  while (1){
    float rzero = params->gasSensor->getRZero();
    float ppm = params->gasSensor->getPPM();

    // Lock mutex before updating shared variables
    if(xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE) {
      *(params->rzeroOut) = rzero;
      *(params->ppmOut) = ppm;
      xSemaphoreGive(sensorDataMutex); // Unlock mutex
    }

    Serial.println(String(params->sensorName) + " RZero: " + String(rzero));
    Serial.println(String(params->sensorName) + " PPM: " + String(ppm));

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
        Firebase.printf("User UID: %s\n", uid.c_str());
        databasePath = "UsersData/" + uid;
      }
      
      // Lock mutex to read all sensor data safely
      if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE) {
        // Copy MQ-4 sensor data to local variables
        int mq4_1 = mq4_value1;
        int mq4_2 = mq4_value2;
        int mq4_3 = mq4_value3;
        
        // Copy MQ-135 sensor data to local variables (only ppm needed)
        float mq135_p1 = mq135_ppm1;
        float mq135_p2 = mq135_ppm2;
        float mq135_p3 = mq135_ppm3;
        
        xSemaphoreGive(sensorDataMutex); // Release mutex immediately
        
        // Send data to Firebase
        Serial.println("Sending data to Firebase...");
        
        // MQ-4 Sensor data (Methane - analog values)
        String mq4_1_Path = databasePath + "/mq4_sensor1/value";
        Database.set<int>(aClient, mq4_1_Path, mq4_1, processData, "MQ4_1_Value");
        
        String mq4_2_Path = databasePath + "/mq4_sensor2/value";
        Database.set<int>(aClient, mq4_2_Path, mq4_2, processData, "MQ4_2_Value");
        
        String mq4_3_Path = databasePath + "/mq4_sensor3/value";
        Database.set<int>(aClient, mq4_3_Path, mq4_3, processData, "MQ4_3_Value");
        
        // MQ-135 Sensor 1 data (PPM only)
        String mq135_1_ppm_Path = databasePath + "/mq135_sensor1/ppm";
        Database.set<float>(aClient, mq135_1_ppm_Path, mq135_p1, processData, "MQ135_1_PPM");
        
        // MQ-135 Sensor 2 data (PPM only)
        String mq135_2_ppm_Path = databasePath + "/mq135_sensor2/ppm";
        Database.set<float>(aClient, mq135_2_ppm_Path, mq135_p2, processData, "MQ135_2_PPM");
        
        // MQ-135 Sensor 3 data (PPM only)
        String mq135_3_ppm_Path = databasePath + "/mq135_sensor3/ppm";
        Database.set<float>(aClient, mq135_3_ppm_Path, mq135_p3, processData, "MQ135_3_PPM");
        
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
                    
  if (aResult.isDebug())
    Firebase.printf("Debug task: %s, msg: %s\n", 
                    aResult.uid().c_str(), 
                    aResult.debug().c_str());
                    
  if (aResult.isError())
    Firebase.printf("Error task: %s, msg: %s, code: %d\n", 
                    aResult.uid().c_str(), 
                    aResult.error().message().c_str(), 
                    aResult.error().code());
                    
  if (aResult.available())
    Firebase.printf("task: %s, payload: %s\n", 
                    aResult.uid().c_str(), 
                    aResult.c_str());
}