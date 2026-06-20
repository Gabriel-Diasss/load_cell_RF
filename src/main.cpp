// =============================================================================
// load_cell_RF - Leitura de Sensores com Transmissão ESP-NOW
// =============================================================================
// Este firmware roda em um ESP32 e faz:
//   1. Lê duas células de carga (HX711 #1 e #2).
//   2. Lê dois termopares tipo K (MAX6675 #1 e #2).
//   3. Aplica filtro de média móvel nas células de carga.
//   4. Envia todos os dados via ESP-NOW a cada 100ms (10 Hz).
//
// O loop usa millis() para controle de tempo, sem delay(), permitindo
// que o polling dos HX711 seja feito de forma não-bloqueante.
// =============================================================================

// =============================================================================
// BIBLIOTECAS
// =============================================================================
#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "HX711.h"
#include <max6675.h>
#include "MovingAverageFilter.h"

// =============================================================================
// CONFIGURAÇÃO DOS SENSORES
// =============================================================================

// --- Células de Carga (HX711) ---

// Fator de calibração: converte leitura bruta do ADC para Newtons.
// Obtido experimentalmente com peso conhecido.
// Cada célula pode ter seu próprio fator (calibre individualmente).
#define CAL_FACTOR_1  45300
#define CAL_FACTOR_2  45300

// Pinos GPIO do HX711 #1
#define HX1_DOUT  21
#define HX1_SCK   22

// Pinos GPIO do HX711 #2
#define HX2_DOUT  25
#define HX2_SCK   26

// --- Termopares (MAX6675) ---
// Os dois MAX6675 compartilham SCK e SO (mesmo barramento SPI),
// cada um com seu próprio pino CS.
#define MAX1_CS   5
#define MAX2_CS   23
#define MAX_SCK   18
#define MAX_SO    19

// =============================================================================
// CONSTANTES FÍSICAS
// =============================================================================
// A célula de carga mede força (Newtons). Para obter massa (gramas):
//   massa(g) = força(N) × 1000 / 9.807
constexpr float GRAVITY = 9.807f;
constexpr float N_TO_G  = 1000.0f / GRAVITY;

// =============================================================================
// CONTROLE DE TEMPO
// =============================================================================
const unsigned long SEND_INTERVAL_MS  = 100;   // 10 Hz
const unsigned long MAX_INTERVAL_MS   = 250;   // MAX6675: ~4 Hz

// =============================================================================
// OBJETOS GLOBAIS
// =============================================================================

// --- HX711 ---
HX711 scale_1;
HX711 scale_2;

// --- MAX6675 ---
// Construtor: MAX6675(pino_SCK, pino_CS, pino_SO)
MAX6675 thermo_1(MAX_SCK, MAX1_CS, MAX_SO);
MAX6675 thermo_2(MAX_SCK, MAX2_CS, MAX_SO);

// --- Filtros de média móvel (5 amostras cada) ---
MovingAverageFilter filter_1(5);
MovingAverageFilter filter_2(5);

// =============================================================================
// CONFIGURAÇÃO ESP-NOW
// =============================================================================

// MAC do receptor, definido via build_flag no platformio.ini
#ifndef RECEIVER_MAC
#define RECEIVER_MAC {0x80, 0xF3, 0xDA, 0x5D, 0x35, 0x64}
#endif
uint8_t broadcastAddress[] = RECEIVER_MAC;

// Estrutura dos dados enviados via rádio (16 bytes)
typedef struct struct_message {
    float load_cell_1_g;     // Massa filtrada, célula 1 [gramas]
    float load_cell_2_g;     // Massa filtrada, célula 2 [gramas]
    float thermocouple_1_c;  // Temperatura termopar 1 [°C]
    float thermocouple_2_c;  // Temperatura termopar 2 [°C]
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;
volatile bool lastSendSuccess = false;

// =============================================================================
// TIMERS (millis)
// =============================================================================
unsigned long lastSendMs = 0;
unsigned long lastMaxMs  = 0;

// =============================================================================
// CALLBACK ESP-NOW
// =============================================================================
// Chamada ao finalizar cada transmissão. Roda em contexto de callback
// (não usar Serial.print aqui).
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    lastSendSuccess = (status == ESP_NOW_SEND_SUCCESS);
}

