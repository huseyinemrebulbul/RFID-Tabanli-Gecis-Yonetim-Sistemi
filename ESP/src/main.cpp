#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h> 
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "FS.h"
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include "time.h"
#include "EspUsbHost.h" 
#include <ArduinoOTA.h>

// =============================================================
// AYARLAR VE GLOBAL DEĞİŞKENLER
// =============================================================
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

// Sistem kilitlenmelerine karşı Watchdog Timer süresi (saniye)
#define WDT_TIMEOUT 60 

// Donanım Pin Tanımlamaları
#define OLED_SDA 8
#define OLED_SCL 9
#define BUZZER_PIN 15 
#define LED_R 4       
#define LED_G 5       
#define LED_B 6       

// OLED Ekran Konfigürasyonu
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Ekran Parlaklık Kontrolü (OLED ömrünü uzatmak için)
unsigned long lastActivityTime = 0;
const unsigned long DIMMING_TIMEOUT = 60000; // 1 dakika işlem olmazsa ekran kararır
bool isScreenDimmed = false;

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

/**
 * USB Host sınıfı üzerinden klavye/RFID okuyucu girdilerini yakalayan özel sınıf.
 * Kart okuyucu sisteme bir USB HID (Klavye) olarak bağlandığı için 
 * tuş vuruşlarını (keystrokes) birleştirerek UID'yi elde eder.
 */
class MyEspUsbHost : public EspUsbHost {
public:
  String tempBuffer = "";
  void onKeyboardKey(uint8_t ascii, uint8_t code, uint8_t modifier) {
    // Okuma işlemi tamamlandığında (Enter karakteri geldiğinde)
    if (ascii == '\n' || ascii == '\r') {
      if (tempBuffer.length() > 0) {
        
        // --- ÇİFT OKUMA KORUMASI BAŞLANGICI ---
        // Gelen kart bir öncekiyle aynıysa VE aradan 3 dakika geçmediyse işlemi yoksay.
        if (tempBuffer == lastReadUid && (millis() - lastReadTime < COOLDOWN_TIME)) {
          unsigned long kalanSaniye = (COOLDOWN_TIME - (millis() - lastReadTime)) / 1000;
          Serial.print("[COOLDOWN] Engellendi! Ayni kart. Kalan sure: ");
          Serial.print(kalanSaniye);
          Serial.println(" saniye.");
          
          tempBuffer = ""; // Tamponu boşalt ve işlemi doğrudan iptal et
          return; 
        }
        
        // Kart FARKLIYSA veya aynı kart ama 3 DAKİKA DOLDUYSA: Hafızayı güncelle
        lastReadUid = tempBuffer;
        lastReadTime = millis();
        // --- ÇİFT OKUMA KORUMASI BİTİŞİ ---

        // Okunan UID'yi struct'a paketle ve FreeRTOS kuyruğuna gönder (Core 0'da işlenmesi için)
        RfidData data;
        tempBuffer.toCharArray(data.uid, 50);
        xQueueSend(rfidQueue, &data, 0); 
        Serial.print("[USB] Kart Okundu: "); Serial.println(tempBuffer);
        
        tempBuffer = ""; 
        lastActivityTime = millis();
      }
    } 
    // Gelen karakter yazdırılabilir bir ASCII karakteriyse tampona ekle
    else if (ascii >= 32 && ascii <= 126) { 
      tempBuffer += (char)ascii;
    }
  }
};

MyEspUsbHost usbHost;

// Fonksiyon Prototipleri
void loadConfig();
void connectWiFi();
void ledColor(bool r, bool g, bool b);
void beep(int times);
void drawScreen(String title, String msg);
void processCard(String uid);
void sendToSupabase(String uid, String islem, bool durum, String tabloAdi);
void saveOffline(String uid);
void syncOfflineData();
void handleScreenDimming();
String getIsoTime();

