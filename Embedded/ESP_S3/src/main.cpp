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
// AYARLAR VE GLOBAL DEĞİŞKENLER
// =============================================================
// Master-Slave Haberleşme Portu
#define DISPLAY_SERIAL Serial1 
#define S3_TX_PIN 17
#define S3_RX_PIN 18

// Ağ ve Veritabanı kimlik bilgileri (LittleFS üzerinden config.txt'den okunur)
String WIFI_SSID = ""; 
String WIFI_PASS = "";
String SUPABASE_URL = "";
String SUPABASE_KEY = "";

// Supabase üzerinde kontrol edilecek tablo isimleri
const char* userTables[] = {"personel", "Gonullu"}; 
const int tableCount = 2; 

// Zaman senkronizasyonu için NTP ayarları
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 10800; // UTC+3 (Türkiye Saati)
const int   daylightOffset_sec = 0;

// Sistem kilitlenmelerine karşı Watchdog Timer süresi (saniye)
#define WDT_TIMEOUT 60 

// Donanım Pin Tanımlamaları
#define BUZZER_PIN 15 
#define LED_R 4       
#define LED_G 5       
#define LED_B 6       

// Ekran Parlaklık Kontrolü (Ekran ömrünü uzatmak için)
unsigned long lastActivityTime = 0;
const unsigned long DIMMING_TIMEOUT = 60000; // 1 dakika işlem olmazsa ekran kararır
bool isScreenDimmed = false;

// Geçici Uyarı Ekranı Kontrolü (Core 1 Non-blocking)
unsigned long warningTimer = 0;
bool isWarningActive = false;
const unsigned long WARNING_DURATION = 1500; // 1.5 saniye sonra logoya dön

// RFID verilerini asenkron işlemek için FreeRTOS Kuyruğu (Queue)
QueueHandle_t rfidQueue;
struct RfidData {
  char uid[50];
};

// Cooldown (Çift okuma koruması) değişkenleri
const unsigned long COOLDOWN_TIME = 180000; // 3 Dakika
#define MAX_HISTORY 10

struct CardHistory {
  char uid[50];
  unsigned long readTime;
};

// Geçmişte okutulan kartları hafızada tutacak statik dizi (Circular Buffer mantığı)
CardHistory cardHistory[MAX_HISTORY];
uint8_t historyIndex = 0;

// Fonksiyon Prototipleri
void drawScreen(String title, String msg);
void wakeScreen();
void handleScreenDimming();
void handleWarningScreen();
void loadConfig();
void connectWiFi();
void ledColor(bool r, bool g, bool b);
void beep(int times);
void processCard(String uid);
void sendToSupabase(String uid, String islem, bool durum, String tabloAdi);
void saveOffline(String uid);
void syncOfflineData();
String getIsoTime();

// USB Host sınırları üzerinden klavye/RFID okuyucu girdilerini yakalayan özel sınıf
class MyEspUsbHost : public EspUsbHost {
public:
  String tempBuffer = "";

  void onKeyboardKey(uint8_t ascii, uint8_t code, uint8_t modifier) {
    if (ascii == '\n' || ascii == '\r') {
      if (tempBuffer.length() > 0) {
        
        bool onCooldown = false;
        for (int i = 0; i < MAX_HISTORY; i++) {
          // strcmp kullanarak dinamik bellek ayrımını (String oluşturmayı) engelliyoruz
          // Geçerli bir UID statik dizide varsa ve bekleme süresi dolmamışsa engelle
          if (strcmp(cardHistory[i].uid, tempBuffer.c_str()) == 0 && (millis() - cardHistory[i].readTime < COOLDOWN_TIME)) {
            onCooldown = true;
            break;
          }
        }
        
        if (onCooldown) {
          DISPLAY_SERIAL.println("SHOW|Bekleyiniz|Islem Devam Ediyor");
          warningTimer = millis();
          isWarningActive = true;
          lastActivityTime = millis();
          tempBuffer = ""; 
          return; 
        }
        
        // Yeni kartı dairesel tampon (circular buffer) mantığıyla geçmişe statik olarak ekle
        tempBuffer.toCharArray(cardHistory[historyIndex].uid, 50);
        cardHistory[historyIndex].readTime = millis();
        historyIndex = (historyIndex + 1) % MAX_HISTORY;
        
        RfidData data;
        tempBuffer.toCharArray(data.uid, 50);
        xQueueSend(rfidQueue, &data, 0);
        
        Serial.print("[USB] Kart Okundu: "); Serial.println(tempBuffer);
        
        tempBuffer = ""; 
        lastActivityTime = millis();
      }
    }
    else if (ascii >= 32 && ascii <= 126) {
      tempBuffer += (char)ascii;
    }
  }
};

