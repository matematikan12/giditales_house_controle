#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <nRF24L01.h>
#include <SPI.h>
#include <RF24.h>
#include <string.h>

// Список можливих команд
#define HUB_SET_STATE_CONTROLLER 0x01
#define HUB_CHECK_CONTROLLER_STATE 0x02
#define DEVICE_SEND_STATUS 0x21
#define DEVICE_SENSOR_SEND_VALUE 0x22

// Список можливих типів пристроїв
#define SENSOR_LIGHT 0x01
#define SENSOR_DHT 0x02
#define SENSOR_WATER 0x03
#define SENSOR_GROUND_HUM 0x04
#define CONTROLLER_RELAY 0x81
#define CONTROLLER_SERVO 0x82

// Дані Wi-Fi мережі
#define WIFI_SSID "*****"
#define WIFI_PASSWORD "******"

// Індивідуальний токен бота
#define BOT_TOKEN "********"

X509List cert(TELEGRAM_CERTIFICATE_ROOT);
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

unsigned long loopDelayTimer;

typedef struct {
  char* name;
  uint32_t device_address;
  uint8_t device_type;
  uint8_t initialized;
  volatile uint32_t actual_data;
  char* captionStatusON;
  char* captionStatusOFF;
} DeviceData;

typedef struct __attribute__((__packed__)) {
  uint8_t command;
  uint32_t device_address;
  uint8_t device_type;
  uint32_t data;
} DeviceRadioData;

typedef struct {
  int8_t t;
  uint8_t h;
} DHTData;

uint8_t devicesCount = 0;
DeviceData devicesData[10];

int saveSensorValue(uint32_t, uint32_t);
void handleNewMessages(int);
String getListDevices(void);

RF24 radio(4, 15);

void setup(void) {
  SPI.setHwCs(true);
  SPI.begin();
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);

  radio.begin();                   // Ініціалізація модуля NRF24L01
  radio.setChannel(5);             // Обмін даними ведеться на п'ятому каналі (2,405 ГГц)
  radio.setDataRate(RF24_1MBPS);   // Швидкість обміну даними 1 Мбіт/сек
  radio.setPALevel(RF24_PA_HIGH);  // Вибір високої потужності передавача (-6dBm)

  // Канал отримання значень з датчиків
  radio.openReadingPipe(1, 0x00000011LL);
  // Канал отримання стану контролерів
  radio.openReadingPipe(2, 0x000000BBLL);
  radio.openWritingPipe(0x000000CCLL);

  radio.startListening();  // Початок прослуховування відкритої труби

  Serial.begin(115200);

  configTime(0, 0, "pool.ntp.org");       // Отримання часу з сервера
  secured_client.setTrustAnchors(&cert);  // Додавання кореневого сертифіката для api.telegram.org

  Serial.print("Connecting to Wifi SSID ");
  Serial.print(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.print("\nWiFi connected. IP address: ");
  Serial.println(WiFi.localIP());

  Serial.print("Retrieving time: ");
  time_t now = time(nullptr);
  while (now < 24 * 3600) {
    Serial.print(".");
    delay(100);
    now = time(nullptr);
  }
  Serial.println(now);

  devicesData[0] = { "Датчик темп. і вологості на 2-му поверсі", 0xF5, SENSOR_DHT, 0x00, 0x00 };
  devicesData[1] = { "Датчик освітлення зовні", 0x21, SENSOR_LIGHT, 0x00, 0x00 };
  devicesData[2] = { "Автополив городу", 0xA2, CONTROLLER_RELAY, 0x00, 0x00, "Увімкнено", "Вимкнено" };
  devicesData[3] = { "Головні ворота", 0xAE, CONTROLLER_SERVO, 0x00, 0x00, "Відкриті", "Закриті" };
  devicesData[4] = { "Датчик затоплення", 0xE7, SENSOR_WATER, 0x00, 0x00 };
  devicesData[5] = { "Датчик вологості ґрунту", 0x43, SENSOR_GROUND_HUM, 0x00, 0x00 };
  devicesCount = 6;

  initControllers();
}

void loop(void) {
  if (millis() - loopDelayTimer > 1000) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages) {
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    initControllers();
    loopDelayTimer = millis();
  }

  uint8_t pipe;
  if (radio.available(&pipe)) {
    DeviceRadioData rx_data;
    radio.read(&rx_data, sizeof(rx_data));

    if (pipe == 1) {
      // Отримання значення з датчиків
      if (rx_data.command == DEVICE_SENSOR_SEND_VALUE) {
        uint8_t response = saveSensorValue(rx_data.device_address, rx_data.data);
        DeviceData sensorData = getDataByAddress(rx_data.device_address);
        char str[128];
        sprintf(str, "Значення з \"%s\" [0x%08X] отримано (%d)", sensorData.name, rx_data.device_address, rx_data.data);
        Serial.println(str);
      }
    } else if (pipe == 2) {
      // Отримання стану пристрою
      if (rx_data.command == DEVICE_SEND_STATUS) {
        String res = saveControllerState(rx_data.device_address, rx_data.data);
        if (res != "") {
          char str[128];
          sprintf(str, "Стан пристрою %s [0x%08X] отримано (%d)", res.c_str(), rx_data.device_address, rx_data.data);
          Serial.println(str);
          sendExecuteCommandReply(rx_data.device_address);
        }
      }
    }
  }
}

/* Функції пристроїв */

/* Збереження отриманих з датчиків даних в ОЗП за адресою */
int saveSensorValue(uint32_t __device_address, uint32_t __data) {
  for (uint8_t i = 0; i < devicesCount; i++) {
    if (devicesData[i].device_address == __device_address) {
      devicesData[i].actual_data = __data;
      return 1;
    }
  }
  return 0;
}

