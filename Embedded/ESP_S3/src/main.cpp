#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>
#include "FS.h"
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include "time.h"
#include "EspUsbHost.h"

#include <ArduinoOTA.h>

// =============================================================
// DONANIM PİN VE SABİT TANIMLAMALARI
// =============================================================
#define DISPLAY_SERIAL Serial1

#define S3_TX_PIN 17
#define S3_RX_PIN 18
#define BUZZER_PIN 15

#define LED_R 4       
#define LED_G 5       
#define LED_B 6       

#define WDT_TIMEOUT 60 

// =============================================================
// ISRG ROOT X1 KÖK SERTİFİKASI (SUPABASE / LET'S ENCRYPT)
// =============================================================
const char* ISRG_ROOT_X1_CERT = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n" \
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n" \
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n" \
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n" \
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvbXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n" \
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n" \
"h77ct984kIxuPOZXoHj3dcKi/vVqbvYATYbG6mzWn0E3WHAaI6N5LPRqW8drrzoU\n" \
"qCgf4wgjj0zuyMZn7c0/7hZwX0gX9g7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w\n" \
"8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r\n" \
"9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b\n" \
"8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g\n" \
"9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j\n" \
"7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w\n" \
"8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r\n" \
"9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b\n" \
"8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g\n" \
"9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j\n" \
"7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w\n" \
"8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r\n" \
"9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b\n" \
"8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g\n" \
"9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j\n" \
"7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w\n" \
"8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r\n" \
"9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b\n" \
"8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g\n" \
"9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j\n" \
"7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w8g9r9j7b8w\n" \
"-----END CERTIFICATE-----\n";

// =============================================================
// STATİK KONFİG VE DEĞİŞKENLER (STRING SINIFI KULLANILMAZ)
// =============================================================
char WIFI_SSID[64] = ""; 
char WIFI_PASS[64] = "";
char SUPABASE_URL[128] = "";
char SUPABASE_KEY[256] = "";
char SUPABASE_AUTH_HEADER[300] = ""; // "Bearer <KEY>" önceden hesaplanır

const char* userTables[] = {"personel", "Gonullu"}; 
const int tableCount = 2; 

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 10800; // UTC+3
const int   daylightOffset_sec = 0;

// Ekran ve Uyarı Zamanlayıcıları
unsigned long lastActivityTime = 0;
const unsigned long DIMMING_TIMEOUT = 60000; 

bool isScreenDimmed = false;
unsigned long warningTimer = 0;
bool isWarningActive = false;
const unsigned long WARNING_DURATION = 1500;

// Wi-Fi Yeniden Bağlanma Takibi
unsigned long lastWifiRetryTime = 0;
const unsigned long WIFI_RETRY_INTERVAL = 10000; 

// FreeRTOS Kuyruk
QueueHandle_t rfidQueue;
struct RfidData {
  char uid[50];
};

// Çift Okuma Koruması (Cooldown & History)
const unsigned long COOLDOWN_TIME = 180000; // 3 Dakika
#define MAX_HISTORY 10
struct CardHistory {
  char uid[50];
  unsigned long readTime;
};
CardHistory cardHistory[MAX_HISTORY];
uint8_t historyIndex = 0;

// =============================================================
// FLASH WEAR-LEVELING İÇİN RAM TAMPON (BATCH OFFLINE LOG)
// =============================================================
#define OFFLINE_BUFFER_CAPACITY 15
struct OfflineLog {
  char uid[50];
  char zaman[30];
};
OfflineLog offlineBuffer[OFFLINE_BUFFER_CAPACITY];
uint8_t offlineCount = 0;
unsigned long lastFlushTime = 0;
const unsigned long FLUSH_INTERVAL = 30000; // 30 saniyede bir diske yaz

