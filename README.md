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
| OCUPADA | El sensor ultrasónico detecta a alguien: luces encendidas | verde | `AULA:OCUPADA AUT` / `LUZ:ON T:27.5C` |
| ESPERA | Nadie se mueve: cuenta atrás (30 s por defecto) | amarillo | `APAGA EN  12s` |
| VACIA | Se cumplió el tiempo: luces apagadas | azul | `MODO AHORRO` |

## Conexiones

| Componente | Pines del módulo | Arduino UNO R4 WiFi |
|---|---|---|
| Sensor ultrasónico HC-SR04 (en la puerta) | VCC / Trig / Echo / GND | 5V / **D12** / **D2** / GND |
| Sensor DHT11 (temperatura y humedad) | VCC / DATA / GND | 5V / **D4** / GND |
| Módulo relé 5 V → ventilador 5 V | VCC / GND / IN | 5V / GND / **D7** |
| Luz 1 del aula (LED blanco) | + / − | **D6** (con 330 Ω) / GND |
| Luz 2 del aula (LED blanco) | + / − | **D3** (con 330 Ω) / GND |
| Luz 3 del aula (LED blanco) | + / − | **D5** (con 330 Ω) / GND |
| Luz 4 del aula (LED blanco) | + / − | **D11** (con 330 Ω) / GND |
| LED verde (aula ocupada) | + / − | **D8** (con 220 Ω) / GND |
| LED amarillo (ventilador) | + / − | **D9** (con 220 Ω) / GND |
| LED rojo (ahorro) | + / − | **D10** (con 220 Ω) / GND |
| Buzzer | + / − | **A0** / GND (pita al encender las luces y al apagar el ventilador) |
| LCD 16x2 I2C | VCC / GND / SDA / SCL | 5V / GND / **SDA** / **SCL** |

- El sensor ultrasónico de la puerta detecta a alguien a menos de 20 cm → se encienden las luces. Si nadie pasa en 30 s → se apagan.
- Ventilador: COM del relé → 5V; NO → ventilador (+); ventilador (−) → GND.
  Se enciende solo si hay alguien y hace 28 °C o más (se cambia en la página); se apaga 1.5 °C por debajo o si el aula queda vacía.
- Si el ventilador funciona al revés, cambia `RELE_ACTIVO_ALTO` a `false`.

## Bluetooth (sin HC-05)

La placa trae Bluetooth (BLE). En la página pulsa **Conectar por Bluetooth** (Chrome en Android
o computadora; en iPhone no funciona) y elige **EnergySentinel**. La placa usa Bluetooth **o** WiFi:
para volver a WiFi/internet pon `MODO_BLUETOOTH = false`.

## Subir el programa

1. Abre `firmware/EnergySentinel/EnergySentinel.ino` en Arduino IDE.
2. Instala las librerías **ArduinoMqttClient** y **ArduinoBLE** (el DHT11 y la LCD no necesitan librerías).
3. Opcional: copia `arduino_secrets.example.h` como `arduino_secrets.h` y pon tu WiFi
   para usar internet.
4. Placa: **Arduino UNO R4 WiFi** → Subir.

## Comandos (USB a 115200, Bluetooth, `http://IP/api/cmd?c=...` o MQTT)

`LUZ_ON` · `LUZ_OFF` · `AUTO` · `TIEMPO 30` · `POTENCIA 144` · `REINICIAR` · `ESTADO`

`LUZ1_ON` … `LUZ4_ON` · `LUZ1_OFF` … `LUZ4_OFF` (una luz sola)

`VENT_ON` · `VENT_OFF` · `VENT_AUTO` · `TEMP_VENT 28` · `GENTE_SI` · `GENTE_NO` · `GENTE_AUTO` · `DISTANCIA 20`

`BUZZER_ON` · `BUZZER_OFF` (silenciar) · `PITAR` (prueba)