// =============================================================
// NETWORK & OTA GÖREVİ (CORE 0)
// =============================================================
/**
 * Core 0 üzerinde çalışan, RFID verilerini kuyruktan alıp Supabase (PostgreSQL) 
 * ile haberleşmeyi sağlayan asenkron FreeRTOS görevi.
 * Ağ bağlantısı uzun sürebileceğinden ana döngüyü (Core 1) bloklamaması için ayrı çekirdektedir.
 */
void networkTask(void * parameter) {
  esp_task_wdt_add(NULL); // Bu görevi Watchdog'a kaydet
  RfidData receivedData;
  
  while(1) {
    esp_task_wdt_reset(); // Görevin kilitlenmediğini WDT'ye bildir
    ArduinoOTA.handle();  // Kablosuz yazılım güncelleme (Over-The-Air) dinleyicisi

    // Kuyrukta işlenmeyi bekleyen RFID verisi var mı kontrol et (Maks 100ms bekle)
    if (xQueueReceive(rfidQueue, &receivedData, pdMS_TO_TICKS(100))) {
      isScreenDimmed = false;
      display.dim(false); // İşlem geldiğinde ekran parlaklığını geri aç
      
      String uid = String(receivedData.uid);
      Serial.print("[CORE 0] Isleniyor: "); Serial.println(uid);
      
      drawScreen("Kontrol", "Ediliyor...");
      processCard(uid); // Kartı işle (Veritabanı veya Offline kayıt)
      
      delay(1500); // Sonucun ekranda okunabilmesi için kısa bir bekleme
      ledColor(0,0,1); // Mavi (Hazır durumu)
      drawScreen("Hazir", "Kart Okutun");
    }
    // Görevin sürekli CPU'yu meşgul etmesini engellemek için küçük bir gecikme
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// =============================================================
// SETUP (CORE 1)
// =============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Pin modları ayarlanıyor
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);

  // OLED Ekran başlatma (I2C)
  Wire.begin(OLED_SDA, OLED_SCL); 
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  
  // WDT (Watchdog Timer) Başlat (Sistem 60 saniye tepki vermezse resetlenir)
  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);

  // Dosya sistemini başlat ve ayarları oku
  LittleFS.begin(true);
  loadConfig(); 

  // USB Host (HID okuyucu) başlat
  usbHost.begin();
  usbHost.setHIDLocal(HID_LOCAL_US);

  // 10 elemanlı RFID işlem kuyruğu oluşturuluyor
  rfidQueue = xQueueCreate(10, sizeof(RfidData));

  ledColor(0,0,1); 
  drawScreen("Sistem", "Baslatiliyor...");
  
  connectWiFi();

  if(WiFi.status() == WL_CONNECTED) {
    // WiFi bağlandıysa zamanı NTP sunucularından güncelle
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    // OTA (Over The Air) Ayarları
    ArduinoOTA.setHostname("IpekYolu-Giris");
    ArduinoOTA.begin();
  }

  // İş yükünü bölmek için ağ görevini Core 0'da başlat
  xTaskCreatePinnedToCore(networkTask, "NetworkTask", 10000, NULL, 1, NULL, 0);

  ledColor(0,0,1); 
  drawScreen("Hazir", "Kart Okutun");
  lastActivityTime = millis();
}

// =============================================================
// LOOP (CORE 1)
// =============================================================
void loop() {
  esp_task_wdt_reset(); // WDT Reset (Core 1 sağlıklı)
  usbHost.task();       // USB okuyucuyu sürekli dinle
  handleScreenDimming();// Hareketsizlik durumunda ekranı karart
  delay(1);             // RTOS idle görevlerine zaman tanı
}

// =============================================================
// YARDIMCI FONKSİYONLAR
// =============================================================

/**
 * LittleFS üzerinden cihaz içindeki `config.txt` dosyasını okuyarak 
 * WiFi ve Supabase bilgilerini global değişkenlere atar.
 */
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

