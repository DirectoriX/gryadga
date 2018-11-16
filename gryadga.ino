#include <timer-api.h>
#include <TM1637Display.h> // 4-digit module
#include <DS1302.h> // RTC
#include <SimpleDHT.h> // DHT11
#include <EEPROM.h> // EEPROM for settings

//////////
// Ноги //
//////////

// Цифровой модуль
const int CLK = 2;
const int DIO = 3;
TM1637Display display(CLK, DIO);

// Регистр сдвига
const int shiftLatch = 4; // ST_CP - зелёный
const int shiftClock = 5; // SH_CP - жёлтый
const int shiftData = 6; // DS - синий

// Часы
const int kCePin = 7; // Chip Enable / RST
const int kIoPin = 8; // Input/Output / DAT
const int kSclkPin = 9; // Serial Clock / CLK
DS1302 rtc(kCePin, kIoPin, kSclkPin);

// ШИМ для показа яркости лент
const int stripPWM = 10;

// DHT11
int pinDHT11 = 11;
SimpleDHT11 dht11(pinDHT11);

// Пищалка
const int buzzer = 12;

// Водичка
const int valve = A0;

// Фоновый свет
const int backlight = A1;


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
int power[24];

// Флаги обновления
volatile bool needUpdate = true;
volatile bool checkMoisture = true;
volatile bool watering = false;
volatile bool stopCheckMoisture = false;
volatile bool startWatering = false;
volatile bool nextState = false;

// Блок настроек
int beginHour = 6;
int lightTime = 15;
int backLevel = 3;
int minMoisture = 50;
int waterTime = 200;

// Вспомогательные данные
bool showColon = true;
bool sensorRead = false;
int hour = 0;
int minute = 0;
int second = 0;
byte temperature = 0;
byte humidity = 0;
int moisture = 0;
int water = 0;
uint8_t data[] = { 0xff, 0xff, 0x00, 0xff };
int state = 0;

// Обработчик прерывания таймера
void timer_handle_interrupts(int timer)
{
  // Считать будем 500 срабатываний - 0,5 секунды
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

  // Для подсчёта времени игнорирования влажности почвы
  static long moistureCount = 0;
  // Для подсчёта времени полива
  static long waterCount = 0;

  if (startWatering)
    {
      waterCount = waterTime * 1000;
      startWatering = false;
      watering = true;
    }

  if (waterCount == 0)
    {
      watering = false;
    }
  else
    {
      waterCount--;
    }

  if (stopCheckMoisture)
    {
      moistureCount = waterTime * 1000 + 900000;
      stopCheckMoisture = false;
      checkMoisture = false;
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

void setup()
{
  timer_init_ISR_1KHz(TIMER_DEFAULT); // Включаем таймер на 1кГц
  display.setBrightness(3); // Устанавливаем яркость
  // Настраиваем ноги
  pinMode(shiftLatch, OUTPUT);
  pinMode(shiftClock, OUTPUT);
  pinMode(shiftData, OUTPUT);
  pinMode(stripPWM, OUTPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(valve, OUTPUT);
  pinMode(backlight, OUTPUT);
  // Читаем настройки из EEPROM
  ////////////////////
  // Считаем освещение
  int endHour = beginHour + lightTime;

  for (int i = 0; i < 24; i++)
    {
      power[i] = (i < beginHour || i >= endHour) ? 0 : min(i - beginHour + 1, min(endHour - i, 4));
    }
}

void shiftWrite()
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
  if (water <= 90)
    {
      bitSet(shift, 7);
    }

  // Если поливаем - поливем и показываем
  if (watering)
    {
      bitSet(shift, 5);
    }

  digitalWrite(valve, watering ? HIGH : LOW);
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
          startWatering = true;
          stopCheckMoisture = true;
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
          case 0: // Показ времени и датчиков, основной
          {
            // Получаем время
            Time t = rtc.time();
            hour = t.hr ;
            minute = t.min;
            second = t.sec;
            // Включаем нужные ленты и фоновый свет
            byte PWM = (power[hour] > 0) ? (power[hour] * 64 - 1) : 0;
            analogWrite(stripPWM, PWM);
            shiftWrite();

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
        }
    }
}
