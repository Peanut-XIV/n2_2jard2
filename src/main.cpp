// ==========================================
// CODE POUR LA CARTE MAÎTRE
// Réception BLE, stockage SD, serveur pour Android
// ==========================================
#include <Arduino.h>
#include <esp_sleep.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <SD.h>
#include <SPI.h>
#include <config.h>

union Bytes
{
    char table[4];
    float value;
};


// TYPE DEFINITIONS ---------------------
typedef enum {
  SCAN_SLAVES,
  PROCESS_DATA,
  WAIT_ANDROID,
  PREPARE_SLEEP,
  BROKEN_LINK
} MasterState;

// CONSTANTS ----------------------------
#define MAX_TIMEOUT_COUNT 3
#define MAX_SLAVES 3
#define DATE_FILENAME "/datetime.txt"

// Fichiers CSV pour chaque capteur
const char* MASTER_FILE = "/master.csv";
const char* APPORT_FILE = "/apport.csv";
const char* MATURATION_FILE = "/maturation.csv";
const char* EXTERIEUR_FILE = "/exterieur.csv";

// ==========================================
// STRUCTURES DE DONNÉES
// ==========================================
struct SlaveData {
    uint8_t boardId;
    float temperature;
    float pressure;
    float humidity;
    float oxygen;
    bool received;
    char isoTime[20];  // Format: "YYYY-MM-DDTHH:MM:SS"
};

struct DateTime {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
};

// ==========================================
// VARIABLES GLOBALES
// ==========================================
SlaveData slavesData[MAX_SLAVES];  // Données des 3 esclaves
BLEScan* pBLEScan = nullptr;
BLEServer* pServer = nullptr;
BLECharacteristic* pCharTX = nullptr;
BLECharacteristic* pCharRX = nullptr;

bool androidConnected = false;
bool dataRequested = false;
bool clearRequested = false;
bool allSlavesScanned = false;

DateTime currentDateTime;

// PERSISTENT STATE ---------------------
RTC_DATA_ATTR int TIMEOUT_COUNTER = 0;
RTC_DATA_ATTR int64_t SLEEP_DURATION = SLEEP_TIME_US;

// ==========================================
// DÉCLARATIONS DE FONCTIONS
// ==========================================
void init_BLE();
void scanSlaves();
void connectAndReadSlave(BLEAdvertisedDevice* device);
bool initSD();
void saveDataToSD();
void sendDataToAndroid();
void clearSDData();
bool loadDateTime();
void saveDateTime();
void incrementDateTime(int seconds);
void formatISO8601(char* buffer, DateTime dt);

// Fonctions utilitaires SD
void listDir(fs::FS &fs, const char * dirname, uint8_t levels);
void readFile(fs::FS &fs, const char * path);
void writeFile(fs::FS &fs, const char * path, const char * message);
void deleteRecursive(fs::FS &fs, const char * path);
void resetCarteSD(fs::FS &fs);
void initCSVFiles();

// ==========================================
// CALLBACKS BLE POUR ANDROID
// ==========================================
class AndroidServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        androidConnected = true;
        DEBUG_PRINTLN("[BLE] Android connected!");
    }

    void onDisconnect(BLEServer* pServer) {
        androidConnected = false;
        DEBUG_PRINTLN("[BLE] Android disconnected!");
        BLEDevice::startAdvertising();
    }
};

// Callback pour les commandes reçues d'Android
class AndroidCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        
        if (value.length() > 0) {
            DEBUG_PRINT("[BLE] Android command received: ");
            DEBUG_PRINTLN(value.c_str());
            
            if (value == "READ") {
                dataRequested = true;
            } else if (value == "CLEAR") {
                clearRequested = true;
            }
        }
    }
};

// ==========================================
// SCAN BLE POUR TROUVER LES ESCLAVES
// ==========================================
class AdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        if (advertisedDevice.haveServiceUUID() && 
            advertisedDevice.isAdvertisingService(BLEUUID(SENSOR_SERVICE_UUID))) {
            
            DEBUG_PRINT("[BLE] Slave found: ");
            DEBUG_PRINTLN(advertisedDevice.getName().c_str());
            
            // Se connecter et récupérer les données
            BLEAdvertisedDevice* devicePtr = new BLEAdvertisedDevice(advertisedDevice);
            connectAndReadSlave(devicePtr);
            delete devicePtr;
        }
    }
};

