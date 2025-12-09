#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

/* 
For Calculating CO2 concentration,
in the MQ135 library, change the rzero and rload value
rzero = 46.0 & rload = 22.0 (based on calibration)

Also change the ATMOCO2 value according to environment
Depok = 478.70 ppm (based on: https://lib.fkm.ui.ac.id/detail?id=131080&lokasi=lokal)
*/
#include <MQ135.h>
#include <time.h>

#include "secrets.h" 

#define WIFI_SSID SSID_WIFI
#define WIFI_PASSWORD PASSWORD_WIFI

#define API_KEY KEY_API
#define DATABASE_URL URL_DATABASE
#define USER_EMAIL EMAIL_USER
#define USER_PASSWORD PASSWORD_USER

// Pin Definitions
#define MQ4_PIN1   32 
#define MQ4_PIN2   33 
#define MQ4_PIN3   34 

#define MQ135_PIN1 35 
#define MQ135_PIN2 36 
#define MQ135_PIN3 39

#define FIREBASE_SEND_INTERVAL 15000

// Firebase components
FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
RealtimeDatabase Database;

// Structs
struct MQ4SensorParams{
  int pin;
  int* valueOut;
  const char* sensorName;
};

struct MQ135SensorParams{
  int pin;
  float* rzeroOut;
  float* valueOut;
  const char* sensorName;
};

// Authentication
UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);

// Task Handles
TaskHandle_t mq4_1_TaskHandle, mq4_2_TaskHandle, mq4_3_TaskHandle;
TaskHandle_t mq135_1_TaskHandle, mq135_2_TaskHandle, mq135_3_TaskHandle;
TaskHandle_t firebaseLoopTaskHandle;
TaskHandle_t firebaseSendTaskHandle;

// Mutex
SemaphoreHandle_t sensorDataMutex;

// Variables
// MQ-4 Variables
int mq4_val1, mq4_val2, mq4_val3; 

// MQ-135 Variables
float mq135_rzero1, mq135_rzero2, mq135_rzero3;
float mq135_val1, mq135_val2, mq135_val3;

String uid;
String databasePath;

// MQ-4 Parameters
MQ4SensorParams mq4_1 = {MQ4_PIN1, &mq4_val1, "MQ-4 (1)"};
MQ4SensorParams mq4_2 = {MQ4_PIN2, &mq4_val2, "MQ-4 (2)"};
MQ4SensorParams mq4_3 = {MQ4_PIN3, &mq4_val3, "MQ-4 (3)"};

// MQ-135 Parameters
MQ135SensorParams mq135_1 = {MQ135_PIN1, &mq135_rzero1, &mq135_val1, "MQ-135 (1)"};
MQ135SensorParams mq135_2 = {MQ135_PIN2, &mq135_rzero2, &mq135_val2, "MQ-135 (2)"};
MQ135SensorParams mq135_3 = {MQ135_PIN3, &mq135_rzero3, &mq135_val3, "MQ-135 (3)"};

// Functions Declaration
void readMQ4Sensor(void *pvParameters);
void readMQ135Sensor(void *pvParameters);
void firebaseLoopTask(void *pvParameters);
void firebaseSendTask(void *pvParameters);
void processData(AsyncResult &aResult);
unsigned long getEpochTime();

void setup(){
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);

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
  Serial.println("WiFi connected! ");
  Serial.println(WiFi.localIP());

  // Timestamp 
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("\nWaiting for NTP time");
  while (time(nullptr) < 100000) {
    Serial.print(".");
    delay(100);
  }
  Serial.println("Time synchronized");

  ssl_client.setInsecure();
  ssl_client.setHandshakeTimeout(5);

  initializeApp(aClient, app, getAuth(user_auth), processData, "🔐 authTask");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);

  // MQ-4 Tasks
  xTaskCreatePinnedToCore(readMQ4Sensor, "Task_MQ4_1", 4096, &mq4_1, 1, &mq4_1_TaskHandle, 1);
  xTaskCreatePinnedToCore(readMQ4Sensor, "Task_MQ4_2", 4096, &mq4_2, 1, &mq4_2_TaskHandle, 1);
  xTaskCreatePinnedToCore(readMQ4Sensor, "Task_MQ4_3", 4096, &mq4_3, 1, &mq4_3_TaskHandle, 1);

  // MQ-135 Tasks
  xTaskCreatePinnedToCore(readMQ135Sensor, "Task_MQ135_1", 4096, &mq135_1, 1, &mq135_1_TaskHandle, 1);
  xTaskCreatePinnedToCore(readMQ135Sensor, "Task_MQ135_2", 4096, &mq135_2, 1, &mq135_2_TaskHandle, 1);
  xTaskCreatePinnedToCore(readMQ135Sensor, "Task_MQ135_3", 4096, &mq135_3, 1, &mq135_3_TaskHandle, 1);

  // Firebase Tasks
  xTaskCreatePinnedToCore(firebaseLoopTask, "Firebase Loop", 12000, NULL, 2, &firebaseLoopTaskHandle, 0);
  xTaskCreatePinnedToCore(firebaseSendTask, "Firebase Send", 12000, NULL, 1, &firebaseSendTaskHandle, 0);
}

void loop(){}

