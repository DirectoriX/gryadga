// Таймер с прерываниями
// https://github.com/sadr0b0t/arduino-timer-api
#include <timer-api.h>

// Циферки
// https://github.com/avishorp/TM1637
#include <TM1637Display.h>

// RTC
// https://github.com/msparks/arduino-ds1302
#include <DS1302.h>

// DHT11
#include <SimpleDHT.h>

// EEPROM для настроек
#include <EEPROM.h>

//////////
// Ноги //
//////////

// Регистр сдвига
const int shiftLatch = 2; // ST_CP - зелёный
const int shiftClock = 3; // SH_CP - жёлтый
const int shiftData = 4; // DS - синий

// ШИМ для показа яркости лент
const int stripPWM = 5;

// Цифровой модуль
const int CLK = 6;
const int DIO = 7;
TM1637Display display(CLK, DIO);

// Часы
const int kCePin = 8; // Chip Enable / RST
const int kIoPin = 9; // Input/Output / DAT
const int kSclkPin = 10; // Serial Clock / CLK
DS1302 rtc(kCePin, kIoPin, kSclkPin);

// DHT11
int pinDHT11 = 11;
SimpleDHT11 dht11(pinDHT11);

// Пищалка
const int buzzer = 12;

// Водичка
const int valve = A0;

// Фоновый свет
const int backlight = A1;

const int minWater = 90;

// Выходы сдвигового регистра, по номерам контактов
// 12345678
// 1 Мало воды
// 2 Работают фоновые лампы
// 3 Работает полив
// 4-8 Светодиодные ленты

const byte strip[][5] = {0b00010000, 0b00001000, 0b00000100, 0b00000010, 0b00000001,
                         0b00011000, 0b00001100, 0b00000110, 0b00000011, 0b00010001,
                         0b00011100, 0b00001110, 0b00000111, 0b00010011, 0b00011001,
                         0b00011110, 0b00001111, 0b00010111, 0b00011011, 0b00011101
                        };

// Массив с яркостью освещения
byte power[24];

// Флаги обновления
volatile bool needUpdate = true;
volatile bool nextState = false;

// Блок настроек
byte beginHour = 6; // Час начала освещения
byte lightTime = 15; // Общая длительность освещения
byte backLevel = 3; // Кол-во лент, при котором включён фоновый свет
byte minMoisture = 50; // Влажность, ниже которой включается полив
byte waterTime = 1; // Длительность полива в десятках секунд

// Вспомогательные данные
bool showColon = true;
bool sensorRead = false;
bool checkMoisture = true;
bool watering = false;
int hour = 0;
int minute = 0;
int second = 0;
byte temperature = 0;
byte humidity = 0;
int moisture = 0;
int water = 0;
uint8_t data[] = { 0xff, 0xff, 0x00, 0xff };
int state = 0;
int moistureCount = 0;
int waterCount = 0;

// Обработчик прерывания таймера
void timer_handle_interrupts(int timer)
{
  // Считать будем по 500 срабатываний - 0,5 секунды
  static int count = 500;

  if (count == 0)
    {
      needUpdate = true;
      count = 500;
    }
  else
    {
      count--;
    }
}

void setup()
{
  // Устанавливаем яркость
  display.setBrightness(3);
  // Настраиваем ноги
  pinMode(shiftLatch, OUTPUT);
  pinMode(shiftClock, OUTPUT);
  pinMode(shiftData, OUTPUT);
  pinMode(stripPWM, OUTPUT);
  pinMode(buzzer, OUTPUT);
  digitalWrite(buzzer, HIGH);
  pinMode(valve, OUTPUT);
  pinMode(backlight, OUTPUT);
  // Читаем настройки из EEPROM
  byte first = EEPROM.read(0);

  if (first == 0xda)
    {
      beginHour = EEPROM.read(1);
      lightTime = EEPROM.read(2);
      backLevel = EEPROM.read(3);
      minMoisture = EEPROM.read(4);
      waterTime = EEPROM.read(5);
    }

  // Считаем освещение
  int endHour = beginHour + lightTime;

  for (int i = 0; i < 24; i++)
    {
      power[i] = (i < beginHour || i >= endHour) ? 0 : min(i - beginHour + 1, min(endHour - i, 4));
    }

  // Включаем таймер на 1кГц
  timer_init_ISR_1KHz(TIMER_DEFAULT);
}

