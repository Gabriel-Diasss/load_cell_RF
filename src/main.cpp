// =============================================================================
// load_cell_RF - Leitura de Célula de Carga com Transmissão ESP-NOW
// =============================================================================
// Este firmware roda em um ESP32 e faz três coisas:
//   1. Lê o peso de uma célula de carga usando o ADC HX711.
//   2. Aplica um filtro de média móvel para suavizar o sinal.
//   3. Envia os dados (massa filtrada e bruta) via ESP-NOW para outro ESP32.
// =============================================================================

// =============================================================================
// BIBLIOTECAS
// =============================================================================

// Arduino.h é o núcleo do Arduino Framework. Fornece tipos como uint8_t,
// float, bool, e funções como delay(), millis(), Serial.print(), etc.
#include <Arduino.h>

// ESP-NOW é um protocolo de comunicação sem fio da Espressif (fabricante do
// ESP32). Diferente do WiFi comum, ele não precisa de roteador: os ESP32s
// se comunicam diretamente, par-a-par (peer-to-peer), de forma rápida e
// com baixo consumo de energia. Ideal para sensores.
#include <esp_now.h>

// WiFi.h permite configurar o rádio WiFi do ESP32. O ESP-NOW exige que o
// WiFi esteja ativo, mesmo que não estejamos conectados a uma rede.
#include <WiFi.h>

// HX711.h é a biblioteca para o conversor analógico-digital HX711 de 24 bits,
// usado especificamente para ler células de carga (strain gauges).
#include "HX711.h"

// MovingAverageFilter.h é a nossa própria classe de filtro de média móvel,
// definida nos arquivos include/MovingAverageFilter.h e
// src/MovingAverageFilter.cpp.
#include "MovingAverageFilter.h"

// Wire.h e LiquidCrystal_I2C.h estão comentados porque o display LCD
// físico ainda não está disponível. Quando chegar, basta descomentar.
// Wire.h:   biblioteca padrão do Arduino para comunicação I2C.
// LiquidCrystal_I2C.h: driver para displays LCD com backpack I2C (PCF8574).
//#include <Wire.h>
//#include <LiquidCrystal_I2C.h>

// =============================================================================
// CONFIGURAÇÃO DA CÉLULA DE CARGA / HX711
// =============================================================================

// Fator de calibração: converte a leitura bruta do ADC (units) para
// força em Newtons (N). Este valor foi obtido experimentalmente usando
// um peso conhecido e ajustando até a leitura ficar correta.
// Quanto maior o fator, menor o valor lido (mais "rígida" a escala).
#define CALIBRATION_FACTOR 45300

// Pinos GPIO do ESP32 conectados ao HX711:
// DOUT (Data Out) -> pino digital onde o HX711 sinaliza que há dado pronto
// SCK  (Serial Clock) -> pino onde enviamos pulsos para ler os bits
#define LOADCELL_DOUT_PIN  21
#define LOADCELL_SCK_PIN  22

// A célula de carga medeFORÇA (Newtons), mas queremos MASSA (gramas).
// A relação é: Peso(N) = massa(kg) * gravidade(m/s²)
//                massa(g) = (Peso(N) * 1000) / 9.807
// NEWTON_TO_GRAM é a constante pré-calculada: 1000 / 9.807 ≈ 101.97
// Assim, basta multiplicar o valor em Newtons por esta constante.
constexpr float GRAVITY = 9.807f;          // Aceleração da gravidade (m/s²)
constexpr float NEWTON_TO_GRAM = 1000.0f / GRAVITY;  // Fator de conversão N -> g

// Cria o objeto 'scale' da classe HX711 para controlar o ADC.
HX711 scale;

// Cria o filtro de média móvel com 5 amostras.
// A cada leitura, a média é calculada sobre as 5 leituras mais recentes.
// Isso suaviza variações bruscas causadas por vibração ou ruído elétrico.
MovingAverageFilter filter(5);

// Objeto do LCD (comentado até o display chegar).
// Endereço I2C 0x27, display 20 colunas por 4 linhas.
// Parâmetros: (endereço I2C, colunas, linhas)
//LiquidCrystal_I2C lcd(0x27, 20, 4);

// =============================================================================
// CONFIGURAÇÃO ESP-NOW
// =============================================================================

// Endereço MAC do ESP32 que vai RECEBER os dados.
// O valor vem da build_flag definida no platformio.ini:
//   -D RECEIVER_MAC={0x80,0xF3,0xDA,0x5D,0x35,0x64}
// Se a flag não for passada, usa o valor padrão abaixo (fallback).
#ifndef RECEIVER_MAC
#define RECEIVER_MAC {0x80, 0xF3, 0xDA, 0x5D, 0x35, 0x64}
#endif

