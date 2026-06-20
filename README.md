# load_cell_RF

Sistema de aquisição de dados com ESP32 utilizando **2 células de carga** (HX711) e **2 termopares tipo K** (MAX6675), com envio via **ESP-NOW** a 10 Hz.

---

## Sumário

- [Visão Geral](#visão-geral)
- [Materiais Necessários](#materiais-necessários)
- [Pinagem](#pinagem)
- [Instalação](#instalação)
- [Calibração das Células de Carga](#calibração-das-células-de-carga)
- [Configuração do MAC de Destino](#configuração-do-mac-de-destino)
- [Estrutura dos Dados ESP-NOW](#estrutura-dos-dados-esp-now)
- [Comportamento do Sistema](#comportamento-do-sistema)
- [Exemplo de Saída Serial](#exemplo-de-saída-serial)
- [Solução de Problemas](#solução-de-problemas)

---

## Visão Geral

```
  Célula Carga 1 ── HX711 #1 ──┐
  Célula Carga 2 ── HX711 #2 ──┤
  Termopar K 1   ── MAX6675 #1 ─┤
  Termopar K 2   ── MAX6675 #2 ─┤
                                │
                                ▼
                        ┌──────────────┐
                        │    ESP32     │
                        │ (Sender)     │
                        │              │
                        │ 10 Hz via    │
                        │ ESP-NOW      │
                        └──────┬───────┘
                               │
                               ▼
                        ┌──────────────┐
                        │    ESP32     │
                        │ (Receiver)   │
                        └──────────────┘
```

### Taxa de operação

| Operação | Taxa | Método |
|---|---|---|
| Leitura HX711 | ~10 amostras/s | Polling `is_ready()` não-bloqueante |
| Filtro média móvel | 5 amostras | Suavização do sinal |
| Leitura MAX6675 | ~4 amostras/s | Timer a cada 250ms |
| Envio ESP-NOW | **10 Hz** | Timer `millis()` a cada 100ms |

## Materiais Necessários

| Componente | Quantidade |
|---|---|
| ESP32 Dev Board (sender) | 1x |
| Módulo HX711 | 2x |
| Célula de carga (strain gauge) | 2x |
| Módulo MAX6675 | 2x |
| Termopar tipo K | 2x |
| ESP32 (receiver) | 1x |
| Protoboard e jumpers | conforme necessário |

## Pinagem

### HX711 #1 (célula de carga 1)

| HX711 #1 | ESP32 |
|---|---|
| DOUT | GPIO21 |
| SCK | GPIO22 |
| VCC | 3.3V |
| GND | GND |

### HX711 #2 (célula de carga 2)

| HX711 #2 | ESP32 |
|---|---|
| DOUT | GPIO25 |
| SCK | GPIO26 |
| VCC | 3.3V |
| GND | GND |

### MAX6675 #1 (termopar 1)

| MAX6675 #1 | ESP32 |
|---|---|
| CS | GPIO5 |
| SCK | GPIO18 |
| SO | GPIO19 |
| VCC | 3.3V |
| GND | GND |

### MAX6675 #2 (termopar 2)

| MAX6675 #2 | ESP32 |
|---|---|
| CS | GPIO22 |
| SCK | GPIO18 *(compartilhado)* |
| SO | GPIO19 *(compartilhado)* |
| VCC | 3.3V |
| GND | GND |

> Os dois MAX6675 compartilham SCK e SO no mesmo barramento SPI. Cada um tem seu próprio CS, evitando conflitos.

### Diagrama de conexões

```
          ┌──────────────┐
GPIO21 ───┤ DOUT HX711#1 ├── Célula 1
GPIO22 ───┤ SCK          │
          └──────────────┘
          ┌──────────────┐
GPIO25 ───┤ DOUT HX711#2 ├── Célula 2
GPIO26 ───┤ SCK          │
          └──────────────┘
          ┌──────────────┐
GPIO5  ───┤ CS  MAX6675#1├── Termopar 1
GPIO18 ───┤ SCK          │
GPIO19 ───┤ SO           │
          └──────────────┘
          ┌──────────────┐
GPIO22 ───┤ CS  MAX6675#2├── Termopar 2
GPIO18 ───┤ SCK          │
GPIO19 ───┤ SO           │
          └──────────────┘
```

## Instalação

### 1. Dependências

As bibliotecas são instaladas automaticamente pelo PlatformIO (definidas em `platformio.ini`):

| Biblioteca | Versão | Finalidade |
|---|---|---|
| `bogde/HX711` | ^0.7.5 | ADC para célula de carga |
| `adafruit/MAX6675 library` | ^1.1.2 | ADC para termopar tipo K |
| `marcoschwartz/LiquidCrystal_I2C` | ^1.1.4 | Display LCD (futuro) |

### 2. Compilar

```bash
pio run
```

### 3. Upload

```bash
pio run -t upload
```

Se houver problemas com a porta:

```bash
pio run -t upload --upload-port /dev/ttyUSB0
```

### 4. Monitor Serial

```bash
pio device monitor -b 115200
```

## Calibração das Células de Carga

Cada HX711 precisa ser calibrado individualmente. O fator de calibração converte a leitura bruta do ADC para Newtons.

### Procedimento

1. **Tara**: Com a célula vazia, `scale.tare()` zera a leitura.
2. **Medição**: Coloque um peso conhecido (ex: 1 kg) sobre a célula.
3. **Leia o valor bruto**: `scale.get_units()` retorna o valor em Newtons.
4. **Calcule o fator**:
   ```
   fator = valor_bruto / (massa_kg * 9.807)
   ```
   Exemplo: se `get_units()` retorna 87000 para 1 kg:
   ```
   fator = 87000 / (1.0 * 9.807) ≈ 8871
   ```

5. **Atualize a constante** no `src/main.cpp`:
   ```cpp
   #define CAL_FACTOR_1 8871
   #define CAL_FACTOR_2 8871  // (idem para a célula 2)
   ```

> **Atenção:** os fatores podem ser diferentes para cada célula de carga. Calibre cada uma individualmente.

### Valores negativos?

Se a leitura aparecer negativa, inverta os fios DOUT e SCK do HX711, ou ajuste o sinal do fator de calibração (`-8871`).

## Configuração do MAC de Destino

O MAC do ESP receptor é configurado via `build_flags` no `platformio.ini`:

```ini
build_flags =
    -D RECEIVER_MAC={0x80,0xF3,0xDA,0x5D,0x35,0x64}
```

Para descobrir o MAC do receptor, carregue este código nele:

```cpp
#include <WiFi.h>
void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    Serial.println(WiFi.macAddress());
}
void loop() {}
```

O MAC aparecerá como `80:F3:DA:5D:35:64`. Atualize o `platformio.ini` com este valor.

> **Broadcast:** use `{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}` para enviar a todos os ESPs no alcance (útil para testes).

## Estrutura dos Dados ESP-NOW

A struct enviada tem exatamente **16 bytes**:

```cpp
typedef struct struct_message {
    float load_cell_1_g;     // offset 0,  4 bytes
    float load_cell_2_g;     // offset 4,  4 bytes
    float thermocouple_1_c;  // offset 8,  4 bytes
    float thermocouple_2_c;  // offset 12, 4 bytes
} struct_message;
```

| Campo | Tipo | Unidade | Descrição |
|---|---|---|---|
| `load_cell_1_g` | float | gramas | Massa filtrada (média móvel 5) da célula 1 |
| `load_cell_2_g` | float | gramas | Massa filtrada (média móvel 5) da célula 2 |
| `thermocouple_1_c` | float | °C | Temperatura do termopar 1 |
| `thermocouple_2_c` | float | °C | Temperatura do termopar 2 |

> **Nota:** O receiver precisa usar a **mesma struct** para interpretar os dados corretamente.

## Comportamento do Sistema

### Loop principal (sem `delay()`)

O loop usa `millis()` para controle de tempo, permitindo que cada sensor seja lido de forma independente e não-bloqueante:

1. **HX711s**: `is_ready()` é verificado continuamente. Quando verdadeiro, a leitura é feita e passa pelo filtro de média móvel (5 amostras). Como o HX711 opera a ~10 SPS, cada célula é atualizada ~10 vezes/segundo.

2. **MAX6675s**: Lidos a cada 250ms (~4 Hz). `readCelsius()` retorna `NaN` se o termopar estiver desconectado — o valor anterior é mantido.

3. **Envio ESP-NOW**: A cada 100ms (10 Hz), os valores mais recentes são empacotados e enviados.

### Filtro de média móvel

As células de carga usam um filtro de média móvel com 5 amostras (`MovingAverageFilter`). Isso suaviza o sinal sem introduzir atraso significativo:

- Aquecimento: nas primeiras 5 leituras (~500ms), a média considera menos amostras
- Estabilizado: média das 5 leituras mais recentes
- Resposta a degrau: ~500ms para atingir 100% do novo valor

### Ausência de sensor

| Situação | Comportamento |
|---|---|
| HX711 não conectado | `is_ready()` nunca retorna true. O valor permanece 0. |
| Termopar desconectado | `readCelsius()` retorna NaN. O valor anterior é mantido. |
| ESP-NOW sem receptor | `lastSendSuccess` fica `false`. O sistema continua tentando. |

## Exemplo de Saída Serial

```
=== load_cell_RF ===
MAC: 3C:61:05:12:34:56
Destino: 80:F3:DA:5D:35:64
Setup OK. Aguardando sensores...

LC1:    0.0g  LC2:    0.0g  TC1: 25.4C  TC2: 26.1C  Send:OK
LC1:  125.3g  LC2:    0.2g  TC1: 25.4C  TC2: 26.1C  Send:OK
LC1:  124.8g  LC2:   10.5g  TC1: 25.5C  TC2: 26.1C  Send:OK
LC1:  125.1g  LC2:   10.2g  TC1: 25.5C  TC2: 26.2C  Send:OK
```

- `LC1/LC2`: massa filtrada em gramas (7 caracteres, 1 casa decimal)
- `TC1/TC2`: temperatura em °C (5 caracteres, 1 casa decimal)
- `Send:OK/FAIL`: status da última transmissão

O log aparece a cada 100ms (10 linhas/segundo).

## Solução de Problemas

### "Failed to add peer"
- Verifique o MAC address no `platformio.ini`
- Certifique-se de que o `esp_now_init()` foi bem-sucedido

### HX711 não responde ("not found" no setup)
- Verifique as conexões DOUT e SCK
- Confirme a alimentação (3.3V ou 5V conforme o módulo)
- Verifique se os pinos GPIO correspondem aos definidos no código

### MAX6675 retorna temperatura fixa ou NaN
- Termopar desconectado ou em curto
- Verifique as conexões CS, SCK, SO
- Confirme a alimentação do módulo

### ESP-NOW "FAIL" constante
- Verifique se o receiver está ligado e no alcance
- Se usar broadcast (`FF:FF:FF:FF:FF:FF`), funciona sem peer específico
- Confirme que o receiver tem o ESP-NOW initialized e registrou o callback de recebimento

### Upload falha
- Segure BOOT, clique Upload, solte BOOT quando começar
- Verifique a porta: `ls /dev/ttyUSB*` ou `ls /dev/ttyACM*`
- Use `--upload-port` para especificar a porta manualmente

---

## Estrutura do Projeto

```
load_cell_RF/
├── platformio.ini              # Configuração do build
├── README.md                   # Este documento
├── src/
│   ├── main.cpp                # Código principal
│   └── MovingAverageFilter.cpp # Implementação do filtro
├── include/
│   ├── MovingAverageFilter.h   # Header do filtro
│   └── README                  # Template PlatformIO
├── lib/                        # (bibliotecas locais)
├── test/                       # (testes)
└── .pio/                       # Build artifacts (não versionar)
```
