/*
  ENERGY SENTINEL - Aula inteligente IoT para el ahorro energetico
  "Detectar. Automatizar. Ahorrar."   (ODS 7)
  ------------------------------------------------------------
  Placa: Arduino UNO R4 WiFi
  Libreria necesaria (Gestor de librerias del Arduino IDE):
    - ArduinoMqttClient (Arduino)
  El DHT11 y la pantalla LCD I2C se manejan con codigo propio (sin librerias).

  CONEXIONES
    Sensor PIR HC-SR501          VCC -> 5V   OUT -> D2    GND -> GND
    Sensor DHT11 (temp/humedad)  VCC -> 5V   DATA -> D4   GND -> GND
    Modulo rele 5 V              VCC -> 5V   GND -> GND   IN -> D7
      (ventilador 5 V)           COM -> 5V   NO -> ventilador (+) ; ventilador (-) -> GND
    Luces del aula (LED blanco)  D6  -> 220 ohm -> LED (+) ; LED (-) -> GND
    LED verde  (aula ocupada)    D8  -> 220 ohm -> LED -> GND
    LED amarillo (ventilador)    D9  -> 220 ohm -> LED -> GND
    LCD 16x2 I2C                 VCC -> 5V   GND -> GND   SDA -> SDA   SCL -> SCL
                                 (direccion 0x27 o 0x3F: se detecta sola)

  CONTROL
    - Por USB (Serial 115200): lo usa la pagina web (boton "Conectar Arduino (USB)")
    - Por Bluetooth (BLE, el que trae la placa; no hace falta HC-05):
      pagina web -> "Conectar por Bluetooth" (Chrome en Android o en computadora)
      La placa no usa WiFi y Bluetooth a la vez: se elige con MODO_BLUETOOTH.
    - Por WiFi: panel en http://192.168.4.1 (red propia) o en la IP de tu WiFi
    - Por internet (MQTT): solo si configuras el WiFi de casa

  COMANDOS (texto, uno por linea)
    LUZ_ON | LUZ_OFF | AUTO | TIEMPO <seg> | POTENCIA <W> | REINICIAR | ESTADO
    VENT_ON | VENT_OFF | VENT_AUTO | TEMP_VENT <C>
*/

#include <WiFiS3.h>
#include <ArduinoMqttClient.h>
#include <ArduinoBLE.h>
#include <Wire.h>

// Datos de WiFi: en "arduino_secrets.h" (copia de arduino_secrets.example.h).
// Si no existe, se usan estos valores por defecto.
#if __has_include("arduino_secrets.h")
  #include "arduino_secrets.h"
#endif
#ifndef SECRET_SSID
  #define SECRET_SSID ""
  #define SECRET_PASS ""
#endif
#ifndef SECRET_CODIGO_AULA
  #define SECRET_CODIGO_AULA "aula-1234"
#endif
const char CODIGO_AULA[] = SECRET_CODIGO_AULA;   // el mismo que en la pagina web

// Red propia del Arduino (cuando no hay WiFi de casa)
const char AP_NOMBRE[] = "EnergySentinel";
const char AP_CLAVE[]  = "aula12345";

const char BROKER[] = "broker.hivemq.com";       // servidor MQTT gratuito
const int  PUERTO   = 1883;

// ================== PINES ==================
const uint8_t PIN_PIR         = 2;
const uint8_t PIN_DHT         = 4;
const uint8_t PIN_LUCES       = 6;
const uint8_t PIN_LED_OCUPADA = 8;   // verde: aula ocupada (luces encendidas)
const uint8_t PIN_LED_VENT    = 9;   // amarillo: ventilador encendido
const uint8_t PIN_RELE_VENT   = 7;    // modulo rele 5 V -> ventilador

// ================== OPCIONES ==================
// true  = Bluetooth (BLE) + USB.   false = WiFi / internet (MQTT) + USB.
const bool MODO_BLUETOOTH = true;
const char BLE_NOMBRE[] = "EnergySentinel";
const bool HAY_PIR = true;
// Logica del rele: true = se activa con HIGH. Si el ventilador funciona al reves, pon false.
const bool RELE_ACTIVO_ALTO = true;