// Converte a macro em um array de 6 bytes que o ESP-NOW entende.
uint8_t broadcastAddress[] = RECEIVER_MAC;

// Estrutura da mensagem que será enviada via rádio.
// Cada pacote ESP-NOW pode ter no MÁXIMO 250 bytes.
// Aqui temos: 32 + 4 + 32 + 4 = 72 bytes (bem dentro do limite).
typedef struct struct_message {
    char labelA[32];       // String com o rótulo do primeiro valor
    float filteredMass;    // Massa filtrada em gramas (float = 4 bytes)
    char labelC[32];       // String com o rótulo do segundo valor
    float rawMass;         // Massa bruta (sem filtro) em gramas
} struct_message;

// Instância da estrutura que será preenchida e enviada a cada ciclo.
struct_message myData;

// Informações do peer (receptor) necessárias para o ESP-NOW.
esp_now_peer_info_t peerInfo;

// Flag volátil que indica se o último envio foi bem-sucedido.
// 'volatile' é necessário porque esta variável é modificada dentro de
// um callback (contexto de interrupção/FreeRTOS). Sem 'volatile', o
// compilador pode otimizar as leituras e nunca ver o novo valor.
volatile bool lastSendSuccess = false;

// =============================================================================
// CALLBACK DE ENVIO ESP-NOW
// =============================================================================
// Esta função é chamada automaticamente pelo ESP-NOW quando um pacote
// termina de ser transmitido (com sucesso ou falha).
// ATENÇÃO: ela roda em um contexto especial (callback do ESP-NOW), então
// NÃO deve conter operações lentas ou chamadas como Serial.print().
// Por isso apenas atualizamos a flag lastSendSuccess.
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    // status pode ser: ESP_NOW_SEND_SUCCESS ou ESP_NOW_SEND_FAIL
    lastSendSuccess = (status == ESP_NOW_SEND_SUCCESS);
}

// =============================================================================
// FUNÇÃO setup() - Roda UMA VEZ ao ligar ou resetar o ESP32
// =============================================================================
void setup() {
    // Inicializa a comunicação serial com o computador a 115200 baud.
    // Permite ver mensagens de debug no Monitor Serial da PlatformIO.
    Serial.begin(115200);

    // ---- Inicialização do WiFi + ESP-NOW ----

    // Configura o rádio WiFi no modo Station (cliente).
    // O ESP-NOW exige que o WiFi esteja em modo Station (WIFI_STA) ou
    // modo SoftAP + Station (WIFI_AP_STA). Não funciona com WiFi desligado.
    WiFi.mode(WIFI_STA);

    // Inicializa o protocolo ESP-NOW.
    // Retorna ESP_OK se deu certo, ESP_ERR_INVALID_STATE se o WiFi não está
    // no modo correto, etc.
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;  // Se falhou, para o setup aqui (não adianta continuar)
    }

    // Registra a função OnDataSent como callback de envio.
    // Sempre que um pacote for enviado, o ESP-NOW chamará esta função.
    esp_now_register_send_cb(OnDataSent);

    // Preenche a estrutura peerInfo com os dados do receptor:
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);  // MAC de 6 bytes
    peerInfo.channel = 0;    // Canal 0 = usa o mesmo canal do WiFi (qualquer)
    peerInfo.encrypt = false;  // Sem criptografia (mais simples e rápido)

    // Adiciona o receptor à lista de peers do ESP-NOW.
    // O ESP-NOW precisa conhecer o peer antes de enviar dados para ele.
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }

    // ---- Inicialização do HX711 (célula de carga) ----

    // Informa quais pinos GPIO estão conectados ao DOUT e SCK do HX711.
    scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

    // Aplica o fator de calibração. As leituras de scale.get_units() serão
    // (valor_bruto - offset) / calibration_factor, resultando em Newtons.
    scale.set_scale(CALIBRATION_FACTOR);

    // Tare (tara): faz uma leitura com a balança vazia e guarda esse valor
    // como offset (zero). Todas as leituras futuras serão relativas a este zero.
    scale.tare();

    // Aguarda o HX711 ficar pronto. O parâmetros são:
    //   - 10: número de tentativas
    //   - 500: tempo de espera entre tentativas (em microssegundos? ms?)
    // Retorna false se esgotar as tentativas sem resposta.
    if (!scale.wait_ready_retry(10, 500)) {
        Serial.println("HX711 not found");
        // Não damos return aqui porque o HX711 pode demorar mais para
        // responder, e tentaremos ler novamente no loop().
    }

    // ---- Inicialização do LCD (comentado - display ainda não disponível) ----
    // lcd.init();         // Inicializa a comunicação I2C com o display
    // lcd.backlight();    // Liga a luz de fundo (backlight)
    // lcd.clear();        // Limpa o display

    Serial.println("HX711 scale demo");
    Serial.println("Readings:");
}

