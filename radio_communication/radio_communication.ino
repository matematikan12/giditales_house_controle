#include <SPI.h>
#include <nRF24L01.h>
#include "RF24.h"
#include <Servo.h>
#include <EEPROM.h>

// Ініціалізація модуля RF24
RF24 radio(9, 10);

// Список команд
#define HUB_SET_STATE_CONTROLLER 0x01
#define HUB_CHECK_CONTROLLER_STATE 0x02
#define DEVICE_SEND_STATUS 0x21
#define DEVICE_SENSOR_SEND_VALUE 0x22

// Адреса контролерів
#define ADDRESS_CONTROLLER_RELAY 0xA2
#define ADDRESS_CONTROLLER_SERVO 0xAE

// Типи контролерів
#define CONTROLLER_RELAY 0x81
#define CONTROLLER_SERVO 0x82

// Адреса контролерів у EEPROM
#define EEPROM_CONTROLLER_SERVO 0x01
#define EEPROM_CONTROLLER_RELAY 0x02

// Пін реле
#define PIN_RELAY 2

// Пін сервопривода
#define PIN_SERVO 3

Servo servo;
uint8_t servoState;
uint8_t relayState;

// Структура для передачі даних по радіо
typedef struct __attribute__((__packed__)) {
  uint8_t command;
  uint32_t device_address;
  uint8_t device_type;
  uint32_t data;
} RadioData;

void setup() {
  Serial.begin(115200);

  // Ініціалізація модуля NRF24L01
  radio.begin();
  radio.setChannel(5);             // Обмін даними ведеться на п'ятому каналі (2,405 ГГц)
  radio.setDataRate(RF24_1MBPS);   // Швидкість обміну даними 1 Мбіт/сек
  radio.setPALevel(RF24_PA_HIGH);  // Вибір високої потужності передавача (-6dBm)
  radio.openReadingPipe(1, 0x000000CCLL);
  radio.openWritingPipe(0x000000BBLL);
  radio.startListening();  // Початок прослуховування відкритої труби

  // Ініціалізація пінів
  pinMode(PIN_RELAY, OUTPUT);
  servo.attach(PIN_SERVO);

  // Завантаження стану сервопривода з EEPROM
  servoState = EEPROM.read(EEPROM_CONTROLLER_SERVO);
  if (servoState == 0) {
    servo.write(0);
  } else {
    servo.write(180);
  }

  // Завантаження стану реле з EEPROM
  relayState = EEPROM.read(EEPROM_CONTROLLER_RELAY);
  digitalWrite(PIN_RELAY, relayState);

  // Відправка початкового стану контролерів на хаб
  sendStateToHub(ADDRESS_CONTROLLER_SERVO, CONTROLLER_SERVO, servoState);
  sendStateToHub(ADDRESS_CONTROLLER_RELAY, CONTROLLER_RELAY, relayState);

  Serial.println("Controller Ready");
}

void loop() {
  if (radio.available()) {
    RadioData rx_data;
    radio.read(&rx_data, sizeof(rx_data));

    // Зміна стану сервопривода за запитом
    if (rx_data.command == HUB_SET_STATE_CONTROLLER && rx_data.device_address == ADDRESS_CONTROLLER_SERVO) {
      servo.write(180);
      if (rx_data.data == 0x00) {
        servo.write(0);
      } else if (rx_data.data == 0x01) {
        servo.write(180);
      }
      servoState = (uint8_t)rx_data.data;
      EEPROM.write(EEPROM_CONTROLLER_SERVO, servoState);
      sendStateToHub(ADDRESS_CONTROLLER_SERVO, CONTROLLER_SERVO, rx_data.data);
      Serial.println("Обновлено");
    }

    // Отримання та відправка стану сервопривода
    if (rx_data.command == HUB_CHECK_CONTROLLER_STATE && rx_data.device_address == ADDRESS_CONTROLLER_SERVO) {
      sendStateToHub(ADDRESS_CONTROLLER_SERVO, CONTROLLER_SERVO, servoState);
    }

    // Зміна стану реле за запитом
    if (rx_data.command == HUB_SET_STATE_CONTROLLER && rx_data.device_address == ADDRESS_CONTROLLER_RELAY) {
      relayState = (uint8_t)rx_data.data;
      digitalWrite(PIN_RELAY, relayState);
      EEPROM.write(EEPROM_CONTROLLER_RELAY, relayState);
      sendStateToHub(ADDRESS_CONTROLLER_RELAY, CONTROLLER_RELAY, relayState);
      Serial.println("Обновлено");
    }

    // Отримання та відправка стану реле
    if (rx_data.command == HUB_CHECK_CONTROLLER_STATE && rx_data.device_address == ADDRESS_CONTROLLER_RELAY) {
      sendStateToHub(ADDRESS_CONTROLLER_RELAY, CONTROLLER_RELAY, relayState);
    }
  }
  delay(10);
}

// Функція для відправки стану контролера на хаб
void sendStateToHub(uint32_t address, uint32_t type, uint8_t data) {
  radio.stopListening();
  RadioData tx_data;
  tx_data.command = DEVICE_SEND_STATUS;
  tx_data.device_address = address;
  tx_data.device_type = type;
  tx_data.data = data;
  radio.write(&tx_data, sizeof(tx_data));
  radio.startListening();
}