// ================== AJUSTES ==================
float POTENCIA_W = 144.0;                  // aula real equivalente: 8 lamparas LED de 18 W
unsigned long tiempoApagadoMs = 30000UL;   // tiempo sin presencia antes de apagar
const unsigned long MS_CALENTAR_PIR = HAY_PIR ? 30000UL : 0;  // el PIR tarda ~30 s en estabilizarse
float tempVent = 28.0;                     // el ventilador se enciende desde esta temperatura
const float HISTERESIS = 1.5;              // y se apaga 1.5 C por debajo

// ================== ESTADO ==================
// 0 = VACIA, 1 = OCUPADA, 2 = ESPERA (cuenta atras)
int  estado = 0;
bool luces = false, manual = false, activo = false, pir = false;
float temperatura = NAN, humedad = NAN;
unsigned long ultimoMovimiento = 0, ultimoTick = 0;
double usoSeg = 0, consumoWh = 0, sinSistemaWh = 0;
String lcd1 = "", lcd2 = "";
bool vent = false, ventManual = false, bleConectado = false;
double ventSeg = 0;

WiFiServer servidor(80);
WiFiClient wifiCliente;
MqttClient mqtt(wifiCliente);
bool wifiOk = false, modoAP = false;
String temaEstado, temaCmd;

const char* nombreEstado() {
  if (estado == 1) return "OCUPADA";
  if (estado == 2) return "ESPERA";
  return "VACIA";
}

long restante() {
  if (estado != 2) return 0;
  long r = (long)(tiempoApagadoMs - (millis() - ultimoMovimiento)) / 1000;
  return r < 0 ? 0 : r;
}

void evento(const String& msg) {
  String m = msg; m.replace("\"", "'");
  Serial.print("{\"t\":\"ev\",\"msg\":\""); Serial.print(m); Serial.println("\"}");
}

void ponerLuces(bool on) {
  if (luces == on) return;
  luces = on;
  digitalWrite(PIN_LUCES, on ? HIGH : LOW);
  evento(String("Luces ") + (on ? "encendidas" : "apagadas") + (manual ? " (manual)" : " (automatico)"));
}

void ponerVent(bool on) {
  if (vent == on) return;
  vent = on;
  digitalWrite(PIN_RELE_VENT, (on == RELE_ACTIVO_ALTO) ? HIGH : LOW);
  evento(String("Ventilador ") + (on ? "encendido" : "apagado") + (ventManual ? " (manual)" : " (automatico)"));
}

// ================== DHT11 (sin libreria) ==================
// Espera a que el pin cambie de nivel; devuelve los microsegundos o 0 si se pasa del tiempo.
uint32_t esperarNivel(int nivel, uint32_t maxUs) {
  uint32_t t0 = micros();
  while (digitalRead(PIN_DHT) == nivel) if (micros() - t0 > maxUs) return 0;
  return micros() - t0;
}

bool leerDHT11() {
  uint8_t d[5] = {0, 0, 0, 0, 0};
  pinMode(PIN_DHT, OUTPUT);
  digitalWrite(PIN_DHT, LOW);
  delay(20);                              // senal de inicio (>18 ms)
  pinMode(PIN_DHT, INPUT_PULLUP);
  delayMicroseconds(30);
  if (!esperarNivel(HIGH, 100)) return false;   // respuesta: baja ~80 us
  if (!esperarNivel(LOW, 100))  return false;   //            sube ~80 us
  if (!esperarNivel(HIGH, 100)) return false;
  for (int i = 0; i < 40; i++) {
    if (!esperarNivel(LOW, 80)) return false;   // ~50 us en bajo antes de cada bit
    uint32_t alto = esperarNivel(HIGH, 100);    // 26 us = 0, 70 us = 1
    if (!alto) return false;
    d[i / 8] <<= 1;
    if (alto > 45) d[i / 8] |= 1;
  }
  if ((uint8_t)(d[0] + d[1] + d[2] + d[3]) != d[4]) return false;
  humedad = d[0] + d[1] / 10.0;
  temperatura = d[2] + (d[3] & 0x7F) / 10.0;
  return true;
}

// ================== LCD 16x2 I2C (PCF8574, sin libreria) ==================
// P0=RS P1=RW P2=EN P3=luz P4..P7=D4..D7
uint8_t lcdDir = 0;