// ==========================================
// CONNEXION À UN ESCLAVE ET LECTURE DES DONNÉES
// ==========================================
void connectAndReadSlave(BLEAdvertisedDevice* device) {
    BLEClient* pClient = BLEDevice::createClient();
    
    DEBUG_PRINTLN("[BLE] Connecting to slave...");
    
    if (pClient->connect(device)) {
        DEBUG_PRINTLN("[BLE] Connected");
        
        // Récupérer le service
        BLERemoteService* pRemoteService = pClient->getService(SENSOR_SERVICE_UUID);
        if (pRemoteService == nullptr) {
            DEBUG_PRINTLN("[BLE] Service not found");
            pClient->disconnect();
            delete pClient;
            return;
        }
        
        // Extraire l'ID depuis le nom du dispositif (format: "EnvSensor_X")
        std::string deviceName = device->getName();
        uint8_t boardId = 0;
        
        // Chercher le dernier caractère qui devrait être l'ID
        if (deviceName.length() > 0) {
            char lastChar = deviceName[deviceName.length() - 1];
            if (lastChar >= '1' && lastChar <= '3') {
                boardId = lastChar - '0';
            }
        }
        
        if (boardId < 1 || boardId > MAX_SLAVES) {
            DEBUG_PRINTLN("[BLE] Invalid board ID");
            pClient->disconnect();
            delete pClient;
            return;
        }
        
        DEBUG_PRINT("[BLE]    Board ID: ");
        DEBUG_PRINTLN(boardId);
        
        // Lire la température
        BLERemoteCharacteristic* pCharTemp = pRemoteService->getCharacteristic(TEMP_CHARACTERISTIC_UUID);
        if (pCharTemp && pCharTemp->canRead()) {
            std::string value = pCharTemp->readValue();
            if (value.length() >= sizeof(float)) {
                slavesData[boardId-1].temperature = *((float*)value.data());
                DEBUG_PRINT("[BLE]    Temperature: ");
                DEBUG_PRINTLN(slavesData[boardId-1].temperature);
            }
        }
        
        // Lire l'humidité
        BLERemoteCharacteristic* pCharHum = pRemoteService->getCharacteristic(HUMID_CHARACTERISTIC_UUID);
        if (pCharHum && pCharHum->canRead()) {
            std::string value = pCharHum->readValue();
            if (value.length() >= sizeof(float)) {
                slavesData[boardId-1].humidity = *((float*)value.data());
                DEBUG_PRINT("[BLE]    Humidity: ");
                DEBUG_PRINTLN(slavesData[boardId-1].humidity);
            }
        }
        
        // Lire la pression
        BLERemoteCharacteristic* pCharPress = pRemoteService->getCharacteristic(PRES_CHARACTERISTIC_UUID);
        if (pCharPress && pCharPress->canRead()) {
            std::string value = pCharPress->readValue();
            if (value.length() >= sizeof(float)) {
                slavesData[boardId-1].pressure = *((float*)value.data());
                DEBUG_PRINT("[BLE]    Pressure: ");
                DEBUG_PRINTLN(slavesData[boardId-1].pressure);
            }
        }
        
        // Lire l'oxygène
        BLERemoteCharacteristic* pCharOxy = pRemoteService->getCharacteristic(OXY_CHARACTERISTIC_UUID);
        if (pCharOxy && pCharOxy->canRead()) {
            std::string value = pCharOxy->readValue();
            if (value.length() >= sizeof(float)) {
                slavesData[boardId-1].oxygen = *((float*)value.data());
                DEBUG_PRINT("[BLE]    Oxygen: ");
                DEBUG_PRINTLN(slavesData[boardId-1].oxygen);
            }
        }
        
        // Marquer les données comme reçues et ajouter la date
        slavesData[boardId-1].boardId = boardId;
        formatISO8601(slavesData[boardId-1].isoTime, currentDateTime);
        slavesData[boardId-1].received = true;
        
        DEBUG_PRINTLN("[BLE] Data retrieved");
        
        // Déconnexion
        pClient->disconnect();
    } else {
        DEBUG_PRINTLN("[BLE] Connection failed");
    }
    
    delete pClient;
}