void updateView()
{
  byte shift = (power[hour] > 0) ? strip[power[hour] - 1][minute / 12] : 0;

  if (power[hour] >= backLevel)
    {
      bitSet(shift, 6);
      digitalWrite(backlight, LOW);
    }
  else
    {
      digitalWrite(backlight, HIGH);
    }

  // Проверяем уровень воды
  if (water <= minWater)
    {
      bitSet(shift, 7);
    }

  // Если поливаем - поливаем и показываем
  if (watering)
    {
      bitSet(shift, 5);
    }

  digitalWrite(valve, !watering ? HIGH : LOW);
  // Пишем в регистр
  digitalWrite(shiftLatch, LOW);
  shiftOut(shiftData, shiftClock, LSBFIRST, shift);
  digitalWrite(shiftLatch, HIGH);
}

void readSensor()
{
  dht11.read(&temperature, &humidity, NULL);
  water = map(analogRead(A6), 200, 500, 0, 100);

  if (checkMoisture)
    {
      moisture = map(analogRead(A7), 0, 1023, 100, 0);

      if (moisture < minMoisture)
        {
          waterCount = waterTime * 10;
          watering = true;
          moistureCount = waterTime * 10 + 9; // FIXME 900
          checkMoisture = false;
        }
    }
}

void showTemp()
{
  data[0] = display.encodeDigit(temperature / 10);
  data[1] = display.encodeDigit(temperature % 10);
  data[3] = SEG_A | SEG_D | SEG_E | SEG_F;
  display.setSegments(data);
}

void showHum()
{
  data[0] = display.encodeDigit(humidity / 10);
  data[1] = display.encodeDigit(humidity % 10);
  data[3] = SEG_B | SEG_C | SEG_E | SEG_F | SEG_G;
  display.setSegments(data);
}

void showMoist()
{
  data[0] = display.encodeDigit(moisture / 10);
  data[1] = display.encodeDigit(moisture % 10);
  data[3] = SEG_A | SEG_C | SEG_D | SEG_F | SEG_G;
  display.setSegments(data);
}

void showLevel()
{
  data[0] = display.encodeDigit(water / 10);
  data[1] = display.encodeDigit(water % 10);
  data[3] = SEG_D | SEG_E | SEG_F;
  display.setSegments(data);
}

void loop()
{
  if (needUpdate) // Если надо обновить
    {
      needUpdate = false; // Уже не надо
      showColon = !showColon; // Переключаем точки

      if (nextState)
        {
          nextState = false;
          state++;
        }

      switch (state)
        {
          case 0: // Показ времени и датчиков, основной режим
          {
            if (showColon)
              {
                if (waterCount == 0)
                  {
                    watering = false;
                  }
                else
                  {
                    waterCount--;
                  }

                if (moistureCount == 0)
                  {
                    checkMoisture = true;
                  }
                else
                  {
                    moistureCount--;
                  }
              }

            // Получаем время
            Time t = rtc.time();
            hour = t.hr ;
            minute = t.min;
            second = t.sec;
            hour = t.min % 24;
            minute = t.sec;
            // Включаем нужные ленты и фоновый свет
            byte PWM = (power[hour] > 0) ? (power[hour] * 64 - 1) : 0;
            analogWrite(stripPWM, PWM);
            updateView();

            if (water <= minWater && showColon)
              {
                tone(buzzer, 500);
              }
            else
              {
                noTone(buzzer);
                digitalWrite(buzzer, HIGH);
              }

            // Надо ли считать показания датчиков?
            if (second == 0)
              {
                if (!sensorRead)
                  {
                    sensorRead = true;
                    readSensor();
                  }
              }

            switch (second / 5)
              {
                case 0: // Температура
                {
                  showTemp();
                  break;
                }

                case 1: // Влажность воздуха
                {
                  showHum();
                  break;
                }

                case 2: // Влажность почвы
                {
                  showMoist();
                  break;
                }

                case 3: // Уровень воды
                {
                  showLevel();
                  break;
                }

                default: // Время
                {
                  display.showNumberDecEx(hour * 100 + minute, showColon ? 0x40 : 0x00, true);
                  sensorRead = false;
                  break;
                }
              }

            break;
          }

          case 1: // Установка часов
          {
            break;
          }

          case 2: // Установка минут
          {
            break;
          }

          case 3: // Установка начала освещения
          {
            break;
          }

          case 4: // Установка длительности освещения
          {
            break;
          }

          case 5: // Установка влажности полива
          {
            break;
          }

          case 6: // Установка времени полива
          {
            break;
          }

          case 7: // Установка включения фонового света
          {
            break;
          }

          case 8: // Сохранение настроек в EEPROM
          {
            EEPROM.update(0, 0xda);
            EEPROM.update(1, beginHour);
            EEPROM.update(2, lightTime);
            EEPROM.update(3, backLevel);
            EEPROM.update(4, minMoisture);
            EEPROM.update(5, waterTime);
            state = 0; // После сохранения настроек переходим в основной режим
            break;
          }
        }
    }
}
