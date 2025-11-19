#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Pins definition for MQ-4 and MQ-135 sensors
#define MQ4_PIN1   2  // ADC2_0
#define MQ4_PIN2   4  // ADC2_2
#define MQ4_PIN3   15 // ADC2_3

#define MQ135_PIN1 12  // ADC1_CH7
#define MQ135_PIN2 13  // ADC1_CH0
#define MQ135_PIN3 14  // ADC1_CH3

// Task Handles
TaskHandle_t mq4_1_TaskHandle, mq4_2_TaskHandle, mq4_3_TaskHandle;
TaskHandle_t mq135_1_TaskHandle, mq135_2_TaskHandle, mq135_3_TaskHandle;
TaskHandle_t avgMq4_TaskHandle, avgMq135_TaskHandle;

struct GasSensorParams {
  int pin;                  // ADC Pins
  int* valueOut;            // Value Output
  int threshold;            // Sensors Threshold
  const char* sensorName;   // Sensors name
};

// Variables
int mq4_value1, mq4_value2, mq4_value3;
int mq135_value1, mq135_value2, mq135_value3;
float avg_mq4_value, avg_mq135_value;

// MQ-4 (Methane) Sensor Parameters
GasSensorParams p_mq4_1 = {MQ4_PIN1, &mq4_value1, 3000, "MQ-4 (1)"};
GasSensorParams p_mq4_2 = {MQ4_PIN2, &mq4_value2, 3000, "MQ-4 (2)"};
GasSensorParams p_mq4_3 = {MQ4_PIN3, &mq4_value3, 3000, "MQ-4 (3)"};

// MQ-1351  (Air Quality) Sensor Parameters
GasSensorParams p_mq135_1 = {MQ135_PIN1, &mq135_value1, 2500, "MQ-135 (1)"};
GasSensorParams p_mq135_2 = {MQ135_PIN2, &mq135_value2, 2500, "MQ-135 (2)"};
GasSensorParams p_mq135_3 = {MQ135_PIN3, &mq135_value3, 2500, "MQ-135 (3)"};

// Functions Declaration
void readGasSensor(void *pvParameters);
void avgMQ4_Task(void *pvParameters);
void avgMQ135_Task(void *pvParameters);

void setup(){
  Serial.begin(115200);

  pinMode(MQ4_PIN1, INPUT_PULLDOWN);
  pinMode(MQ4_PIN2, INPUT_PULLDOWN);
  pinMode(MQ4_PIN3, INPUT_PULLDOWN);

  pinMode(MQ135_PIN1, INPUT_PULLDOWN);
  pinMode(MQ135_PIN2, INPUT_PULLDOWN);
  pinMode(MQ135_PIN3, INPUT_PULLDOWN);

  xTaskCreatePinnedToCore(readGasSensor, "Task MQ4_1", 4096, &p_mq4_1, 1, &mq4_1_TaskHandle, 0);
  xTaskCreatePinnedToCore(readGasSensor, "Task MQ4_2", 4096, &p_mq4_2, 1, &mq4_2_TaskHandle, 0);
  xTaskCreatePinnedToCore(readGasSensor, "Task MQ4_3", 4096, &p_mq4_3, 1, &mq4_3_TaskHandle, 0);

  xTaskCreatePinnedToCore(readGasSensor, "Task MQ135_1", 4096, &p_mq135_1, 1, &mq135_1_TaskHandle, 0);
  xTaskCreatePinnedToCore(readGasSensor, "Task MQ135_2", 4096, &p_mq135_2, 1, &mq135_2_TaskHandle, 0);
  xTaskCreatePinnedToCore(readGasSensor, "Task MQ135_3", 4096, &p_mq135_3, 1, &mq135_3_TaskHandle, 0);

  xTaskCreatePinnedToCore(avgMQ4_Task, "Avg MQ4", 4096, NULL, 1, &avgMq4_TaskHandle, 1);
  xTaskCreatePinnedToCore(avgMQ135_Task, "Avg MQ135", 4096, NULL, 1, &avgMq135_TaskHandle, 1);
}

void loop(){}

void readGasSensor(void *pvParameters) {
  GasSensorParams* params = (GasSensorParams*)pvParameters;

  while (1) {
    int temp = analogRead(params->pin); 

    if (isnan(temp)) { 
       Serial.println("[ERROR] sensor  " + String(params->sensorName) + " tidak terdeteksi");
    }else {
       *(params->valueOut) = temp;
       
      if (temp > params->threshold) {
        Serial.println(String("[BAHAYA] ") + params->sensorName + " Melewati Batas! Nilai: " + String(temp));
       } else {
        Serial.println(String("[NORMAL] ") + params->sensorName + " Nilai: " + String(temp));
       }
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

void avgMQ4_Task(void *pvParameters) {
  while (1) {
    avg_mq4_value = (mq4_value1 + mq4_value2 + mq4_value3) / 3.0;
    Serial.println("--- Rata-rata Metana (MQ4): " + String(avg_mq4_value));
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}

void avgMQ135_Task(void *pvParameters) {
  while (1) {
    avg_mq135_value = (mq135_value1 + mq135_value2 + mq135_value3) / 3.0;
    Serial.println("--- Rata-rata Udara (MQ135): " + String(avg_mq135_value));
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}