// ==========================================
// INITIALISATION DE LA CARTE SD
// ==========================================
bool initSD() {
    DEBUG_PRINTLN("[SD] Initializing SD card...");
    
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    
    if (!SD.begin(SD_CS)) {
        DEBUG_PRINTLN("[SD] Failed to initialize SD card");
        return false;
    }
    
    DEBUG_PRINTLN("[SD] SD card initialized");
    
    uint8_t cardType = SD.cardType();
    DEBUG_PRINT("[SD] Card Type: ");
    DEBUG_PRINTLN(cardType);
    DEBUG_PRINT("[SD] Total space: ");
    DEBUG_PRINT(SD.totalBytes() / (1024 * 1024));
    DEBUG_PRINTLN(" MB");
    
    // Initialiser les fichiers CSV avec leurs en-têtes si nécessaire
    initCSVFiles();
    
    // Charger ou initialiser la date/heure
    loadDateTime();
    
    return true;
}

// ==========================================
// INITIALISER LES FICHIERS CSV
// ==========================================
void initCSVFiles() {
    // Fichier Master (température uniquement)
    if (!SD.exists(MASTER_FILE)) {
        File file = SD.open(MASTER_FILE, FILE_WRITE);
        if (file) {
            file.println("date;temperature;");
            file.close();
            DEBUG_PRINTLN("[SD] Master CSV created");
        }
    }
    
    // Fichier Apport (T, H, O2)
    if (!SD.exists(APPORT_FILE)) {
        File file = SD.open(APPORT_FILE, FILE_WRITE);
        if (file) {
            file.println("date;temperature;humidity;oxygene;");
            file.close();
            DEBUG_PRINTLN("[SD] Apport CSV created");
        }
    }
    
    // Fichier Maturation (T, H)
    if (!SD.exists(MATURATION_FILE)) {
        File file = SD.open(MATURATION_FILE, FILE_WRITE);
        if (file) {
            file.println("date;temperature;humidity;");
            file.close();
            DEBUG_PRINTLN("[SD] Maturation CSV created");
        }
    }
    
    // Fichier Extérieur (T, H)
    if (!SD.exists(EXTERIEUR_FILE)) {
        File file = SD.open(EXTERIEUR_FILE, FILE_WRITE);
        if (file) {
            file.println("date;temperature;humidity;");
            file.close();
            DEBUG_PRINTLN("[SD] Exterieur CSV created");
        }
    }
}

// ==========================================
// SAUVEGARDE DES DONNÉES SUR SD
// ==========================================
void saveDataToSD() {
    DEBUG_PRINTLN("[SD] Saving to SD card...");
    
    // Sauvegarder les données de chaque esclave dans son fichier respectif
    for (int i = 0; i < MAX_SLAVES; i++) {
        if (slavesData[i].received) {
            const char* filename;
            String data;
            
            // Déterminer le fichier et le format selon le boardId
            switch (slavesData[i].boardId) {
                case 1: // Apport (T, H, O2)
                    filename = APPORT_FILE;
                    data = String(slavesData[i].isoTime) + ";" + 
                           String(slavesData[i].temperature, 2) + ";" +
                           String(slavesData[i].humidity, 2) + ";" +
                           String(slavesData[i].oxygen, 2) + ";\n";
                    break;
                    
                case 2: // Maturation (T, H)
                    filename = MATURATION_FILE;
                    data = String(slavesData[i].isoTime) + ";" + 
                           String(slavesData[i].temperature, 2) + ";" +
                           String(slavesData[i].humidity, 2) + ";\n";
                    break;
                    
                case 3: // Extérieur (T, H)
                    filename = EXTERIEUR_FILE;
                    data = String(slavesData[i].isoTime) + ";" + 
                           String(slavesData[i].temperature, 2) + ";" +
                           String(slavesData[i].humidity, 2) + ";\n";
                    break;
                    
                default:
                    DEBUG_PRINT("[SD] Unknown board ID: ");
                    DEBUG_PRINTLN(slavesData[i].boardId);
                    continue;
            }
            
            // Écrire dans le fichier
            writeFile(SD, filename, data.c_str());
            
            DEBUG_PRINT("[SD]    Board ");
            DEBUG_PRINT(slavesData[i].boardId);
            DEBUG_PRINTLN(" saved");
        }
    }
    
    DEBUG_PRINTLN("[SD] Save complete");
}

