#include <SPI.h>
#include <nRF24L01.h>
#include "RF24.h"

RF24 radio(9, 10);

// Список можливих команд
#define HUB_SET_STATE_CONTROLLER 0x01
#define HUB_CHECK_CONTROLLER_STATE 0x02
#define DEVICE_SEND_STATUS 0x21
#define DEVICE_SENSOR_SEND_VALUE 0x22

// Список можливих типів датчиків
#define SENSOR_LIGHT 0x01
#define SENSOR_DHT 0x02
#define SENSOR_WATER 0x03
#define SENSOR_GROUND_HUM 0x04

// Адреси датчиків
#define ADDRESS_SENSOR_DHT 0xF5
#define ADDRESS_SENSOR_LIGHT 0x21
#define ADDRESS_SENSOR_WATER 0xE7
#define ADDRESS_SENSOR_GROUND_HUM 0x43

// Пін датчика освітленості
#define PIN_LIGHT_SENSOR 2
// Пін датчика затоплення
#define PIN_WATER_SENSOR A1
// Пін датчика вологості ґрунту
#define PIN_GROUND_HUM_SENSOR A2

typedef struct __attribute__((__packed__)) {
  uint8_t command;
  uint32_t device_address;
  uint8_t device_type;
  uint32_t data;
} RadioData;

typedef struct {
  int8_t t;
  uint8_t h;
} DHTData;

void setup() {
  radio.begin();                   // Ініціалізація модуля NRF24L01
  radio.setChannel(5);             // Обмін даними ведеться на п'ятому каналі (2,405 ГГц)
  radio.setDataRate(RF24_1MBPS);   // Швидкість обміну даними 1 Мбіт/сек
  radio.setPALevel(RF24_PA_HIGH);  // Вибір високої потужності передавача (-6dBm)
  radio.openWritingPipe(0x00000011LL);

  pinMode(PIN_LIGHT_SENSOR, INPUT);
  pinMode(PIN_WATER_SENSOR, INPUT);
  pinMode(PIN_GROUND_HUM_SENSOR, INPUT);
}

void loop() {
  RadioData data;
  DHTData dhtData;

  // Датчик DHT
  dhtData.t = 20;
  dhtData.h = 63;
  data.command = DEVICE_SENSOR_SEND_VALUE;
  data.device_address = ADDRESS_SENSOR_DHT;
  data.device_type = SENSOR_DHT;
  data.data = *(uint32_t*)&dhtData;
  radio.write(&data, sizeof(data));
  delay(4000);

  // Датчик освітленості
  uint8_t lightState = digitalRead(PIN_LIGHT_SENSOR);
  data.command = DEVICE_SENSOR_SEND_VALUE;
  data.device_address = ADDRESS_SENSOR_LIGHT;
  data.device_type = SENSOR_LIGHT;
  data.data = lightState;
  radio.write(&data, sizeof(data));
  delay(4000);

  // Датчик рівня води
  uint16_t waterSensorValue = analogRead(PIN_WATER_SENSOR);
  data.command = DEVICE_SENSOR_SEND_VALUE;
  data.device_address = ADDRESS_SENSOR_WATER;
  data.device_type = SENSOR_WATER;
  data.data = waterSensorValue;
  radio.write(&data, sizeof(data));
  delay(4000);

  // Датчик вологості ґрунту
  uint16_t groundSensorValue = analogRead(PIN_GROUND_HUM_SENSOR);
  data.command = DEVICE_SENSOR_SEND_VALUE;
  data.device_address = ADDRESS_SENSOR_GROUND_HUM;
  data.device_type = SENSOR_GROUND_HUM;
  data.data = groundSensorValue;
  radio.write(&data, sizeof(data));
  delay(4000);
}