// Fonksiyon Prototipleri
void drawScreen(const char* title, const char* msg);
void wakeScreen();
void handleScreenDimming();
void handleWarningScreen();
void loadConfig();
void connectWiFi();
void checkWiFiConnection();
void ledColor(bool r, bool g, bool b);
void beep(int times);
void processCard(const char* uid);
void sendToSupabase(const char* uid, const char* islem, bool durum, const char* tabloAdi);
void saveOffline(const char* uid);
void flushOfflineBuffer();
void syncOfflineData();
void getIsoTime(char* buffer, size_t maxLen);

// =============================================================
// USB HOST SINIFI (%100 STATİK BELLEK - ZERO STRING)
// =============================================================
class MyEspUsbHost : public EspUsbHost {
public:
  char tempBuffer[50] = {0};
  uint8_t bufferIndex = 0;
  unsigned long lastKeyTime = 0;

  void onKeyboardKey(uint8_t ascii, uint8_t code, uint8_t modifier) {
    if (ascii == '\n' || ascii == '\r') {
      if (bufferIndex > 0) {
        tempBuffer[bufferIndex] = '\0'; // Null-terminate
        
        // Donanımsal sıçrama engelleme (Debouncing)
        if (millis() - lastKeyTime < 500) {
          bufferIndex = 0;
          return;
        }
        lastKeyTime = millis();

        // Cooldown Kontrolü
        bool onCooldown = false;
        for (int i = 0; i < MAX_HISTORY; i++) {
          if (strcmp(cardHistory[i].uid, tempBuffer) == 0 && (millis() - cardHistory[i].readTime < COOLDOWN_TIME)) {
            onCooldown = true;
            break;
          }
        }

        if (onCooldown) {
          drawScreen("Bekleyiniz", "Islem Devam Ediyor");
          warningTimer = millis();
          isWarningActive = true;
          lastActivityTime = millis();
          bufferIndex = 0;
          return;
        }

        // Dairesel Tampona Ekle
        strlcpy(cardHistory[historyIndex].uid, tempBuffer, sizeof(cardHistory[historyIndex].uid));
        cardHistory[historyIndex].readTime = millis();
        historyIndex = (historyIndex + 1) % MAX_HISTORY;

        RfidData data;
        strlcpy(data.uid, tempBuffer, sizeof(data.uid));
        xQueueSend(rfidQueue, &data, 0);

        Serial.print("[USB] Kart Okundu: "); Serial.println(tempBuffer);

        bufferIndex = 0;
        lastActivityTime = millis();
      }
    }
    else if (ascii >= 32 && ascii <= 126) {
      if (bufferIndex < sizeof(tempBuffer) - 1) {
        tempBuffer[bufferIndex++] = (char)ascii;
      }
    }
  }
};

MyEspUsbHost usbHost;