// ==========================================
// ENVOI DES DONNÉES À ANDROID
// ==========================================
void sendDataToAndroid() {
    DEBUG_PRINTLN("[BLE] Sending data to Android...");
    
    if (!pCharTX) {
        DEBUG_PRINTLN("[BLE] TX characteristic not available");
        return;
    }
    
    // Liste des fichiers à envoyer
    const char* files[] = {MASTER_FILE, APPORT_FILE, MATURATION_FILE, EXTERIEUR_FILE};
    const char* fileNames[] = {"master", "apport", "maturation", "exterieur"};
    
    for (int i = 0; i < 4; i++) {
        if (!SD.exists(files[i])) continue;
        
        File file = SD.open(files[i], FILE_READ);
        if (!file) {
            DEBUG_PRINT("[BLE] Failed to open file: ");
            DEBUG_PRINTLN(files[i]);
            continue;
        }
        
        // Envoyer le nom du fichier
        String header = String("{\"file\":\"") + fileNames[i] + "\"}\n";
        pCharTX->setValue(header.c_str());
        pCharTX->notify();
        delay(100);
        
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
                delay(100);
            }
        }
        
        // Envoyer le reste
        if (chunk.length() > 0) {
            pCharTX->setValue(chunk.c_str());
            pCharTX->notify();
            delay(100);
        }
        
        file.close();
    }
    
    // Signal de fin
    pCharTX->setValue("{\"end\":true}");
    pCharTX->notify();
    
    DEBUG_PRINTLN("[BLE] Data sent");
}

// ==========================================
// EFFACER LES DONNÉES SD
// ==========================================
void clearSDData() {
    DEBUG_PRINTLN("[SD] Clearing data...");
    resetCarteSD(SD);
    DEBUG_PRINTLN("[SD] Data cleared");
    
    if (pCharTX) {
        pCharTX->setValue("{\"status\":\"cleared\"}");
        pCharTX->notify();
    }
}

// ==========================================
// FONCTIONS UTILITAIRES SD
// ==========================================
void listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
    DEBUG_PRINT("[SD] Listing directory: ");
    DEBUG_PRINTLN(dirname);

    File root = fs.open(dirname);
    if(!root) {
        DEBUG_PRINTLN("[SD] Failed to open directory");
        return;
    }
    if(!root.isDirectory()) {
        DEBUG_PRINTLN("[SD] Not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file) {
        if(file.isDirectory()) {
            DEBUG_PRINT("[SD]   DIR : ");
            DEBUG_PRINTLN(file.name());
            if(levels) {
                listDir(fs, file.name(), levels - 1);
            }
        } else {
            DEBUG_PRINT("[SD]   FILE: ");
            DEBUG_PRINT(file.name());
            DEBUG_PRINT("  SIZE: ");
            DEBUG_PRINTLN(file.size());
        }
        file = root.openNextFile();
    }
}

void readFile(fs::FS &fs, const char * path) {
    DEBUG_PRINT("[SD] Reading file: ");
    DEBUG_PRINTLN(path);

    File file = fs.open(path);
    if(!file) {
        DEBUG_PRINTLN("[SD] Failed to open file for reading");
        return;
    }

    DEBUG_PRINTLN("[SD] [ Start reading ]");
    while(file.available()) {
        Serial.write(file.read());
    }
    file.close();
    DEBUG_PRINTLN("[SD] [ End reading ]");
}

void writeFile(fs::FS &fs, const char * path, const char * message) {
    DEBUG_PRINT("[SD] Appending to file: ");
    DEBUG_PRINTLN(path);

    File file = fs.open(path, FILE_APPEND);
    if(!file) {
        DEBUG_PRINTLN("[SD] Failed to open file for appending");
        return;
    }
    if(file.print(message)) {
        DEBUG_PRINTLN("[SD] Message appended");
    } else {
        DEBUG_PRINTLN("[SD] Append failed");
    }
    file.close();
}

void deleteRecursive(fs::FS &fs, const char * path) {
    File root = fs.open(path);
    if (!root) return;
    if (!root.isDirectory()) {
        fs.remove(path);
        return;
    }

    File file = root.openNextFile();
    while (file) {
        String fullPath = String(path);
        if (!fullPath.endsWith("/")) fullPath += "/";
        fullPath += file.name();

        if (file.isDirectory()) {
            deleteRecursive(fs, fullPath.c_str());
        } else {
            fs.remove(fullPath.c_str());
            DEBUG_PRINT("[SD] Deleted: ");
            DEBUG_PRINTLN(fullPath.c_str());
        }
        file = root.openNextFile();
    }
    fs.rmdir(path);
}

