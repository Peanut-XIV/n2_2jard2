#ifndef CONFIG_H
#define CONFIG_H

//Mesurer l'ecart de temps entre le sleep des slaves et le sleep du maitre
//On est partit avec l'idée d'avoir une periode de mesure de 30 minutes.
//Mais on pourrait faire evoluer cette periode par rapport à la temperature : 
// plus il fait chaud, plus on mesure souvent (car les reactions de compostage sont plus rapides).
//On pourrait aussi imaginer une periode plus longue la nuit que le jour.

// ==========================================
// CONFIGURATION GLOBALE DU PROJET COMPOST
// ==========================================


// ==========================================
// PINS I2C (communes à toutes les cartes)
// ==========================================
#define I2C_SDA 21
#define I2C_SCL 22

// ==========================================
// ADRESSES I2C DES CAPTEURS
// ==========================================
#define BME280_ADDRESS 0x76
#define OXYGEN_ADDRESS 0x73

// ==========================================
// PINS SPI POUR CARTE SD (carte maître uniquement)
// ==========================================
#define SD_CS   5
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK  18

// ==========================================
// TIMING
// ==========================================
#define SLEEP_TIME_MINUTES 30                           // Durée du deep sleep en minutes
#define SLEEP_TIME_US (SLEEP_TIME_MINUTES * 60 * 1000000ULL)  // Conversion en microsecondes
#define BLE_SCAN_TIME 10                                // Temps de scan BLE en secondes (maître)
#define BLE_ADVERTISE_TIME 15                           // Temps de diffusion BLE en secondes (esclave)

// ==========================================
// UUIDs BLE
// ==========================================
// Service UUID pour les données de compost
#define SENSOR_SERVICE_UUID "A870DC1B-0265-4D5F-9A21-8AC5BD2BACD7"


// Caractéristiques pour les esclaves (envoi de données)
#define TEMP_CHARACTERISTIC_UUID "A07038DF-7C8E-4914-87B3-131B91DAAB73"
#define PRES_CHARACTERISTIC_UUID "594BF212-A4FC-4130-ACB1-8FD4FD28EFD3"
#define HUMID_CHARACTERISTIC_UUID "72A7B435-989D-4369-8F58-D6E98B4AB262"
#define OXY_CHARACTERISTIC_UUID "759E38A8-BB58-4F70-96EB-A4BDCEC3977A"

// Service UUID pour l'accès aux données (carte maître vers Android)
#define ANDROID_SERVICE_UUID    "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define ANDROID_CHAR_TX_UUID    "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  // Maître -> Android
#define ANDROID_CHAR_RX_UUID    "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  // Android -> Maître

// ==========================================
// IDENTIFICATION DES CARTES
// ==========================================
// #ifdef SLAVE_APPORT
//     #define BOARD_ID 1
// #endif

// #ifdef SLAVE_MATURATION
//     #define BOARD_ID 2
// #endif

// #ifdef SLAVE_EXTERIEUR
//     #define BOARD_ID 3
// #endif

#ifdef MASTER
    #define BOARD_ID 0
#endif

// ==========================================
// CONFIGURATION CARTE SD
// ==========================================
#define SD_FILENAME "/compost_data.csv"

// ==========================================
// SEUILS ET CALIBRATION
// ==========================================
// Seuils conformes au cahier des charges
#define TEMP_MIN_THRESHOLD 70.0     // Température minimale réacteur : 70°C
#define PARTICLE_SIZE_MAX 12        // Taille maximale particules : 12mm

// ==========================================
// DEBUG
// ==========================================
#define SERIAL_BAUD 115200


#ifdef DEBUG
  #define DEBUG_PRINT(x)  Serial.print(x)
  #define DEBUG_PRINTLN(x)  Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

#endif // CONFIG_H