/* Оновлення статусу контролерів */
String saveControllerState(uint32_t __device_address, uint32_t __data) {
  for (uint8_t i = 0; i < devicesCount; i++) {
    if (devicesData[i].device_address == __device_address) {
      devicesData[i].initialized = 0x01;
      devicesData[i].actual_data = __data;
      return devicesData[i].name;
    }
  }
  return "";
}

DeviceData getDataByAddress(uint32_t __device_address) {
  for (uint8_t i = 0; i < devicesCount; i++) {
    if (devicesData[i].device_address == __device_address) {
      return devicesData[i];
    }
  }
  return DeviceData{};
}

int setState(uint32_t address, uint8_t state) {
  radio.stopListening();
  DeviceRadioData data;
  data.command = HUB_SET_STATE_CONTROLLER;
  data.device_address = address;
  data.device_type = 0x00;
  data.data = state;
  radio.write(&data, sizeof(data));
  radio.startListening();
}

void initControllers() {
  for (uint8_t i = 0; i < devicesCount; i++) {
    if (devicesData[i].device_type > 0x80 && devicesData[i].initialized == 0x00) {
      radio.stopListening();
      DeviceRadioData data;
      data.command = HUB_CHECK_CONTROLLER_STATE;
      data.device_address = devicesData[i].device_address;
      data.device_type = devicesData[i].device_type;
      data.data = 0x00;
      radio.write(&data, sizeof(data));
      radio.startListening();
      delay(100);
    }
  }
}

/* Функції Telegram */
void handleNewMessages(int numNewMessages) {
  Serial.println("Отримані нові команди з Telegram");
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text = bot.messages[i].text;
    Serial.println(bot.messages[i].text);

    char str[128];
    sprintf(str, "[[\"Сводка\"],[\"%s ворота\", \"%s полив\"]]",
            (devicesData[3```cpp
            .actual_data == 0x00 ? "Відкрити" : "Закрити"),
            (devicesData[2].actual_data == 0x00 ? "Увімкнути" : "Вимкнути"));
    String keyboardJson = String(str);

    // Команда відправляє доступні для використання команди
    if (text == "/start") {
      bot.sendMessageWithReplyKeyboard(chat_id, "Доступні команди", "", keyboardJson, true, true);
    }
    if (text == "/devices") {
      bot.sendMessage(chat_id, getListDevices(), "Markdown");
    }
    // Команда відправляє стан датчиків і контролерів
    if (text == "/status" || text == "Сводка") {
      bot.sendMessageWithReplyKeyboard(chat_id, getListValues(), "", keyboardJson, true, true);
    }
    // Команда відправляє завдання на відкриття воріт
    if (text == "/gatesclose" || text == "Відкрити ворота") {
      setState(0xAE, 1);
    }
    // Команда відправляє завдання на закриття воріт
    if (text == "/gatesopen" || text == "Закрити ворота") {
      setState(0xAE, 0);
    }
    // Команда відправляє завдання на увімкнення реле на автополив
    if (text == "/relayon" || text == "Увімкнути полив") {
      setState(0xA2, 1);
    }
    // Команда відправляє завдання на вимкнення реле на автополив
    if (text == "/relayoff" || text == "Вимкнути полив") {
      setState(0xA2, 0);
    }
  }
}

String getListDevices() {
  String str = "Список пристроїв:\n";
  for (uint8_t i = 0; i < devicesCount; i++) {
    char buf[64];
    sprintf(buf, "- %s\n", devicesData[i].name);
    str += String(buf);
  }
  return str;
}

String getListValues() {
  String str = "Стан датчиків і контролерів:\n";
  for (uint8_t i = 0; i < devicesCount; i++) {
    str += "\n";
    str += String(devicesData[i].name);
    str += ":\n";
    if (devicesData[i].device_type == SENSOR_DHT) {
      str += getDHTStringValues(devicesData[i].actual_data);
    } else if (devicesData[i].device_type == SENSOR_LIGHT) {
      if (devicesData[i].actual_data == 0) {
        str += "Світло";
      } else {
        str += "Темно";
      }
    } else if (devicesData[i].device_type > 0x80) {
      if (devicesData[i].actual_data == 0) {
        str += devicesData[i].captionStatusOFF;
      } else {
        str += devicesData[i].captionStatusON;
      }
    } else {
      char str_temp[6];
      dtostrf((devicesData[i].actual_data / 1024.0) * 100.0, 4, 2, str_temp);
      char buf[32];
      sprintf(buf, "%s %%", str_temp);
      str += String(buf);
    }
    str += "\n";
  }
  return str;
}

String getDHTStringValues(uint32_t dht_data) {
  DHTData dhtData;
  dhtData = *(DHTData*)&dht_data;
  char buf[32];
  sprintf(buf, "темп: %d, вол.: %d %%", dhtData.t, dhtData.h);
  return String(buf);
}

void sendExecuteCommandReply(uint32_t __device_address) {
  char str[128];
  sprintf(str, "[[\"Сводка\"],[\"%s ворота\", \"%s полив\"]]",
          (devicesData[3].actual_data == 0x00 ? "Відкрити" : "Закрити"),
          (devicesData[2].actual_data == 0x00 ? "Увімкнути" : "Вимкнути"));
  String keyboardJson = String(str);
  for (uint8_t i = 0; i < devicesCount; i++) {
    if (devicesData[i].device_address == __device_address) {
      sprintf(str, "Стан \"%s\" оновлено на %s",
              devicesData[i].name,
              (devicesData[i].actual_data == 0x00 ? devicesData[i].captionStatusOFF : devicesData[i].captionStatusON));
      bot.sendMessageWithReplyKeyboard("********", String(str), "", keyboardJson, true, true);
    }
  }
}