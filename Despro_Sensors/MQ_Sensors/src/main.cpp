#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Pins definition for MQ-4 and MQ-135 sensors
#define MQ4_PIN1   2  // ADC2_0
#define MQ4_PIN2   4  // ADC2_2
#define MQ4_PIN3   15 // ADC2_3

#define MQ135_PIN1 12 // ADC1_CH7
#define MQ135_PIN2 13 // ADC1_CH0
#define MQ135_PIN3 14 // ADC1_CH3

// Task Handles
TaskHandle_t mq4_1_TaskHandle, mq4_2_TaskHandle, mq4_3_TaskHandle;
TaskHandle_t mq135_1_TaskHandle, mq135_2_TaskHandle, mq135_3_TaskHandle;

struct GasSensorParams{
  int pin;                  // ADC Pin
  int* valueOut;            // Value Output
  const char* sensorName;   // Sensor name
};

// Variables
int mq4_value1, mq4_value2, mq4_value3;
int mq135_value1, mq135_value2, mq135_value3;

// MQ-4 (Methane) Sensor Parameters
GasSensorParams p_mq4_1 = {MQ4_PIN1, &mq4_value1, "MQ-4 (1)"};
GasSensorParams p_mq4_2 = {MQ4_PIN2, &mq4_value2, "MQ-4 (2)"};
GasSensorParams p_mq4_3 = {MQ4_PIN3, &mq4_value3, "MQ-4 (3)"};

// MQ-135 (Air Quality) Sensor Parameters
GasSensorParams p_mq135_1 = {MQ135_PIN1, &mq135_value1, "MQ-135 (1)"};
GasSensorParams p_mq135_2 = {MQ135_PIN2, &mq135_value2, "MQ-135 (2)"};
GasSensorParams p_mq135_3 = {MQ135_PIN3, &mq135_value3, "MQ-135 (3)"};

// Functions Declaration
void readGasSensor(void *pvParameters);

void setup(){
  Serial.begin(115200);

  pinMode(MQ4_PIN1, INPUT_PULLDOWN);
  pinMode(MQ4_PIN2, INPUT_PULLDOWN);
  pinMode(MQ4_PIN3, INPUT_PULLDOWN);

  pinMode(MQ135_PIN1, INPUT_PULLDOWN);
  pinMode(MQ135_PIN2, INPUT_PULLDOWN);
  pinMode(MQ135_PIN3, INPUT_PULLDOWN);

  xTaskCreatePinnedToCore(readGasSensor, "Task MQ-4 (1)", 4096, &p_mq4_1, 1, &mq4_1_TaskHandle, 0);
  xTaskCreatePinnedToCore(readGasSensor, "Task MQ-4 (2)", 4096, &p_mq4_2, 1, &mq4_2_TaskHandle, 0);
  xTaskCreatePinnedToCore(readGasSensor, "Task MQ-4 (3)", 4096, &p_mq4_3, 1, &mq4_3_TaskHandle, 0);

  xTaskCreatePinnedToCore(readGasSensor, "Task MQ-135 (1)", 4096, &p_mq135_1, 1, &mq135_1_TaskHandle, 0);
  xTaskCreatePinnedToCore(readGasSensor, "Task MQ-135 (2)", 4096, &p_mq135_2, 1, &mq135_2_TaskHandle, 0);
  xTaskCreatePinnedToCore(readGasSensor, "Task MQ-135 (3)", 4096, &p_mq135_3, 1, &mq135_3_TaskHandle, 0);
}

void loop(){}

void readGasSensor(void *pvParameters){
  GasSensorParams* params = (GasSensorParams*)pvParameters;

  while (1){
    int temp = analogRead(params->pin);

    // Store the reading and print it to the Serial Monitor
    *(params->valueOut) = temp;
    Serial.println(String(params->sensorName) + " reading: " + String(temp));

    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}