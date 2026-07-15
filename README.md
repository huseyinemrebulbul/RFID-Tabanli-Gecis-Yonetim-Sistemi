# 🌐 İpek Yolu Geçiş Yönetim Sistemi (GYS)

**Endüstriyel IoT & Otonom Geçiş Kontrol Platformu**

![ESP32-S3](https://img.shields.io/badge/Hardware-ESP32--S3%20Master-blue?style=for-the-badge&logo=espressif)
![ESP8266](https://img.shields.io/badge/Display-ESP8266%20Slave-deepskyblue?style=for-the-badge&logo=espressif)
![PlatformIO](https://img.shields.io/badge/Firmware-PlatformIO%20%7C%20C%2B%2B-orange?style=for-the-badge&logo=platformio)
![AvaloniaUI](https://img.shields.io/badge/GUI-Avalonia%20UI%20%7C%20.NET%209.0-purple?style=for-the-badge&logo=dotnet)
![Supabase](https://img.shields.io/badge/Backend-Supabase%20REST-green?style=for-the-badge&logo=supabase)

**İpek Yolu Uluslararası Çocuk ve Gençlik Merkezi** için özel olarak mimarisi tasarlanmış; çift MCU (Master-Slave) donanım altyapısı, gerçek zamanlı işletim sistemi (FreeRTOS) izolasyonu ve cross-platform masaüstü yazılımından oluşan, yüksek dayanıklılığa sahip **Otonom RFID Geçiş ve Personel Takip Ekosistemidir.**

Sistem, sahadaki ağ kopmalarına, siber saldırılara, bellek fragmantasyonuna ve donanım arızalarına karşı **endüstriyel seviyede (Failsafe & Wear-Leveling)** koruma mekanizmalarıyla donatılmıştır.

---

## 🌟 Anahtar Özellikler ve Mühendislik Yenilikleri

### 🛡️ 1. Sıfır Fragmantasyon & Statik Bellek Mimarisi (Zero-String Architecture)

Gömülü sistemlerin en büyük çökme sebebi olan RAM parçalanmasını (Heap Fragmentation) engellemek adına kod tabanında dinamik `String` nesneleri tamamen yok edilmiştir:

- **100% C-String Tamponlar:** Tüm JSON paketlemeleri, URL üretimleri ve dosya okumaları sabit bayt dizileri (`char buffer[]`, `snprintf`) ile yönetilir.
- **Stream-Over-RAM JSON İşleme:** Supabase'den gelen devasa HTTP yanıtları belleğe alınmadan doğrudan ağ akışı (Stream) üzerinden ArduinoJson ile anlık ayrıştırılır.

### 🔋 2. Flash Ömrü Koruması & Otonom Çevrimdışı Mod (Wear-Leveling Batch Write)

İnternet veya sunucu bağlantısı koptuğunda sistem milisaniyeler içinde **Otonom Çevrimdışı Modu** devreye sokar ve geçişleri asla durdurmaz:

- **RAM Tamponlu Yazma:** Okutulan kartlar anlık olarak diske yazılmaz; önce RAM'deki 15 kapasiteli statik blokta biriktirilir.
- **Toplu Yazma (Batching):** Tampon dolduğunda veya periyodik zamanlayıcı tetiklendiğinde veriler tek blok halinde **LittleFS** flash belleğe indirilir. Bu sayede fiziksel çipin yazma ömrü (Write Cycle) 10 kat uzatılır ve log rotasyonu ile sınırsız çevrimdışı çalışma sağlanır.

### ⚡ 3. Gerçek Zamanlı Çift Çekirdek İzolasyonu (Dual-Core FreeRTOS)

İşlemci yükü ve görevler iki çekirdek arasında tamamen izole edilmiştir:

- **Core 1 (Donanım & Zaman Kritik Katman):** USB HID RFID okuma, kart sıçrama engelleme (debouncing), Master-Slave UART ekran haberleşmesi ve LED/Buzzer uyarıları.
- **Core 0 (Asenkron Ağ Katmanı):** Supabase REST API haberleşmesi, çevrimdışı log senkronizasyonu ve kablosuz yazılım güncelleme (OTA). Ağ gecikmeleri asla Core 1'i ve kart okuma hızını bloke etmez.

### 🔐 4. Endüstriyel Ağ Güvenliği & Failsafe Koruma

- **Gerçek TLS Doğrulaması:** Let's Encrypt / Supabase sunucuları için `ISRG Root X1` kök sertifikası statik olarak gömülmüştür (`setInsecure` kullanılmaz, MitM saldırıları engellenir).
- **Güvenlik Duvarı (Firewall) Algılama:** Kurumsal ağlarda FortiGate vb. sistemlerin engelleme (Captive Portal / HTML) sayfaları JSON doğrulaması ile tespit edilir ve sistem sahte onay vermeyerek kendini arıza güvenliği (Failsafe) moduna alır.
- **Donanımsal ve Yazılımsal Watchdog (TWDT):** 60 saniyelik görev izleyicileri, olası kilitlenmelerde sistemi otonom olarak kurtarır.

### 🛑 5. Akıllı Sıçrama & Çift Okuma Koruması (Debounce & Cooldown)

- **Donanımsal Debounce (500ms):** Okuyucu titremelerinden doğan çoklu enter sinyalleri donanım katmanında süzülür.
- **Yazılımsal Cooldown (3 Dakika):** Dairesel tampon (Circular Buffer) mantığıyla çalışan 10 geçmiş kayıt hafızası, aynı kartın yanlışlıkla ardışık okutulmasını engeller.

---

## 🏗️ Sistem Mimarisi ve Teknoloji Yığıtları

Sistem; Uç Bilişim (Edge Computing), Masaüstü Yönetimi ve Bulut Veritabanı olmak üzere 3 ana katmandan oluşur:

```text
+-------------------------------------------------------+
|                GÖMÜLÜ DONANIM KATMANI                 |
|                                                       |
|  [USB HID RFID] ---> (Core 1) ESP32-S3 Master        |
|                         |    (FreeRTOS Queue)         |
|  [LittleFS RAM] <-------+---> (Core 0) Asenkron Ağ    |
|  [LED & Buzzer]         |               |             |
|                         | (UART TX/RX)  | (TLS / X1)  |
|                         v               v             |
|                ESP8266 Slave Display    |             |
+-----------------------------------------|-------------+
                                          |
                                          v
+-------------------------------------------------------+
|             BULUT & YÖNETİM KATMANI                   |
|                                                       |
|    Supabase REST API (PostgreSQL Veritabanı)         |
|                         ^                             |
|                         | (DTO & Async REST)          |
|                         v                             |
|    Avalonia UI (.NET 9.0) Cross-Platform Masaüstü     |
+-------------------------------------------------------+
```
