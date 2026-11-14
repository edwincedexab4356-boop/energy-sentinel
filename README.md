# Energy Sentinel · Aula inteligente IoT

**Detectar · Automatizar · Ahorrar** (ODS 7)

Aula que enciende las luces cuando hay personas y las apaga sola cuando queda vacía,
mide la temperatura y calcula cuánta energía y dinero ahorra. Se controla desde:

- **La página web** (`index.html`, publicada en GitHub Pages): funciona como simulador
  o **controla el Arduino real por USB** con el botón *Conectar Arduino (USB)* (Chrome o Edge).
- **El panel WiFi** que sirve el propio Arduino: red **EnergySentinel**, clave `aula12345`,
  abre **http://192.168.4.1** en el celular.
- **Internet (MQTT)**: si configuras el WiFi de casa, desde la pestaña *Arduino real (IoT)*.

## Cómo funciona

| Estado | Qué pasa | LEDs | Pantalla LCD |
|---|---|---|---|
| OCUPADA | El PIR detecta presencia: luces encendidas | verde | `AULA:OCUPADA AUT` / `LUZ:ON T:27.5C` |
| ESPERA | Nadie se mueve: cuenta atrás (30 s por defecto) | amarillo | `APAGA EN  12s` |
| VACIA | Se cumplió el tiempo: luces apagadas | azul | `MODO AHORRO` |

## Conexiones (con las piezas del grupo)

| Componente | Pines del módulo | Arduino UNO R4 WiFi |
|---|---|---|
| Sensor DHT11 (temperatura y humedad) | VCC / DATA / GND | 5V / **D4** / GND |
| Módulo relé 12 V → ventilador | VCC / GND / IN | +12V fuente / GND común / **D7** |
| Módulo comparador con relé → bomba de riego | VCC / GND / AO / DO | 5V / GND / **A0** / **D3** |
| Sensor de suelo resistivo | 2 pines | entrada de la sonda del comparador |
| Luces LED del techo | + / − | **D6** (con 220 Ω) / GND |
| LED verde (todo normal) | + / − | **D8** (con 220 Ω) / GND |
| LED amarillo (ventilador) | + / − | **D9** (con 220 Ω) / GND |
| LED azul (tierra seca) | + / − | **D10** (con 220 Ω) / GND |
| LCD 16x2 I2C (opcional) | VCC / GND / SDA / SCL | 5V / GND / **SDA** / **SCL** |
| Fuente de 12 V | + / − | **VIN** / GND |
| Sensor PIR (opcional) | VCC / OUT / GND | 5V / **D2** / GND (`HAY_PIR = true`) |

- Ventilador: COM del relé → +12V; NO → ventilador (+); ventilador (−) → GND de la fuente.
  Se enciende solo desde 28 °C (se cambia en la página) y se apaga 1.5 °C por debajo.
- Riego: el relé del comparador conecta la bomba (COM → +12V, NO → bomba +). La perilla azul
  fija cuándo la tierra cuenta como "seca". El Arduino lee AO/DO para mostrarlo en la página.
- Si el relé de 12 V funciona al revés (prende cuando debería apagar), pon su jumper en **H**
  o cambia `RELE_ACTIVO_ALTO` a `false`.
- Todos los GND (Arduino, fuente, relés, sensores) van unidos.

## Subir el programa

1. Abre `firmware/EnergySentinel/EnergySentinel.ino` en Arduino IDE.
2. Instala la librería **ArduinoMqttClient** (el DHT11 y la LCD no necesitan librerías).
3. Opcional: copia `arduino_secrets.example.h` como `arduino_secrets.h` y pon tu WiFi
   para usar internet.
4. Placa: **Arduino UNO R4 WiFi** → Subir.

## Comandos (USB a 115200, `http://IP/api/cmd?c=...` o MQTT)

`LUZ_ON` · `LUZ_OFF` · `AUTO` · `TIEMPO 30` · `POTENCIA 144` · `REINICIAR` · `ESTADO`

`VENT_ON` · `VENT_OFF` · `VENT_AUTO` · `TEMP_VENT 28` · `CAL_SECO` · `CAL_MOJADO`
