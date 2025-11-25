#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

//Pin definitions
#define SOIL_PIN1 32
#define SOIL_PIN2 33
#define SOIL_PIN3 34

TaskHandle_t soil1_TaskHandle, soil2_TaskHandle, soil3_TaskHandle;

//Struct for passing parameters to tasks
struct SoilParams{
  int pin;          //Sensor Pin
  int* reading;     //Sensor Reading Value
  int valMin;       //Dry value (0%)
  int valMax;       //Wet value (100%)
  const char* name; //Sensor Name
};

//Variables
int soil_moist1 = 0, soil_moist2 = 0, soil_moist3 = 0;

//Parameters for each Soil sensor task
SoilParams p_soil1 = {SOIL_PIN1, &soil_moist1, 35, 163, "Soil Sensor 1"};
SoilParams p_soil2 = {SOIL_PIN2, &soil_moist2, 35, 163, "Soil Sensor 2"};
SoilParams p_soil3 = {SOIL_PIN3, &soil_moist3, 35, 163, "Soil Sensor 3"};

//Function Declarations
void readSoilHumid(void *pvParameters);

void setup(){
  Serial.begin(115200);
  
  pinMode(SOIL_PIN1, INPUT);
  pinMode(SOIL_PIN2, INPUT);
  pinMode(SOIL_PIN3, INPUT);

  //Create tasks for reading Soil sensors
  xTaskCreatePinnedToCore(readSoilHumid, "Task Soil 1", 2048, &p_soil1, 1, &soil1_TaskHandle, 0);
  xTaskCreatePinnedToCore(readSoilHumid, "Task Soil 2", 2048, &p_soil2, 1, &soil2_TaskHandle, 0);
  xTaskCreatePinnedToCore(readSoilHumid, "Task Soil 3", 2048, &p_soil3, 1, &soil3_TaskHandle, 0);
}

void loop() {}

// Task: read a soil sensor and print the raw + mapped percentage (no thresholds)
void readSoilHumid(void *pvParameters) {
  SoilParams* params = (SoilParams*)pvParameters;

  while (1) {
    int raw = analogRead(params->pin);
    int moisture = map(raw, params->valMin, params->valMax, 0, 100);
    moisture = constrain(moisture, 0, 100);

    *(params->reading) = moisture;

    // Print a simple reading line to Serial monitor
    Serial.print(params->name);
    Serial.print(": ");
    Serial.print(moisture);
    Serial.print(" %  (raw ");
    Serial.print(raw);
    Serial.println(")");

    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}