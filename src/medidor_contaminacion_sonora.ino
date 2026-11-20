#include <Arduino.h>
#include <math.h>

const int PIN_MIC = 32;      // MAX4466 OUT; cambiar a 35 si ese es el GPIO cableado
const int PIN_POT = 33;

const int LED_VERDE = 25;
const int LED_AMARILLO = 26;
const int LED_ROJO = 27;

const float FS = 48000.0;    // frecuencia asumida para el diseño de los filtros
const float F_HP = 20.0;
const float F_LP = 20000.0;

const unsigned long PERSISTENCIA_LED_MS = 1000;
int estadoLED = 0;           // 0 verde, 1 amarillo, 2 rojo
unsigned long inicioPersistencia = 0;

struct Biquad {
  float b0, b1, b2, a1, a2;
  float x1 = 0, x2 = 0, y1 = 0, y2 = 0;

  float process(float x) {
    float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
    x2 = x1; x1 = x;
    y2 = y1; y1 = y;
    return y;
  }
};

Biquad filtroHP, filtroLP;

void configurarFiltroHP(float fc) {
  const float Q = 0.70710678;
  float w0 = 2.0 * PI * fc / FS;
  float alpha = sin(w0) / (2.0 * Q);
  float cosw = cos(w0);

  float b0 = (1 + cosw) / 2;
  float b1 = -(1 + cosw);
  float b2 = (1 + cosw) / 2;
  float a0 = 1 + alpha;
  float a1 = -2 * cosw;
  float a2 = 1 - alpha;

  filtroHP.b0 = b0/a0; filtroHP.b1 = b1/a0; filtroHP.b2 = b2/a0;
  filtroHP.a1 = a1/a0; filtroHP.a2 = a2/a0;
}

void configurarFiltroLP(float fc) {
  const float Q = 0.70710678;
  float w0 = 2.0 * PI * fc / FS;
  float alpha = sin(w0) / (2.0 * Q);
  float cosw = cos(w0);

  float b0 = (1 - cosw) / 2;
  float b1 = 1 - cosw;
  float b2 = (1 - cosw) / 2;
  float a0 = 1 + alpha;
  float a1 = -2 * cosw;
  float a2 = 1 - alpha;

  filtroLP.b0 = b0/a0; filtroLP.b1 = b1/a0; filtroLP.b2 = b2/a0;
  filtroLP.a1 = a1/a0; filtroLP.a2 = a2/a0;
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARILLO, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);

  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_AMARILLO, LOW);
  digitalWrite(LED_ROJO, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_MIC, ADC_11db);
  analogSetPinAttenuation(PIN_POT, ADC_11db);

  configurarFiltroHP(F_HP);
  configurarFiltroLP(F_LP);

  Serial.println();
  Serial.println("=================================");
  Serial.println(" MEDIDOR DE CONTAMINACION SONORA");
  Serial.println("=================================");
  Serial.println("Frecuencia asumida por filtros: 48 kHz");
  Serial.println("Filtros: 20 Hz - 20 kHz");
  Serial.println("Persistencia minima de alerta: 1 segundo");
}

float medirRMS() {
  const int N = 4800;
  double sumaCuadrados = 0;
  static float offset = 2048;

  // Seguimiento del offset DC
  for (int i = 0; i < 500; i++) {
    int adc = analogRead(PIN_MIC);
    offset = offset * 0.995 + adc * 0.005;
  }

  // Adquisicion, centrado y filtrado
  for (int i = 0; i < N; i++) {
    int adc = analogRead(PIN_MIC);
    float x = adc - offset;
    x = filtroHP.process(x);
    x = filtroLP.process(x);
    sumaCuadrados += x * x;
  }

  return sqrt(sumaCuadrados / N);
}

void loop() {
  int pot = analogRead(PIN_POT);
  int umbralAmarillo = map(pot, 0, 4095, 5, 80);
  int umbralRojo = umbralAmarillo + 10;

  float rms = medirRMS();

  // Escala provisional; no representa dB ni dB(A)
  float nivel = rms * 0.10;
  if (nivel > 100) nivel = 100;
  if (nivel < 0) nivel = 0;

  // Seleccion de categoria solicitada por el nivel actual
  int nuevoEstado;
  if (nivel < umbralAmarillo) nuevoEstado = 0;
  else if (nivel < umbralRojo) nuevoEstado = 1;
  else nuevoEstado = 2;

  unsigned long ahora = millis();

  // Subida de alerta inmediata; bajada retenida al menos 1 segundo
  if (nuevoEstado > estadoLED) {
    estadoLED = nuevoEstado;
    inicioPersistencia = ahora;
  } else if (nuevoEstado < estadoLED) {
    if (ahora - inicioPersistencia >= PERSISTENCIA_LED_MS) {
      estadoLED = nuevoEstado;
      inicioPersistencia = ahora;
    }
  }

  digitalWrite(LED_VERDE, estadoLED == 0 ? HIGH : LOW);
  digitalWrite(LED_AMARILLO, estadoLED == 1 ? HIGH : LOW);
  digitalWrite(LED_ROJO, estadoLED == 2 ? HIGH : LOW);

  Serial.print("RMS = "); Serial.print(rms);
  Serial.print(" | Nivel = "); Serial.print(nivel);
  Serial.print(" | Amarillo = "); Serial.print(umbralAmarillo);
  Serial.print(" | Rojo = "); Serial.print(umbralRojo);
  Serial.print(" | Estado LED = "); Serial.println(estadoLED);
}
