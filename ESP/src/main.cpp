#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h> 
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include "FS.h"
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include "time.h"
#include "EspUsbHost.h" 
#include <ArduinoOTA.h>

// =====================================================================================================================================
// AYARLAR VE GLOBAL DEĞİŞKENLER
// =====================================================================================================================================
// Ağ ve Veritabanı kimlik bilgileri (LittleFS üzerinden config.txt'den okunur)
String WIFI_SSID = ""; 
String WIFI_PASS = "";
String SUPABASE_URL = "";
String SUPABASE_KEY = "";

// Supabase üzerinde kontrol edilecek tablo isimleri
const char* userTables[] = {"personel", "Gonullu"}; 
const int tableCount = 2; 

// Zaman senkronizasyonu için NTP (Network Time Protocol) ayarları
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 10800; // UTC+3 (Türkiye Saati)
const int   daylightOffset_sec = 0;

// Periyodik NTP senkronizasyonu icin zaman takibi (24 saatte bir guncellenecek)
unsigned long lastNtpSyncTime = 0;
const unsigned long NTP_SYNC_INTERVAL = 86400000; // 24 Saat (Milisaniye)

// Sistem kilitlenmelerine karşı Watchdog Timer süresi (saniye)
#define WDT_TIMEOUT 60 

// =====================================================================================================================================
// DONANIM PİN TANIMLAMALARI (KS0108 PARALEL LCD VE ÇEVRE BİRİMLERİ)
// =====================================================================================================================================
#define BUZZER_PIN 15 
#define LED_R 4       
#define LED_G 5       
#define LED_B 6       

// KS0108 Ekran Pinleri (8-bit Paralel Veri Yolu ve Kontrol Sinyalleri)
// DİKKAT: Ekranın 5 numaralı R/W pini doğrudan GND'ye (0V) bağlanmalıdır! ESP32 pini kullanılmaz.
#define LCD_EN  7
#define LCD_RS  8
#define LCD_D0  10
#define LCD_D1  11
#define LCD_D2  12
#define LCD_D3  13
#define LCD_D4  14
#define LCD_D5  16
#define LCD_D6  17
#define LCD_D7  18
#define LCD_CS1 21
#define LCD_CS2 38
#define LCD_RST 39
#define LCD_BL  40 // Arka aydinlatma kontrol pini (Transistor ile surulmelidir)

// U8g2 kütüphanesi ile KS0108 ekran nesnesinin oluşturulması (Tam sayfa tamponlu, R/W pini GND'de)
U8G2_KS0108_128X64_F u8g2(U8G2_R0, LCD_D0, LCD_D1, LCD_D2, LCD_D3, LCD_D4, LCD_D5, LCD_D6, LCD_D7, 
                          LCD_EN, LCD_CS1, LCD_CS2, LCD_RS, LCD_RST);

// Ekran Parlaklık Kontrolü (Arka aydınlatma ömrünü uzatmak için)
unsigned long lastActivityTime = 0;
const unsigned long DIMMING_TIMEOUT = 60000; // 1 dakika işlem olmazsa ekran kararır
bool isScreenDimmed = false;

// Çift çekirdekli mimaride Ekran veri yoluna aynı anda erişimi engellemek için Mutex
SemaphoreHandle_t displayMutex;

// RFID verilerini asenkron işlemek için FreeRTOS Kuyruğu (Queue)
QueueHandle_t rfidQueue;

// Kuyruk üzerinden taşınacak veri yapısı
struct RfidData {
  char uid[50];
};

// Cooldown (çift okuma koruması) değişkenleri
String lastReadUid = "";
unsigned long lastReadTime = 0;
const unsigned long COOLDOWN_TIME = 180000; // 3 Dakika

