#include "sensors.h"

// ==========================================
// CONSTRUCTEUR
// ==========================================
CompostSensors::CompostSensors() 
    : bmeInitialized(false), oxygenInitialized(false) {
#ifdef HAS_OXYGEN_SENSOR
    oxygen = DFRobot_OxygenSensor(OXYGEN_ADDRESS);
#endif
}

// ==========================================
// INITIALISATION DES CAPTEURS
// ==========================================
bool CompostSensors::begin() {
    // Initialisation du bus I2C
    Wire.begin(I2C_SDA, I2C_SCL);
    
    // Initialisation du BME280
    bmeInitialized = bme.begin(BME280_ADDRESS, &Wire);
    if (!bmeInitialized) {
        Serial.println("❌ Erreur : BME280 non trouvé à l'adresse 0x76");
        return false;
    }
    
    // Configuration du BME280 pour une mesure précise
    bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1,  // temperature
                    Adafruit_BME280::SAMPLING_X1,  // pressure (non utilisé mais nécessaire)
                    Adafruit_BME280::SAMPLING_X1,  // humidity
                    Adafruit_BME280::FILTER_OFF);
    
    Serial.println("✓ BME280 initialisé");
    
#ifdef HAS_OXYGEN_SENSOR
    // Initialisation du capteur d'oxygène
    oxygenInitialized = oxygen.begin(Oxygen_IIC);
    if (!oxygenInitialized) {
        Serial.println("⚠ Attention : Capteur O2 non trouvé à l'adresse 0x73");
        // Ce n'est pas une erreur fatale pour les autres cartes
    } else {
        Serial.println("✓ Capteur O2 (SEN0322) initialisé");
    }
#endif
    
    return bmeInitialized;
}

// ==========================================
// LECTURE DES DONNÉES
// ==========================================
SensorData CompostSensors::readSensors() {
    SensorData data;
    data.valid = false;
    data.boardId = BOARD_ID;
    data.timestamp = millis();
    data.oxygen = -1.0;  // Valeur par défaut si pas de capteur O2
    
    // Lecture du BME280
    if (bmeInitialized) {
        // Forcer une nouvelle mesure
        bme.takeForcedMeasurement();
        
        data.temperature = bme.readTemperature();
        data.humidity = bme.readHumidity();
        
        // Vérification de la validité des données
        if (!isnan(data.temperature) && !isnan(data.humidity)) {
            data.valid = true;
            
#ifdef DEBUG_SERIAL
            Serial.println("📊 Lecture BME280 :");
            Serial.printf("   Température : %.2f °C\n", data.temperature);
            Serial.printf("   Humidité : %.2f %%\n", data.humidity);
#endif
        } else {
            Serial.println("❌ Erreur de lecture BME280");
        }
    }
    
#ifdef HAS_OXYGEN_SENSOR
    // Lecture du capteur d'oxygène
    if (oxygenInitialized) {
        data.oxygen = oxygen.readOxygenData(COLLECT_NUMBER);
        
        if (data.oxygen >= 0) {
#ifdef DEBUG_SERIAL
            Serial.printf("   Oxygène : %.2f %%\n", data.oxygen);
#endif
        } else {
            Serial.println("⚠ Erreur de lecture capteur O2");
        }
    }
#endif
    
    return data;
}

// ==========================================
// AFFICHAGE DES DONNÉES (DEBUG)
// ==========================================
void CompostSensors::printData(const SensorData& data) {
    Serial.println("════════════════════════════════════════");
    Serial.printf("Carte : %s (ID: %d)\n", BOARD_NAME, data.boardId);
    Serial.println("────────────────────────────────────────");
    
    if (data.valid) {
        Serial.printf("🌡  Température : %.2f °C\n", data.temperature);
        Serial.printf("💧 Humidité    : %.2f %%\n", data.humidity);
        
#ifdef HAS_OXYGEN_SENSOR
        if (data.oxygen >= 0) {
            Serial.printf("🫁 Oxygène     : %.2f %%\n", data.oxygen);
        }
#endif
        
        Serial.printf("⏱  Timestamp   : %lu ms\n", data.timestamp);
    } else {
        Serial.println("❌ Données invalides");
    }
    
    Serial.println("════════════════════════════════════════");
}
