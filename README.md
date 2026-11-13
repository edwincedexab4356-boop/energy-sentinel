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

## Conexiones

| Componente | Pines del módulo | Arduino UNO R4 WiFi |
|---|---|---|
| Sensor PIR HC-SR501 | VCC / OUT / GND | 5V / **D2** / GND |
| Sensor DHT11 | VCC / DATA / GND | 5V / **D4** / GND |
| Luces LED del techo | + / − | **D6** (con 220 Ω) / GND |
| LED verde (ocupada) | + / − | **D8** (con 220 Ω) / GND |
| LED amarillo (espera) | + / − | **D9** (con 220 Ω) / GND |
| LED azul (ahorro) | + / − | **D10** (con 220 Ω) / GND |
| LCD 16x2 I2C | VCC / GND / SDA / SCL | 5V / GND / **SDA** / **SCL** |

### Importante al conectar

1. Cada LED lleva su resistencia de 220 Ω. La pata larga (+) va hacia el pin del Arduino.
2. Un pin del Arduino da poca corriente: para más de 2–3 LEDs de techo o una lámpara real
   usa un **módulo relé** o un transistor en D6.
3. El **PIR** tarda unos 30 s en estabilizarse al encender; el programa lo ignora ese tiempo.
   En el PIR pon el jumper en **H** y gira el potenciómetro de tiempo al mínimo.
4. La pantalla LCD se detecta sola (0x27 o 0x3F). Si no se ven letras, gira el potenciómetro
   azul de la parte de atrás (contraste).

## Subir el programa

1. Abre `firmware/EnergySentinel/EnergySentinel.ino` en Arduino IDE.
2. Instala la librería **ArduinoMqttClient** (el DHT11 y la LCD no necesitan librerías).
3. Opcional: copia `arduino_secrets.example.h` como `arduino_secrets.h` y pon tu WiFi
   para usar internet.
4. Placa: **Arduino UNO R4 WiFi** → Subir.

## Comandos (USB a 115200, `http://IP/api/cmd?c=...` o MQTT)

`LUZ_ON` · `LUZ_OFF` · `AUTO` · `TIEMPO 30` · `POTENCIA 144` · `REINICIAR` · `ESTADO`
