/*
ЭКГ монитор с расчетом ЧСС и ВСР
Исправленная версия: 
- Убран баг RMSSD:nan
- Исправлена адаптация порога
- Упрощён буфер RR-INTERVALS
*/

#include <TimerOne.h>

// ==================== КОНСТАНТЫ ====================
const int SAMPLE_INTERVAL_US = 3000;    // ~333 Гц
const int REFRACTORY_PERIOD_MS = 200;   
const int MIN_RR_INTERVAL_MS = 300;     
const int MAX_RR_INTERVAL_MS = 2000;    
const int RR_BUFFER_SIZE = 10;          

// Параметры фильтра (0.5-40 Гц)
const int HP_ALPHA = 1005;   
const int LP_ALPHA = 439;    
const int FIXED_POINT = 1024;

// Настройки отладки
#define DEBUG_TEXT_OUTPUT true
#define TEXT_UPDATE_INTERVAL_MS 20

// ==================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ====================

// --- Фильтрация ---
int hpPrevInput = 0, hpPrevOutput = 512, lpPrevOutput = 512;

// --- Детектирование ---
int peakThreshold = 700;        // Стартовое значение (адаптируется автоматически)
unsigned long lastPeakTime = 0;
bool inRefractoryPeriod = false;
unsigned long refractoryStartTime = 0;
int previousSignal = 0;

// --- Адаптивный порог ---
float thresholdAlpha = 0.95;        // 0.95 = быстрая адаптация, 0.99 = плавная
const float PEAK_FRACTION = 0.7;    
const int STATS_WINDOW = 50;        // Окно ~150 мс
int signalMin = 1023, signalMax = 0, statsCount = 0;

// --- RR-интервалы (простой массив со сдвигом, без кольцевой математики) ---
unsigned long rrIntervals[RR_BUFFER_SIZE];
int rrCount = 0;

// --- Текущие значения ---
int currentHeartRate = 0;
int currentRRInterval = 0;

// --- Для вывода ---
unsigned long lastTextSendTime = 0;
int debugRR = 0, debugHR = 0;
float debugRMSSD = 0, debugSDNN = 0;
int filteredValue = 0, rawValue = 0;

// --- Показания с джойстика ---
int Vx; int Vy;

// ==================== ФИЛЬТРАЦИЯ ====================
void initBandpassFilter() {
  hpPrevInput = 0; hpPrevOutput = 512; lpPrevOutput = 512;
}

int highPassFilter(int input) {
  int diff = input - hpPrevInput;
  long output = hpPrevOutput + ((long)HP_ALPHA * diff) / FIXED_POINT;
  hpPrevInput = input; hpPrevOutput = constrain(output, 0, 1023);
  return hpPrevOutput;
}

int lowPassFilter(int input) {
  long output = lpPrevOutput + ((long)LP_ALPHA * (input - lpPrevOutput)) / FIXED_POINT;
  lpPrevOutput = constrain(output, 0, 1023);
  return lpPrevOutput;
}

int applyBandpassFilter(int newValue) {
  return lowPassFilter(highPassFilter(newValue));
}

// ==================== АДАПТАЦИЯ ПОРОГА ====================
void updateAdaptiveThreshold(int signal) {
  signalMin = min(signalMin, signal);
  signalMax = max(signalMax, signal);
  statsCount++;

  if (statsCount >= STATS_WINDOW) {
    // Опорное значение: 60% от размаха (min+max)
    int refValue = (int)(PEAK_FRACTION * (signalMin + signalMax));
    
    // Устойчивая формула: thresh = α*old + (1-α)*ref
    peakThreshold = (int)(thresholdAlpha * peakThreshold + (1.0 - thresholdAlpha) * refValue);
    peakThreshold = constrain(peakThreshold, 300, 900); // Защита от выбросов

    // Сброс окна
    signalMin = 1023; signalMax = 0; statsCount = 0;
  }
}

// ==================== ДЕТЕКТИРОВАНИЕ ПИКОВ ====================
bool detectRPeak(int signal) {
  unsigned long currentTime = millis();
  
  // 1. Адаптация порога
  updateAdaptiveThreshold(signal);
  
  // 2. Инициализация первого отсчёта
  if (lastPeakTime == 0) {
    lastPeakTime = currentTime;
    previousSignal = signal;
    return false;
  }
  
  // 3. Рефрактерный период
  if (inRefractoryPeriod) {
    if (currentTime - refractoryStartTime >= REFRACTORY_PERIOD_MS) {
      inRefractoryPeriod = false;
    } else {
      previousSignal = signal;
      return false;
    }
  }
  
  // 4. Детектирование пересечения порога
  bool peakDetected = false;
  if (previousSignal < peakThreshold && signal >= peakThreshold) {
    unsigned long rrInterval = currentTime - lastPeakTime;
    
    // Первый пик или валидный интервал
    if (rrCount == 0 || (rrInterval >= MIN_RR_INTERVAL_MS && rrInterval <= MAX_RR_INTERVAL_MS)) {
      peakDetected = true;
      addRRInterval(rrInterval);
      currentHeartRate = calculateInstantHeartRate(rrInterval);
      currentRRInterval = rrInterval;
      
      lastPeakTime = currentTime;
      inRefractoryPeriod = true;
      refractoryStartTime = currentTime;
    }
  }
  
  previousSignal = signal;
  return peakDetected;
}