MyEspUsbHost usbHost;

// =============================================================
// NETWORK & OTA GÖREVİ (CORE 0)
// =============================================================
void networkTask(void * parameter) {
  esp_task_wdt_add(NULL); 
  
  RfidData receivedData;

  while(1) {
    esp_task_wdt_reset(); 
    ArduinoOTA.handle(); 
    
    if (xQueueReceive(rfidQueue, &receivedData, pdMS_TO_TICKS(100))) {
      wakeScreen();
      
      String uid = String(receivedData.uid);
      Serial.print("[CORE 0] Isleniyor: "); Serial.println(uid);
      
      drawScreen("Lutfen Bekleyin", "Islem Yapiliyor...");
      processCard(uid);
      
      delay(1500); 
      ledColor(0,0,1); 
      
      DISPLAY_SERIAL.println("LOGO|0|0");
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// =============================================================
// SETUP (CORE 1)
// =============================================================
void setup() {
  Serial.begin(115200); 
  delay(20); // Seri portun bağlanması için kısa bir süre tanı
  Serial.println("\n\n=== SISTEM BASLIYOR ===");
  
  // ESP8266 ile UART haberleşmesi başlatılıyor
  DISPLAY_SERIAL.begin(115200, SERIAL_8N1, S3_RX_PIN, S3_TX_PIN);
  Serial.println("[OK] ESP8266 UART Portu Acildi.");
  
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);

  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);

  LittleFS.begin(true);
  Serial.println("[OK] LittleFS Dosya Sistemi Baslatildi.");
  loadConfig(); 
  Serial.print("[BİLGİ] Okunan WiFi SSID: "); Serial.println(WIFI_SSID);

  // USB cihazın uyanması için zaman tanı
  delay(10);
  usbHost.begin();
  usbHost.setHIDLocal(HID_LOCAL_US);
  Serial.println("[OK] USB HID RFID Okuyucu Baslatildi.");

  rfidQueue = xQueueCreate(10, sizeof(RfidData));

  ledColor(0,0,1); 
  Serial.println("[BİLGİ] Ekrana 'Baslatiliyor' komutu gonderiliyor...");
  drawScreen("Sistem", "Baslatiliyor...");
  
  Serial.println("[BİLGİ] WiFi Agina Baglaniliyor...");

  connectWiFi();

  if(WiFi.status() == WL_CONNECTED) {
      Serial.println("\n[OK] WiFi Baglandi!");
      // IT BIRIMINE VERILECEK MAC ADRESI BURADA YAZAR:
      Serial.print("[BİLGİ] Cihaz MAC Adresi (Whitelist için): ");
      Serial.println(WiFi.macAddress());
      
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      ArduinoOTA.setHostname("IpekYolu-Giris");
      ArduinoOTA.begin();
    } else {
      Serial.println("[HATA] WiFi Baglantisi Kurulamadi!");
    }

    xTaskCreatePinnedToCore(networkTask, "NetworkTask", 10000, NULL, 1, NULL, 0);

    ledColor(0,0,1); 
    Serial.println("[BİLGİ] Ekrana varsayılan bos (logo) ekran komutu gonderiliyor...");
    
    DISPLAY_SERIAL.println("LOGO|0|0"); 
    
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
    Serial.println("Config dosyasi bulunamadi!");
    return;
  }
  WIFI_SSID = file.readStringUntil('\n'); WIFI_SSID.trim();
  WIFI_PASS = file.readStringUntil('\n'); WIFI_PASS.trim();
  SUPABASE_URL = file.readStringUntil('\n'); SUPABASE_URL.trim();
  SUPABASE_KEY = file.readStringUntil('\n'); SUPABASE_KEY.trim();
  file.close();
}

