#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "config.h"

#ifdef HAS_OXYGEN_SENSOR
#include <DFRobot_OxygenSensor.h>
#endif

// ==========================================
// Structure pour stocker les données des capteurs
// ==========================================
struct SensorData {
    float temperature;      // °C
    float humidity;         // %
    float oxygen;          // % (seulement pour bac d'apport)
    bool valid;            // Indique si les données sont valides
    uint8_t boardId;       // ID de la carte (1=apport, 2=maturation, 3=exterieur)
    unsigned long timestamp; // Timestamp en millisecondes
};

// ==========================================
// Classe de gestion des capteurs
// ==========================================
class CompostSensors {
private:
    Adafruit_BME280 bme;
    
#ifdef HAS_OXYGEN_SENSOR
    DFRobot_OxygenSensor oxygen;
#endif
    
    bool bmeInitialized;
    bool oxygenInitialized;

public:
    CompostSensors();
    
    // Initialisation des capteurs
    bool begin();
    
    // Lecture des données
    SensorData readSensors();
    
    // Vérification de l'état des capteurs
    bool isBMEReady() { return bmeInitialized; }
    bool isOxygenReady() { return oxygenInitialized; }
    
    // Affichage des données (debug)
    void printData(const SensorData& data);
};

#endif // SENSORS_H
