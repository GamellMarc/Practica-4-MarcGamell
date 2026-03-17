#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// Definimos el pin del LED RGB por si PlatformIO no lo tiene predefinido
#ifndef RGB_BUILTIN
#define RGB_BUILTIN 48
#endif

// Declaración de los "testigos" (Semáforos)
SemaphoreHandle_t semaforoEncender;
SemaphoreHandle_t semaforoApagar;

// Prototipos de las tareas
void tareaEncender(void * parameter);
void tareaApagar(void * parameter);

void setup() {
  Serial.begin(115200); 

  // Apagamos el LED al iniciar (R=0, G=0, B=0)
  neopixelWrite(RGB_BUILTIN, 0, 0, 0);

  // 1. Creamos los semáforos binarios
  semaforoEncender = xSemaphoreCreateBinary();
  semaforoApagar = xSemaphoreCreateBinary();

  // 2. Damos el "testigo" inicial a la tarea de encender
  xSemaphoreGive(semaforoEncender);

  // 3. Creamos las dos tareas
  xTaskCreate(tareaEncender, "Encender LED", 2048, NULL, 1, NULL);
  xTaskCreate(tareaApagar, "Apagar LED", 2048, NULL, 1, NULL);
}

void loop() {
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}

// --- TAREA 1: ENCIENDE EN ROJO ---
void tareaEncender(void * parameter) {
  for(;;) {
    if (xSemaphoreTake(semaforoEncender, portMAX_DELAY) == pdTRUE) {
      
      // Encendemos en ROJO (R, G, B). 
      // Nota: Le pongo 60 en vez de 255 para que el brillo no te deje ciego.
      neopixelWrite(RGB_BUILTIN, 60, 0, 0); 
      Serial.println("LED RGB Encendido (Rojo) por Tarea 1");
      
      vTaskDelay(1000 / portTICK_PERIOD_MS); 
      xSemaphoreGive(semaforoApagar);
    }
  }
}

// --- TAREA 2: APAGA EL LED ---
void tareaApagar(void * parameter) {
  for(;;) {
    if (xSemaphoreTake(semaforoApagar, portMAX_DELAY) == pdTRUE) {
      
      // Apagamos enviando 0 a todos los colores
      neopixelWrite(RGB_BUILTIN, 0, 0, 0); 
      Serial.println("LED RGB Apagado por Tarea 2");
      
      vTaskDelay(1000 / portTICK_PERIOD_MS); 
      xSemaphoreGive(semaforoEncender);
    }
  }
}