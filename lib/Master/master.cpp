// ==========================================
// CODE POUR LA CARTE MAÎTRE
// Réception BLE, stockage SD, serveur pour Android
// ==========================================
#ifdef MASTER
#include "Master.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include "config.h"

// ==========================================
// STRUCTURES DE DONNÉES
// ==========================================
struct SlaveData {
    uint8_t boardId;
    float temperature;
    float humidity;
    float oxygen;
    unsigned long timestamp;
    bool received;
};

// ==========================================
// VARIABLES GLOBALES
// ==========================================
SlaveData slavesData[3];  // Données des 3 esclaves
BLEScan* pBLEScan;
BLEServer* pServer = nullptr;
BLECharacteristic* pCharTX = nullptr;
bool androidConnected = false;
unsigned long lastScanTime = 0;

// ==========================================
// DÉCLARATIONS DE FONCTIONS
// ==========================================
void connectAndReadSlave(BLEAdvertisedDevice device);
void sendDataToAndroid();
void clearSDData();

// ==========================================
// CALLBACKS BLE POUR ANDROID
// ==========================================
class AndroidServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        androidConnected = true;
        Serial.println("📱 Android connecté");
    };

    void onDisconnect(BLEServer* pServer) {
        androidConnected = false;
        Serial.println("📱 Android déconnecté");
        // Redémarrer l'advertising
        BLEDevice::startAdvertising();
    }
};

// Callback pour les commandes reçues d'Android
class AndroidCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        
        if (value.length() > 0) {
            Serial.println("📥 Commande Android reçue : " + String(value.c_str()));
            
            // Commande "READ" - Envoyer les données du fichier SD
            if (value == "READ") {
                sendDataToAndroid();
            }
            // Commande "CLEAR" - Effacer le fichier SD
            else if (value == "CLEAR") {
                clearSDData();
            }
        }
    }
};

// ==========================================
// SCAN BLE POUR TROUVER LES ESCLAVES
// ==========================================
class AdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        // Vérifier si c'est un de nos esclaves
        if (advertisedDevice.haveServiceUUID() && 
            advertisedDevice.isAdvertisingService(BLEUUID(SERVICE_UUID))) {
            
            Serial.printf("🔍 Esclave trouvé : %s\n", advertisedDevice.getName().c_str());
            
            // Se connecter et récupérer les données
            connectAndReadSlave(advertisedDevice);
        }
    }
};

// ==========================================
// CONNEXION À UN ESCLAVE ET LECTURE DES DONNÉES
// ==========================================
void connectAndReadSlave(BLEAdvertisedDevice device) {
    BLEClient* pClient = BLEDevice::createClient();
    
    Serial.println("🔗 Connexion à l'esclave...");
    
    if (pClient->connect(&device)) {
        Serial.println("✓ Connecté");
        
        // Récupérer le service
        BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
        if (pRemoteService == nullptr) {
            Serial.println("❌ Service non trouvé");
            pClient->disconnect();
            delete pClient;
            return;
        }
        
        // Lire l'ID de la carte
        BLERemoteCharacteristic* pCharBoardId = pRemoteService->getCharacteristic(CHAR_BOARDID_UUID);
        if (pCharBoardId == nullptr) {
            Serial.println("❌ Caractéristique BoardID non trouvée");
            pClient->disconnect();
            delete pClient;
            return;
        }
        
        uint8_t boardId = pCharBoardId->readUInt8();
        Serial.printf("   ID Carte : %d\n", boardId);
        
        if (boardId < 1 || boardId > 3) {
            Serial.println("❌ ID de carte invalide");
            pClient->disconnect();
            delete pClient;
            return;
        }
        
        // Lire la température
        BLERemoteCharacteristic* pCharTemp = pRemoteService->getCharacteristic(CHAR_TEMPERATURE_UUID);
        if (pCharTemp && pCharTemp->canRead()) {
            std::string value = pCharTemp->readValue();
            slavesData[boardId-1].temperature = *((float*)value.data());
            Serial.printf("   🌡  Température : %.2f °C\n", slavesData[boardId-1].temperature);
        }
        
        // Lire l'humidité
        BLERemoteCharacteristic* pCharHum = pRemoteService->getCharacteristic(CHAR_HUMIDITY_UUID);
        if (pCharHum && pCharHum->canRead()) {
            std::string value = pCharHum->readValue();
            slavesData[boardId-1].humidity = *((float*)value.data());
            Serial.printf("   💧 Humidité : %.2f %%\n", slavesData[boardId-1].humidity);
        }
        
        // Lire l'oxygène
        BLERemoteCharacteristic* pCharOxy = pRemoteService->getCharacteristic(CHAR_OXYGEN_UUID);
        if (pCharOxy && pCharOxy->canRead()) {
            std::string value = pCharOxy->readValue();
            slavesData[boardId-1].oxygen = *((float*)value.data());
            if (slavesData[boardId-1].oxygen >= 0) {
                Serial.printf("   🫁 Oxygène : %.2f %%\n", slavesData[boardId-1].oxygen);
            }
        }
        
        // Marquer les données comme reçues
        slavesData[boardId-1].boardId = boardId;
        slavesData[boardId-1].timestamp = millis();
        slavesData[boardId-1].received = true;
        
        Serial.println("✓ Données récupérées");
        
        // Déconnexion
        pClient->disconnect();
    } else {
        Serial.println("❌ Échec de connexion");
    }
    
    delete pClient;
}