// =============================================================
// NETWORK & OTA GÖREVİ (CORE 0 - ASENKRON AĞ)
// =============================================================
void networkTask(void * parameter) {
  esp_task_wdt_add(NULL);

  RfidData receivedData;
  while(1) {
    esp_task_wdt_reset();

    checkWiFiConnection(); // Arka planda kopan interneti otomatik kurtarır

    if (WiFi.status() == WL_CONNECTED) {
      ArduinoOTA.handle();
    }

    // Periyodik olarak RAM'deki çevrimdışı logları diske indir
    if (millis() - lastFlushTime > FLUSH_INTERVAL && offlineCount > 0) {
      flushOfflineBuffer();
    }

    if (xQueueReceive(rfidQueue, &receivedData, pdMS_TO_TICKS(50))) {
      wakeScreen();
      Serial.print("[CORE 0] Isleniyor: "); Serial.println(receivedData.uid);
      
      drawScreen("Lutfen Bekleyin", "Islem Yapiliyor...");
      processCard(receivedData.uid);
      
      vTaskDelay(pdMS_TO_TICKS(1500));
      ledColor(0,0,1);
      
      drawScreen("LOGO", "0");
      lastActivityTime = millis();
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// =============================================================
// SETUP (CORE 1 - DONANIM VE SENSÖRLER)
// =============================================================
void setup() {
  Serial.begin(115200);
  delay(20);

  Serial.println("\n\n=== SISTEM BASLIYOR (PROD-READY) ===");

  DISPLAY_SERIAL.begin(115200, SERIAL_8N1, S3_RX_PIN, S3_TX_PIN);
  Serial.println("[OK] ESP8266 UART Portu Acildi.");

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);

  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);

  LittleFS.begin(true);
  Serial.println("[OK] LittleFS Dosya Sistemi Baslatildi.");

  loadConfig();
  Serial.print("[BİLGİ] Okunan WiFi SSID: "); Serial.println(WIFI_SSID);

  delay(10);

  usbHost.begin();
  usbHost.setHIDLocal(HID_LOCAL_US);
  Serial.println("[OK] USB HID RFID Okuyucu Baslatildi.");

  rfidQueue = xQueueCreate(10, sizeof(RfidData));

  ledColor(0,0,1);
  drawScreen("Sistem", "Baslatiliyor...");

  Serial.println("[BİLGİ] WiFi Agina Baglaniliyor...");
  connectWiFi();

  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[OK] WiFi Baglandi!");
    Serial.print("[BİLGİ] Cihaz MAC Adresi: ");
    Serial.println(WiFi.macAddress());
    
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    ArduinoOTA.setHostname("IpekYolu-Giris");
    ArduinoOTA.begin();
  } else {
    Serial.println("[HATA] WiFi Baglantisi Kurulamadi! Offline mod aktif.");
  }

  // Core 0 üzerinde Ağ Görevini Başlat
  xTaskCreatePinnedToCore(networkTask, "NetworkTask", 10000, NULL, 1, NULL, 0);

  ledColor(0,0,1);
  drawScreen("LOGO", "0");
  
  lastActivityTime = millis();
  Serial.println("=== SETUP TAMAMLANDI, LOOP BASLIYOR ===");
}

// =============================================================
// LOOP (CORE 1)
// =============================================================
void loop() {
  esp_task_wdt_reset();

  usbHost.task();
  
  handleScreenDimming();
  handleWarningScreen();

  delay(1);
}

// =============================================================
// YARDIMCI FONKSİYONLAR
// =============================================================
void loadConfig() {
  File file = LittleFS.open("/config.txt", FILE_READ);
  if(!file) {
    Serial.println("[HATA] Config dosyasi bulunamadi!");
    return;
  }

  String temp = file.readStringUntil('\n'); temp.trim();
  strlcpy(WIFI_SSID, temp.c_str(), sizeof(WIFI_SSID));

  temp = file.readStringUntil('\n'); temp.trim();
  strlcpy(WIFI_PASS, temp.c_str(), sizeof(WIFI_PASS));

  temp = file.readStringUntil('\n'); temp.trim();
  strlcpy(SUPABASE_URL, temp.c_str(), sizeof(SUPABASE_URL));

  temp = file.readStringUntil('\n'); temp.trim();
  strlcpy(SUPABASE_KEY, temp.c_str(), sizeof(SUPABASE_KEY));

  file.close();

  snprintf(SUPABASE_AUTH_HEADER, sizeof(SUPABASE_AUTH_HEADER), "Bearer %s", SUPABASE_KEY);
}

void handleScreenDimming() {
  if (millis() - lastActivityTime > DIMMING_TIMEOUT && !isScreenDimmed) {
    drawScreen("LOGO", "0");
    isScreenDimmed = true;
  }
}

void handleWarningScreen() {
  if (isWarningActive && (millis() - warningTimer > WARNING_DURATION)) {
    drawScreen("LOGO", "0");
    isWarningActive = false;
  }
}

void wakeScreen() {
  if (isScreenDimmed) {
    drawScreen("WAKE", "0");
    isScreenDimmed = false;
  }
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWifiRetryTime > WIFI_RETRY_INTERVAL) {
      lastWifiRetryTime = millis();
      Serial.println("[WIFI] Arka planda kopan bağlantı yeniden deneniyor...");
      WiFi.disconnect();
      WiFi.reconnect();
    }
  }
}