// =============================================================================
// FUNÇÃO AUXILIAR: converte Newtons para gramas
// =============================================================================
// A HX711.get_units() retorna a força em Newtons (após calibração e tara).
// Multiplicamos por NEWTON_TO_GRAM (≈101.97) para obter a massa em gramas.
float convertToGrams(float units) {
    return units * NEWTON_TO_GRAM;
}

// =============================================================================
// FUNÇÃO loop() - Roda REPETIDAMENTE enquanto o ESP32 estiver ligado
// =============================================================================
void loop() {
    // Antes de ler, verifica se o HX711 tem um novo dado disponível.
    // is_ready() retorna true quando o pino DOUT vai para LOW,
    // indicando que a conversão terminou e o dado pode ser lido.
    if (!scale.is_ready()) {
        // Se não estiver pronto, espera 100ms e tenta de novo no próximo ciclo.
        // Isso evita ler dados corrompidos ou incompletos.
        Serial.println("HX711 not ready");
        delay(100);
        return;  // Sai do loop() sem fazer nada, volta no próximo ciclo
    }

    // Lê o valor do HX711 UMA ÚNICA VEZ.
    // get_units() retorna a força em Newtons: (leitura_bruta - offset) / escala.
    // Importante: lemos uma vez só e usamos este mesmo valor para calcular
    // tanto a massa bruta quanto a filtrada. No código original, o filtro
    // lia o sensor internamente, resultando em duas leituras em instantes
    // diferentes (leituras inconsistentes).
    float rawUnits = scale.get_units();

    // Converte o valor bruto (Newtons) para massa (gramas).
    float rawMass = convertToGrams(rawUnits);

    // Passa o mesmo valor pelo filtro de média móvel e converte para gramas.
    // filter.addReading() armazena a nova amostra e retorna a média das
    // últimas 5 leituras. Aplicamos convertToGrams() no resultado médio.
    float filteredMass = convertToGrams(filter.addReading(rawUnits));

    // ---- Impressão no Monitor Serial ----
    // Mostra ambos os valores para comparação: o filtrado é mais suave,
    // o bruto reage mais rápido mas tem mais ruído.
    Serial.print("Filtered Mass: ");
    Serial.print(filteredMass, 3);  // 3 casas decimais (ex: 125.437g)
    Serial.print("g || Raw Mass: ");
    Serial.print(rawMass, 3);
    Serial.println("g");

    // ---- Atualização do Display LCD (comentado - futuro) ----
    // lcd.clear();
    // lcd.setCursor(0, 0);      // Coluna 0, Linha 0 (primeira linha)
    // lcd.print("Bancada de teste");
    // lcd.setCursor(1, 1);      // Coluna 1, Linha 1 (segunda linha)
    // lcd.print("Mass: ");
    // lcd.print(filteredMass, 3);
    // lcd.print("g");

    // ---- Preparação e Envio do Pacote ESP-NOW ----
    // Preenche a estrutura com os dados a serem enviados.
    // strcpy copia a string literal para o array de char.
    strcpy(myData.labelA, "Filtered Mass:");
    myData.filteredMass = filteredMass;
    strcpy(myData.labelC, "Raw Mass:");
    myData.rawMass = rawMass;

    // Envia o pacote via ESP-NOW.
    // Parâmetros:
    //   - broadcastAddress: MAC do destinatário
    //   - (uint8_t *)&myData: ponteiro para a estrutura convertida para bytes
    //   - sizeof(myData): tamanho do pacote em bytes
    // Retorna:
    //   - ESP_OK: pacote enfileirado para envio (NÃO significa que chegou)
    //   - ESP_FAIL: erro ao enfileirar
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));

    // Verifica se o pacote foi enfileirado corretamente.
    // O resultado da transmissão em si é informado via callback OnDataSent,
    // que preenche a flag lastSendSuccess.
    if (result == ESP_OK) {
        Serial.print("Send: ");
        Serial.println(lastSendSuccess ? "OK" : "FAIL");
    } else {
        Serial.println("Send error");
    }

    // Aguarda 100ms antes do próximo ciclo.
    // Isso define a taxa de amostragem em ~10 leituras por segundo.
    // O delay é importante para:
    //   1. Dar tempo do HX711 fazer a próxima conversão.
    //   2. Não sobrecarregar o rádio ESP-NOW.
    //   3. Manter uma taxa estável e legível no Serial Monitor.
    delay(100);
}
