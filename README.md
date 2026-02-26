İpek Yolu Geçiş Yönetim Sistemi (RFID Tabanlı)
İpek Yolu Uluslararası Çocuk ve Gençlik Merkezi için geliştirilmiş, donanım (ESP32-S3) ve masaüstü yazılım (Avalonia UI) bileşenlerinden oluşan, çevrimdışı (offline) çalışabilme yeteneğine sahip kapsamlı bir RFID geçiş ve personel/gönüllü takip sistemidir.

🌟 Öne Çıkan Özellikler
Kesintisiz Çalışma (Offline Mode): İnternet veya Supabase bağlantısı koptuğunda sistem durmaz. Okutulan kartlar ESP32 üzerindeki LittleFS dosya sistemine zaman damgasıyla kaydedilir ve bağlantı sağlandığında otomatik olarak veritabanına senkronize edilir (Log Rotasyonu destekli).

Asenkron FreeRTOS Mimarisi: Ağ işlemleri (Supabase HTTP istekleri) ve donanım kesmeleri (USB HID okuma) farklı çekirdeklere (Core 0 ve Core 1) bölünerek sistemin kilitlenmesi veya kart okumalarını kaçırması engellenmiştir.

Akıllı Çift Okuma Koruması (Cooldown): Aynı kartın arka arkaya yanlışlıkla okutulmasını engellemek için 3 dakikalık yazılımsal filtreleme mekanizması mevcuttur.

Cross-Platform Yönetim Paneli: Yöneticiler için C# ve Avalonia UI ile geliştirilmiş; Windows, macOS ve Linux üzerinde çalışabilen masaüstü arayüzü.

Gelişmiş Raporlama: Masaüstü uygulaması üzerinden personelin giriş-çıkış hareketleri analiz edilerek ISO standartlarında Haftalık Çalışma Saati raporları otomatik oluşturulur.

OTA (Over-The-Air) Güncelleme: Cihazı bilgisayara bağlamadan uzaktan kablosuz yazılım güncelleme desteği.

🏗️ Sistem Mimarisi ve Kullanılan Teknolojiler
Sistem, uç cihaz (Edge Device) ve yönetim paneli olmak üzere iki ana birimden oluşur ve gerçek zamanlı veritabanı ile haberleşir.

1. Gömülü Sistem (Donanım)

Mikrodenetleyici: ESP32-S3 DevKitC-1

Geliştirme Ortamı: PlatformIO (C++) / Arduino Framework

Çevre Birimleri: \* USB HID RFID Okuyucu (EspUsbHost kütüphanesi ile)

128x64 I2C SSD1306 OLED Ekran

RGB LED ve Aktif Buzzer (Görsel ve işitsel bildirim)

Kritik Kütüphaneler: ArduinoJson, LittleFS, FreeRTOS

2. Masaüstü Arayüzü (GUI)

Framework: Avalonia UI (C# / .NET 9.0)

Mimari: Data Transfer Object (DTO) destekli, asenkron yapılı UI tasarımı.

Özellikler: Gerçek zamanlı log izleme, personel/gönüllü CRUD işlemleri, manuel giriş/çıkış müdahalesi, izin yönetimi.

3. Veritabanı (Backend)

Servis: Supabase (PostgreSQL tabanlı REST API)

Tablolar: personel, Gonullu, hareketler, izinler, anlik_kart
