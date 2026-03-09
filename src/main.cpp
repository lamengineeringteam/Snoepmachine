/**
 * Secret Knock Candy Dispenser
 *
 * Een project dat snoep uitdeelt wanneer het "We Will Rock You" ritme wordt
 * herkend. Werkt op Uno, Nano, Mega, ESP32 en Uno R4 WiFi.
 *
 * Hardware:
 * - Touch Sensor: Pin 5
 * - Servo: Pin 9
 * - LED Rood (Fout): Pin 2
 * - LED Oranje (Luisteren): Pin 3
 * - LED Groen (Succes): Pin 4
 */

#include <Arduino.h>

// Voor ESP32 gebruiken we de ESP32Servo bibliotheek
#ifdef ESP32
#include <ESP32Servo.h>
#else
#include <Servo.h>
#endif

// LED Matrix ondersteuning (Uno R4 WiFi)
#ifdef ARDUINO_UNOR4_WIFI
#include "ArduinoGraphics.h"
#include "Arduino_LED_Matrix.h"
ArduinoLEDMatrix matrix;
#endif

// Draadloze features ondersteuning (Uno R4 WiFi of ESP32)
#if defined(ARDUINO_UNOR4_WIFI) || defined(ESP32)
#define HAS_WIFI_BLE
#include <ArduinoBLE.h>
#include <EEPROM.h>
#include <WiFi.h>
#endif

// --- PIN DEFINITIES ---
#if defined(ARDUINO_UNOR4_WIFI) || defined(ESP32)
const int PIN_INPUT = A0;   // Analoog (KY-037 of Touch)
const int PIN_LED_ROOD = 6; // Verhuisd van 2 naar 6 voor R4/ESP32 consistentie
const int PIN_SECRET_BUTTON = 7; // Geheime knop
#else
const int PIN_INPUT = 5; // Standaard touch sensor op Pin 5
const int PIN_LED_ROOD = 2;
const int PIN_SECRET_BUTTON = 7;
#endif

const int PIN_SERVO = 9;
const int PIN_LED_GEEL = 3;
const int PIN_LED_GROEN = 4;
const int PIN_BUZZER = 11; // Passive buzzer op Pin 11

// --- CONFIGURATIE ---
const unsigned long DEBOUNCE_TIME = 100; // Snappere respons voor klappen
const unsigned long RESET_TIMEOUT =
    3000;                    // ms om de sessie te resetten naar niks doen
const int TARGET_KNOCKS = 3; // Aantal klappen voor activatie
const int MICROPHONE_BASELINE = 28;         // Rust-waarde van jouw sensor
const int MICROPHONE_THRESHOLD = 15;        // Hoeveel moet hij afwijken van 28
const unsigned long COOLDOWN_TIME = 10000;  // 10 seconden afkoelperiode
const unsigned long RESET_HOLD_TIME = 3000; // 3 seconden voor reset

// --- STATISTIEKEN ---
int totalCandiesDispensed = 0;
const int EEPROM_ADDR = 0;

// --- BLUETOOTH & WIFI ---
#ifdef HAS_WIFI_BLE
BLEService candyService("19B10000-E8F2-537E-4F6C-D104768A1214");
BLEIntCharacteristic candyCharacteristic("19B10001-E8F2-537E-4F6C-D104768A1214",
                                         BLERead | BLENotify);
BLEStringCharacteristic
    statusCharacteristic("19B10002-E8F2-537E-4F6C-D104768A1214",
                         BLERead | BLENotify, 20);
BLEByteCharacteristic
    resetCharacteristic("19B10003-E8F2-537E-4F6C-D104768A1214", BLEWrite);
BLEByteCharacteristic
    dispenseCharacteristic("19B10004-E8F2-537E-4F6C-D104768A1214", BLEWrite);

WiFiServer server(80);
#endif

// --- SERVO VARIABELEN ---
Servo candyServo;
const int SERVO_CLOSED = 0;
const int SERVO_OPEN = 90;
const unsigned long SERVO_WAIT_TIME = 1500; // ms dat de servo open blijft

// --- RITME VARIABELEN ---
unsigned long knockTimes[TARGET_KNOCKS];
int knockCount = 0;
unsigned long lastTouchTime = 0;
bool lastTouchState = LOW;