// ==========================================
// INITIALISATION DE LA CARTE SD
// ==========================================
bool initSD() {
    Serial.println("💾 Initialisation carte SD...");
    
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    
    if (!SD.begin(SD_CS)) {
        Serial.println("❌ Échec initialisation carte SD");
        return false;
    }
    
    Serial.println("✓ Carte SD initialisée");
    
    // Vérifier si le fichier existe, sinon créer l'en-tête CSV
    if (!SD.exists(SD_FILENAME)) {
        File file = SD.open(SD_FILENAME, FILE_WRITE);
        if (file) {
            file.println("timestamp,board_id,board_name,temperature,humidity,oxygen");
            file.close();
            Serial.println("✓ Fichier CSV créé avec en-tête");
        } else {
            Serial.println("❌ Impossible de créer le fichier");
            return false;
        }
    }
    
    return true;
}

// ==========================================
// SAUVEGARDE DES DONNÉES SUR SD
// ==========================================
void saveDataToSD() {
    Serial.println("\n💾 Sauvegarde sur carte SD...");
    
    File file = SD.open(SD_FILENAME, FILE_APPEND);
    if (!file) {
        Serial.println("❌ Échec ouverture fichier");
        return;
    }
    
    // Sauvegarder les données de chaque esclave
    for (int i = 0; i < 3; i++) {
        if (slavesData[i].received) {
            String boardName;
            if (i == 0) boardName = "Bac_Apport";
            else if (i == 1) boardName = "Bac_Maturation";
            else boardName = "Exterieur";
            
            // Format CSV : timestamp,board_id,board_name,temperature,humidity,oxygen
            file.printf("%lu,%d,%s,%.2f,%.2f,%.2f\n",
                slavesData[i].timestamp,
                slavesData[i].boardId,
                boardName.c_str(),
                slavesData[i].temperature,
                slavesData[i].humidity,
                slavesData[i].oxygen
            );
            
            Serial.printf("   ✓ Carte %d sauvegardée\n", slavesData[i].boardId);
        }
    }
    
    file.close();
    Serial.println("✓ Sauvegarde terminée");
}

// ==========================================
// ENVOI DES DONNÉES À ANDROID
// ==========================================
void sendDataToAndroid() {
    Serial.println("📤 Envoi des données à Android...");
    
    if (!pCharTX) {
        Serial.println("❌ Caractéristique TX non disponible");
        return;
    }
    
    File file = SD.open(SD_FILENAME, FILE_READ);
    if (!file) {
        Serial.println("❌ Impossible d'ouvrir le fichier");
        pCharTX->setValue("{\"error\":\"Fichier introuvable\"}");
        pCharTX->notify();
        return;
    }
    
    // Lire et envoyer le fichier par chunks
    String chunk = "";
    int lineCount = 0;
    
    while (file.available()) {
        String line = file.readStringUntil('\n');
        chunk += line + "\n";
        lineCount++;
        
        // Envoyer par paquets de 10 lignes
        if (lineCount % 10 == 0) {
            pCharTX->setValue(chunk.c_str());
            pCharTX->notify();
            chunk = "";
            delay(100);  // Laisser le temps au téléphone de recevoir
        }
    }
    
    // Envoyer le reste
    if (chunk.length() > 0) {
        pCharTX->setValue(chunk.c_str());
        pCharTX->notify();
    }
    
    // Signal de fin
    pCharTX->setValue("{\"end\":true}");
    pCharTX->notify();
    
    file.close();
    Serial.println("✓ Données envoyées");
}

