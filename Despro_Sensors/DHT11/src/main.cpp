#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <DHT.h>

// Define sensor type and pin for DHT sensors
#define DHTTYPE DHT11 
#define DHTPIN1 21       // Pin for DHT sensor 1
#define DHTPIN2 22       // Pin for DHT sensor 2
#define DHTPIN3 23       // Pin for DHT sensor 3

DHT dht1(DHTPIN1, DHTTYPE);
DHT dht2(DHTPIN2, DHTTYPE);
DHT dht3(DHTPIN3, DHTTYPE);

// Task handles
TaskHandle_t dht1_TaskHandle, dht2_TaskHandle, dht3_TaskHandle;
TaskHandle_t avgDHT_TaskHandle;

// Struct for passing parameters to tasks
struct DhtParams {
  DHT* dhtSensor;
  float* humidityRead;
  float* temperatureRead;
  const char* sensorName;
};

// Variables
float humidity1, humidity2, humidity3;
float temperature1, temperature2, temperature3;
float avgHumidity, avgTemperature;

// Funtions Declaration
void readDHT(void *pvParameters);
void avgDHT(void *pvParameters);

// Parameters for each DHT sensor task
DhtParams params1 = {&dht1, &humidity1, &temperature1, "Sensor 1"};
DhtParams params2 = {&dht2, &humidity2, &temperature2, "Sensor 2"};
DhtParams params3 = {&dht3, &humidity3, &temperature3, "Sensor 3"};

void setup() {
  Serial.begin(115200);
  dht1.begin();
  dht2.begin();
  dht3.begin();

  // Create tasks for reading DHT sensors
  xTaskCreatePinnedToCore(readDHT, "DHT 1 Task", 4096, &params1, 1, &dht1_TaskHandle, 0);
  xTaskCreatePinnedToCore(readDHT, "DHT 2 Task", 4096, &params2, 1, &dht2_TaskHandle, 0);
  xTaskCreatePinnedToCore(readDHT, "DHT 3 Task", 4096, &params3, 1, &dht3_TaskHandle, 0);
  
  // Create task for calculating average
  xTaskCreatePinnedToCore(avgDHT, "Average DHT Task", 4096, NULL, 1, &avgDHT_TaskHandle, 1);
}

void loop(){}

void readDHT(void *pvParameters){
  DhtParams* params = (DhtParams*) pvParameters;

  while(1){
    float tempHumidity = params->dhtSensor->readHumidity();
    float tempTemperature = params->dhtSensor->readTemperature();

    if(isnan(tempHumidity) || isnan(tempTemperature)) {
        Serial.println(String(params->sensorName) + " Error!");
    }else{
        *(params->humidityRead) = tempHumidity;
        *(params->temperatureRead) = tempTemperature;
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS); // Delay for 2 seconds;
  }
}

void avgDHT(void *pvParameters){
  while(1){
    avgHumidity = (humidity1 + humidity2 + humidity3) / 3.0;
    avgTemperature = (temperature1 + temperature2 + temperature3) / 3.0;

    Serial.println("Average Humidity: " + String(avgHumidity) + " %");
    Serial.println("Average Temperature: " + String(avgTemperature) + " °C");

    vTaskDelay(5000 / portTICK_PERIOD_MS); // Delay for 5 seconds;
  }
}