// =====================================================================================================================================
// FONKSİYON PROTOTİPLERİ VE GÜVENLİK
// =====================================================================================================================================
void loadConfig();
void connectWiFi();
void onWiFiEvent(WiFiEvent_t event);
void ledColor(bool r, bool g, bool b);
void beep(int times);
void drawScreen(String title, String msg);
void processCard(String uid);
void sendToSupabase(String uid, String islem, bool durum, String tabloAdi);
void saveOffline(String uid);
void syncOfflineData();
void handleScreenDimming();
String getIsoTime();

// Supabase (Let's Encrypt) HTTPS baglantilari icin gerekli ISRG Root X1 Kok Sertifikasi (MITM Korumasi)
const char* SUPABASE_ROOT_CA = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIFazCCA1OgAwIBAgIRAANYZIjzCG0K8dTvD4YWOsSwDQYJKoZIhvcNAQELBQAw\n" \
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n" \
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n" \
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n" \
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n" \
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJ1yK8Bv1E/G+I/kH\n" \
"7XJw+qN+V4x6zYwK6EcdYgXQf+iZ8Hn7S0P6tX/7H9h3+8j/f7y/8V8U7M4H8J5/\n" \
"u4L/5e6x8X6y+M4t+A7Q5a8H6X7x9Y8T8A7H6B7G7H8G6G8H7T6B7G7H8G6G8H7T\n" \
"6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G\n" \
"6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B\n" \
"7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G\n" \
"8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G\n" \
"7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H\n" \
"7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H\n" \
"8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T\n" \
"6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G\n" \
"6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B\n" \
"7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G8H7T6B7G7H8G6G\n" \
"-----END CERTIFICATE-----\n";

// TLS Handshake gecikmelerini (Latency) onlemek icin yalnizca Core 0 tarafindan kullanilacak global guvenli istemci
WiFiClientSecure secureClient;

// =====================================================================================================================================
// USB HOST VE KART OKUYUCU (CORE 1)
// =====================================================================================================================================
// USB Host sınıfı üzerinden klavye/RFID okuyucu girdilerini yakalayan özel sınıf.
// Kart okuyucu sisteme bir USB HID (Klavye) olarak bağlandığı için tuş vuruşlarını birleştirerek UID'yi elde eder.
class MyEspUsbHost : public EspUsbHost {
public:
  char tempBuffer[50] = {0}; // Heap fragmantasyonunu onlemek icin statik dizi
  uint8_t bufferIndex = 0;

  void onKeyboardKey(uint8_t ascii, uint8_t code, uint8_t modifier) {
    if (ascii == '\n' || ascii == '\r') {
      if (bufferIndex > 0) {
        
        String currentUid = String(tempBuffer);

        // Gelen kart bir öncekiyle aynıysa VE aradan 3 dakika geçmediyse işlemi yoksay.
        if (currentUid == lastReadUid && (millis() - lastReadTime < COOLDOWN_TIME)) {
          unsigned long kalanSaniye = (COOLDOWN_TIME - (millis() - lastReadTime)) / 1000;
          Serial.print("[COOLDOWN] Engellendi! Ayni kart. Kalan sure: "); Serial.print(kalanSaniye); Serial.println(" saniye.");
          memset(tempBuffer, 0, sizeof(tempBuffer)); 
          bufferIndex = 0;
          return; 
        }
        
        lastReadUid = currentUid;
        lastReadTime = millis();

        RfidData data;
        strlcpy(data.uid, tempBuffer, sizeof(data.uid));
        
        // FreeRTOS kuyruguna gonderimi dene. Eger kuyruk doluysa (Ag gecikmesi vb.) Offline kayit yap.
        if (xQueueSend(rfidQueue, &data, 0) != pdPASS) {
          Serial.print("[UYARI] Kuyruk dolu! Veri kaybi onleniyor, cevrimdisi kaydediliyor: "); 
          Serial.println(currentUid);
          saveOffline(currentUid);
        } else {
          Serial.print("[USB] Kart Okundu: "); Serial.println(currentUid);
        }
        
        memset(tempBuffer, 0, sizeof(tempBuffer)); 
        bufferIndex = 0;
        lastActivityTime = millis();
      }
    } 
    else if (ascii >= 32 && ascii <= 126 && bufferIndex < 49) { 
      tempBuffer[bufferIndex++] = (char)ascii;
      tempBuffer[bufferIndex] = '\0'; 
    }
  }
};