/**
 * Belirli bir süre (DIMMING_TIMEOUT) yeni kart okutulmazsa 
 * donanımsal olarak OLED panelin parlaklığını kısar.
 */
void handleScreenDimming() {
  if (millis() - lastActivityTime > DIMMING_TIMEOUT && !isScreenDimmed) {
    display.dim(true); 
    isScreenDimmed = true;
  }
}

/**
 * Kuyruktan alınan UID'yi işler. 
 * İnternet yoksa veriyi offline kaydeder, varsa Supabase üzerinden kişinin varlığını kontrol eder.
 */
void processCard(String uid) {
  ledColor(1, 1, 0); // İşlem süresince Sarı LED yanar

  // 1. Çevrimdışı Mod Kontrolü
  if (WiFi.status() != WL_CONNECTED) {
    saveOffline(uid);
    drawScreen("Kayit", "Offline Mod");
    return;
  }

  // WiFi bağlıysa önce bekleyen offline kayıtları veritabanına gönder
  syncOfflineData();
  
  WiFiClientSecure client;
  client.setInsecure(); // Supabase SSL sertifikasını manuel doğrulamayı atla
  HTTPClient http;

  bool userFound = false;
  String foundTable = "";
  String ad = "";
  bool iceride = false;

  // 2. Kullanıcıyı Veritabanında Ara ("personel" ve "Gonullu" tablolarında)
  for (int i = 0; i < tableCount; i++) {
    String currentTable = userTables[i];
    String queryUrl = SUPABASE_URL + "/rest/v1/" + currentTable + "?uid=eq." + uid + "&select=*";
    
    http.begin(client, queryUrl); 
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + SUPABASE_KEY);
    int code = http.GET();
    
    if (code == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload);

      // JSON dizisinde eleman varsa, kullanıcı bu tabloda bulunmuştur
      if (doc.size() > 0) {
        userFound = true;
        foundTable = currentTable;
        ad = doc[0]["ad_soyad"].as<String>();
        iceride = doc[0]["iceride_mi"];
        break; // Diğer tabloyu aramaya gerek yok, döngüden çık
      }
    }
    http.end(); 
  }

  // 3. Duruma Göre İşlem Yap
  if (userFound) {
    // Kullanıcı bulundu: İçerideyken okutursa ÇIKIŞ, dışarıdayken okutursa GİRİŞ yap
    String yeniIslem = iceride ? "CIKIS" : "GIRIS";
    bool yeniDurum = !iceride;
    
    sendToSupabase(uid, yeniIslem, yeniDurum, foundTable);
    
    ledColor(0, 1, 0); beep(1); // Başarılı: Yeşil LED ve 1 kısa bip
    drawScreen(ad.c_str(), yeniIslem == "GIRIS" ? "Hosgeldin" : "Gule Gule");
  } else {
    // Kullanıcı bulunamadı: Sistem yöneticisinin masaüstü uygulamasından 
    // görebilmesi için UID'yi "anlik_kart" tablosuna kaydet
    String url = SUPABASE_URL + "/rest/v1/anlik_kart?id=eq.1";
    http.begin(client, url);
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + SUPABASE_KEY);
    http.addHeader("Content-Type", "application/json");
    http.PATCH("{\"uid\": \"" + uid + "\", \"zaman\": \"" + getIsoTime() + "\"}");
    http.end();

    ledColor(1, 0, 0); beep(3); // Hata: Kırmızı LED ve 3 kısa bip
    drawScreen("Tanimsiz", "Kart!");
  }
}

/**
 * Bulunan kişinin hareket (log) kaydını oluşturur ve mevcut durumunu (İçeride/Dışarıda) günceller.
 */