// ==================== РАБОТА С RR-ИНТЕРВАЛАМИ ====================
void addRRInterval(unsigned long interval) {
  // Сдвигаем массив (для 10 элементов это мгновенно и надёжно)
  for (int i = RR_BUFFER_SIZE - 1; i > 0; i--) {
    rrIntervals[i] = rrIntervals[i-1];
  }
  rrIntervals[0] = interval;
  if (rrCount < RR_BUFFER_SIZE) rrCount++;
}

// ==================== РАСЧЁТ ЧСС И ВСР (ПРЯМОЙ, БЕЗ NAN) ====================
int calculateInstantHeartRate(unsigned long rrInterval) {
  return (rrInterval == 0) ? 0 : (int)(60000.0 / rrInterval);
}

float calculateSDNN() {
  if (rrCount < 2) return 0.0;
  float mean = 0.0;
  for (int i = 0; i < rrCount; i++) mean += rrIntervals[i];
  mean /= rrCount;
  
  float variance = 0.0;
  for (int i = 0; i < rrCount; i++) {
    variance += sq(rrIntervals[i] - mean);
  }
  return sqrt(variance / rrCount);
}

float calculateRMSSD() {
  if (rrCount < 2) return 0.0;
  float sumSqDiff = 0.0;
  int validDiffs = 0;
  
  for (int i = 1; i < rrCount; i++) {
    long diff = rrIntervals[i] - rrIntervals[i-1];
    sumSqDiff += sq(diff);
    validDiffs++;
  }
  return (validDiffs == 0) ? 0.0 : sqrt(sumSqDiff / validDiffs);
}

// ==================== ОТПРАВКА ДАННЫХ ====================
byte scaleToByte(int value, int minIn, int maxIn) {
  return (byte)constrain(map(value, minIn, maxIn, 0, 255), 0, 255);
}

void sendDebugText() {
  if (!DEBUG_TEXT_OUTPUT) return;
  unsigned long currentTime = millis();
  if (currentTime - lastTextSendTime < TEXT_UPDATE_INTERVAL_MS) return;
  lastTextSendTime = currentTime;
  
  Serial.print("SIG:"); Serial.print(filteredValue);
  Serial.print(" MIN:"); Serial.print(signalMin);
  Serial.print(" MAX:"); Serial.print(signalMax);
  Serial.print(" THR:"); Serial.print(peakThreshold);
  Serial.print(" RR:"); Serial.print(debugRR);
  Serial.print(" HR:"); Serial.print(debugHR);
  Serial.print(" RMSSD:"); Serial.print(debugRMSSD, 1);
  Serial.print(" SDNN:"); Serial.print(debugSDNN, 1);
  Serial.print(" CNT:"); Serial.print(rrCount);
  Serial.print(" Vx:"); Serial.print(Vx);
  Serial.print(" Vy:"); Serial.println(Vy);
  
}

void sendDebugCommands(){
    if (!DEBUG_TEXT_OUTPUT) return;
  unsigned long currentTime = millis();
  if (currentTime - lastTextSendTime < TEXT_UPDATE_INTERVAL_MS) return;
    lastTextSendTime = currentTime;
  
  Serial.print("SIG:"); Serial.print(filteredValue);
  // Serial.print(" MIN:"); Serial.print(signalMin);
  // Serial.print(" MAX:"); Serial.print(signalMax);
  //Serial.print(" THR:"); Serial.print(peakThreshold);
  //Serial.print(" RR:"); Serial.print(debugRR);
  Serial.print(" HR:"); Serial.print(debugHR);
  Serial.print(" RMSSD:"); Serial.print(debugRMSSD, 1);
  Serial.print(" SDNN:"); Serial.print(debugSDNN, 1);
  //Serial.print(" CNT:"); Serial.print(rrCount);
  Serial.print(" Vx:"); Serial.print(Vx);
  Serial.print(" Vy:"); Serial.println(Vy);
}

void sendByteData() {
  Serial.write("A0"); Serial.write(map(filteredValue, 0, 1023, 0, 255));
  Serial.write("A1"); Serial.write(scaleToByte(currentRRInterval > 0 ? currentRRInterval : 1150, 300, 2000));
  Serial.write("A2"); Serial.write(scaleToByte(currentHeartRate > 0 ? currentHeartRate : 75, 30, 200));
  Serial.write("A3"); Serial.write(scaleToByte((int)(debugRMSSD * 10), 0, 2000));
}


int ecg_pin=A0;
int vy_pin=A5;
int vx_pin=A6;

void sendData() {
  rawValue = analogRead(ecg_pin);
  Vx = analogRead(vx_pin);
  Vy = analogRead(vy_pin);
  filteredValue = applyBandpassFilter(rawValue);
  
  bool peakDetected = detectRPeak(filteredValue);
  
  if (peakDetected) {
    debugRR = currentRRInterval;
    debugHR = currentHeartRate;
  }
  debugRMSSD = calculateRMSSD();
  debugSDNN = calculateSDNN();

  //sendDebugText();
  sendDebugCommands();
  //sendByteData();
}

// ==================== ИНИЦИАЛИЗАЦИЯ ====================
void initAll() {
  Serial.begin(9600);
  initBandpassFilter();
  
  // Сброс буферов
  for(int i=0; i<RR_BUFFER_SIZE; i++) rrIntervals[i] = 0;
  rrCount = 0; lastPeakTime = 0; inRefractoryPeriod = false;
  
  Timer1.initialize(SAMPLE_INTERVAL_US);
  Timer1.attachInterrupt(sendData);
  
  delay(100);
  Serial.println(">> ECG monitor ready (Adaptive Threshold Fixed)");
}

void setup() { initAll(); }
void loop() { /* Всё на прерываниях */ }