void lcdBus(uint8_t v) { Wire.beginTransmission(lcdDir); Wire.write(v | 0x08); Wire.endTransmission(); }
void lcdNibble(uint8_t v) { lcdBus(v | 0x04); delayMicroseconds(1); lcdBus(v & ~0x04); delayMicroseconds(50); }
void lcdByte(uint8_t v, uint8_t rs) { lcdNibble((v & 0xF0) | rs); lcdNibble(((v << 4) & 0xF0) | rs); }
void lcdCmd(uint8_t c) { lcdByte(c, 0); if (c < 4) delay(2); }

void lcdIniciar() {
  Wire.begin();
  const uint8_t dirs[] = {0x27, 0x3F, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21, 0x20, 0x3E, 0x3D, 0x3C, 0x3B, 0x3A, 0x39, 0x38};
  for (uint8_t a : dirs) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { lcdDir = a; break; }
  }
  if (!lcdDir) { evento("Pantalla LCD no encontrada (revisa SDA/SCL)"); return; }
  delay(50);
  lcdBus(0);
  lcdNibble(0x30); delay(5); lcdNibble(0x30); delay(1); lcdNibble(0x30); lcdNibble(0x20);  // modo 4 bits
  lcdCmd(0x28);  // 2 lineas
  lcdCmd(0x0C);  // pantalla on, sin cursor
  lcdCmd(0x06);  // escribir hacia la derecha
  lcdCmd(0x01);  // borrar
  evento("Pantalla LCD en 0x" + String(lcdDir, HEX));
}

void lcdLinea(uint8_t fila, String txt) {
  while (txt.length() < 16) txt += ' ';
  txt = txt.substring(0, 16);
  String& previa = fila ? lcd2 : lcd1;
  if (txt == previa) return;
  previa = txt;
  if (!lcdDir) return;
  lcdCmd(0x80 | (fila ? 0x40 : 0x00));
  for (uint8_t i = 0; i < 16; i++) lcdByte(txt[i], 1);
}

void actualizarLCD() {
  if (millis() < MS_CALENTAR_PIR) {
    lcdLinea(0, "ENERGY SENTINEL");
    lcdLinea(1, "Iniciando PIR " + String((MS_CALENTAR_PIR - millis()) / 1000 + 1) + "s");
    return;
  }
  if (estado == 2 && !manual) lcdLinea(0, "APAGA EN " + String(restante()) + "s");
  else if (estado != 0)        lcdLinea(0, String("AULA OCUPADA ") + (manual ? "MAN" : ""));
  else                         lcdLinea(0, String(luces ? "LUZ ON (MANUAL)" : "AULA VACIA"));
  lcdLinea(1, "T:" + (isnan(temperatura) ? String("--.-") : String(temperatura, 1)) + "C VENT:" + (vent ? "ON" : "OFF"));
}

// ================== COMANDOS ==================
void reiniciarContadores() {
  usoSeg = consumoWh = sinSistemaWh = 0;
  activo = false;
}

String ejecutar(String cmd) {
  cmd.trim();
  String up = cmd; up.toUpperCase();
  up.replace(":", " ");
  if (up == "LUZ_ON" || up == "LUZ ON")   { manual = true; ponerLuces(true);  evento("Orden: encender luces"); return "OK"; }
  if (up == "LUZ_OFF" || up == "LUZ OFF") { manual = true; ponerLuces(false); evento("Orden: apagar luces"); return "OK"; }
  if (up == "AUTO") { manual = false; evento("Orden: modo automatico"); ponerLuces(estado != 0); return "OK"; }
  if (up.startsWith("TIEMPO")) {
    long s = up.substring(6).toInt();
    if (s < 5 || s > 600) return "ERROR tiempo 5-600 s";
    tiempoApagadoMs = s * 1000UL;
    evento("Apagar tras " + String(s) + " s sin presencia");
    return "OK";
  }
  if (up.startsWith("POTENCIA")) {
    float w = up.substring(8).toFloat();
    if (w < 1 || w > 5000) return "ERROR potencia 1-5000 W";
    POTENCIA_W = w; evento("Potencia del aula = " + String(w, 0) + " W"); return "OK";
  }
  if (up == "REINICIAR") { reiniciarContadores(); evento("Contadores de energia reiniciados"); return "OK"; }
  if (up == "ESTADO") return "OK";
  if (up == "VENT_ON")   { ventManual = true; ponerVent(true);  return "OK"; }
  if (up == "VENT_OFF")  { ventManual = true; ponerVent(false); return "OK"; }
  if (up == "VENT_AUTO") { ventManual = false; evento("Ventilador en automatico (desde " + String(tempVent, 1) + " C)"); return "OK"; }
  if (up.startsWith("TEMP_VENT")) {
    float t = up.substring(9).toFloat();
    if (t < 15 || t > 45) return "ERROR temperatura 15-45 C";
    tempVent = t; evento("Ventilador se enciende desde " + String(t, 1) + " C"); return "OK";
  }

  return "ERROR comando desconocido";
}

