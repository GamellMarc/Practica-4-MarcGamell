#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Configuración de red WiFi (Modo Estación)
const char* ssid = "Nautilus"; 
const char* password = "20000Leguas"; 

// Definición de pines actualizados a tu montaje
#define LED1 4        // LED Verde
#define LED2 5        // LED Rosa
#define LED3 6        // LED Rojo
#define BTN1 16       // Botón para LED Verde
#define BTN2 17       // Botón para LED Rosa
#define BTN3 18       // Botón para LED Rojo
#define LED_STATUS 2  // LED de Estado

// Variables del juego
volatile int puntuacion = 0;
volatile int tiempoJuego = 30; 
volatile int ledActivo = -1;
volatile bool juegoActivo = false;
volatile int dificultad = 1; 

// Variables FreeRTOS
QueueHandle_t botonQueue;
SemaphoreHandle_t juegoMutex;
TaskHandle_t tareaJuegoHandle = NULL;

// Servidor web
AsyncWebServer server(80);
AsyncEventSource events("/events");

// Estructura para los eventos de botones
typedef struct {
  uint8_t boton;
  uint32_t tiempo;
} EventoBoton;

// Prototipos de tareas y funciones
void TareaServidorWeb(void *pvParameters);
void TareaJuego(void *pvParameters);
void TareaLecturaBotones(void *pvParameters);
void TareaTiempo(void *pvParameters);
void iniciarJuego();
void detenerJuego();
void desactivarTodosLEDs();
String obtenerEstadoJuego();
void enviarActualizacionWeb();

// Interrupción de botones (con mini anti-rebote hardware)
void IRAM_ATTR ISR_Boton(void *arg) {
  uint8_t numeroPulsador = (uint32_t)arg;
  EventoBoton evento;
  evento.boton = numeroPulsador;
  evento.tiempo = xTaskGetTickCountFromISR();
  xQueueSendFromISR(botonQueue, &evento, NULL);
}

// HTML para la página principal (¡Colores actualizados!)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <title>ESP32 Game - Atrapa el LED</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial; text-align: center; margin:0px auto; padding: 20px; }
    .container { display: flex; flex-direction: column; width: 100%; max-width: 500px; margin: 0 auto; }
    .card { background-color: #F8F7F9; box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5); padding: 20px; margin: 10px 0; border-radius: 5px; }
    .button { padding: 15px 50px; font-size: 24px; color: #fff; background-color: #0f8b8d; border: none; border-radius: 5px; cursor: pointer; }
    .progress-container { width: 100%; background-color: #ddd; border-radius: 5px; margin: 10px 0; }
    #progress { height: 30px; background-color: #04AA6D; border-radius: 5px; width: 100%; color: white; line-height: 30px;}
    .led-container { display: flex; justify-content: space-around; margin: 20px 0; }
    .led { width: 50px; height: 50px; border-radius: 50%; background-color: #333; opacity: 0.3; }
    #led1 { background-color: #00ff00; } /* Verde */
    #led2 { background-color: #ff66b2; } /* Rosa */
    #led3 { background-color: #ff0000; } /* Rojo */
  </style>
</head>
<body>
  <div class="container">
    <h2>ESP32 Game - Atrapa el LED</h2>
    <div class="card">
      <p>Puntuación: <span id="score">0</span></p>
      <p>Tiempo: <span id="time">30</span> s</p>
      <div class="progress-container"><div id="progress">30s</div></div>
      <label>Dificultad: <span id="diffValue">1</span></label>
      <input type="range" min="1" max="5" value="1" id="difficultyRange">
      <br><br>
      <button id="startBtn" class="button">Iniciar Juego</button>
    </div>
    <div class="card">
      <div class="led-container">
        <div class="led" id="led1"></div>
        <div class="led" id="led2"></div>
        <div class="led" id="led3"></div>
      </div>
      <p id="gameStatus">Juego detenido</p>
    </div>
  </div>
  <script>
    if (!!window.EventSource) {
      var source = new EventSource('/events');
      source.addEventListener('update', function(e) {
        var data = JSON.parse(e.data);
        document.getElementById('score').innerHTML = data.score;
        document.getElementById('time').innerHTML = data.time;
        document.getElementById('progress').style.width = (data.time/30*100) + '%';
        document.getElementById('progress').innerHTML = data.time + 's';
        document.getElementById('gameStatus').innerHTML = data.active ? "Juego en curso" : "Juego detenido";
        document.getElementById('startBtn').innerHTML = data.active ? "Detener Juego" : "Iniciar Juego";
        
        document.getElementById('led1').style.opacity = (data.led === 0) ? "1.0" : "0.3";
        document.getElementById('led2').style.opacity = (data.led === 1) ? "1.0" : "0.3";
        document.getElementById('led3').style.opacity = (data.led === 2) ? "1.0" : "0.3";
      }, false);
    }
    document.getElementById('startBtn').addEventListener('click', function() {
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/toggle", true);
      xhr.send();
    });
    document.getElementById('difficultyRange').addEventListener('change', function() {
      var diff = this.value;
      document.getElementById('diffValue').innerHTML = diff;
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/difficulty?value=" + diff, true);
      xhr.send();
    });
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  
  if(!SPIFFS.begin(true)) {
    Serial.println("Error al montar SPIFFS");
  }

  // Configurar pines
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  pinMode(LED_STATUS, OUTPUT);
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);

  desactivarTodosLEDs();
  digitalWrite(LED_STATUS, LOW);

  botonQueue = xQueueCreate(20, sizeof(EventoBoton));
  juegoMutex = xSemaphoreCreateMutex();

  attachInterruptArg(BTN1, ISR_Boton, (void*)BTN1, FALLING);
  attachInterruptArg(BTN2, ISR_Boton, (void*)BTN2, FALLING);
  attachInterruptArg(BTN3, ISR_Boton, (void*)BTN3, FALLING);

  // Conectarse a la red WiFi existente (Nautilus)
  Serial.print("Abordando el submarino WiFi ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA); 
  WiFi.begin(ssid, password);

  // Esperar hasta que el router nos deje entrar
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("¡Conexión exitosa al Nautilus!");
  Serial.print("Tu Dirección IP para jugar es: ");
  Serial.println(WiFi.localIP());

  // Rutas del servidor
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request){
    if (juegoActivo) detenerJuego();
    else iniciarJuego();
    request->send(200, "text/plain", "OK");
  });

  server.on("/difficulty", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("value")) {
      int valor = request->getParam("value")->value().toInt();
      if (valor >= 1 && valor <= 5) {
        if (xSemaphoreTake(juegoMutex, portMAX_DELAY) == pdTRUE) {
          dificultad = valor;
          xSemaphoreGive(juegoMutex);
        }
      }
    }
    request->send(200, "text/plain", "OK");
  });

  events.onConnect([](AsyncEventSourceClient *client){
    client->send(obtenerEstadoJuego().c_str(), "update", millis(), 10000);
  });
  
  server.addHandler(&events);
  server.begin();

  // Crear tareas RTOS
  xTaskCreate(TareaServidorWeb, "WebServer", 4096, NULL, 1, NULL);
  xTaskCreate(TareaLecturaBotones, "Botones", 2048, NULL, 2, NULL);
  xTaskCreate(TareaTiempo, "Tiempo", 2048, NULL, 1, NULL);
}