void sendToSupabase(String uid, String islem, bool durum, String tabloAdi) {
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    
    // 1. Hareket Logu Ekle (POST)
    String logUrl = SUPABASE_URL + "/rest/v1/hareketler";
    http.begin(client, logUrl);
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + SUPABASE_KEY);
    http.addHeader("Content-Type", "application/json");
    http.POST("{\"uid\": \"" + uid + "\", \"islem_tipi\": \"" + islem + "\", \"zaman\": \"" + getIsoTime() + "\"}");
    http.end();

    // 2. Kullanıcının Mevcut Durumunu Güncelle (PATCH)
    String updateUrl = SUPABASE_URL + "/rest/v1/" + tabloAdi + "?uid=eq." + uid;
    http.begin(client, updateUrl);
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + SUPABASE_KEY);
    http.addHeader("Content-Type", "application/json");
    http.PATCH("{\"iceride_mi\": " + String(durum ? "true" : "false") + "}");
    http.end();
}

/**
 * Ağ bağlantısı koptuğunda UID ve Timestamp verilerini LittleFS üzerine metin belgesi olarak kaydeder.
 * Dosya boyutu 50KB'ı geçerse log rotasyonu yaparak çökme ihtimalini ortadan kaldırır.
 */
void saveOffline(String uid) {
  File checkFile = LittleFS.open("/offline_logs.txt", FILE_READ);
  if (checkFile) {
    if (checkFile.size() > 50000) { // Log Rotasyonu: 50KB üstündeyse eski dosyayı yedekle
      checkFile.close();
      LittleFS.remove("/offline_logs_old.txt");
      LittleFS.rename("/offline_logs.txt", "/offline_logs_old.txt");
    } else {
      checkFile.close();
    }
  }

  // Yeni kaydı dosyanın sonuna ekle (APPEND)
  File file = LittleFS.open("/offline_logs.txt", FILE_APPEND);
  if(file){
    file.println(uid + "," + getIsoTime());
    file.close();
  }
}

/**
 * Bağlantı geri geldiğinde `offline_logs.txt` dosyasını satır satır okuyarak
 * biriken verileri `OFFLINE_SYNC` işlemi olarak Supabase'e toplu şekilde iletir.
 */
void syncOfflineData() {
  if(!LittleFS.exists("/offline_logs.txt")) return;
  
  // Senkronizasyon işleminin çok sık tetiklenmesini engelle (En az 60 sn bekle)
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

        // Çevrimdışı okunan kaydı Supabase'e gönder
        http.begin(client, SUPABASE_URL + "/rest/v1/hareketler");
        http.addHeader("apikey", SUPABASE_KEY);
        http.addHeader("Authorization", "Bearer " + SUPABASE_KEY);
        http.addHeader("Content-Type", "application/json");
        http.POST("{\"uid\": \"" + uid + "\", \"islem_tipi\": \"OFFLINE_SYNC\", \"zaman\": \"" + zaman + "\"}");
        http.end();
        delay(50); // API limitlerine takılmamak için hafif bir gecikme
      }
    }
  }
  file.close();
  // Başarılı senkronizasyon sonrası dosyayı sil
  LittleFS.remove("/offline_logs.txt");
}

/**
 * WiFi ağına bağlanma rutini. 15 denemeden sonra bırakır.
 */
void connectWiFi() {
  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASS.c_str());
  int retry = 0;
  while(WiFi.status() != WL_CONNECTED && retry < 15) {
    delay(500); retry++;
  }
}

/**
 * NTP sunucusundan alınan yerel zamanı standart ISO string formatına (YYYY-MM-DD HH:MM:SS) dönüştürür.
 * Veritabanı ile senkronizasyon için kritik öneme sahiptir.
 */
String getIsoTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    // NTP henüz saat çekemediyse varsayılan bir tarih döndür (Hata loglarında kolay tespit edilir)
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

// OLED ekran çizim fonksiyonu (Başlık ve Alt mesaj formatında)
void drawScreen(String title, String msg) {
  display.clearDisplay();
  display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0); display.println("Ipek Yolu Gecis");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
  display.setTextSize(2); display.setCursor(0, 25); display.println(title);
  display.setTextSize(1); display.setCursor(0, 50); display.println(msg);
  display.display();
}