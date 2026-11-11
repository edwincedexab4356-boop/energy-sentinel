/*
  ENERGY SENTINEL - Sistema Inteligente IoT para el Ahorro Energetico
  "Detectar. Automatizar. Ahorrar."   (ODS 7)

  Placa: Arduino UNO R4 WiFi
  Librerias (Gestor de librerias del Arduino IDE):
    - ArduinoMqttClient   (Arduino)
    - DHT sensor library  (Adafruit)  + Adafruit Unified Sensor
    - LiquidCrystal I2C   (Frank de Brabander)

  CONEXIONES
    Sensor PIR HC-SR501   OUT -> D2    VCC -> 5V   GND -> GND
    Sensor DHT11          DATA -> D4   VCC -> 5V   GND -> GND
    Luces LED del techo   D6 -> resistencia 220 ohm -> LED -> GND
                          (si son muchos LEDs: D6 -> transistor/modulo rele)
    LED verde  (ocupada)  D8  -> 220 ohm -> GND
    LED amarillo (espera) D9  -> 220 ohm -> GND
    LED azul  (ahorro)    D10 -> 220 ohm -> GND
    LCD 16x2 I2C (0x27)   SDA -> SDA   SCL -> SCL   VCC -> 5V   GND -> GND

  PANEL WEB: abre el enlace del panel, entra en "Arduino real" y escribe el
  mismo CODIGO_AULA que pongas aqui abajo.
*/

#include <WiFiS3.h>
#include <ArduinoMqttClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// ---------- CAMBIA ESTO ----------
const char WIFI_SSID[]   = "NOMBRE_DE_TU_WIFI";
const char WIFI_PASS[]   = "CLAVE_DE_TU_WIFI";
const char CODIGO_AULA[] = "aula-1234";          // el mismo que en el panel web
// ---------------------------------

const char BROKER[] = "broker.hivemq.com";       // servidor MQTT gratuito
const int  PUERTO   = 1883;

const int PIN_PIR = 2;
const int PIN_DHT = 4;
const int PIN_LUCES = 6;
const int PIN_LED_OCUPADA = 8;
const int PIN_LED_ESPERA  = 9;
const int PIN_LED_AHORRO  = 10;

const float POTENCIA_W = 144.0;    // aula real equivalente: 8 lamparas LED de 18 W
unsigned long tiempoApagadoMs = 30000UL;   // tiempo sin presencia antes de apagar

LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(PIN_DHT, DHT11);
WiFiClient wifi;
MqttClient mqtt(wifi);
String temaEstado, temaCmd;

// estado: 0 = VACIA, 1 = OCUPADA, 2 = ESPERA (cuenta atras)
int estado = 0;
bool luces = false;
bool manual = false;
bool activo = false;   // la jornada empieza con la primera persona
float temperatura = NAN;
unsigned long ultimoMovimiento = 0, ultimoTick = 0, ultimoEnvio = 0;
unsigned long ultimoDHT = 0, ultimoIntento = 0, ultimoLCD = 0;
double usoSeg = 0, consumoWh = 0, sinSistemaWh = 0;

const char* nombreEstado() {
  if (estado == 1) return "OCUPADA";
  if (estado == 2) return "ESPERA";
  return "VACIA";
}

void ponerLuces(bool on) {
  luces = on;
  digitalWrite(PIN_LUCES, on ? HIGH : LOW);
}

void conectar() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("WiFi... ");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    if (WiFi.status() != WL_CONNECTED) { Serial.println("sin conexion"); return; }
    Serial.println("OK");
  }
  if (!mqtt.connected()) {
    Serial.print("MQTT... ");
    String id = String("sentinel-") + CODIGO_AULA + "-" + String(random(10000));
    mqtt.setId(id);
    mqtt.onMessage(alRecibir);
    if (!mqtt.connect(BROKER, PUERTO)) { Serial.println("fallo"); return; }
    mqtt.subscribe(temaCmd);
    Serial.println("OK");
  }
}

void publicarEstado() {
  if (!mqtt.connected()) return;
  long restante = 0;
  if (estado == 2) restante = (long)(tiempoApagadoMs - (millis() - ultimoMovimiento)) / 1000;
  String j = "{\"estado\":\"" + String(nombreEstado()) + "\"";
  j += ",\"luces\":" + String(luces ? 1 : 0);
  j += ",\"modo\":\"" + String(manual ? "MANUAL" : "AUTO") + "\"";
  j += ",\"temp\":" + (isnan(temperatura) ? String("null") : String(temperatura, 1));
  j += ",\"restante\":" + String(restante);
  j += ",\"tiempo\":" + String(tiempoApagadoMs / 1000);
  j += ",\"usoSeg\":" + String((long)usoSeg);
  j += ",\"consumoWh\":" + String(consumoWh, 2);
  j += ",\"sinSistemaWh\":" + String(sinSistemaWh, 2);
  j += ",\"potencia\":" + String(POTENCIA_W, 0) + "}";
  mqtt.beginMessage(temaEstado);
  mqtt.print(j);
  mqtt.endMessage();
}