// =============================================================================
// UTILITÁRIOS
// =============================================================================
float convertToGrams(float units) {
    return units * N_TO_G;
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    Serial.begin(115200);

    // --- Wi-Fi + ESP-NOW ---
    WiFi.mode(WIFI_STA);

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_send_cb(OnDataSent);

    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    peerInfo.ifidx   = WIFI_IF_STA;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }

    // --- HX711 #1 ---
    scale_1.begin(HX1_DOUT, HX1_SCK);
    scale_1.set_scale(CAL_FACTOR_1);
    scale_1.tare();
    if (!scale_1.wait_ready_retry(10, 500)) {
        Serial.println("HX711 #1 not found");
    }

    // --- HX711 #2 ---
    scale_2.begin(HX2_DOUT, HX2_SCK);
    scale_2.set_scale(CAL_FACTOR_2);
    scale_2.tare();
    if (!scale_2.wait_ready_retry(10, 500)) {
        Serial.println("HX711 #2 not found");
    }

    // --- MAX6675 ---
    // Inicializados no construtor global.

    Serial.println("\n=== load_cell_RF ===");
    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());
    Serial.print("Destino: ");
    for (int i = 0; i < 6; i++) {
        Serial.printf("%02X%c", broadcastAddress[i], i < 5 ? ':' : '\n');
    }
    Serial.println("Setup OK. Aguardando sensores...\n");
}

// =============================================================================
// LOOP
// =============================================================================
// O loop executa continuamente sem delay():
//   - HX711s: polling não-bloqueante via is_ready()
//   - MAX6675s: leitura a cada ~250ms (timer)
//   - Envio ESP-NOW: a cada 100ms (timer via millis())
// =============================================================================
void loop() {
    unsigned long now = millis();

    // ========================================================================
    // 1. POLLING HX711 #1 (não-bloqueante)
    // ========================================================================
    // Quando is_ready() retorna true, o HX711 completou uma conversão.
    // Lemos o valor e passamos pelo filtro de média móvel.
    if (scale_1.is_ready()) {
        float rawUnits = scale_1.get_units(1);
        float filteredUnits = filter_1.addReading(rawUnits);
        myData.load_cell_1_g = convertToGrams(filteredUnits);
    }

    // ========================================================================
    // 2. POLLING HX711 #2 (não-bloqueante)
    // ========================================================================
    if (scale_2.is_ready()) {
        float rawUnits = scale_2.get_units(1);
        float filteredUnits = filter_2.addReading(rawUnits);
        myData.load_cell_2_g = convertToGrams(filteredUnits);
    }

    // ========================================================================
    // 3. LEITURA DOS MAX6675 (timer ~250ms)
    // ========================================================================
    // MAX6675 é mais lento (~4 Hz), então lemos em intervalo separado.
    // readCelsius() retorna NaN se o termopar estiver desconectado.
    if (now - lastMaxMs >= MAX_INTERVAL_MS) {
        float t = thermo_1.readCelsius();
        if (!isnan(t)) myData.thermocouple_1_c = t;

        t = thermo_2.readCelsius();
        if (!isnan(t)) myData.thermocouple_2_c = t;

        lastMaxMs = now;
    }

    // ========================================================================
    // 4. ENVIO VIA ESP-NOW (timer 100ms)
    // ========================================================================
    if (now - lastSendMs >= SEND_INTERVAL_MS) {
        esp_err_t result = esp_now_send(
            broadcastAddress,
            (uint8_t *)&myData,
            sizeof(myData)
        );

        if (result == ESP_OK) {
            Serial.printf(
                "LC1:%7.1fg  LC2:%7.1fg  "
                "TC1:%5.1fC  TC2:%5.1fC  "
                "Send:%s\n",
                myData.load_cell_1_g,
                myData.load_cell_2_g,
                myData.thermocouple_1_c,
                myData.thermocouple_2_c,
                lastSendSuccess ? "OK" : "FAIL"
            );
        } else {
            Serial.println("Send error");
        }

        lastSendMs = now;
    }
}