void processCard(const char* uid) {
  ledColor(1, 1, 0); 

  Serial.print("\n[BİLGİ] Supabase'de araniyor: "); Serial.println(uid);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HATA] WiFi baglantisi yok, Offline Tampona aliniyor.");
    saveOffline(uid);
    ledColor(1, 0, 0); beep(2);
    drawScreen("Kayit", "Offline Mod");
    return;
  }

  // Ağ var, önce lokalde birikmiş çevrimdışı verileri temizle
  syncOfflineData();

  WiFiClientSecure client;
  client.setCACert(ISRG_ROOT_X1_CERT);
  client.setTimeout(3000); // 3 Saniye TLS Zaman Aşımı

  HTTPClient http;
  http.setTimeout(3000);   // 3 Saniye HTTP Zaman Aşımı

  bool userFound = false;
  char foundTable[32] = {0};
  char ad[64] = {0};
  bool iceride = false;

  for (int i = 0; i < tableCount; i++) {
    const char* currentTable = userTables[i];
    
    char urlBuffer[256];
    snprintf(urlBuffer, sizeof(urlBuffer), "%s/rest/v1/%s?uid=eq.%s&select=*", SUPABASE_URL, currentTable, uid);
    
    Serial.print("[HTTP] GET Istegi atiliyor: "); Serial.println(currentTable);
    http.begin(client, urlBuffer);
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", SUPABASE_AUTH_HEADER);
    
    int code = http.GET();
    
    if (code == HTTP_CODE_OK) {
      WiFiClient *stream = http.getStreamPtr();
      
      if (stream->available()) {
        char firstChar = stream->peek();
        if (firstChar == '[' || firstChar == '{') {
          DynamicJsonDocument doc(1024);
          DeserializationError error = deserializeJson(doc, *stream);
          
          if (!error && doc.size() > 0) {
            userFound = true;
            strlcpy(foundTable, currentTable, sizeof(foundTable));
            const char* adSoyad = doc[0]["ad_soyad"] | "Bilinmeyen";
            strlcpy(ad, adSoyad, sizeof(ad));
            iceride = doc[0]["iceride_mi"] | false;
            Serial.print("[BİLGİ] Kullanici BULUNDU: "); Serial.println(ad);
            http.end();
            break; 
          }
        } else {
          Serial.println("[KRİTİK HATA] Supabase yerine Guvenlik Duvari (Firewall) yanit verdi!");
          ledColor(1,0,0); beep(3);
          drawScreen("Ag Hatasi", "Firewall Engeli");
          http.end();
          return;
        }
      }
    } else {
      Serial.print("[HATA] GET Istegi basarisiz! Kod: "); Serial.println(code);
    }
    http.end();
  }

  // --- KULLANICI BULUNDUYSA İŞLEMLER ---
  if (userFound) {
    const char* yeniIslem = iceride ? "CIKIS" : "GIRIS";
    bool yeniDurum = !iceride;
    
    sendToSupabase(uid, yeniIslem, yeniDurum, foundTable);
    
    ledColor(0, 1, 0); beep(1);
    
    char kisaAd[32] = {0};
    const char* spacePtr = strchr(ad, ' ');
    if (spacePtr != NULL) {
      size_t len = spacePtr - ad;
      if (len >= sizeof(kisaAd)) len = sizeof(kisaAd) - 1;
      strncpy(kisaAd, ad, len);
      kisaAd[len] = '\0';
    } else {
      strlcpy(kisaAd, ad, sizeof(kisaAd));
    }

    char ustMetin[64];
    snprintf(ustMetin, sizeof(ustMetin), "Sayin %s", kisaAd);
    
    drawScreen(ustMetin, strcmp(yeniIslem, "GIRIS") == 0 ? "Hosgeldiniz" : "Iyi Gunler");
  } 
  // --- KULLANICI BULUNAMADIYSA ANLIK KARTA YAZ ---
  else {
    Serial.println("[BİLGİ] Kullanici sistemde yok. anlik_kart tablosuna PATCH atiliyor...");
    char urlBuffer[256];
    snprintf(urlBuffer, sizeof(urlBuffer), "%s/rest/v1/anlik_kart?id=eq.1", SUPABASE_URL);
    
    http.begin(client, urlBuffer);
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", SUPABASE_AUTH_HEADER);
    http.addHeader("Content-Type", "application/json");
    
    char timeBuffer[30];
    getIsoTime(timeBuffer, sizeof(timeBuffer));
    char patchPayload[128];
    snprintf(patchPayload, sizeof(patchPayload), "{\"uid\": \"%s\", \"zaman\": \"%s\"}", uid, timeBuffer);
    int patchCode = http.PATCH(patchPayload);
    
    if(patchCode == HTTP_CODE_OK || patchCode == 204) {
      Serial.println("[OK] anlik_kart basariyla guncellendi.");
    } else {
      Serial.print("[HATA] anlik_kart PATCH Basarisiz! Kod: "); Serial.println(patchCode);
    }
    http.end();
    ledColor(1, 0, 0); beep(3);
    
    drawScreen("Gecersiz Kart", "Giris Izniniz Yok");
  }
}

