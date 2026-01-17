# Système de Monitoring de Compost - ESP32

## Description
Système de monitoring pour bacs de compost avec 4 cartes ESP32 :
- **1 carte maître** : Collecte les données BLE, stockage SD, interface Android
- **3 cartes esclaves** : Acquisition de température, humidité (et O2 pour le bac d'apport)

## Architecture

### Bacs monitorés
1. **Bac d'apport** (Esclave 1) : Température, Humidité, Oxygène (BME280 + SEN0322)
2. **Bac de maturation** (Esclave 2) : Température, Humidité (BME280)
3. **Extérieur** (Esclave 3) : Température, Humidité (BME280)

### Communication
- **Esclaves → Maître** : BLE (Bluetooth Low Energy)
- **Maître → Android** : BLE
- **Stockage** : Carte SD (SPI) sur carte maître

### Cycle de fonctionnement
- **Esclaves** : Réveil toutes les 30 min → Mesure → Transmission BLE → Deep Sleep
- **Maître** : Scan BLE continu → Réception données → Sauvegarde SD

## Compilation et Upload

### Pour compiler la carte MAÎTRE :
```bash
pio run -e master
pio run -e master -t upload
```

### Pour compiler les cartes ESCLAVES :

**Bac d'apport (avec capteur O2) :**
```bash
pio run -e slave_apport
pio run -e slave_apport -t upload
```

**Bac de maturation :**
```bash
pio run -e slave_maturation
pio run -e slave_maturation -t upload
```

**Extérieur :**
```bash
pio run -e slave_exterieur
pio run -e slave_exterieur -t upload
```

## Configuration matérielle

### Bus I2C (toutes les cartes)
- **SDA** : GPIO 21
- **SCL** : GPIO 22

### Adresses I2C
- **BME280** : 0x76
- **SEN0322 (Oxygène)** : 0x73

### SPI Carte SD (carte maître uniquement)
- **CS** : GPIO 5
- **MOSI** : GPIO 23
- **MISO** : GPIO 19
- **SCK** : GPIO 18

## Format des données SD

Fichier CSV : `/compost_data.csv`

Format :
```
timestamp,board_id,board_name,temperature,humidity,oxygen
1234567890,1,Bac_Apport,45.23,65.40,18.50
1234567890,2,Bac_Maturation,42.10,70.30,-1.00
1234567890,3,Exterieur,22.50,55.20,-1.00
```

## Communication Android

### Connexion BLE
- **Nom du dispositif** : `Compost_Master`
- **Service UUID** : `6e400001-b5a3-f393-e0a9-e50e24dcca9e`

### Commandes disponibles
- **`READ`** : Récupérer toutes les données du fichier SD
- **`CLEAR`** : Effacer toutes les données

### Exemple d'utilisation Android
```
1. Se connecter au dispositif "Compost_Master"
2. Écrire "READ" sur la caractéristique RX
3. Lire les données sur la caractéristique TX (envoi par chunks)
4. Fin signalée par {"end":true}
```

## Bibliothèques utilisées

### Externes (installées automatiquement)
- Adafruit BME280 Library (^2.2.2)
- Adafruit Unified Sensor (^1.1.14)
- DFRobot OxygenSensor (^1.0.0)
- ArduinoJson (^7.0.0)

### Locale
- **CompostSensors** : Gestion des capteurs BME280 et SEN0322

## Structure du projet

```
Composte/
├── platformio.ini          # Configuration des 4 environnements
├── include/
│   └── config.h            # Configuration globale (pins, UUIDs, constantes)
├── lib/
│   └── CompostSensors/     # Bibliothèque de gestion des capteurs
│       ├── library.json
│       ├── CompostSensors.h
│       └── CompostSensors.cpp
└── src/
    ├── main.cpp            # Point d'entrée (inclusion conditionnelle)
    ├── master.cpp          # Code carte maître
    └── slave.cpp           # Code cartes esclaves
```

## Paramètres de monitoring

### Conformité cahier des charges
- Température minimale réacteur : **70°C**
- Taille maximale particules : **12 mm**
- Durée minimale sans interruption : **60 minutes**

### Intervalles
- **Cycle de mesure** : 30 minutes
- **Temps d'advertising esclave** : 15 secondes
- **Temps de scan maître** : 10 secondes

## Démarrage rapide

1. **Connecter les capteurs** sur le bus I2C (GPIO 21/22)
2. **Compiler et uploader** le code pour chaque carte (voir commandes ci-dessus)
3. **Insérer une carte SD** dans la carte maître
4. **Alimenter les cartes** : Les esclaves commenceront leur cycle automatiquement
5. **Application Android** : Se connecter à "Compost_Master" pour récupérer les données

## Debug

Le monitoring série est activé à **115200 bauds** sur toutes les cartes.

Pour surveiller :
```bash
pio device monitor -b 115200
```

## Notes importantes

- Les esclaves entrent en **deep sleep** pour économiser l'énergie
- La carte maître reste **toujours active** pour recevoir les données
- Les données sont sauvegardées **même sans connexion Android**
- Format de fichier SD : **CSV** (facilement exploitable)