void handleScreenDimming() {
  if (millis() - lastActivityTime > DIMMING_TIMEOUT && !isScreenDimmed) {
    DISPLAY_SERIAL.println("LOGO|0|0");
    isScreenDimmed = true;
  }
}

void handleWarningScreen() {
  if (isWarningActive && (millis() - warningTimer > WARNING_DURATION)) {
    DISPLAY_SERIAL.println("LOGO|0|0");
    isWarningActive = false;
  }
}

void wakeScreen() {
  if (isScreenDimmed) {
    DISPLAY_SERIAL.println("WAKE|0|0");
    isScreenDimmed = false;
  }
}

void processCard(String uid) {
  ledColor(1, 1, 0); 
  Serial.print("\n[BİLGİ] Supabase'de araniyor: "); Serial.println(uid);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HATA] WiFi baglantisi yok, Offline kaydediliyor.");
    saveOffline(uid);
    drawScreen("Kayit", "Offline Mod");
    return;
  }

  syncOfflineData();

  WiFiClientSecure client;
  client.setInsecure(); 
  HTTPClient http;

  bool userFound = false;
  String foundTable = "";
  String ad = "";
  bool iceride = false;

  for (int i = 0; i < tableCount; i++) {
    String currentTable = userTables[i];
    String queryUrl = SUPABASE_URL + "/rest/v1/" + currentTable + "?uid=eq." + uid + "&select=*";
    
    Serial.print("[HTTP] GET Istegi atiliyor: "); Serial.println(currentTable);
    http.begin(client, queryUrl); 
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + SUPABASE_KEY);
    
    int code = http.GET();
    
    if (code == 200) {
      String payload = http.getString();
      
      // KRİTİK GÜVENLİK DUVARI KONTROLÜ: Gelen veri JSON mu yoksa FortiGate HTML'i mi?
      if (payload.startsWith("[") || payload.startsWith("{")) {
          DynamicJsonDocument doc(1024);
          deserializeJson(doc, payload);

          if (doc.size() > 0) {
            userFound = true;
            foundTable = currentTable;
            ad = doc[0]["ad_soyad"].as<String>();
            iceride = doc[0]["iceride_mi"];
            Serial.print("[BİLGİ] Kullanici BULUNDU: "); Serial.println(ad);
            break; 
          }
      } else {
          Serial.println("[KRİTİK HATA] Supabase yerine Güvenlik Duvarı (Firewall) yanıt verdi!");
          Serial.println("[KRİTİK HATA] Lütfen cihazın MAC adresini ağda beyaz listeye (Bypass) alın.");
          ledColor(1,0,0); beep(3);
          drawScreen("Ag Hatasi", "Firewall Engeli");
          http.end();
          return; // İşlemi iptal et, ağda internet yok.
      }
    } else {
      Serial.print("[HATA] GET Istegi basarisiz! Kod: "); Serial.println(code);
    }
    http.end(); 
  }

  // --- KULLANICI BULUNDUYSA İŞLEMLER ---
  if (userFound) {
    String yeniIslem = iceride ? "CIKIS" : "GIRIS";
    bool yeniDurum = !iceride;
    
    sendToSupabase(uid, yeniIslem, yeniDurum, foundTable);
    
    ledColor(0, 1, 0); beep(1); 

    // 3. İSTEK: Kişiye özel Hoşgeldiniz / İyi Günler mesajı oluştur (İsmi ayırarak)
    String kisaAd = ad;
    int boslukIndex = ad.indexOf(' ');
    if (boslukIndex > 0) {
        kisaAd = ad.substring(0, boslukIndex);
    }
    String ustMetin = "Sayin " + kisaAd;
    
    drawScreen(ustMetin, yeniIslem == "GIRIS" ? "Hosgeldiniz" : "Iyi Gunler");
  } 
  // --- KULLANICI BULUNAMADIYSA ANLIK KARTA YAZ ---
  else {
    Serial.println("[BİLGİ] Kullanici sistemde yok. anlik_kart tablosuna PATCH atiliyor...");

    String url = SUPABASE_URL + "/rest/v1/anlik_kart?id=eq.1";
    http.begin(client, url);
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + SUPABASE_KEY);
    http.addHeader("Content-Type", "application/json");
    
    String patchPayload = "{\"uid\": \"" + uid + "\", \"zaman\": \"" + getIsoTime() + "\"}";
    int patchCode = http.PATCH(patchPayload);
    
    if(patchCode == 200 || patchCode == 204) {
       Serial.println("[OK] anlik_kart basariyla guncellendi.");
    } else {
       Serial.print("[HATA] anlik_kart PATCH Basarisiz! Kod: "); Serial.println(patchCode);
    }
    http.end();

    ledColor(1, 0, 0); beep(3); 
    
    drawScreen("Gecersiz Kart", "Giris Izniniz Yok");
  }
}

