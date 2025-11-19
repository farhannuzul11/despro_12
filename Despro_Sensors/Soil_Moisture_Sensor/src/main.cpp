#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Pin definitions
#define SOIL_PIN1 32
#define SOIL_PIN2 33
#define SOIL_PIN3 34


TaskHandle_t soil1_TaskHandle, soil2_TaskHandle, soil3_TaskHandle;
TaskHandle_t avgSoil_TaskHandle;

// Struct for passing parameters to tasks
struct SoilParams {
  int pin;              // Sensor Pin
  int* reading;         // Sensor Reading Value
  int valMin;          // Dry value (0%)
  int valMax;          // Wet value (100%)
  const char* name;    // Sensor Name
};

// Variables
int soil_moist1, soil_moist2, soil_moist3;
float avg_soil_moist;

// Parameters for each Soil sensor task
SoilParams p_soil1 = {SOIL_PIN1, &soil_moist1, 0, 4095, "Soil Sensor 1"};
SoilParams p_soil2 = {SOIL_PIN2, &soil_moist2, 0, 4095, "Soil Sensor 2"};
SoilParams p_soil3 = {SOIL_PIN3, &soil_moist3, 0, 4095, "Soil Sensor 3"};

// Function Declarations
void readSoil_Task(void *pvParameters);
void avgSoil_Task(void *pvParameters);

void setup() {
  Serial.begin(115200);
  
  pinMode(SOIL_PIN1, INPUT);
  pinMode(SOIL_PIN2, INPUT);
  pinMode(SOIL_PIN3, INPUT);

  // Create tasks for reading Soil sensors
  xTaskCreatePinnedToCore(readSoil_Task, "Task Soil 1", 2048, &p_soil1, 1, &soil1_TaskHandle, 0);
  xTaskCreatePinnedToCore(readSoil_Task, "Task Soil 2", 2048, &p_soil2, 1, &soil2_TaskHandle, 0);
  xTaskCreatePinnedToCore(readSoil_Task, "Task Soil 3", 2048, &p_soil3, 1, &soil3_TaskHandle, 0);

  // Create average task
  xTaskCreatePinnedToCore(avgSoil_Task, "Avg Soil", 2048, NULL, 1, &avgSoil_TaskHandle, 1);
}

void loop() {}

void readSoil_Task(void *pvParameters) {
  SoilParams* params = (SoilParams*)pvParameters;

  while (1) {
    int temp = analogRead(params->pin);

    if(isnan(temp)){
      Serial.println(String("[ERROR] ") + params->name + ": Gagal membaca sensor (Kabel putus/Short?)");
    } else {
       int moisture = map(temp, params->valMin, params->valMax, 0, 100);
       moisture = constrain(moisture, 0, 100);

       *(params->reading) = moisture;

       Serial.print("[INFO] " + String(params->name));
  
       if (moisture < 50){
         Serial.println(": KERING! (" + String(moisture) + " %)");
       } else if (moisture > 55){
         Serial.println(": BASAH! (" + String(moisture) + " %)");
       } else {
         Serial.println(": IDEAL (" + String(moisture) + " %)");
       }
    }

    vTaskDelay(2000 / portTICK_PERIOD_MS); // Delay 2 detik
  }
}

void avgSoil_Task(void *pvParameters) {
  while(1) {
    // Menghitung rata-rata dari 3 variabel global
    avg_soil_moist = (soil_moist1 + soil_moist2 + soil_moist3) / 3.0;

    Serial.println("------------------------------------------------");
    Serial.println(">>> RATA-RATA KELEMBABAN TANAH: " + String(avg_soil_moist) + " %");
    Serial.println("------------------------------------------------");

    vTaskDelay(5000 / portTICK_PERIOD_MS); // Update rata-rata tiap 5 detik
  }
}