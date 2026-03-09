# 🍬 Secret Knock Candy Dispenser (Opendeurdag Editie)

Een interactieve snoepautomaat die reageert op ritmische klappen of tikken. Speciaal ontworpen voor de opendeurdag met live Bluetooth-monitoring en een ingebouwde snoepjes-teller.

## 🚀 Hoofdfuncties

- **Ritmische Activatie**: Werkt met de KY-037 microfoon module (3 klappen/tikken).
- **Trillingsgevoelig**: Dankzij de gevoelige analoge uitlezing werkt het zelfs door 17mm dik hout!
- **Bluetooth Live Monitoring**: Volg de status en de teller live op een tablet via BLE.
- **Sessie Statistieken**: Houdt bij hoeveel snoepjes er in totaal zijn uitgedeeld.
- **Auto-Cooldown**: Een 10-seconden pauze na elk snoepje om misbruik te voorkomen.
- **Multi-Board Support**: Werkt op Uno R4 WiFi, ESP32, Nano, en meer (via PlatformIO).

## 🛠️ Hardware Setup

- **Microcontroller**: Arduino Uno R4 WiFi (of ESP32/Nano).
- **Sensing**: KY-037 Microphone Module (**Pin A0**).
- **Servo**: SM-S2309S (**Pin 9**).
- **Feedback**:
  - Passive Buzzer (**Pin 11**).
  - Gele LED (**Pin 3**): Luister-modus.
  - Groene LED (**Pin 4**): Succes-modus.
  - Rode LED (**Pin 6**): Fout / Cooldown.

## 📱 App & Monitoring

Je kunt de automaat volgen via Bluetooth Low Energy (BLE).

- **Apparaatnaam**: `Snoepautomaat`
- **Kenmerken**: Live status updates en een actuele snoepjes-teller.
- **Flutter App**: De API-specificaties voor een eigen app vind je in de `brain` map.

## 💻 Installatie

Dit project maakt gebruik van **PlatformIO**.

1. Kloon deze repository.
2. Open de map in VS Code met PlatformIO.
3. Selecteer het juiste environment (bijv. `uno_r4_wifi`).
4. Klik op **Upload**.

---

_Gemaakt door Mats Tanghe - Opendeurdag Snoepautomaat Project_