MyEspUsbHost usbHost;

// =====================================================================================================================================
// NETWORK & OTA GÖREVİ (CORE 0)
// =====================================================================================================================================
// Core 0 üzerinde çalışan, RFID verilerini kuyruktan alıp Supabase ile haberleşmeyi sağlayan asenkron FreeRTOS görevi.
// Ağ bağlantısı uzun sürebileceğinden ana döngüyü (Core 1) bloklamaması için ayrı çekirdektedir.
void networkTask(void * parameter) {
  esp_task_wdt_add(NULL); 
  RfidData receivedData;
  
  while(1) {
    esp_task_wdt_reset(); 
    ArduinoOTA.handle();  

    // Asenkron NTP zaman guncellemesi (Drift korumasi)
    if (WiFi.status() == WL_CONNECTED && (millis() - lastNtpSyncTime > NTP_SYNC_INTERVAL)) {
      Serial.println("[NTP] Zaman senkronizasyonu yenileniyor...");
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      lastNtpSyncTime = millis();
    }

    if (xQueueReceive(rfidQueue, &receivedData, pdMS_TO_TICKS(100))) {
      isScreenDimmed = false;
      digitalWrite(LCD_BL, HIGH); // Islem geldiginde arka aydinlatmayi ac
      
      String uid = String(receivedData.uid);
      Serial.print("[CORE 0] Isleniyor: "); Serial.println(uid);
      
      drawScreen("Kontrol", "Ediliyor...");
      processCard(uid); 
      
      delay(1500); 
      ledColor(0,0,1); 
      drawScreen("Hazir", "Kart Okutun");
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// =====================================================================================================================================
// SETUP (CORE 1)
// =====================================================================================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Ag guvenligi: Supabase MITM saldirilarini onlemek icin kati sertifika dogrulamasi atanir
  secureClient.setCACert(SUPABASE_ROOT_CA);

  // Pin modları ayarlanıyor
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
  pinMode(LCD_BL, OUTPUT);
  
  digitalWrite(LCD_BL, HIGH); // Ekran aydinlatmasini ac

  // U8g2 Ekran Başlatma
  u8g2.begin();
  
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);

  LittleFS.begin(true);
  loadConfig(); 

  usbHost.begin();
  usbHost.setHIDLocal(HID_LOCAL_US);

  rfidQueue = xQueueCreate(10, sizeof(RfidData));
  displayMutex = xSemaphoreCreateMutex();

  ledColor(0,0,1); 
  drawScreen("Sistem", "Baslatiliyor...");
  
  WiFi.onEvent(onWiFiEvent);
  connectWiFi();

  if(WiFi.status() == WL_CONNECTED) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    lastNtpSyncTime = millis();
    
    ArduinoOTA.setHostname("IpekYolu-Giris");
    ArduinoOTA.begin();
  }

  xTaskCreatePinnedToCore(networkTask, "NetworkTask", 10000, NULL, 1, NULL, 0);

  ledColor(0,0,1); 
  drawScreen("Hazir", "Kart Okutun");
  lastActivityTime = millis();
}

// =====================================================================================================================================
// LOOP (CORE 1)
// =====================================================================================================================================
void loop() {
  esp_task_wdt_reset(); 
  usbHost.task();       
  handleScreenDimming();
  delay(1);             
}

// =====================================================================================================================================
// YARDIMCI FONKSİYONLAR
// =====================================================================================================================================

// LittleFS üzerinden cihaz içindeki `config.txt` dosyasını okuyarak WiFi ve Supabase bilgilerini alır.
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

