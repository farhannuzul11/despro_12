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
TaskHandle_t dht1_TaskHandle;
TaskHandle_t dht2_TaskHandle;
TaskHandle_t dht3_TaskHandle;
TaskHandle_t firebaseLoopTaskHandle;
TaskHandle_t firebaseSendTaskHandle;

//Mutex for protecting sensor data
SemaphoreHandle_t sensorDataMutex;

//Variables
float humidity1, humidity2, humidity3;
float temperature1, temperature2, temperature3;
String uid;
String databasePath;

//Functions Declaration
void readDHT(void *pvParameters);
void firebaseLoopTask(void *pvParameters);
void firebaseSendTask(void *pvParameters);
void processData(AsyncResult &aResult);

//Parameters for each DHT sensor task
DhtParams params1 = {&dht1, &humidity1, &temperature1, "DHT 1"};
DhtParams params2 = {&dht2, &humidity2, &temperature2, "DHT 2"};
DhtParams params3 = {&dht3, &humidity3, &temperature3, "DHT 3"};

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
  WiFi.begin("JURASIK", "adeadeaje");
  Serial.println("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)    {
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

// Sends all sensor data to Firebase every 8 seconds
void firebaseSendTask(void *pvParameters) {
  Serial.println("Firebase Send Task started");
  
  while(1) {
    //Wait for Firebase to be ready
    if (app.ready()) {
      
      //Get User UID (only once or when changed)
      if (uid.isEmpty()) {
        uid = app.getUid().c_str();
        Firebase.printf("User UID: %s\n", uid.c_str());
        databasePath = "UsersData/" + uid;
      }
      
      //Lock mutex to read all sensor data safely
      if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE) {
        //Copy sensor data to local variables
        float temp1 = temperature1;
        float hum1 = humidity1;
        float temp2 = temperature2;
        float hum2 = humidity2;
        float temp3 = temperature3;
        float hum3 = humidity3;
        xSemaphoreGive(sensorDataMutex); //Release mutex immediately
        
        //Send data to Firebase
        Serial.println("Sending data to Firebase...");
        
        // DHT Sensor 1 data
        String temp1Path = databasePath + "/sensor1/temperature";
        String hum1Path = databasePath + "/sensor1/humidity";
        Database.set<float>(aClient, temp1Path, temp1, processData, "DHT1_Temperature");
        Database.set<float>(aClient, hum1Path, hum1, processData, "DHT1_Humidity");
        
        // DHT Sensor 2 data
        String temp2Path = databasePath + "/sensor2/temperature";
        String hum2Path = databasePath + "/sensor2/humidity";
        Database.set<float>(aClient, temp2Path, temp2, processData, "DHT2_Temperature");
        Database.set<float>(aClient, hum2Path, hum2, processData, "DHT2_Humidity");
        
        // DHT Sensor 3 data
        String temp3Path = databasePath + "/sensor3/temperature";
        String hum3Path = databasePath + "/sensor3/humidity";
        Database.set<float>(aClient, temp3Path, temp3, processData, "DHT3_Temperature");
        Database.set<float>(aClient, hum3Path, hum3, processData, "DHT3_Humidity");
        
        Serial.println("Data sent successfully!");
      }
    } else {
      Serial.println("Waiting for Firebase authentication...");
    }
    
    vTaskDelay(pdMS_TO_TICKS(8000)); //Send every 8 seconds
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