#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Definición de pines (Actualizados y funcionando)
#define LED_SEGUNDOS 2    // LED que parpadea cada segundo
#define LED_MODO 20       // LED que indica modo de ajuste 
#define BTN_MODO 35       // Botón para cambiar modo 
#define BTN_INCREMENTO 38 // Botón para incrementar valor 

// Variables para el reloj
volatile int horas = 0;
volatile int minutos = 0;
volatile int segundos = 0;
volatile int modo = 0; // 0: modo normal, 1: ajuste horas, 2: ajuste minutos

// Variables FreeRTOS
QueueHandle_t botonQueue;
SemaphoreHandle_t relojMutex;

// Estructura para los eventos de botones
typedef struct {
  uint8_t boton;
  uint32_t tiempo;
} EventoBoton;

// Prototipos de tareas
void TareaReloj(void *pvParameters);
void TareaLecturaBotones(void *pvParameters);
void TareaActualizacionDisplay(void *pvParameters);
void TareaControlLEDs(void *pvParameters);

// Función para manejar interrupciones de botones
void IRAM_ATTR ISR_Boton(void *arg) {
  uint8_t numeroPulsador = (uint32_t)arg;
  EventoBoton evento;
  evento.boton = numeroPulsador;
  evento.tiempo = xTaskGetTickCountFromISR();
  xQueueSendFromISR(botonQueue, &evento, NULL);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Inicializando Reloj Digital con RTOS");

  // Configurar pines
  pinMode(LED_SEGUNDOS, OUTPUT);
  pinMode(LED_MODO, OUTPUT);
  pinMode(BTN_MODO, INPUT_PULLUP);
  pinMode(BTN_INCREMENTO, INPUT_PULLUP);

  // Crear recursos RTOS
  botonQueue = xQueueCreate(20, sizeof(EventoBoton));
  relojMutex = xSemaphoreCreateMutex();

  // Configurar interrupciones para los botones
  attachInterruptArg(BTN_MODO, ISR_Boton, (void*)BTN_MODO, FALLING);
  attachInterruptArg(BTN_INCREMENTO, ISR_Boton, (void*)BTN_INCREMENTO, FALLING);

  // Crear tareas
  xTaskCreate(TareaReloj, "Reloj Task", 2048, NULL, 1, NULL);
  xTaskCreate(TareaLecturaBotones, "Botones Task", 2048, NULL, 2, NULL); 
  xTaskCreate(TareaActualizacionDisplay, "Display Task", 2048, NULL, 1, NULL);
  xTaskCreate(TareaControlLEDs, "LEDsTask", 1024, NULL, 1, NULL);
}

void loop() {
  // loop() queda vacío en aplicaciones RTOS
  vTaskDelay(portMAX_DELAY);
}

// Tarea que actualiza el tiempo del reloj
void TareaReloj(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xPeriod = pdMS_TO_TICKS(1000); // Periodo de 1 segundo

  for (;;) {
    vTaskDelayUntil(&xLastWakeTime, xPeriod);

    if (xSemaphoreTake(relojMutex, portMAX_DELAY) == pdTRUE) {
      if (modo == 0) {
        segundos++;
        if (segundos >= 60) {
          segundos = 0;
          minutos++;
          if (minutos >= 60) {
            minutos = 0;
            horas++;
            if (horas >= 24) {
              horas = 0;
            }
          }
        }
      }
      xSemaphoreGive(relojMutex);
    }
  }
}

// Tarea que gestiona los botones y sus acciones
void TareaLecturaBotones(void *pvParameters) {
  EventoBoton evento;
  uint32_t ultimoTiempoModo = 0;
  uint32_t ultimoTiempoInc = 0;
  const uint32_t debounceTime = pdMS_TO_TICKS(300); // Tiempo anti-rebote

  for (;;) {
    if (xQueueReceive(botonQueue, &evento, portMAX_DELAY) == pdPASS) {
      
      if (evento.boton == BTN_MODO) {
        if ((evento.tiempo - ultimoTiempoModo) >= debounceTime) {
          if (xSemaphoreTake(relojMutex, portMAX_DELAY) == pdTRUE) {
            modo = (modo + 1) % 3;
            Serial.printf("Cambio de modo: %d\n", modo);
            xSemaphoreGive(relojMutex);
          }
          ultimoTiempoModo = evento.tiempo;
        }
      } 
      else if (evento.boton == BTN_INCREMENTO) {
        if ((evento.tiempo - ultimoTiempoInc) >= debounceTime) {
          if (xSemaphoreTake(relojMutex, portMAX_DELAY) == pdTRUE) {
            if (modo == 1) { // Ajuste de horas
              horas = (horas + 1) % 24;
              Serial.printf("Horas ajustadas a: %d\n", horas);
            } 
            else if (modo == 2) { // Ajuste de minutos
              minutos = (minutos + 1) % 60;
              segundos = 0; // Reiniciar segundos al cambiar minutos
              Serial.printf("Minutos ajustados a: %d\n", minutos);
            }
            xSemaphoreGive(relojMutex);
          }
          ultimoTiempoInc = evento.tiempo;
        }
      }
    }
  }
}

// Tarea que actualiza la información en el display (puerto serie)
void TareaActualizacionDisplay(void *pvParameters) {
  int horasAnterior = -1, minutosAnterior = -1, segundosAnterior = -1, modoAnterior = -1;

  for (;;) {
    if (xSemaphoreTake(relojMutex, portMAX_DELAY) == pdTRUE) {
      bool cambios = (horas != horasAnterior) || (minutos != minutosAnterior) || 
                     (segundos != segundosAnterior) || (modo != modoAnterior);

      if (cambios) {
        Serial.printf("%02d:%02d:%02d", horas, minutos, segundos);
        
        if (modo == 0) Serial.println(" [Modo Normal]");
        else if (modo == 1) Serial.println(" [Ajuste Horas]");
        else if (modo == 2) Serial.println(" [Ajuste Minutos]");

        horasAnterior = horas;
        minutosAnterior = minutos;
        segundosAnterior = segundos;
        modoAnterior = modo;
      }
      xSemaphoreGive(relojMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// Tarea que controla los LEDs según el estado del reloj
void TareaControlLEDs(void *pvParameters) {
  bool estadoLedSegundos = false;

  for (;;) {
    if (xSemaphoreTake(relojMutex, portMAX_DELAY) == pdTRUE) {
      if (segundos % 2 == 0 != estadoLedSegundos) {
        estadoLedSegundos = !estadoLedSegundos;
        digitalWrite(LED_SEGUNDOS, estadoLedSegundos);
      }
      digitalWrite(LED_MODO, modo > 0);
      xSemaphoreGive(relojMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}