// ==========================================
// EFFACER LES DONNÉES SD
// ==========================================
void clearSDData() {
    Serial.println("🗑️ Effacement des données...");
    
    if (SD.remove(SD_FILENAME)) {
        // Recréer le fichier avec l'en-tête
        File file = SD.open(SD_FILENAME, FILE_WRITE);
        if (file) {
            file.println("timestamp,board_id,board_name,temperature,humidity,oxygen");
            file.close();
            Serial.println("✓ Données effacées");
            
            if (pCharTX) {
                pCharTX->setValue("{\"status\":\"cleared\"}");
                pCharTX->notify();
            }
        }
    } else {
        Serial.println("❌ Échec effacement");
    }
}

// ==========================================
// INITIALISATION BLE (MAÎTRE)
// ==========================================
void initBLEMaster() {
    Serial.println("🔵 Initialisation BLE Maître...");
    
    BLEDevice::init("Compost_Master");
    
    // Créer le scanner pour trouver les esclaves
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    
    // Créer le serveur pour Android
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new AndroidServerCallbacks());
    
    BLEService *pService = pServer->createService(ANDROID_SERVICE_UUID);
    
    // Caractéristique TX (Maître -> Android)
    pCharTX = pService->createCharacteristic(
        ANDROID_CHAR_TX_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    pCharTX->addDescriptor(new BLE2902());
    
    // Caractéristique RX (Android -> Maître)
    BLECharacteristic* pCharRX = pService->createCharacteristic(
        ANDROID_CHAR_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    pCharRX->setCallbacks(new AndroidCharacteristicCallbacks());
    
    pService->start();
    
    // Démarrer l'advertising pour Android
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(ANDROID_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    BLEDevice::startAdvertising();
    
    Serial.println("✓ BLE Maître prêt");
    Serial.println("   - Scanner actif pour esclaves");
    Serial.println("   - Serveur actif pour Android");
}

// ==========================================
// SCAN DES ESCLAVES
// ==========================================
void scanSlaves() {
    Serial.println("\n🔍 Scan des esclaves...");
    
    // Réinitialiser les flags de réception
    for (int i = 0; i < 3; i++) {
        slavesData[i].received = false;
    }
    
    // Scanner pendant BLE_SCAN_TIME secondes
    BLEScanResults foundDevices = pBLEScan->start(BLE_SCAN_TIME, false);
    Serial.printf("   %d dispositifs trouvés\n", foundDevices.getCount());
    
    pBLEScan->clearResults();
}

// ==========================================
// SETUP
// ==========================================
void my_setup() {
    Serial.begin(SERIAL_BAUD);
    delay(1000);
    
    Serial.println("\n\n");
    Serial.println("════════════════════════════════════════");
    Serial.println("   SYSTÈME DE MONITORING COMPOST");
    Serial.println("   MODE : MAÎTRE");
    Serial.println("════════════════════════════════════════");
    
    // Initialisation de la carte SD
    if (!initSD()) {
        Serial.println("⚠ Attention : Carte SD non disponible");
        Serial.println("   Le système continuera sans sauvegarde");
    }
    
    // Initialisation BLE
    initBLEMaster();
    
    // Initialiser les structures de données
    for (int i = 0; i < 3; i++) {
        slavesData[i].boardId = i + 1;
        slavesData[i].temperature = 0.0;
        slavesData[i].humidity = 0.0;
        slavesData[i].oxygen = -1.0;
        slavesData[i].timestamp = 0;
        slavesData[i].received = false;
    }
    
    Serial.println("\n✓ Carte maître initialisée");
}

// ==========================================
// LOOP
// ==========================================
void my_loop() {
    unsigned long currentTime = millis();
    
    // Scanner les esclaves toutes les 30 minutes
    if (currentTime - lastScanTime >= (SLEEP_TIME_MINUTES * 60 * 1000)) {
        scanSlaves();
        saveDataToSD();
        lastScanTime = currentTime;
        
        // Afficher un résumé
        Serial.println("\n📊 RÉSUMÉ DES DONNÉES :");
        for (int i = 0; i < 3; i++) {
            if (slavesData[i].received) {
                Serial.printf("   Carte %d : T=%.2f°C  H=%.2f%%  O2=%.2f%%\n",
                    slavesData[i].boardId,
                    slavesData[i].temperature,
                    slavesData[i].humidity,
                    slavesData[i].oxygen
                );
            } else {
                Serial.printf("   Carte %d : Aucune donnée reçue\n", i+1);
            }
        }
    }
    
    // Gérer les connexions Android
    if (androidConnected) {
        // Le serveur est actif, les commandes sont gérées par les callbacks
    }
    
    delay(1000);
}

#endif // MASTER