void sendToSupabase(String uid, String islem, bool durum, String tabloAdi) {
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    
    Serial.println("[BİLGİ] Hareketler tablosuna LOG ekleniyor...");
    String logUrl = SUPABASE_URL + "/rest/v1/hareketler";
    http.begin(client, logUrl);
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + SUPABASE_KEY);
    http.addHeader("Content-Type", "application/json");
    
    String postPayload = "{\"uid\": \"" + uid + "\", \"islem_tipi\": \"" + islem + "\", \"zaman\": \"" + getIsoTime() + "\"}";
    int postCode = http.POST(postPayload);
    Serial.print("[HTTP] POST Cevap Kodu: "); Serial.println(postCode);
    if(postCode != 201) {
        Serial.print("[HATA] POST Basarisiz: "); Serial.println(http.getString());
    }
    http.end();

    Serial.println("[BİLGİ] Kullanici durumu guncelleniyor (PATCH)...");
    String updateUrl = SUPABASE_URL + "/rest/v1/" + tabloAdi + "?uid=eq." + uid;
    http.begin(client, updateUrl);
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + SUPABASE_KEY);
    http.addHeader("Content-Type", "application/json");
    
    int updateCode = http.PATCH("{\"iceride_mi\": " + String(durum ? "true" : "false") + "}");
    Serial.print("[HTTP] UPDATE PATCH Cevap Kodu: "); Serial.println(updateCode);
    http.end();
}

void saveOffline(String uid) {
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
    file.println(uid + "," + getIsoTime());
    file.close();
  }
}

void syncOfflineData() {
  if(!LittleFS.exists("/offline_logs.txt")) return;
  
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 60000) return;
  lastCheck = millis();

  File file = LittleFS.open("/offline_logs.txt", FILE_READ);
  if(!file) return;

  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;

  while(file.available()){
    String line = file.readStringUntil('\n'); line.trim();
    if(line.length() > 0) {
      int commaIndex = line.indexOf(',');
      if(commaIndex > 0) {
        String uid = line.substring(0, commaIndex);
        String zaman = line.substring(commaIndex + 1);

        http.begin(client, SUPABASE_URL + "/rest/v1/hareketler");
        http.addHeader("apikey", SUPABASE_KEY);
        http.addHeader("Authorization", "Bearer " + SUPABASE_KEY);
        http.addHeader("Content-Type", "application/json");
        http.POST("{\"uid\": \"" + uid + "\", \"islem_tipi\": \"OFFLINE_SYNC\", \"zaman\": \"" + zaman + "\"}");
        http.end();
        delay(50); 
      }
    }
  }
  file.close();
  LittleFS.remove("/offline_logs.txt");
}

void connectWiFi() {
  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASS.c_str());
  int retry = 0;
  while(WiFi.status() != WL_CONNECTED && retry < 15) {
    delay(500); retry++;
  }
}

String getIsoTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return "2024-01-01 00:00:00"; 
  }
  char timeStringBuff[30];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeStringBuff);
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

void drawScreen(String title, String msg) {
  DISPLAY_SERIAL.print("SHOW|");
  DISPLAY_SERIAL.print(title);
  DISPLAY_SERIAL.print("|");
  DISPLAY_SERIAL.println(msg);
}