String jsonEstado() {
  String j = "{\"t\":\"st\",\"estado\":\"" + String(nombreEstado()) + "\"";
  j += ",\"luces\":" + String(luces ? 1 : 0);
  j += ",\"modo\":\"" + String(manual ? "MANUAL" : "AUTO") + "\"";
  j += ",\"pir\":" + String(pir ? 1 : 0);
  j += ",\"pir_listo\":" + String(millis() > MS_CALENTAR_PIR ? 1 : 0);
  j += ",\"temp\":" + (isnan(temperatura) ? String("null") : String(temperatura, 1));
  j += ",\"hum\":" + (isnan(humedad) ? String("null") : String(humedad, 0));
  j += ",\"restante\":" + String(restante());
  j += ",\"tiempo\":" + String(tiempoApagadoMs / 1000);
  j += ",\"usoSeg\":" + String((long)usoSeg);
  j += ",\"consumoWh\":" + String(consumoWh, 2);
  j += ",\"sinSistemaWh\":" + String(sinSistemaWh, 2);
  j += ",\"potencia\":" + String(POTENCIA_W, 0);
  j += ",\"lcd1\":\"" + lcd1 + "\",\"lcd2\":\"" + lcd2 + "\"";
  j += ",\"lcd_ok\":" + String(lcdDir ? 1 : 0);
  j += ",\"ip\":\"" + (wifiOk ? WiFi.localIP().toString() : String("")) + "\"";
  j += ",\"red\":\"" + String(modoAP ? AP_NOMBRE : (wifiOk ? SECRET_SSID : "")) + "\"";
  j += ",\"mqtt\":" + String(mqtt.connected() ? 1 : 0);
  j += ",\"hay_pir\":" + String(HAY_PIR ? 1 : 0);
  j += ",\"vent\":" + String(vent ? 1 : 0);
  j += ",\"vent_modo\":\"" + String(ventManual ? "MANUAL" : "AUTO") + "\"";
  j += ",\"temp_vent\":" + String(tempVent, 1);
  j += ",\"ventSeg\":" + String((long)ventSeg);
  j += ",\"ble\":" + String(bleConectado ? 1 : 0);
  j += "}";
  return j;
}

// Lectura de comandos por USB sin bloquear
String bufSerial = "";
void leerSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (bufSerial.length()) {
        String r = ejecutar(bufSerial);
        Serial.print("{\"t\":\"resp\",\"cmd\":\""); Serial.print(bufSerial);
        Serial.print("\",\"r\":\""); Serial.print(r); Serial.println("\"}");
        Serial.println(jsonEstado());
        bufSerial = "";
      }
    } else if (bufSerial.length() < 64) bufSerial += c;
  }
}

