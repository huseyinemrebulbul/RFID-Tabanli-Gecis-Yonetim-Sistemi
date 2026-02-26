📘 İpek Yolu Geçiş Yönetim Sistemi - Teknik Dokümantasyon

## 1. Proje Özeti
Bu sistem, bir kurumdaki (Personel ve Gönüllü) giriş-çıkış hareketlerini RFID kartlar aracılığıyla takip eden, verileri bulutta (Supabase) saklayan, internet kesintisinde offline çalışabilen ve gelişmiş bir masaüstü yazılımı ile yönetilen hibrit bir IoT çözümüdür.

## 2. Veritabanı Yapısı (Supabase / PostgreSQL)
Sistem 4 ana tablo ve 1 otomatik zamanlayıcı (Cron Job) üzerine kuruludur.

### A. Tablolar
1. **`personel`**: Kadrolu çalışanların tutulduğu tablo.
* `uid` (Kart ID), `ad_soyad`, `TC`, `iceride_mi` (Boolean), `created_at`.

2. **`"Gonullu"`**: (Dikkat: Çift tırnaklı ve Büyük Harf). Gönüllü çalışanların tutulduğu tablo.
* `uid`, `ad_soyad`, `TC`, `iceride_mi`, `created_at`.

3. **`hareketler`**: Tüm giriş-çıkış loglarının tutulduğu arşiv.
* `uid`, `islem_tipi` (GIRIS/CIKIS/OFFLINE_SYNC), `zaman` (Timestamp).

4. **`anlik_kart`**: Tanımsız bir kart okutulduğunda ID'sinin düştüğü geçici hafıza. Masaüstü uygulaması "Kartı Çek" dediğinde buradan okur.
* `id` (Sabit 1), `uid`, `zaman`.

### B. Otomasyon
`pg_cron` eklentisi.
Her gece saat **23:59**'da çalışır. İçeride unutan personeli tespit eder, otomatik "CIKIS" logu ekler ve durumlarını "Dışarıda" olarak günceller.

## 3. Gömülü Sistem (ESP32 - Donanım & Yazılım)
Cihaz, RFID okuyucuyu USB üzerinden (HID Modu) okur ve WiFi ile sunucuya bağlanır.

### A. Donanım Bağlantı Şeması (Pinout)
| Bileşen | ESP32 Pini | Açıklama |
| **OLED Ekran (SDA)** | GPIO 8 | Ekran Veri Hattı |
| **OLED Ekran (SCL)** | GPIO 9 | Ekran Saat Hattı |
| **Buzzer** | GPIO 15 | Sesli Uyarı |
| **RGB LED (Kırmızı)** | GPIO 4 | Hata / Çıkış Durumu |
| **RGB LED (Yeşil)** | GPIO 5 | Başarılı / Giriş Durumu |
| **RGB LED (Mavi)** | GPIO 6 | Bekleme / İşlem Durumu |
| **RFID Okuyucu** | USB Port (D+/D-) | `EspUsbHost` kütüphanesi ile USB üzerinden okuma |

### B. Yazılım Özellikleri (Firmware)

Dual Core (Çift Çekirdek) Mimari:
Core 1: USB'den kart okuma ve ekran çizimi.
Core 0: WiFi iletişimi ve Supabase veri transferi (Sistemin donmasını engeller).

Akıllı Arama Algoritması: Kart okunduğunda sırasıyla `personel` -> `Gonullu` tablolarını arar. Hangisinde bulursa işlemi oraya yazar.
Offline Mod: WiFi kesilirse veriyi kaybetmez, dahili hafızaya (`LittleFS`) kaydeder. İnternet gelince otomatik senkronize eder (`OFFLINE_SYNC`).
Tanımsız Kart Yönetimi: Kayıtsız kart okununca Buzzer 3 kere öter ve kart ID'si veritabanındaki `anlik_kart` tablosuna gönderilir (Kayıt kolaylığı için).

## 4. Masaüstü Yönetim Paneli (Python Arayüzü)
Sistemin beyni olan, Windows ve Mac uyumlu kontrol yazılımı.

### A. Kullanılan Teknolojiler
Dil:** Python 3.11+
GUI Kütüphanesi:** `tkinter` + `sv_ttk` (Modern Tema - Sun Valley).
Veri İşleme:** `pandas` (Excel ve Analiz için).
Paketleme:** `pyinstaller` (Tek dosya .exe/.app).

### B. Özellikler
Sekmeli Yapı:** "Hareket Logları" ve "Kullanıcı Yönetimi" olarak iki ana ekran.
Filtreleme:** Logları ve Kullanıcıları "Tümü", "Personel" veya "Gönüllü" olarak filtreleyebilme.
Kolay Kayıt (Remote Fetch):** "📡 Çek" butonu ile ESP32'ye okutulan son kartın ID'sini otomatik forma getirir. Elle yazma hatasını önler.
Analiz Sistemi:** Bir kullanıcı seçildiğinde **son 4 haftanın** çalışma saatlerini hesaplar ve popup olarak gösterir.
Gelişmiş Raporlama:** "Excel'e İndir" dendiğinde tek bir dosyada 3 ayrı sayfa oluşturur:
Sheet 1: Personel Listesi
Sheet 2: Gönüllü Listesi
Sheet 3: Tüm Hareket Logları