// Read MQ-4 Sensor
void readMQ4Sensor(void *pvParameters){
  MQ4SensorParams* params = (MQ4SensorParams*)pvParameters;

  while (1){
    int raw = analogRead(params->pin);
    
    // Convert raw value to percentage (0-100%)
    int percentage = map(raw, 200, 4095, 0, 100);
    percentage = constrain(percentage, 0, 100);

    if(xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE) {
      *(params->valueOut) = percentage;
      xSemaphoreGive(sensorDataMutex); 
    }

    Serial.println(String(params->sensorName) + " Raw: " + String(raw) + " -> " + String(percentage) + "%");
    vTaskDelay(pdMS_TO_TICKS(2000)); 
  }
}

// Read MQ-135 Sensor
void readMQ135Sensor(void *pvParameters){
  MQ135SensorParams* params = (MQ135SensorParams*)pvParameters;
  MQ135 sensor(params->pin);

  while (1){
    float rzero = sensor.getRZero();
    float ppm = sensor.getPPM();

    // Convert PPM to percentage (0-100%) based on typical CO2 levels
    int percentage = map((long)ppm, 400, 2000, 0, 100); 
    percentage = constrain(percentage, 0, 100);

    if(xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE) {
      *(params->rzeroOut) = rzero;
      *(params->valueOut) = (float)percentage;
      xSemaphoreGive(sensorDataMutex); 
    }

    Serial.println(String(params->sensorName) + " PPM: " + String(ppm) + " -> " + String(percentage) + "%");
    vTaskDelay(pdMS_TO_TICKS(2000)); 
  }
}

void firebaseLoopTask(void *pvParameters) {
  Serial.println("Firebase Loop Task started");
  while(1) {
    app.loop(); 
    vTaskDelay(pdMS_TO_TICKS(10)); 
  }
}

void firebaseSendTask(void *pvParameters) {
  while(1) {
    if (app.ready()) {
      int methane1, methane2, methane3;
      float co2_1, co2_2, co2_3;

      if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE) {
        methane1 = mq4_val1; 
        methane2 = mq4_val2; 
        methane3 = mq4_val3;
        
        co2_1 = mq135_val1; 
        co2_2 = mq135_val2; 
        co2_3 = mq135_val3;
        xSemaphoreGive(sensorDataMutex); 
      }
      
      Serial.println("Sending Data...");
      unsigned long timestamp = getEpochTime();
      String timestampStr = String(timestamp);

      String latestPath = "/latest/session_001";
      String logPath = "/sensor_logs/session_001/" + timestampStr;

      // Sensor 1
      Database.set<int>(aClient, latestPath + "/sensor1/methane", methane1, processData, "Latest_MQ4_1");
      Database.set<float>(aClient, latestPath + "/sensor1/co2", co2_1, processData, "Latest_MQ135_1");
      vTaskDelay(pdMS_TO_TICKS(50));

      Database.set<int>(aClient, logPath + "/sensor1/methane", methane1, processData, "Log_MQ4_1");
      Database.set<float>(aClient, logPath + "/sensor1/co2", co2_1, processData, "Log_MQ135_1");
      vTaskDelay(pdMS_TO_TICKS(50));

      // Sensor 2
      Database.set<int>(aClient, latestPath + "/sensor2/methane", methane2, processData, "Latest_MQ4_2");
      Database.set<float>(aClient, latestPath + "/sensor2/co2", co2_2, processData, "Latest_MQ135_2");
      vTaskDelay(pdMS_TO_TICKS(50));

      Database.set<int>(aClient, logPath + "/sensor2/methane", methane2, processData, "Log_M4_2");
      Database.set<float>(aClient, logPath + "/sensor2/co2", co2_2, processData, "Log_135_2");
      vTaskDelay(pdMS_TO_TICKS(50));

      // Sensor 3
      Database.set<int>(aClient, latestPath + "/sensor3/methane", methane3, processData, "Latest_MQ4_3");
      Database.set<float>(aClient, latestPath + "/sensor3/co2", co2_3, processData, "Latest_MQ135_3");
      vTaskDelay(pdMS_TO_TICKS(50));

      Database.set<int>(aClient, logPath + "/sensor3/methane", methane3, processData, "Log_MQ4_3");
      Database.set<float>(aClient, logPath + "/sensor3/co2", co2_3, processData, "Log_MQ135_3");
      vTaskDelay(pdMS_TO_TICKS(50));

      // Timestamp
      Database.set<int>(aClient, latestPath + "/timestamp", timestamp, processData, "Time");
      Database.set<int>(aClient, logPath + "/timestamp", timestamp, processData, "Time");

      Serial.println("Data sent!");
    }
    vTaskDelay(pdMS_TO_TICKS(FIREBASE_SEND_INTERVAL)); 
  }
}

void processData(AsyncResult &aResult) {
  if (!aResult.isResult()) return;
  if (aResult.isEvent()) {
    Firebase.printf("Event: %s\n", aResult.eventLog().message().c_str());
  }
  if (aResult.isError()) {
    Firebase.printf("Error: %s\n", aResult.error().message().c_str());
  }
}

unsigned long getEpochTime() {
  time_t now;
  time(&now);
  return now;
}