bool enviarPronto = false;

void alRecibir(int largo) {
  String c = "";
  while (mqtt.available()) c += (char)mqtt.read();
  c.trim();
  Serial.println("Orden: " + c);
  if (c == "LUZ_ON")       { manual = true;  ponerLuces(true); }
  else if (c == "LUZ_OFF") { manual = true;  ponerLuces(false); }
  else if (c == "AUTO")    { manual = false; ponerLuces(estado != 0); }
  else if (c.startsWith("TIEMPO:")) {
    long s = c.substring(7).toInt();
    if (s >= 5 && s <= 600) tiempoApagadoMs = s * 1000UL;
  }
  enviarPronto = true;
}

void actualizarLCD() {
  char l1[17], l2[17];
  snprintf(l1, sizeof(l1), "AULA:%-7s %s", nombreEstado(), manual ? "MAN" : "AUT");
  if (estado == 2 && !manual) {
    long r = (long)(tiempoApagadoMs - (millis() - ultimoMovimiento)) / 1000;
    snprintf(l2, sizeof(l2), "APAGA EN %3lds   ", r);
  } else if (!luces) {
    snprintf(l2, sizeof(l2), "MODO AHORRO     ");
  } else {
    char t[8];
    if (isnan(temperatura)) strcpy(t, "--.-"); else dtostrf(temperatura, 4, 1, t);
    snprintf(l2, sizeof(l2), "LUZ:ON  T:%sC  ", t);
  }
  lcd.setCursor(0, 0); lcd.print(l1);
  lcd.setCursor(0, 1); lcd.print(l2);
}

void setup() {
  Serial.begin(9600);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_LUCES, OUTPUT);
  pinMode(PIN_LED_OCUPADA, OUTPUT);
  pinMode(PIN_LED_ESPERA, OUTPUT);
  pinMode(PIN_LED_AHORRO, OUTPUT);
  ponerLuces(false);
  dht.begin();
  lcd.init(); lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("ENERGY SENTINEL ");
  lcd.setCursor(0, 1); lcd.print("Detectar.Ahorrar");
  randomSeed(analogRead(A0));
  temaEstado = String("energysentinel/") + CODIGO_AULA + "/estado";
  temaCmd    = String("energysentinel/") + CODIGO_AULA + "/cmd";
  delay(2000);   // el PIR necesita unos segundos para estabilizarse
  conectar();
  ultimoTick = millis();
}

void loop() {
  unsigned long ahora = millis();

  // 1) DETECTAR
  if (digitalRead(PIN_PIR) == HIGH) {
    ultimoMovimiento = ahora;
    estado = 1;
  } else if (estado != 0) {
    estado = (ahora - ultimoMovimiento < tiempoApagadoMs) ? 2 : 0;
  }

  // 2) AUTOMATIZAR
  if (!manual) {
    if (estado != 0 && !luces) ponerLuces(true);
    if (estado == 0 && luces)  ponerLuces(false);
  }
  digitalWrite(PIN_LED_OCUPADA, estado == 1);
  digitalWrite(PIN_LED_ESPERA,  estado == 2);
  digitalWrite(PIN_LED_AHORRO,  !luces);

  // 3) AHORRAR: medir uso y energia cada segundo
  if (ahora - ultimoTick >= 1000) {
    double s = (ahora - ultimoTick) / 1000.0;
    ultimoTick = ahora;
    if (estado != 0) usoSeg += s;
    if (luces) consumoWh += POTENCIA_W * s / 3600.0;
    if (estado != 0) activo = true;
    if (activo) sinSistemaWh += POTENCIA_W * s / 3600.0;   // sin el sistema la luz quedaria encendida toda la jornada
  }

  if (ahora - ultimoDHT >= 2500) {
    ultimoDHT = ahora;
    float t = dht.readTemperature();
    if (!isnan(t)) temperatura = t;
  }
  if (ahora - ultimoLCD >= 500) { ultimoLCD = ahora; actualizarLCD(); }

  // 4) IoT
  if (WiFi.status() != WL_CONNECTED || !mqtt.connected()) {
    if (ahora - ultimoIntento > 10000) { ultimoIntento = ahora; conectar(); }
  } else {
    mqtt.poll();
    if (enviarPronto || ahora - ultimoEnvio >= 2000) {
      enviarPronto = false; ultimoEnvio = ahora; publicarEstado(); }
  }
}