void resetCarteSD(fs::FS &fs) {
    DEBUG_PRINTLN("[SD] !!! CLEARING SD CARD !!!");
    File root = fs.open("/");
    File file = root.openNextFile();

    while (file) {
        String path = String("/") + file.name();
        if (file.isDirectory()) {
            deleteRecursive(fs, path.c_str());
        } else {
            fs.remove(path.c_str());
            DEBUG_PRINT("[SD] Deleted: ");
            DEBUG_PRINTLN(path.c_str());
        }
        file = root.openNextFile();
    }
    DEBUG_PRINTLN("[SD] Done. Card is empty.");

    // Recréer les fichiers avec leurs en-têtes
    initCSVFiles();
}

// ==========================================
// FORMATER DATE EN ISO 8601
// ==========================================
void formatISO8601(char* buffer, DateTime dt) {
    sprintf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d",
        dt.year, dt.month, dt.day,
        dt.hour, dt.minute, dt.second);
}

// ==========================================
// INCRÉMENTER LA DATE
// ==========================================
void incrementDateTime(int seconds) {
    currentDateTime.second += seconds;
    
    // Gestion des dépassements
    while (currentDateTime.second >= 60) {
        currentDateTime.second -= 60;
        currentDateTime.minute++;
    }
    
    while (currentDateTime.minute >= 60) {
        currentDateTime.minute -= 60;
        currentDateTime.hour++;
    }
    
    while (currentDateTime.hour >= 24) {
        currentDateTime.hour -= 24;
        currentDateTime.day++;
    }
    
    // Gestion simplifiée des jours par mois (peut être amélioré)
    int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    
    // Année bissextile
    if ((currentDateTime.year % 4 == 0 && currentDateTime.year % 100 != 0) || 
        (currentDateTime.year % 400 == 0)) {
        daysInMonth[1] = 29;
    }
    
    while (currentDateTime.day > daysInMonth[currentDateTime.month - 1]) {
        currentDateTime.day -= daysInMonth[currentDateTime.month - 1];
        currentDateTime.month++;
        
        if (currentDateTime.month > 12) {
            currentDateTime.month = 1;
            currentDateTime.year++;
        }
    }
}

// ==========================================
// CHARGER LA DATE DEPUIS LA CARTE SD
// ==========================================
bool loadDateTime() {
    DEBUG_PRINTLN("[SD] Loading datetime...");
    
    if (!SD.exists(DATE_FILENAME)) {
        // Initialiser avec une date par défaut
        DEBUG_PRINTLN("[SD] Datetime file not found, using default");
        currentDateTime.year = 2026;
        currentDateTime.month = 1;
        currentDateTime.day = 17;
        currentDateTime.hour = 0;
        currentDateTime.minute = 0;
        currentDateTime.second = 0;
        saveDateTime();
        return true;
    }
    
    File file = SD.open(DATE_FILENAME, FILE_READ);
    if (!file) {
        DEBUG_PRINTLN("[SD] Failed to open datetime file");
        return false;
    }
    
    // Format: YYYY-MM-DDTHH:MM:SS
    String dateStr = file.readStringUntil('\n');
    file.close();
    
    if (dateStr.length() < 19) {
        DEBUG_PRINTLN("[SD] Invalid datetime format");
        return false;
    }
    
    // Parser la date
    currentDateTime.year = dateStr.substring(0, 4).toInt();
    currentDateTime.month = dateStr.substring(5, 7).toInt();
    currentDateTime.day = dateStr.substring(8, 10).toInt();
    currentDateTime.hour = dateStr.substring(11, 13).toInt();
    currentDateTime.minute = dateStr.substring(14, 16).toInt();
    currentDateTime.second = dateStr.substring(17, 19).toInt();
    
    DEBUG_PRINT("[SD] Loaded datetime: ");
    DEBUG_PRINTLN(dateStr);
    
    return true;
}

// ==========================================
// SAUVEGARDER LA DATE SUR LA CARTE SD
// ==========================================
void saveDateTime() {
    DEBUG_PRINTLN("[SD] Saving datetime...");
    
    File file = SD.open(DATE_FILENAME, FILE_WRITE);
    if (!file) {
        DEBUG_PRINTLN("[SD] Failed to open datetime file for writing");
        return;
    }
    
    char buffer[20];
    formatISO8601(buffer, currentDateTime);
    file.println(buffer);
    file.close();
    
    DEBUG_PRINT("[SD] Saved datetime: ");
    DEBUG_PRINTLN(buffer);
}