// --- BUTTON VARIABELEN ---
unsigned long buttonPressTime = 0;
bool buttonWasPressed = false;

// --- STATE MACHINE ---
enum State { IDLE, LISTENING, SUCCESS, FAILURE, COOLDOWN };
State currentState = IDLE;
unsigned long stateStartTime = 0;
bool isManualOperation = false;

// --- FUNCTIES ---

/**
 * Geluidssignalen
 */
void playKnockSound() { tone(PIN_BUZZER, 2000, 30); }
void playSuccessSound() {
  tone(PIN_BUZZER, 1000, 100);
  delay(150);
  tone(PIN_BUZZER, 1500, 150);
  delay(200);
  tone(PIN_BUZZER, 2000, 300);
}
void playFailureSound() { tone(PIN_BUZZER, 200, 500); }

/**
 * Update BLE status string
 */
void updateBLEStatus(const char *status) {
#ifdef HAS_WIFI_BLE
  statusCharacteristic.writeValue(status);
#endif
}

/**
 * Toon de actuele statistieken
 */
void printStats() {
  Serial.println("---------------------------------");
  Serial.print("Totaal snoepjes uitgedeeld: ");
  Serial.println(totalCandiesDispensed);
  Serial.println("---------------------------------");
#ifdef HAS_WIFI_BLE
  candyCharacteristic.writeValue(totalCandiesDispensed);
  // Opslaan in EEPROM (NVS)
  EEPROM.put(EEPROM_ADDR, totalCandiesDispensed);
#endif

#ifdef ARDUINO_UNOR4_WIFI
  matrix.beginDraw();
  matrix.background(0); // Scherm wissen
  matrix.stroke(0xFFFFFFFF);
  matrix.textFont(Font_5x7);
  matrix.beginText(0, 1, 0xFFFFFF);
  matrix.println(totalCandiesDispensed);
  matrix.endText();
  matrix.endDraw();
#endif
}

/**
 * Reset de teller
 */
void resetCounter() {
  Serial.println("Resetting teller...");
  totalCandiesDispensed = 0;
  printStats();
}

/**
 * Start het snoepproces handmatig (omzeil klappen)
 */
void triggerManualDispense() {
  // Sta handmatige dispense toe vanuit elke status (behalve als de servo al
  // open staat)
  if (currentState != SUCCESS) {
    currentState = SUCCESS;
    isManualOperation = true;
    stateStartTime = millis();
    totalCandiesDispensed++;
    digitalWrite(PIN_LED_GEEL, LOW);
    digitalWrite(PIN_LED_GROEN, HIGH);
    candyServo.write(SERVO_OPEN);
    delay(500); // Wait 500ms for full transit before matrix/EEPROM updates
    Serial.println("MANUELE DISPENSE! Snoepje voor de baas.");
    updateBLEStatus("SNOEPJE!");
    printStats();
  }
}

/**
 * Synchroniseer alle data met de verbonden BLE centrale
 */
void syncBLE() {
#ifdef HAS_WIFI_BLE
  candyCharacteristic.writeValue(totalCandiesDispensed);
  switch (currentState) {
  case IDLE:
    updateBLEStatus("Wachten...");
    break;
  case LISTENING:
    updateBLEStatus("Luisteren...");
    break;
  case SUCCESS:
    updateBLEStatus("SNOEPJE!");
    break;
  case FAILURE:
    updateBLEStatus("FOUT!");
    break;
  case COOLDOWN:
    updateBLEStatus("Cooldown...");
    break;
  }
#endif
}

/**
 * Reset de sessie naar de basisstatus of naar cooldown.
 */
void resetSession() {
  knockCount = 0;
  currentState = IDLE;
  isManualOperation = false;
  digitalWrite(PIN_LED_GEEL, LOW);
  digitalWrite(PIN_LED_ROOD, LOW);
  digitalWrite(PIN_LED_GROEN, LOW);
  Serial.println("Klaar voor volgende bezoeker.");
  updateBLEStatus("Wachten...");
}

/**
 * Controleert of er voldaan is aan de 3 tikken.
 */
bool validateRhythm() {
  if (knockCount == TARGET_KNOCKS) {
    return true;
  }
  return false;
}