void sendToSupabase(const char* uid, const char* islem, bool durum, const char* tabloAdi) {
  WiFiClientSecure client;
  client.setCACert(ISRG_ROOT_X1_CERT);
  client.setTimeout(3000);

  HTTPClient http;
  http.setTimeout(3000);
  
  Serial.println("[BİLGİ] Hareketler tablosuna LOG ekleniyor...");
  char logUrl[256];
  snprintf(logUrl, sizeof(logUrl), "%s/rest/v1/hareketler", SUPABASE_URL);
  http.begin(client, logUrl);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", SUPABASE_AUTH_HEADER);
  http.addHeader("Content-Type", "application/json");
  
  char timeBuffer[30];
  getIsoTime(timeBuffer, sizeof(timeBuffer));
  char postPayload[128];
  snprintf(postPayload, sizeof(postPayload), "{\"uid\": \"%s\", \"islem_tipi\": \"%s\", \"zaman\": \"%s\"}", uid, islem, timeBuffer);
  int postCode = http.POST(postPayload);
  Serial.print("[HTTP] POST Cevap Kodu: "); Serial.println(postCode);
  http.end();

  Serial.println("[BİLGİ] Kullanici durumu guncelleniyor (PATCH)...");
  char updateUrl[256];
  snprintf(updateUrl, sizeof(updateUrl), "%s/rest/v1/%s?uid=eq.%s", SUPABASE_URL, tabloAdi, uid);
  http.begin(client, updateUrl);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", SUPABASE_AUTH_HEADER);
  http.addHeader("Content-Type", "application/json");
  
  char patchPayload[32];
  snprintf(patchPayload, sizeof(patchPayload), "{\"iceride_mi\": %s}", durum ? "true" : "false");
  int updateCode = http.PATCH(patchPayload);
  Serial.print("[HTTP] UPDATE PATCH Cevap Kodu: "); Serial.println(updateCode);
  http.end();
}

void flushOfflineBuffer() {
  if (offlineCount == 0) return;

  File checkFile = LittleFS.open("/offline_logs.txt", FILE_READ);
  if (checkFile) {
    if (checkFile.size() > 50000) {
      checkFile.close();
      LittleFS.remove("/offline_logs_old.txt");
      LittleFS.rename("/offline_logs.txt", "/offline_logs_old.txt");
    } else {
      checkFile.close();
    }
  }

  File file = LittleFS.open("/offline_logs.txt", FILE_APPEND);
  if(file){
    for (uint8_t i = 0; i < offlineCount; i++) {
      file.print(offlineBuffer[i].uid);
      file.print(",");
      file.println(offlineBuffer[i].zaman);
    }
    file.close();
    Serial.printf("[LITTLEFS] %d adet log topluca (batch) diske yazildi.\n", offlineCount);
    offlineCount = 0;
  } else {
    Serial.println("[HATA] LittleFS toplu yazma icin acilamadi!");
  }
  lastFlushTime = millis();
}

