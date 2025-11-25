#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <DHT.h>

//Define sensor type and pin for DHT sensors
#define DHTTYPE DHT11 
#define DHTPIN1 21       //Pin for DHT sensor 1
#define DHTPIN2 22       //Pin for DHT sensor 2
#define DHTPIN3 23       //Pin for DHT sensor 3

DHT dht1(DHTPIN1, DHTTYPE);
DHT dht2(DHTPIN2, DHTTYPE);
DHT dht3(DHTPIN3, DHTTYPE);

//Task handles
TaskHandle_t dht1_TaskHandle, dht2_TaskHandle, dht3_TaskHandle;

//Struct for passing parameters to tasks
struct DhtParams {
  DHT* dhtSensor;         //DHT object
  float* humidityRead;    //Humidity variable
  float* temperatureRead; //Temperature variable
  const char* sensorName; //Sensor Name
};

//Variables
float humidity1, humidity2, humidity3;
float temperature1, temperature2, temperature3;

//Functions Declaration
void readDHT(void *pvParameters);

//Parameters for each DHT sensor task
DhtParams params1 = {&dht1, &humidity1, &temperature1, "DHT 1"};
DhtParams params2 = {&dht2, &humidity2, &temperature2, "DHT 2"};
DhtParams params3 = {&dht3, &humidity3, &temperature3, "DHT 3"};

void setup() {
  Serial.begin(115200);
  dht1.begin();
  dht2.begin();
  dht3.begin();

  //Create tasks for reading DHT sensors
  xTaskCreatePinnedToCore(readDHT, "DHT 1 Task", 4096, &params1, 1, &dht1_TaskHandle, 0);
  xTaskCreatePinnedToCore(readDHT, "DHT 2 Task", 4096, &params2, 1, &dht2_TaskHandle, 0);
  xTaskCreatePinnedToCore(readDHT, "DHT 3 Task", 4096, &params3, 1, &dht3_TaskHandle, 0);
}

void loop(){}

void readDHT(void *pvParameters){
  DhtParams* params = (DhtParams*) pvParameters;

  while(1){
    float tempHumidity = params->dhtSensor->readHumidity();
    float tempTemperature = params->dhtSensor->readTemperature();

    if(isnan(tempHumidity) || isnan(tempTemperature)) {
        Serial.println(String(params->sensorName) + " ERROR!");
    }else{
        *(params->humidityRead) = tempHumidity;
        *(params->temperatureRead) = tempTemperature;
        Serial.println(String(params->sensorName) + " - Kelembaban: " + String(tempHumidity) + " %"); 
        Serial.println(String(params->sensorName) + " - Temperatur: " + String(tempTemperature) + "°C");
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS); //Delay for 2 seconds;
  }
}