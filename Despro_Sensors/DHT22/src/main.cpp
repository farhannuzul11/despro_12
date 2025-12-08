#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <DHT.h>
#include <time.h>

#include "secrets.h" // Make the secrets file

#define WIFI_SSID SSID_WIFI
#define WIFI_PASSWORD PASSWORD_WIFI

#define API_KEY KEY_API
#define DATABASE_URL URL_DATABASE
#define USER_EMAIL EMAIL_USER
#define USER_PASSWORD PASSWORD_USER

//Define sensor type and pin for DHT sensors
#define DHTTYPE DHT22 
#define DHTPIN1 21       //Pin for DHT sensor 1
#define DHTPIN2 22       //Pin for DHT sensor 2
#define DHTPIN3 23       //Pin for DHT sensor 3

//DHT objects
DHT dht1(DHTPIN1, DHTTYPE);
DHT dht2(DHTPIN2, DHTTYPE);
DHT dht3(DHTPIN3, DHTTYPE);

// Firebase components
FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
RealtimeDatabase Database;

//Struct for passing parameters to tasks
struct DhtParams {
  DHT* dhtSensor;         //DHT object
  float* humidityRead;    //Humidity variable
  float* temperatureRead; //Temperature variable
  const char* sensorName; //Sensor Name
};

// Authentication
UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);

//Task handles
TaskHandle_t dht1_TaskHandle, dht2_TaskHandle, dht3_TaskHandle;
TaskHandle_t firebaseLoopTaskHandle;
TaskHandle_t firebaseSendTaskHandle;

//Mutex for protecting sensor data
SemaphoreHandle_t sensorDataMutex;

//Variables
float humidity1, humidity2, humidity3;
float temperature1, temperature2, temperature3;
String uid;
String databasePath;
int sendFirebase = 15000;

//Parameters for each DHT sensor task
DhtParams params1 = {&dht1, &humidity1, &temperature1, "DHT 1"};
DhtParams params2 = {&dht2, &humidity2, &temperature2, "DHT 2"};
DhtParams params3 = {&dht3, &humidity3, &temperature3, "DHT 3"};

//Functions Declaration
void readDHT(void *pvParameters);
void firebaseLoopTask(void *pvParameters);
void firebaseSendTask(void *pvParameters);
void processData(AsyncResult &aResult);

void setup(){
  Serial.begin(115200);
  
  //Create mutex before starting tasks
  sensorDataMutex = xSemaphoreCreateMutex();
  if (sensorDataMutex == NULL) {
    Serial.println("Failed to create mutex!");
    while(1);
  }
  
  dht1.begin(); 
  dht2.begin();
  dht3.begin();

  // Connect to Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)    {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi connected! ");
  Serial.println(WiFi.localIP());

  // Timestamp
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

  //Create tasks for reading DHT sensors
  xTaskCreatePinnedToCore(readDHT, "DHT 1 Task", 4096, &params1, 1, &dht1_TaskHandle, 1);
  xTaskCreatePinnedToCore(readDHT, "DHT 2 Task", 4096, &params2, 1, &dht2_TaskHandle, 1);
  xTaskCreatePinnedToCore(readDHT, "DHT 3 Task", 4096, &params3, 1, &dht3_TaskHandle, 1);
  
  //Create Firebase loop task (maintains connection)
  xTaskCreatePinnedToCore(firebaseLoopTask, "Firebase Loop", 12000, NULL, 2, &firebaseLoopTaskHandle, 0);
  
  //Create Firebase send task (sends data every 8 seconds)
  xTaskCreatePinnedToCore(firebaseSendTask, "Firebase Send", 12000, NULL, 1, &firebaseSendTaskHandle, 0);
}

void loop(){}

// Runs every 2 seconds, updates shared variables with mutex protection
void readDHT(void *pvParameters){
  DhtParams* params = (DhtParams*) pvParameters;

  while(1){
    float tempHumidity = params->dhtSensor->readHumidity();
    float tempTemperature = params->dhtSensor->readTemperature();

    if(isnan(tempHumidity) || isnan(tempTemperature)) {
        Serial.println(String(params->sensorName) + " ERROR!");
    }else{
        //Lock mutex before updating shared variables
        if(xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE) {
          *(params->humidityRead) = tempHumidity;
          *(params->temperatureRead) = tempTemperature;
          xSemaphoreGive(sensorDataMutex); //Unlock mutex
        }
        
        Serial.println(String(params->sensorName) + " - Kelembaban: " + String(tempHumidity) + " %"); 
        Serial.println(String(params->sensorName) + " - Temperatur: " + String(tempTemperature) + "°C");
    }
    vTaskDelay(pdMS_TO_TICKS(2000)); //Delay for 2 seconds
  }
}

// Maintains Firebase authentication and handles async operations
void firebaseLoopTask(void *pvParameters) {
  Serial.println("Firebase Loop Task started");
  
  while(1) {
    app.loop(); //Keep Firebase connection alive
    vTaskDelay(pdMS_TO_TICKS(10)); //Short delay to prevent watchdog timeout
  }
}

// Sends all sensor data to Firebase every 15 seconds
void firebaseSendTask(void *pvParameters) {
  while(1) {
    if (app.ready()) {
      float t1, h1, t2, h2, t3, h3;
      
      if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE) {
        t1 = temperature1; 
        t2 = temperature2; 
        t3 = temperature3; 
        
        h1 = humidity1;
        h2 = humidity2; 
        h3 = humidity3;

        xSemaphoreGive(sensorDataMutex); 
      }
      
      Serial.println("Sending Data...");
      unsigned long timestamp = getEpochTime();
      String timestampStr = String(timestamp);

      String latestPath = "/latest/session_001";
      String logPath = "/sensor_logs/session_001/" + timestampStr;

      // Sensor 1
      Database.set<float>(aClient, latestPath + "/sensor1/temperature", t1, processData, "Latest_T1");
      Database.set<float>(aClient, latestPath + "/sensor1/humidity", h1, processData, "Latest_H1");
      Database.set<float>(aClient, logPath + "/sensor1/temperature", t1, processData, "Log_T1");
      Database.set<float>(aClient, logPath + "/sensor1/humidity", h1, processData, "Log_H1");

      // Sensor 2
      Database.set<float>(aClient, latestPath + "/sensor2/temperature", t2, processData, "Latest_T2");
      Database.set<float>(aClient, latestPath + "/sensor2/humidity", h2, processData, "Latest_H2");
      Database.set<float>(aClient, logPath + "/sensor2/temperature", t2, processData, "Log_T2");
      Database.set<float>(aClient, logPath + "/sensor2/humidity", h2, processData, "Log_H2");

      // Sensor 3
      Database.set<float>(aClient, latestPath + "/sensor3/temperature", t3, processData, "Latest_T3");
      Database.set<float>(aClient, latestPath + "/sensor3/humidity", h3, processData, "Latest_H3");
      Database.set<float>(aClient, logPath + "/sensor3/temperature", t3, processData, "Log_T3");
      Database.set<float>(aClient, logPath + "/sensor3/humidity", h3, processData, "Log_H3");

      // Timestamp
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

// Get current epoch time
unsigned long getEpochTime() {
  time_t now;
  time(&now);
  return now;
}