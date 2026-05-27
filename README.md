# ESP32-CAM Web Server, Streaming, Relay, Battery Monitor e OTA

Firmware Arduino per ESP32-CAM AI Thinker con camera OV2640. Espone una pagina web locale per streaming video, scatti periodici, controllo di due uscite digitali, lettura batteria e aggiornamento firmware via Wi-Fi.

## Funzioni

- Streaming live MJPEG dalla camera.
- Foto singola aggiornata a intervallo configurabile dalla pagina web.
- Aggiornamento firmware OTA da Arduino IDE.
- Aggiornamento firmware via browser caricando un file `.bin`.
- Controllo LED flash integrato.
- Due uscite digitali per relay o altri moduli.
- Lettura tensione batteria con percentuale stimata.
- Salvataggio in memoria di intervallo foto, flash e stato uscite.

## Hardware

- ESP32-CAM AI Thinker.
- Camera OV2640.
- Alimentazione stabile 5V, consigliati almeno 1A.
- Modulo USB-seriale solo per il primo caricamento.
- Moduli relay o transistor/MOSFET per pilotare carichi esterni.
- Partitore resistivo per leggere la batteria.

## Pin usati

| Funzione | GPIO | Note |
| --- | ---: | --- |
| Flash LED | 4 | LED integrato, puo scaldare se resta acceso |
| Relay 1 | 13 | Disponibile se non usi la microSD |
| Relay 2 | 14 | Disponibile se non usi la microSD |
| Batteria ADC | 33 | ADC1, utilizzabile con Wi-Fi attivo |
| Streaming | porta 81 | Endpoint `/stream` |
| Web UI | porta 80 | Pagina principale |

## Configurazione Wi-Fi

Apri [Esp32cam.ino](./Esp32cam.ino) e modifica:

```cpp
const char *WIFI_SSID = "NOME_WIFI";
const char *WIFI_PASSWORD = "PASSWORD_WIFI";
```

Cambia anche la password OTA, soprattutto su reti non private:

```cpp
const char *UPDATE_USER = "admin";
const char *UPDATE_PASSWORD = "admin";
```

## Impostazioni Arduino IDE

- Board: `AI Thinker ESP32-CAM`
- Upload Speed: `115200`
- Flash Frequency: `40MHz`
- PSRAM: `Enabled`
- Partition Scheme: scegli una partizione con supporto OTA, per esempio `Default 4MB with spiffs`. Evita schemi senza OTA.

Per il primo caricamento serve il collegamento USB-seriale. Dopo il primo avvio, apri il monitor seriale e copia l'indirizzo IP stampato dalla scheda.

## Uso

Apri nel browser:

```text
http://IP_DELLA_ESP32CAM/
```

Lo streaming video usa:

```text
http://IP_DELLA_ESP32CAM:81/stream
```

Dalla pagina puoi:

- vedere lo stream live;
- aggiornare la foto periodica;
- cambiare l'intervallo di scatto;
- accendere o spegnere il flash;
- pilotare Relay 1 e Relay 2;
- leggere tensione e percentuale batteria;
- accedere alla pagina di aggiornamento firmware.

## Relay e uscite digitali

Nel codice:

```cpp
#define RELAY1_PIN 13
#define RELAY2_PIN 14
#define RELAY_ACTIVE_HIGH true
```

Se il tuo modulo relay e attivo LOW:

```cpp
#define RELAY_ACTIVE_HIGH false
```

Non alimentare un relay meccanico direttamente dal pin ESP32. Usa un modulo relay compatibile 3.3V oppure un transistor/MOSFET con diodo di protezione. Collega sempre il GND in comune tra ESP32-CAM e modulo esterno.

## Lettura batteria

Lo sketch legge la batteria su `GPIO33`. Non collegare mai una batteria direttamente al pin ADC: l'ingresso non deve superare 3.3V.

Per una cella Li-ion/LiPo 1S puoi usare un partitore 100k/100k:

```text
BAT+ --- 100k --- GPIO33 --- 100k --- GND
```

Con questo partitore:

```cpp
const float BATTERY_DIVIDER_RATIO = 2.0;
```

La percentuale e una stima lineare tra:

```cpp
const float BATTERY_EMPTY_V = 3.20;
const float BATTERY_FULL_V = 4.20;
```

Se usi un altro partitore o una batteria diversa, aggiorna questi valori.

## Aggiornamento firmware via Wi-Fi

### Da browser

1. In Arduino IDE usa `Sketch > Export Compiled Binary`.
2. Apri:

```text
http://IP_DELLA_ESP32CAM/update
```

3. Inserisci utente e password impostati nello sketch.
4. Carica il file `.bin`.
5. La scheda si riavvia automaticamente.

### Da Arduino IDE

Dopo il primo upload via USB, se PC e scheda sono sulla stessa rete, in `Tools > Port` dovrebbe comparire una porta di rete simile a `esp32cam`. La password richiesta e `UPDATE_PASSWORD`.

## GitHub Pages

Questo repository include una pagina statica in [docs/index.html](./docs/index.html). Per pubblicarla:

1. Vai nelle impostazioni del repository GitHub.
2. Apri `Pages`.
3. Come source scegli `Deploy from a branch`.
4. Seleziona branch `main` e cartella `/docs`.
5. Salva.

## Note

- Se lo stream non e fluido, riduci `FRAMESIZE_VGA` a `FRAMESIZE_QVGA`.
- Se l'immagine e capovolta, cambia `sensor->set_vflip(sensor, 1);` mettendo `0`.
- Se usi la microSD, evita GPIO13 e GPIO14 per i relay.
- Non pubblicare password Wi-Fi reali nel repository.