void loop() { vTaskDelay(portMAX_DELAY); }

void TareaServidorWeb(void *pvParameters) {
  for (;;) {
    enviarActualizacionWeb();
    vTaskDelay(pdMS_TO_TICKS(200)); 
  }
}

void TareaJuego(void *pvParameters) {
  int ultimoLed = -1;
  for (;;) {
    if (xSemaphoreTake(juegoMutex, portMAX_DELAY) == pdTRUE) {
      if (juegoActivo) {
        int nuevoLed;
        do { nuevoLed = random(0, 3); } while (nuevoLed == ultimoLed);
        ledActivo = nuevoLed;
        ultimoLed = nuevoLed;
        desactivarTodosLEDs();
        
        switch (ledActivo) {
          case 0: digitalWrite(LED1, HIGH); break;
          case 1: digitalWrite(LED2, HIGH); break;
          case 2: digitalWrite(LED3, HIGH); break;
        }
      }
      xSemaphoreGive(juegoMutex);
    }
    int espera = 1000 - (dificultad * 150); 
    vTaskDelay(pdMS_TO_TICKS(espera));
  }
}

void TareaLecturaBotones(void *pvParameters) {
  EventoBoton evento;
  uint32_t ultimoTiempoBoton = 0;
  const uint32_t debounceTime = pdMS_TO_TICKS(200); 

  for (;;) {
    if (xQueueReceive(botonQueue, &evento, portMAX_DELAY) == pdPASS) {
      if ((evento.tiempo - ultimoTiempoBoton) >= debounceTime) {
        if (xSemaphoreTake(juegoMutex, portMAX_DELAY) == pdTRUE) {
          if (juegoActivo) {
            int botonPresionado = -1;
            if (evento.boton == BTN1) botonPresionado = 0;
            else if (evento.boton == BTN2) botonPresionado = 1;
            else if (evento.boton == BTN3) botonPresionado = 2;

            if (botonPresionado == ledActivo) {
              puntuacion++;
            } else if (puntuacion > 0) {
              puntuacion--;
            }
          }
          xSemaphoreGive(juegoMutex);
        }
        ultimoTiempoBoton = evento.tiempo;
      }
    }
  }
}

void TareaTiempo(void *pvParameters) {
  for (;;) {
    if (xSemaphoreTake(juegoMutex, portMAX_DELAY) == pdTRUE) {
      if (juegoActivo && tiempoJuego > 0) {
        tiempoJuego--;
        if (tiempoJuego == 0) {
          juegoActivo = false;
          desactivarTodosLEDs();
        }
      }
      xSemaphoreGive(juegoMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); 
  }
}

void iniciarJuego() {
  if (xSemaphoreTake(juegoMutex, portMAX_DELAY) == pdTRUE) {
    tiempoJuego = 30;
    puntuacion = 0;
    juegoActivo = true;
    digitalWrite(LED_STATUS, HIGH);
    if (tareaJuegoHandle == NULL) {
      xTaskCreate(TareaJuego, "JuegoTask", 2048, NULL, 1, &tareaJuegoHandle);
    }
    xSemaphoreGive(juegoMutex);
  }
}

void detenerJuego() {
  if (xSemaphoreTake(juegoMutex, portMAX_DELAY) == pdTRUE) {
    juegoActivo = false;
    ledActivo = -1;
    desactivarTodosLEDs();
    digitalWrite(LED_STATUS, LOW);
    xSemaphoreGive(juegoMutex);
  }
}

void desactivarTodosLEDs() {
  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);
}

String obtenerEstadoJuego() {
  String estado = "{";
  if (xSemaphoreTake(juegoMutex, portMAX_DELAY) == pdTRUE) {
    estado += "\"score\":" + String(puntuacion);
    estado += ",\"time\":" + String(tiempoJuego);
    estado += ",\"led\":" + String(ledActivo);
    estado += ",\"active\":" + String(juegoActivo ? "true" : "false");
    estado += ",\"difficulty\":" + String(dificultad);
    xSemaphoreGive(juegoMutex);
  }
  estado += "}";
  return estado;
}

void enviarActualizacionWeb() {
  events.send(obtenerEstadoJuego().c_str(), "update", millis());
}