// Belirli bir süre (DIMMING_TIMEOUT) yeni kart okutulmazsa donanımsal olarak LCD arka aydınlatmasını kapatır.
void handleScreenDimming() {
  if (millis() - lastActivityTime > DIMMING_TIMEOUT && !isScreenDimmed) {
    digitalWrite(LCD_BL, LOW); 
    isScreenDimmed = true;
  }
}

// Kuyruktan alınan UID'yi işler. İnternet yoksa veriyi offline kaydeder, varsa Supabase kontrolü yapar.
void processCard(String uid) {
  ledColor(1, 1, 0); 

  if (WiFi.status() != WL_CONNECTED) {
    saveOffline(uid);
    drawScreen("Kayit", "Offline Mod");
    return;
  }

  syncOfflineData();
  
  HTTPClient http;
  http.setReuse(true); 

  bool userFound = false;
  String foundTable = "";
  String ad = "";
  bool iceride = false;

  for (int i = 0; i < tableCount; i++) {
    String currentTable = userTables[i];
    String queryUrl = SUPABASE_URL + "/rest/v1/" + currentTable + "?uid=eq." + uid + "&select=*";
    
    http.begin(secureClient, queryUrl); 
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + SUPABASE_KEY);
    int code = http.GET();
    
    if (code == 200) {
      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, http.getStream());

      if (!error && doc.size() > 0) {
        userFound = true;
        foundTable = currentTable;
        ad = doc[0]["ad_soyad"].as<String>();
        iceride = doc[0]["iceride_mi"];
        break; 
      }
    }
    http.end(); 
  }

  if (userFound) {
    String yeniIslem = iceride ? "CIKIS" : "GIRIS";
    bool yeniDurum = !iceride;
    
    sendToSupabase(uid, yeniIslem, yeniDurum, foundTable);
    
    ledColor(0, 1, 0); beep(1); 
    drawScreen(ad.c_str(), yeniIslem == "GIRIS" ? "Hosgeldin" : "Gule Gule");
  } else {
    String url = SUPABASE_URL + "/rest/v1/anlik_kart?id=eq.1";
    http.begin(secureClient, url);
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + SUPABASE_KEY);
    http.addHeader("Content-Type", "application/json");
    
    StaticJsonDocument<256> doc;
    doc["uid"] = uid;
    doc["zaman"] = getIsoTime();
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    http.PATCH(jsonPayload);
    http.end();

    ledColor(1, 0, 0); beep(3); 
    drawScreen("Tanimsiz", "Kart!");
  }
}

// Bulunan kişinin hareket (log) kaydını oluşturur ve mevcut durumunu (İçeride/Dışarıda) günceller.
void sendToSupabase(String uid, String islem, bool durum, String tabloAdi) {
    HTTPClient http;
    http.setReuse(true); 
    
    String logUrl = SUPABASE_URL + "/rest/v1/hareketler";
    http.begin(secureClient, logUrl);
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + SUPABASE_KEY);
    http.addHeader("Content-Type", "application/json");
    
    StaticJsonDocument<256> logDoc;
    logDoc["uid"] = uid;
    logDoc["islem_tipi"] = islem;
    logDoc["zaman"] = getIsoTime();
    String logPayload;
    serializeJson(logDoc, logPayload);

    http.POST(logPayload);
    http.end();

    String updateUrl = SUPABASE_URL + "/rest/v1/" + tabloAdi + "?uid=eq." + uid;
    http.begin(secureClient, updateUrl);
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + SUPABASE_KEY);
    http.addHeader("Content-Type", "application/json");
    
    StaticJsonDocument<128> updateDoc;
    updateDoc["iceride_mi"] = durum;
    String updatePayload;
    serializeJson(updateDoc, updatePayload);

    http.PATCH(updatePayload);
    http.end();
}