// ==========================================
// SCAN DES ESCLAVES
// ==========================================
void scanSlaves() {
    DEBUG_PRINTLN("[BLE] Scanning for slaves...");
    
    // Réinitialiser les flags de réception
    for (int i = 0; i < MAX_SLAVES; i++) {
        slavesData[i].received = false;
    }
    
    // Scanner pendant BLE_SCAN_TIME secondes
    BLEScanResults foundDevices = pBLEScan->start(BLE_SCAN_TIME, false);
    DEBUG_PRINT("[BLE]    Found ");
    DEBUG_PRINT(foundDevices.getCount());
    DEBUG_PRINTLN(" devices");
    
    pBLEScan->clearResults();
    allSlavesScanned = true;
}

// ==========================================
// INITIALISATION BLE (MAÎTRE)
// ==========================================
void init_BLE() {
    DEBUG_PRINTLN("[BLE] Initializing BLE Master...");
    
    BLEDevice::init("Compost_Master");
    
    // Créer le scanner pour trouver les esclaves
    DEBUG_PRINTLN("[BLE] Creating scanner...");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    
    // Créer le serveur pour Android
    DEBUG_PRINTLN("[BLE] Creating server for Android...");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new AndroidServerCallbacks());
    
    BLEService *pService = pServer->createService(ANDROID_SERVICE_UUID);
    
    // Caractéristique TX (Maître -> Android)
    DEBUG_PRINTLN("[BLE] Creating TX characteristic...");
    pCharTX = pService->createCharacteristic(
        ANDROID_CHAR_TX_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    pCharTX->addDescriptor(new BLE2902());
    
    // Caractéristique RX (Android -> Maître)
    DEBUG_PRINTLN("[BLE] Creating RX characteristic...");
    pCharRX = pService->createCharacteristic(
        ANDROID_CHAR_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    pCharRX->setCallbacks(new AndroidCharacteristicCallbacks());
    
    pService->start();
    
    // Démarrer l'advertising pour Android
    DEBUG_PRINTLN("[BLE] Starting advertising...");
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(ANDROID_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    BLEDevice::startAdvertising();
    
    DEBUG_PRINTLN("[BLE] BLE Master ready");
}

// ==========================================
// SETUP
// ==========================================
void setup() {
    // Init Serial for debugging only
    #ifdef DEBUG
        Serial.begin(SERIAL_BAUD);
        while (!Serial) ;
    #endif

    DEBUG_PRINTLN("Starting up...");
    DEBUG_PRINTLN("======================================");
    DEBUG_PRINTLN("   COMPOST MONITORING SYSTEM");
    DEBUG_PRINTLN("   MODE: MASTER");
    DEBUG_PRINTLN("======================================");
    
    // Initialisation de la carte SD
    if (!initSD()) {
        DEBUG_PRINTLN("[SD] Warning: SD card not available");
        DEBUG_PRINTLN("[SD] System will continue without saving");
    }
    
    // Initialisation BLE
    init_BLE();
    
    // Initialiser les structures de données
    for (int i = 0; i < MAX_SLAVES; i++) {
        slavesData[i].boardId = i + 1;
        slavesData[i].temperature = 0.0;
        slavesData[i].pressure = 0.0;
        slavesData[i].humidity = 0.0;
        slavesData[i].oxygen = -1.0;
        strcpy(slavesData[i].isoTime, "0000-00-00T00:00:00");
        slavesData[i].received = false;
    }
    
    DEBUG_PRINTLN("--- Finished setup !!! ---");
}

// ==========================================
// LOOP
// ==========================================
void loop() {
    delay(10);
    MasterState currentState = SCAN_SLAVES;
    int32_t timer_start_time = millis();
    
    while (1) {
        switch(currentState) {
            case SCAN_SLAVES:
                DEBUG_PRINTLN("[SCAN_SLAVES]");
                
                // Incrémenter la date (ajouter le temps de sleep)
                incrementDateTime(SLEEP_TIME_MINUTES * 60);
                saveDateTime();
                
                // Scanner les esclaves
                scanSlaves();
                allSlavesScanned = true;
                currentState = PROCESS_DATA;
                timer_start_time = millis();
                TIMEOUT_COUNTER = 0;
                break;
            
            case PROCESS_DATA:
                DEBUG_PRINTLN("[PROCESS_DATA]");
                
                // Vérifier le timeout
                if (millis() - timer_start_time > SLEEP_DURATION / 1000) {
                    TIMEOUT_COUNTER++;
                    DEBUG_PRINT("[PROCESS_DATA] TIMEOUT_COUNTER = ");
                    DEBUG_PRINTLN(TIMEOUT_COUNTER);
                    timer_start_time = millis();
                }
                
                // Sauvegarder les données sur SD si disponibles
                if (allSlavesScanned) {
                    saveDataToSD();
                    allSlavesScanned = false;
                    
                    // Afficher un résumé
                    DEBUG_PRINTLN("[PROCESS_DATA] Summary:");
                    for (int i = 0; i < MAX_SLAVES; i++) {
                        if (slavesData[i].received) {
                            DEBUG_PRINT("[PROCESS_DATA]    Board ");
                            DEBUG_PRINT(slavesData[i].boardId);
                            DEBUG_PRINT(": T=");
                            DEBUG_PRINT(slavesData[i].temperature);
                            DEBUG_PRINT("C  H=");
                            DEBUG_PRINT(slavesData[i].humidity);
                            DEBUG_PRINT("%  O2=");
                            DEBUG_PRINT(slavesData[i].oxygen);
                            DEBUG_PRINTLN("%");
                        } else {
                            DEBUG_PRINT("[PROCESS_DATA]    Board ");
                            DEBUG_PRINT(i+1);
                            DEBUG_PRINTLN(": No data received");
                        }
                    }
                }
                
                currentState = WAIT_ANDROID;
                timer_start_time = millis();
                break;
            
            case WAIT_ANDROID:
                // Vérifier le timeout
                if (millis() - timer_start_time > SLEEP_DURATION / 1000) {
                    TIMEOUT_COUNTER++;
                    DEBUG_PRINT("[WAIT_ANDROID] TIMEOUT_COUNTER = ");
                    DEBUG_PRINTLN(TIMEOUT_COUNTER);
                    timer_start_time = millis();
                }
                
                // Gérer les requêtes Android
                if (dataRequested) {
                    DEBUG_PRINTLN("[WAIT_ANDROID] Data requested by Android");
                    sendDataToAndroid();
                    dataRequested = false;
                    TIMEOUT_COUNTER = 0;
                }
                
                if (clearRequested) {
                    DEBUG_PRINTLN("[WAIT_ANDROID] Clear requested by Android");
                    clearSDData();
                    clearRequested = false;
                    TIMEOUT_COUNTER = 0;
                }
                
                if (TIMEOUT_COUNTER >= MAX_TIMEOUT_COUNT) {
                    DEBUG_PRINTLN("[WAIT_ANDROID] Timeout reached, preparing sleep...");
                    currentState = PREPARE_SLEEP;
                }
                break;
            
            case PREPARE_SLEEP:
                if (millis() - timer_start_time > SLEEP_DURATION / 1000) {
                    TIMEOUT_COUNTER++;
                    DEBUG_PRINT("[PREPARE_SLEEP] TIMEOUT_COUNTER = ");
                    DEBUG_PRINTLN(TIMEOUT_COUNTER);
                    timer_start_time = millis();
                }
                
                if (TIMEOUT_COUNTER >= MAX_TIMEOUT_COUNT || !androidConnected) {
                    DEBUG_PRINTLN("[PREPARE_SLEEP] Entering deep sleep...");
                    TIMEOUT_COUNTER = 0;
                    
                    // Arrêter les services BLE
                    BLEDevice::deinit(false);
                    
                    // Configurer le deep sleep
                    esp_sleep_enable_timer_wakeup(SLEEP_DURATION);
                    esp_deep_sleep_start();
                } else {
                    // Attendre encore un peu pour Android
                    currentState = WAIT_ANDROID;
                }
                break;
            
            case BROKEN_LINK:
                DEBUG_PRINTLN("[BROKEN_LINK] Too many timeouts, shutting down indefinitely...");
                TIMEOUT_COUNTER = 0;
                esp_deep_sleep_start();
                break;
            
            default:
                currentState = SCAN_SLAVES;
                break;
        }
    }
}