// ================== PANEL WEB (WiFi) ==================
const char PANEL_HTML[] = R"HTML(<!DOCTYPE html><html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Energy Sentinel</title>
<style>body{font-family:system-ui,sans-serif;background:#0a1120;color:#e6edf7;max-width:480px;margin:auto;padding:14px}
h1{font-size:20px;margin:0}.l{color:#45f0a0;font-size:12px;letter-spacing:.12em}
.e{font-size:26px;font-weight:700;padding:14px;border-radius:12px;text-align:center;margin:12px 0;background:#16213a}
.g{display:grid;grid-template-columns:1fr 1fr;gap:8px}.k{background:#111a2e;border:1px solid #22304f;border-radius:10px;padding:10px}
.k small{color:#8a9bb8;display:block;font-size:11px}.k b{font-size:18px}
button{font-size:16px;padding:14px;border:1px solid #2c3f66;border-radius:10px;background:#16213a;color:#e6edf7;width:100%}
.b{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;margin:12px 0}.lcd{background:#1b3fbf;border:4px solid #12266e;border-radius:6px;
font:600 15px monospace;white-space:pre;padding:6px 8px;color:#dfe9ff}</style></head>
<body><h1>ENERGY SENTINEL</h1><div class="l">DETECTAR · AUTOMATIZAR · AHORRAR</div>
<div class="e" id="e">...</div><div class="lcd" id="lcd"> </div>
<div class="b"><button onclick="c('LUZ_ON')">💡 Encender</button><button onclick="c('LUZ_OFF')">🌙 Apagar</button><button onclick="c('AUTO')">⚙ Auto</button></div>
<div class="g"><div class="k"><small>Luces</small><b id="lu">-</b></div><div class="k"><small>Temperatura</small><b id="te">-</b></div>
<div class="k"><small>Consumo</small><b id="co">-</b></div><div class="k"><small>Energia ahorrada</small><b id="ah">-</b></div>
<div class="k"><small>Tiempo de uso</small><b id="us">-</b></div><div class="k"><small>Apagar tras</small><b id="ti">-</b></div></div>
<p><button onclick="t()">Cambiar tiempo de apagado</button></p>
<script>const col={OCUPADA:'#1f6b4c',ESPERA:'#7a5a12',VACIA:'#1d3f73'};
async function c(x){await fetch('/api/cmd?c='+encodeURIComponent(x));u();}
function t(){const s=prompt('Segundos sin presencia antes de apagar (5-600)','30');if(s)c('TIEMPO '+s);}
const wh=v=>v>=1000?(v/1000).toFixed(2)+' kWh':v.toFixed(1)+' Wh';
async function u(){try{const j=await(await fetch('/api/estado')).json();
e.textContent=(j.estado=='ESPERA'?'ESPERA · apaga en '+j.restante+' s':j.estado=='VACIA'?'DESOCUPADA':'OCUPADA');e.style.background=col[j.estado];
lcd.textContent=j.lcd1+'\n'+j.lcd2;lu.textContent=(j.luces?'ENCENDIDAS':'APAGADAS')+(j.modo=='MANUAL'?' (man)':'');
te.textContent=j.temp==null?'--':j.temp+' °C';co.textContent=wh(j.consumoWh);
const a=j.sinSistemaWh-j.consumoWh;ah.textContent=wh(Math.max(0,a))+(j.sinSistemaWh>0?' ('+Math.round(a/j.sinSistemaWh*100)+'%)':'');
const s=j.usoSeg;us.textContent=[s/3600|0,(s/60|0)%60,s%60].map(n=>String(n).padStart(2,'0')).join(':');ti.textContent=j.tiempo+' s';}catch(x){}}
setInterval(u,1000);u();</script></body></html>)HTML";

String urlDecode(const String& s) {
  String r = "";
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '+') r += ' ';
    else if (c == '%' && i + 2 < s.length()) { r += (char)strtol(s.substring(i + 1, i + 3).c_str(), nullptr, 16); i += 2; }
    else r += c;
  }
  return r;
}

void responder(WiFiClient& cli, const char* tipo, const String& cuerpo) {
  cli.print("HTTP/1.1 200 OK\r\nContent-Type: "); cli.print(tipo);
  cli.print("\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: ");
  cli.print(cuerpo.length()); cli.print("\r\n\r\n"); cli.print(cuerpo);
}

void atenderWeb() {
  if (!wifiOk) return;
  WiFiClient cli = servidor.available();
  if (!cli) return;
  String linea = "", peticion = "";
  bool primera = true;
  uint32_t t0 = millis();
  while (cli.connected() && millis() - t0 < 500) {
    if (!cli.available()) continue;
    char c = cli.read();
    if (c == '\n') {
      if (primera) { peticion = linea; primera = false; }
      if (linea.length() <= 1) break;  // fin de cabeceras
      linea = "";
    } else linea += c;
  }
  int a = peticion.indexOf(' '), b = peticion.indexOf(' ', a + 1);
  String ruta = (a > 0 && b > a) ? peticion.substring(a + 1, b) : "/";
  if (ruta.startsWith("/api/estado")) responder(cli, "application/json", jsonEstado());
  else if (ruta.startsWith("/api/cmd?c=")) responder(cli, "text/plain", ejecutar(urlDecode(ruta.substring(11))));
  else responder(cli, "text/html; charset=utf-8", String(PANEL_HTML));
  delay(1);
  cli.stop();
}

// ================== BLUETOOTH (BLE, servicio tipo "UART") ==================
// La pagina escribe ordenes en RX y recibe el estado por TX (lineas JSON en trozos de 20 bytes).
BLEService bleServicio("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
BLECharacteristic bleRx("6E400002-B5A3-F393-E0A9-E50E24DCCA9E", BLEWrite | BLEWriteWithoutResponse, 64);
BLECharacteristic bleTx("6E400003-B5A3-F393-E0A9-E50E24DCCA9E", BLENotify, 20);
bool bleOk = false;
String bleBuf = "";

void iniciarBLE() {
  if (!BLE.begin()) { evento("Bluetooth no disponible"); return; }
  BLE.setLocalName(BLE_NOMBRE);
  BLE.setDeviceName(BLE_NOMBRE);
  BLE.setAdvertisedService(bleServicio);
  bleServicio.addCharacteristic(bleRx);
  bleServicio.addCharacteristic(bleTx);
  BLE.addService(bleServicio);
  BLE.advertise();
  bleOk = true;
  evento(String("Bluetooth listo: busca '") + BLE_NOMBRE + "'");
}

void bleEnviar(const String& linea) {
  if (!bleConectado || !bleTx.subscribed()) return;
  String t = linea + "\n";
  for (unsigned i = 0; i < t.length(); i += 20) {
    String trozo = t.substring(i, i + 20);
    bleTx.writeValue((const uint8_t*)trozo.c_str(), trozo.length());
  }
}

void atenderBLE() {
  if (!bleOk) return;
  BLE.poll();
  bool c = BLE.connected();
  if (c != bleConectado) { bleConectado = c; evento(c ? "Bluetooth conectado" : "Bluetooth desconectado"); }
  if (bleRx.written()) {
    int n = bleRx.valueLength();
    const uint8_t* v = bleRx.value();
    for (int i = 0; i < n; i++) {
      char ch = (char)v[i];
      if (ch == '\n' || ch == '\r') continue;
      bleBuf += ch;
    }
    if (bleBuf.length()) {
      String r = ejecutar(bleBuf);
      bleEnviar("{\"t\":\"resp\",\"cmd\":\"" + bleBuf + "\",\"r\":\"" + r + "\"}");
      bleBuf = "";
    }
  }
}

// ================== MQTT (internet) ==================
void alRecibir(int) {
  String c = "";
  while (mqtt.available()) c += (char)mqtt.read();
  evento("Orden por internet: " + c);
  ejecutar(c);
}

unsigned long ultimoIntentoMqtt = 0;
void conectarMqtt() {
  if (modoAP || !wifiOk || mqtt.connected()) return;
  if (millis() - ultimoIntentoMqtt < 15000 && ultimoIntentoMqtt) return;
  ultimoIntentoMqtt = millis();
  if (WiFi.status() != WL_CONNECTED) { WiFi.begin(SECRET_SSID, SECRET_PASS); return; }
  mqtt.setId(String("sentinel-") + CODIGO_AULA + "-" + String(random(10000)));
  mqtt.onMessage(alRecibir);
  mqtt.setTxPayloadSize(1024);   // el estado mide mas de 256 bytes (el limite por defecto)
  if (mqtt.connect(BROKER, PUERTO)) {
    mqtt.subscribe(temaCmd);
    evento(String("Internet (MQTT) conectado, aula ") + CODIGO_AULA);
  } else evento("Internet (MQTT) no disponible");
}

void publicarMqtt() {
  if (!mqtt.connected()) return;
  mqtt.beginMessage(temaEstado);
  mqtt.print(jsonEstado());
  mqtt.endMessage();
}

void iniciarWiFi() {
  if (WiFi.status() == WL_NO_MODULE) { evento("Modulo WiFi no encontrado"); return; }
  if (strlen(SECRET_SSID) == 0) {
    // Modo punto de acceso: el Arduino crea su propia red
    if (WiFi.beginAP(AP_NOMBRE, AP_CLAVE) == WL_AP_LISTENING) {
      wifiOk = modoAP = true; delay(1000);
      servidor.begin();
      evento(String("Red WiFi '") + AP_NOMBRE + "' -> http://" + WiFi.localIP().toString());
    } else evento("No se pudo crear la red WiFi");
    return;
  }
  lcdLinea(1, "Conectando WiFi");
  for (int i = 0; i < 3 && WiFi.status() != WL_CONNECTED; i++) { WiFi.begin(SECRET_SSID, SECRET_PASS); delay(3000); }
  if (WiFi.status() == WL_CONNECTED) {
    wifiOk = true;
    servidor.begin();
    evento("WiFi conectado -> http://" + WiFi.localIP().toString());
    conectarMqtt();
  } else evento("WiFi no conectado (solo USB)");
}

// ================== SETUP / LOOP ==================
void setup() {
  Serial.begin(115200);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_DHT, INPUT_PULLUP);
  pinMode(PIN_LUCES, OUTPUT);
  pinMode(PIN_LED_OCUPADA, OUTPUT);
  pinMode(PIN_LED_VENT, OUTPUT);
  digitalWrite(PIN_LUCES, LOW);
  digitalWrite(PIN_RELE_VENT, RELE_ACTIVO_ALTO ? LOW : HIGH);   // ventilador apagado al arrancar
  pinMode(PIN_RELE_VENT, OUTPUT);
  randomSeed(analogRead(A5));

  lcdIniciar();
  lcdLinea(0, "ENERGY SENTINEL");
  lcdLinea(1, "Detectar.Ahorrar");

  temaEstado = String("energysentinel/") + CODIGO_AULA + "/estado";
  temaCmd    = String("energysentinel/") + CODIGO_AULA + "/cmd";
  evento("Energy Sentinel iniciado");
  if (MODO_BLUETOOTH) iniciarBLE(); else iniciarWiFi();
  ultimoTick = millis();
}

unsigned long ultimoDHT = 0, ultimoLCD = 0, ultimoUSB = 0, ultimoMqtt = 0, ultimoBLE = 0;
int estadoPrevio = -1;

void loop() {
  unsigned long ahora = millis();
  leerSerial();

  // 1) DETECTAR
  pir = HAY_PIR && ahora > MS_CALENTAR_PIR && digitalRead(PIN_PIR) == HIGH;
  if (pir) { ultimoMovimiento = ahora; estado = 1; }
  else if (estado != 0) estado = (ahora - ultimoMovimiento < tiempoApagadoMs) ? 2 : 0;
  if (estado != estadoPrevio) {
    if (estadoPrevio >= 0) {
      if (estado == 1) evento("PIR detecta presencia: aula ocupada");
      if (estado == 2) evento("Aula vacia: temporizador de " + String(tiempoApagadoMs / 1000) + " s");
      if (estado == 0) evento("Tiempo cumplido: modo ahorro");
    }
    estadoPrevio = estado;
  }

  // 2) AUTOMATIZAR
  if (!manual) ponerLuces(estado != 0);
  if (!ventManual) {
    if (isnan(temperatura)) ponerVent(false);
    else if (temperatura >= tempVent) ponerVent(true);
    else if (temperatura <= tempVent - HISTERESIS) ponerVent(false);
  }
  digitalWrite(PIN_LED_OCUPADA, estado != 0);   // verde: aula ocupada / en espera
  digitalWrite(PIN_LED_VENT,    vent);          // amarillo: ventilador encendido

  // 3) AHORRAR: medir uso y energia cada segundo
  if (ahora - ultimoTick >= 1000) {
    double s = (ahora - ultimoTick) / 1000.0;
    ultimoTick = ahora;
    if (estado != 0) { usoSeg += s; activo = true; }
    if (luces) consumoWh += POTENCIA_W * s / 3600.0;
    if (activo) sinSistemaWh += POTENCIA_W * s / 3600.0;  // sin el sistema la luz quedaria encendida toda la jornada
    if (vent) ventSeg += s;
  }

  if (ahora - ultimoDHT >= 3000) {
    ultimoDHT = ahora;
    if (!leerDHT11()) { delay(50); leerDHT11(); }
  }
  if (ahora - ultimoLCD >= 300) { ultimoLCD = ahora; actualizarLCD(); }

  // 4) IoT
  atenderWeb();
  if (!modoAP && wifiOk) {
    if (!mqtt.connected()) conectarMqtt();
    else {
      mqtt.poll();
      if (ahora - ultimoMqtt >= 2000) { ultimoMqtt = ahora; publicarMqtt(); }
    }
  }
  atenderBLE();
  if (bleConectado && ahora - ultimoBLE >= 1000) { ultimoBLE = ahora; bleEnviar(jsonEstado()); }
  if (ahora - ultimoUSB >= 500) { ultimoUSB = ahora; Serial.println(jsonEstado()); }
}
