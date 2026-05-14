/*
Bio-Hexapod: Управление скоростью паука через ЭКГ (HR/RMSSD)
Пилот регулирует вегетативный тонус -> меняет частоту колебаний сервоприводов.
Совместим с выводом ЭКГ-модуля: "SIG:... HR:72 RMSSD:45.2 ..."
*/

#include <Servo.h>
#include <math.h>

// ==================== НАСТРОЙКИ КОНСТРУКЦИИ ====================
const int SERVO_PINS[] = {5, 6, 9};        // Правая, Центр, Левая
const float OFFSETS[]    = {90.0, 90.0, 90.0}; // Нейтральное положение
float AMPLITUDES[] = {30.0, 12.0, 30.0};  // Базовая амплитуда
const float PHASES[]     = {0.0, 2.5 * PI, 0.0};

// ==================== НАСТРОЙКИ БИОСВЯЗИ ====================
// Выберите источник: 1 = HR (ЧСС), 0 = RMSSD (ВСР)
#define BIO_SOURCE_IS_HR 1  

// Диапазоны для маппинга (подстройте под себя)
#define BIO_MIN_VAL  60.0   // Мин. ЧСС или RMSSD -> МЕДЛЕННО (большой период)
#define BIO_MAX_VAL  120.0  // Макс. ЧСС или RMSSD -> БЫСТРО (малый период)

// Диапазон периода колебаний (мс). Меньше = быстрее.
#define PERIOD_MIN  400.0   // Максимальная скорость (возбуждение)
#define PERIOD_MAX  1200.0  // Минимальная скорость (спокойствие)

// Коэффициент плавности (0.01...0.2). Меньше = плавнее, но медленнее реакция.
#define SMOOTHING_K 0.05    

// ==================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ====================
Servo servos[3];
float currentPeriod = 800.0; // Текущий сглаженный период
float targetPeriod  = 800.0; // Целевой период от био-данных
unsigned long startTime = 0;
bool isRunning = false;
char currentCmd = 'S';       // U, L, R, S

// Буфер для Serial
const byte BUF_SIZE = 48;
char serialBuf[BUF_SIZE];
byte bufIdx = 0;

// ==================== ИНИЦИАЛИЗАЦИЯ ====================
void setup() {
  Serial.begin(9600);
  for (int i = 0; i < 3; i++) {
    servos[i].attach(SERVO_PINS[i]);
    servos[i].write(OFFSETS[i]);
  }
  
  delay(500);
  Serial.println("🕷️ Bio-Hexapod Ready");
  Serial.println("Commands: U=Forward, L=Left, R=Right, S=Stop");
  Serial.println("Bio: Send HR:xx or RMSSD:xx via Serial");
  Serial.println("Mode: M (toggle HR/RMSSD)");
}

// ==================== ОСНОВНОЙ ЦИКЛ ====================
void loop() {
  // 1. Обработка Serial (неблокирующая)
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (bufIdx > 0) processSerialLine();
      bufIdx = 0;
      serialBuf[0] = '\0';
    } else if (bufIdx < BUF_SIZE - 1) {
      serialBuf[bufIdx++] = c;
    }
  }

  // 2. Плавное изменение периода (Low-Pass фильтр)
  currentPeriod += (targetPeriod - currentPeriod) * SMOOTHING_K;

  // 3. Обновление сервоприводов
  unsigned long elapsed = millis() - startTime;
  if (isRunning && currentCmd != 'S') {
    applyCommandBias();
    for (int i = 0; i < 3; i++) {
      float angle = OFFSETS[i] + AMPLITUDES[i] * sin(2.0 * PI * elapsed / currentPeriod + PHASES[i]);
      servos[i].write(constrain(angle, 0, 180));
    }
  } else {
    // Стоп: возврат в нейтраль
    for (int i = 0; i < 3; i++) servos[i].write(OFFSETS[i]);
  }

  // 4. Отладка раз в секунду
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 1000) {
    lastDebug = millis();
    Serial.print("CMD:"); Serial.print(currentCmd);
    Serial.print(" PER:"); Serial.print(currentPeriod, 0);
    Serial.print(" TGT:"); Serial.println(targetPeriod, 0);
  }
}

// ==================== ЛОГИКА ====================

void processSerialLine() {
  serialBuf[bufIdx] = '\0';
  char* ptr = serialBuf;

  // Команды управления
  if (strlen(ptr) == 1) {
    char cmd = toupper(ptr[0]);
    if (cmd == 'U' || cmd == 'L' || cmd == 'R') {
      currentCmd = cmd;
      if (!isRunning) { isRunning = true; startTime = millis(); }
      Serial.print(" Move: "); Serial.println(currentCmd);
      return;
    }
    if (cmd == 'S') {
      isRunning = false; currentCmd = 'S';
      Serial.println("🛑 Stop");
      return;
    }
    if (cmd == 'M') {
      // Переключение источника био-данных
      // Логика переключения может быть добавлена при необходимости
      Serial.println("ℹ️ Mode toggle placeholder");
      return;
    }
  }

  // Парсинг био-данных из ЭКГ-потока
  char* hrPtr = strstr(ptr, "HR:");
  char* rmssdPtr = strstr(ptr, "RMSSD:");
  
  float bioVal = 0;
  bool validBio = false;

  if (BIO_SOURCE_IS_HR && hrPtr) {
    bioVal = atof(hrPtr + 3);
    validBio = (bioVal > 0 && !isnan(bioVal));
  } else if (!BIO_SOURCE_IS_HR && rmssdPtr) {
    bioVal = atof(rmssdPtr + 6);
    validBio = (bioVal > 0 && !isnan(bioVal));
  }

  if (validBio) {
    updateTargetPeriod(bioVal);
  }
}

void updateTargetPeriod(float bioValue) {
  // Нормализация в диапазон [0.0 ... 1.0]
  float norm = constrain(bioValue, BIO_MIN_VAL, BIO_MAX_VAL);
  norm = (norm - BIO_MIN_VAL) / (BIO_MAX_VAL - BIO_MIN_VAL);

  // Маппинг на период
  // HR: больше пульс -> быстрее (меньше период)
  // RMSSD: больше вариабельность -> спокойнее (больше период)
  if (BIO_SOURCE_IS_HR) {
    targetPeriod = PERIOD_MAX - norm * (PERIOD_MAX - PERIOD_MIN);
  } else {
    targetPeriod = PERIOD_MIN + norm * (PERIOD_MAX - PERIOD_MIN);
  }
  
  // Ограничители
  targetPeriod = constrain(targetPeriod, PERIOD_MIN, PERIOD_MAX);
}

void applyCommandBias() {
  // Изменяем амплитуды в зависимости от команды поворота
  switch (currentCmd) {
    case 'L': AMPLITUDES[0] = 20.0; AMPLITUDES[2] = 40.0; break; // Левая сильнее
    case 'R': AMPLITUDES[0] = 40.0; AMPLITUDES[2] = 20.0; break; // Правая сильнее
    default:  AMPLITUDES[0] = 30.0; AMPLITUDES[2] = 30.0; break; // Нейтраль
  }
}