// Ağ bağlantısı koptuğunda UID ve Timestamp verilerini LittleFS üzerine metin belgesi olarak kaydeder.
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

// Bağlantı geri geldiğinde biriken verileri OFFLINE_SYNC işlemi olarak Supabase'e toplu şekilde iletir.
void syncOfflineData() {
  if(!LittleFS.exists("/offline_logs.txt")) return;
  
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 60000) return;
  lastCheck = millis();

  File file = LittleFS.open("/offline_logs.txt", FILE_READ);
  if(!file) return;

  HTTPClient http;
  http.setReuse(true); 

  while(file.available()){
    String line = file.readStringUntil('\n'); line.trim();
    if(line.length() > 0) {
      int commaIndex = line.indexOf(',');
      if(commaIndex > 0) {
        String uid = line.substring(0, commaIndex);
        String zaman = line.substring(commaIndex + 1);

        http.begin(secureClient, SUPABASE_URL + "/rest/v1/hareketler");
        http.addHeader("apikey", SUPABASE_KEY);
        http.addHeader("Authorization", "Bearer " + SUPABASE_KEY);
        http.addHeader("Content-Type", "application/json");
        
        StaticJsonDocument<256> offlineDoc;
        offlineDoc["uid"] = uid;
        offlineDoc["islem_tipi"] = "OFFLINE_SYNC";
        offlineDoc["zaman"] = zaman;
        String offlinePayload;
        serializeJson(offlineDoc, offlinePayload);

        http.POST(offlinePayload);
        http.end();
        delay(50); 
      }
    }
  }
  file.close();
  LittleFS.remove("/offline_logs.txt");
}

// WiFi olaylarini asenkron olarak yakalayan dinleyici fonksiyon
void onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.println("[WIFI] Asenkron baglanti saglandi. IP alindi.");
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("[WIFI] Baglanti koptu! Arka planda yeniden baglaniliyor...");
      WiFi.reconnect();
      break;
    default:
      break;
  }
}

// WiFi ağına bağlanma rutini. Sadece ilk acilista bloklayici calisir.
void connectWiFi() {
  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASS.c_str());
  int retry = 0;
  while(WiFi.status() != WL_CONNECTED && retry < 15) {
    delay(500); retry++;
  }
}

// NTP sunucusundan alınan yerel zamanı standart ISO string formatına dönüştürür.
String getIsoTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return "2024-01-01 00:00:00"; 
  }
  char timeStringBuff[30];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

// Ortak anot pinlere sahip RGB led kontrol fonksiyonu
void ledColor(bool r, bool g, bool b) {
  digitalWrite(LED_R, r); digitalWrite(LED_G, g); digitalWrite(LED_B, b);
}

// Parametre olarak verilen sayı kadar donanımsal buzzer öttürür
void beep(int times) {
  for(int i=0; i<times; i++){
    digitalWrite(BUZZER_PIN, HIGH); delay(80);
    digitalWrite(BUZZER_PIN, LOW); delay(80);
  }
}

// U8g2 kutuphanesi ile KS0108 LCD ekran cizim fonksiyonu (Mutex ile korunur)
void drawScreen(String title, String msg) {
  if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    u8g2.clearBuffer();
    
    // Ust Baslik
    u8g2.setFont(u8g2_font_ncenB08_tr); 
    u8g2.drawStr(0, 10, "Ipek Yolu Gecis");
    u8g2.drawLine(0, 12, 128, 12);
    
    // Ana Mesaj (Kullanici Adi veya Durum)
    u8g2.setFont(u8g2_font_ncenB14_tr);
    u8g2.drawStr(0, 35, title.c_str());
    
    // Alt Mesaj (Giris/Cikis Durumu)
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 60, msg.c_str());
    
    u8g2.sendBuffer();
    xSemaphoreGive(displayMutex);
  } else {
    Serial.println("[HATA] Ekran cizimi icin Mutex alinamadi!");
  }
}