#ifdef HAS_WIFI_BLE
void handleWebServer() {
  WiFiClient client = server.available();
  if (client) {
    String currentLine = "";
    String request = "";
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        request += c;
        if (c == '\n') {
          if (currentLine.length() == 0) {
            // Verwerk endpoints
            if (request.indexOf("GET /dispense") >= 0) {
              triggerManualDispense();
              client.println("HTTP/1.1 200 OK\n\nOK");
              break;
            }
            if (request.indexOf("GET /reset") >= 0) {
              resetCounter();
              client.println("HTTP/1.1 200 OK\n\nOK");
              break;
            }
            if (request.indexOf("GET /stats") >= 0) {
              client.println("HTTP/1.1 200 OK\nContent-Type: text/plain\n");
              client.print(totalCandiesDispensed);
              break;
            }

            // Standaard Dashboard Pagina
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println();

            client.println("<html><head><meta name='viewport' "
                           "content='width=device-width, initial-scale=1'>");
            client.println(
                "<style>body{font-family:sans-serif;text-align:center;"
                "background:#1a1a1a;color:white;padding-top:20px;}");
            client.println(".btn{display:block;width:80%;margin:15px "
                           "auto;padding:25px;font-size:24px;border:none;"
                           "border-radius:15px;color:white;cursor:pointer;font-"
                           "weight:bold;box-shadow:0 5px #999;}");
            client.println(".btn:active{box-shadow:0 2px "
                           "#666;transform:translateY(3px);}");
            client.println(
                ".green{background:#2ecc71;}.red{background:#e74c3c;} "
                ".stat-box{font-size:40px;margin:20px;padding:20px;background:#"
                "333;border-radius:15px;} "
                ".stat-label{font-size:16px;color:#888;}</style></head><body>");
            client.println("<h1>Snoep-Dashboard</h1>");
            client.println(
                "<div class='stat-box'><div class='stat-label'>TOTAAL "
                "UITGEDEELD</div><div id='count'>-</div></div>");
            client.println("<button class='btn green' "
                           "onclick=\"api('/dispense')\">SNOEPJE "
                           "GEVEN</button>");
            client.println(
                "<button class='btn red' onclick=\"if(confirm('Zeker "
                "weten?'))api('/reset')\">RESET TELLER </button>");
            client.println("<script>");
            client.println("function api(u){fetch(u).then(()=>update());}");
            client.println("function "
                           "update(){fetch('/"
                           "stats').then(r=>r.text()).then(t=>{document."
                           "getElementById('count').innerText=t;});}");
            client.println(
                "setInterval(update, 2000); update();"); // Elke 2 sec polls
            client.println("</script></body></html>");
            client.println();
            break;
          } else {
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }
    client.stop();
  }
}
#endif

void setup() {
  Serial.begin(115200);

#ifdef ARDUINO_UNOR4_WIFI
  matrix.begin();
#endif

  // Statistieken inladen uit NVS
#ifdef HAS_WIFI_BLE
#ifdef ESP32
  EEPROM.begin(512); // ESP32 heeft begin() nodig voor EEPROM-emulatie
#endif
  EEPROM.get(EEPROM_ADDR, totalCandiesDispensed);
  if (totalCandiesDispensed < 0 || totalCandiesDispensed > 1000000) {
    totalCandiesDispensed = 0;
  }

  // WiFi Access Point Starten
#ifdef ESP32
  WiFi.softAP("Snoepautomaat-WiFi", "snoepjes");
#else
  WiFi.beginAP("Snoepautomaat-WiFi", "snoepjes");
#endif
  server.begin();
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("WiFi AP gestart. IP: ");
  Serial.println(myIP);

  // Bluetooth Initialisatie
  if (!BLE.begin()) {
    Serial.println("Bluetooth start mislukt!");
  } else {
    BLE.setLocalName("Snoepautomaat");
    BLE.setAdvertisedService(candyService);
    candyService.addCharacteristic(candyCharacteristic);
    candyService.addCharacteristic(statusCharacteristic);
    candyService.addCharacteristic(resetCharacteristic);
    candyService.addCharacteristic(dispenseCharacteristic);
    BLE.addService(candyService);

    candyCharacteristic.writeValue(totalCandiesDispensed);
    statusCharacteristic.writeValue("Opstarten...");
    BLE.advertise();
    Serial.println("Bluetooth actief (Snoepautomaat)");
  }
#else
  pinMode(PIN_INPUT, INPUT); // Standaard touch sensor
#endif
  pinMode(PIN_LED_ROOD, OUTPUT);
  pinMode(PIN_LED_GEEL, OUTPUT);
  pinMode(PIN_LED_GROEN, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_SECRET_BUTTON, INPUT_PULLUP);

  // Servo setup
#ifdef ESP32
  // ESP32 heeft specifieke PWM setup nodig voor Servo
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  candyServo.setPeriodHertz(50);
#endif

  // SM-S2309S werkt goed met 500-2500us
  candyServo.attach(PIN_SERVO, 500, 2500);
  candyServo.write(SERVO_CLOSED);

  Serial.println("Snoepautomaat V3.0 Klaar (WiFi+BT+Stats)!");
  printStats();
  updateBLEStatus("Wachten...");
}

void loop() {
  unsigned long currentTime = millis();

#ifdef HAS_WIFI_BLE
  BLE.poll();        // BLE events afhandelen
  handleWebServer(); // WiFi requests afhandelen

  // Detecteer nieuwe BLE verbinding voor synchronisatie
  static bool wasConnected = false;
  BLEDevice central = BLE.central();
  if (central) {
    if (!wasConnected) {
      Serial.print("BLE Verbonden: ");
      Serial.println(central.address());
      syncBLE();
      wasConnected = true;
    }
  } else {
    if (wasConnected) {
      Serial.println("BLE Verbinding verbroken.");
      wasConnected = false;
    }
  }

  // Check voor Remote Reset via Bluetooth
  if (resetCharacteristic.written()) {
    if (resetCharacteristic.value() == 1) {
      resetCounter();
    }
  }

  // Check voor Remote Dispense via Bluetooth
  if (dispenseCharacteristic.written()) {
    if (dispenseCharacteristic.value() == 1) {
      triggerManualDispense();
    }
  }
#endif

  // --- 0. CHECK FYSIEKE SECRET BUTTON (Incl. Long Press Reset) ---
  bool isButtonPressed = (digitalRead(PIN_SECRET_BUTTON) == LOW);

  if (isButtonPressed && !buttonWasPressed) {
    // Net ingedrukt
    buttonPressTime = currentTime;
    buttonWasPressed = true;
  } else if (!isButtonPressed && buttonWasPressed) {
    // Net losgelaten
    unsigned long holdTime = currentTime - buttonPressTime;
    if (holdTime < RESET_HOLD_TIME) {
      triggerManualDispense(); // Short press: dispense
    }
    buttonWasPressed = false;
  }

  // Real-time check voor long press reset (hoeft niet losgelaten te worden)
  if (isButtonPressed && (currentTime - buttonPressTime > RESET_HOLD_TIME)) {
#ifdef ARDUINO_UNOR4_WIFI
    matrix.beginDraw();
    matrix.background(0); // volledige achtergrond wissen
    matrix.stroke(0xFFFFFFFF);
    matrix.textFont(Font_5x7);
    matrix.beginText(0, 1, 0xFFFFFF);

    // Oude tekst volledig overschrijven door een “blank” regel
    matrix.println("     "); // zoveel spaties als maximale cijfers
    // Nu echte waarde tekenen
    matrix.println(totalCandiesDispensed);

    matrix.endText();
    matrix.endDraw();
#endif

    resetCounter();

    buttonWasPressed = false; // Voorkom herhaalde reset
    // Wacht tot knop wordt losgelaten om triggers te voorkomen
    while (digitalRead(PIN_SECRET_BUTTON) == LOW) {
      delay(10);
    }
  }

  // --- 1. INPUT UITLEZEN (Alleen als we niet in succes/failure/cooldown zijn)
  // ---
  bool isActivated = false;

  if (currentState == IDLE || currentState == LISTENING) {
#ifdef HAS_WIFI_BLE
    // Voor de Uno R4 WiFi met KY-037 Microphone A0 (Analoog)
    int sensorValue = analogRead(PIN_INPUT);

    // We checken of de waarde afwijkt van de rust-waarde (28)
    // Dit vangt zowel pieken (bijv. 60) als dalen (bijv. 5) op.
    if (abs(sensorValue - MICROPHONE_BASELINE) > MICROPHONE_THRESHOLD) {
      isActivated = true;
    }
#else
    // Voor standaard touch sensor: actief bij HIGH
    isActivated = (digitalRead(PIN_INPUT) == HIGH);
#endif
  }

  // Detecteer een "klap" met debounce
  if (isActivated && (lastTouchState == LOW) &&
      (currentTime - lastTouchTime > DEBOUNCE_TIME)) {
    lastTouchTime = currentTime;
    lastTouchState = HIGH;

    playKnockSound(); // Feedback klik

    if (currentState == IDLE) {
      currentState = LISTENING;
      stateStartTime = currentTime;
      digitalWrite(PIN_LED_GEEL, HIGH);
      Serial.println("Luisteren gestart...");
      updateBLEStatus("Luisteren...");
    }

    if (currentState == LISTENING) {
      if (knockCount < TARGET_KNOCKS) {
        knockTimes[knockCount] = currentTime;
        knockCount++;
        Serial.print("Klap ");
        Serial.println(knockCount);
      }
      stateStartTime = currentTime;
    }
  }

  // Als de input wordt 'losgelaten' (of geluid stopt), reset lastTouchState
  if (!isActivated) {
    lastTouchState = LOW;
  }

  // --- 2. LOGICA AFHANDELEN ---
  switch (currentState) {
  case LISTENING:
    // Als we alle klappen hebben ontvangen
    if (knockCount == TARGET_KNOCKS) {
      if (validateRhythm()) {
        currentState = SUCCESS;
        stateStartTime = currentTime;
        totalCandiesDispensed++;
        digitalWrite(PIN_LED_GEEL, LOW);
        digitalWrite(PIN_LED_GROEN, HIGH);
        playSuccessSound();
        candyServo.write(SERVO_OPEN);
        Serial.println("SUCCES! Snoepje komt eraan.");
        updateBLEStatus("SNOEPJE!");
        printStats();
      } else {
        currentState = FAILURE;
        stateStartTime = currentTime;
        digitalWrite(PIN_LED_GEEL, LOW);
        digitalWrite(PIN_LED_ROOD, HIGH);
        playFailureSound();
        Serial.println("FOUT ritme.");
        updateBLEStatus("FOUT!");
      }
    }
    // Timeout: als de gebruiker stopt met kloppen
    else if (currentTime - stateStartTime > RESET_TIMEOUT) {
      resetSession();
    }
    break;

  case SUCCESS:
    // Wacht 1.5 seconde (non-blocking) en sluit de servo
    if (currentTime - stateStartTime > SERVO_WAIT_TIME) {
      candyServo.write(SERVO_CLOSED);
      // Blijf nog even in SUCCESS status voor de groene LED (bijv 500ms extra)
      if (currentTime - stateStartTime > SERVO_WAIT_TIME + 500) {
        if (isManualOperation) {
          resetSession(); // Geen cooldown voor handmatige acties
        } else {
          // Start cooldown na succesvol klappen
          currentState = COOLDOWN;
          stateStartTime = currentTime;
          digitalWrite(PIN_LED_GROEN, LOW);
          Serial.println("Cooldown gestart...");
          updateBLEStatus("Cooldown...");
        }
      }
    }
    break;

  case FAILURE:
    // Laat de rode LED 1 seconde branden (non-blocking)
    if (currentTime - stateStartTime > 1000) {
      resetSession();
    }
    break;

  case COOLDOWN:
    // Rode LED laten knipperen tijdens cooldown
    if (((currentTime - stateStartTime) / 500) % 2 == 0) {
      digitalWrite(PIN_LED_ROOD, HIGH);
    } else {
      digitalWrite(PIN_LED_ROOD, LOW);
    }

    if (currentTime - stateStartTime > COOLDOWN_TIME) {
      Serial.println("Cooldown voorbij.");
      resetSession();
    }
    break;

  case IDLE:
  default:
    // Niks te doen
    break;
  }
}
