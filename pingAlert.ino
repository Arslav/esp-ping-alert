#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Ds1302.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266TimerInterrupt.h>
#include <WiFiClient.h>
#include <EEPROM.h>

enum Mode{
  SETUP,
  WORK,
};

const int TIMER_INTERVAL = 1000000; //1000 нс * 1000 мс = 1 сек
const int PING_DELAY = 30000; //Частота опроса сайта = 30 сек * 1000 мс
const int BACKLIGHT_TIME = 5; //Подсветка будет работать 5 сек после нажатия клавиши
const char* HOST = "https://httpstat.us/400";  //Сайт который будет опрашиваться

Mode mode = Mode::WORK;
int ledLevel = LOW;
int httpStatus = HTTP_CODE_OK;
int backight_counter = 0;

char ssid[255] = "Default SSID";
char pass[255] = "Default PASS";

ESP8266WiFiMulti WiFiMulti; //WiFi Адаптер
ESP8266Timer ITimer; //Таймер 0
LiquidCrystal_I2C lcd(0x27, 16, 2);  //Дисплей 16х2
Ds1302 rtc(D6, D8, D7); //Часы реального времени

//Обработчик прерывания таймера 1
void ICACHE_RAM_ATTR TimerHandler(void) {

  //Получаем и выводим время
  Ds1302::DateTime dt;
  rtc.getDateTime(&dt);
  printTime(dt);

  //Моргаем светодидом и зажигаем подсветку если ошибка
  if(httpStatus != HTTP_CODE_OK && httpStatus > 0) {
    ledLevel = !ledLevel;
    backight_counter = BACKLIGHT_TIME;
  } else {
    ledLevel = LOW;
  }
  digitalWrite(D5, ledLevel);

  //Управление подсветкой дисплея и обработка кнопки
  if(digitalRead(D0) == true) {
    backight_counter = BACKLIGHT_TIME;
  }
  if (backight_counter > 0) lcd.backlight();
  else lcd.noBacklight();
  if(backight_counter > 0) backight_counter--;
}

void setup(){
 
  //Инициализация UART порта, так-как модуль что-то пишет в порт при включении, отделяемся от этого переносом строк
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  //Инициализация устройств
  lcd.init();                    
  lcd.noBacklight();
  rtc.init();

  //Инициализация портов
  pinMode(D5, OUTPUT);
  digitalWrite(D5, LOW);
  pinMode(D0, INPUT_PULLUP);
  
  //Читаем данные из EEPROM
  EEPROM.begin(512);
  EEPROM.get(0, ssid);
  EEPROM.get(255, pass);

  //Если кнопка зажата, то отправляем программу в режим настройки
  if(digitalRead(D0) == true){
    mode = Mode::SETUP;
    return;
  }

  //Иницализация прерывания таймера
  ITimer.attachInterruptInterval(TIMER_INTERVAL, TimerHandler);

  //Включаем WIFI модуль
  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(ssid, pass);
}

void loop(){
  switch (mode){
    case Mode::SETUP: setupMode(); break;
    default: workMode(); break;
  }
}

//Режим настройки
void setupMode()
{
  lcd.print("Setup mode...");
  lcd.backlight();

  //Настройка времени
  Serial.println("Setup time? Y/N");
  if(readString()[0] == 'Y'){
    Ds1302::DateTime dt;
    Serial.println("Input year: ");
    dt.year = readInt();
    Serial.println("Input month: ");
    dt.month = readInt();
    Serial.println("Input days: ");
    dt.day = readInt();
    Serial.println("Input hours: ");
    dt.hour = readInt();
    Serial.println("Input minutes: ");
    dt.minute = readInt();
    Serial.println("Input seconds: ");
    dt.second = readInt();

    //Перезаписываем время
    rtc.halt();
    rtc.setDateTime(&dt);
    rtc.init();
  }

  //Настройка WIFI
  Serial.println("Setup WIFI ssid/pass? Y/N");
  if(readString()[0] == 'Y'){
    Serial.println("Input ssid: ");
    readString().toCharArray(ssid, 255);
    Serial.println("Input pass: ");
    readString().toCharArray(pass, 255);

    //Записываем данные в EEPROM
    EEPROM.put(0, ssid);
    EEPROM.put(255, pass);
    EEPROM.commit();
  }

  //Переводим в режим работы
  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(ssid, pass);
  lcd.noBacklight();
  lcd.clear();
  ITimer.attachInterruptInterval(TIMER_INTERVAL, TimerHandler);
  mode = Mode::WORK;
}

//Рабочий режим
void workMode() {
  getStatus();
  printStatus();
  delay(PING_DELAY);
}

void getStatus() {
  httpStatus = HTTPC_ERROR_CONNECTION_FAILED;
  if ((WiFiMulti.run() == WL_CONNECTED)) {
    WiFiClientSecure client;
    client.setInsecure(); //TODO: Переделать с проверкой FingerPrint'а сертификата
    HTTPClient http;
    if (http.begin(client, HOST)) { 
      httpStatus = http.GET();
      http.end();
    }
  }
}

//Форматирует и выводит время на дисплей
void printTime(Ds1302::DateTime dt){
  lcd.setCursor(0, 0);
  if(dt.hour < 10) lcd.print('0');
  lcd.print(dt.hour);
  lcd.print(':');
  if(dt.minute < 10) lcd.print('0');
  lcd.print(dt.minute);
  lcd.print(':');
  if(dt.second < 10) lcd.print('0');
  lcd.print(dt.second);
}

//Форматирует и выводит статус на дисплей
void printStatus() {
  lcd.setCursor(0,1);
  if(httpStatus > 0) {
    lcd.print("Status: ");
    lcd.print(httpStatus);
    return;
  }
  
  //Выводим сообщение об ошибке, либо её код
  switch (httpStatus) {
    case HTTPC_ERROR_CONNECTION_FAILED: 
      lcd.print("Connection Lost"); 
    break;
    default:
      lcd.print("Error: "); 
      lcd.print(httpStatus);
    break;
  }
}

//Ожидание и чтение int из UART 
//Да простят меня погроммисты микроконтроллеров за это...
//TODO: Сделать менее затратный способ настройки, например передавать все данные с помощью приложения конфигуратора
int readInt() {
  static String input;
  while (!Serial.available()){
    delay(100);
  }
  input = Serial.readString();
  Serial.println(input);
  return input.toInt();
}

//Ожидание и чтение String из UART 
String readString() {
  static String input;
  while (!Serial.available()){
    delay(100);
  }
  input = Serial.readString();
  Serial.println(input);
  return input;
}