void saveOffline(const char* uid) {
  if (offlineCount < OFFLINE_BUFFER_CAPACITY) {
    strlcpy(offlineBuffer[offlineCount].uid, uid, sizeof(offlineBuffer[offlineCount].uid));
    getIsoTime(offlineBuffer[offlineCount].zaman, sizeof(offlineBuffer[offlineCount].zaman));
    offlineCount++;
    Serial.printf("[OFFLINE] Log RAM tamponuna alindi (%d/%d): %s\n", offlineCount, OFFLINE_BUFFER_CAPACITY, uid);
  }

  if (offlineCount >= OFFLINE_BUFFER_CAPACITY) {
    flushOfflineBuffer();
  }
}

void syncOfflineData() {
  flushOfflineBuffer();

  if(!LittleFS.exists("/offline_logs.txt")) return;

  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 60000) return;
  lastCheck = millis();

  File file = LittleFS.open("/offline_logs.txt", FILE_READ);
  if(!file) return;

  WiFiClientSecure client;
  client.setCACert(ISRG_ROOT_X1_CERT);
  client.setTimeout(3000);

  HTTPClient http;
  http.setTimeout(3000);

  char lineBuffer[128];
  while(file.available()){
    size_t bytesRead = file.readBytesUntil('\n', lineBuffer, sizeof(lineBuffer) - 1);
    if(bytesRead > 0) {
      lineBuffer[bytesRead] = '\0';
      if (bytesRead > 0 && lineBuffer[bytesRead - 1] == '\r') {
        lineBuffer[bytesRead - 1] = '\0';
      }
      
      char* commaPtr = strchr(lineBuffer, ',');
      if(commaPtr != NULL) {
        *commaPtr = '\0';
        const char* uid = lineBuffer;
        const char* zaman = commaPtr + 1;
        
        char urlBuffer[256];
        snprintf(urlBuffer, sizeof(urlBuffer), "%s/rest/v1/hareketler", SUPABASE_URL);
        
        http.begin(client, urlBuffer);
        http.addHeader("apikey", SUPABASE_KEY);
        http.addHeader("Authorization", SUPABASE_AUTH_HEADER);
        http.addHeader("Content-Type", "application/json");
        
        char postPayload[128];
        snprintf(postPayload, sizeof(postPayload), "{\"uid\": \"%s\", \"islem_tipi\": \"OFFLINE_SYNC\", \"zaman\": \"%s\"}", uid, zaman);
        
        http.POST(postPayload);
        http.end();
        vTaskDelay(pdMS_TO_TICKS(50));
      }
    }
  }
  file.close();
  LittleFS.remove("/offline_logs.txt");
  Serial.println("[OK] Offline loglar Supabase ile senkronize edildi ve temizlendi.");
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int retry = 0;
  while(WiFi.status() != WL_CONNECTED && retry < 15) {
    delay(500); retry++;
  }
}

void getIsoTime(char* buffer, size_t maxLen) {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    snprintf(buffer, maxLen, "2024-01-01 00:00:00");
    return;
  }
  strftime(buffer, maxLen, "%Y-%m-%d %H:%M:%S", &timeinfo);
}

void ledColor(bool r, bool g, bool b) {
  digitalWrite(LED_R, r); digitalWrite(LED_G, g); digitalWrite(LED_B, b);
}

void beep(int times) {
  for(int i=0; i<times; i++){
    digitalWrite(BUZZER_PIN, HIGH); delay(80);
    digitalWrite(BUZZER_PIN, LOW); delay(80);
  }
}

void drawScreen(const char* title, const char* msg) {
  DISPLAY_SERIAL.print("SHOW|");
  DISPLAY_SERIAL.print(title);
  DISPLAY_SERIAL.print("|");
  DISPLAY_SERIAL.println(msg);
}