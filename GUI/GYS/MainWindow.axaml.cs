#nullable disable
using Avalonia.Controls;
using Avalonia.Interactivity;
using DotNetEnv;
using Supabase;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace IpekYoluGYS
{
    /// <summary>
    /// DataGrid üzerinde kişileri göstermek için kullanılan Data Transfer Object (DTO) sınıfı.
    /// Farklı tabloları (Personel ve Gönüllü) tek bir arayüzde birleştirmek için kullanılır.
    /// </summary>
    public class UserDto 
    {
        public int Sira { get; set; }
        public string Uid { get; set; }
        public string AdSoyad { get; set; }
        public string TC { get; set; }
        public string Rol { get; set; }
        public string Durum { get; set; }
        public bool IcerideMi { get; set; } 
    }

    /// <summary>
    /// Log kayıtlarını DataGrid üzerinde formatlı göstermek için kullanılan DTO.
    /// </summary>
    public class LogDto 
    {
        public int Id { get; set; }
        public string Zaman { get; set; }
        public string AdSoyad { get; set; }
        public string Rol { get; set; }
        public string IslemTipi { get; set; }
        public string GuncelleyenPc { get; set; }
    }

    public partial class MainWindow : Window
    {
        // Global Veritabanı İstemcisi ve RAM üzerinde tutulan veri listeleri (Cache mekanizması)
        private Client _supabase;
        private List<LogDto> _allLogs = new();
        private List<LogDto> _filteredLogs = new();
        private List<UserDto> _allUsers = new();
        private List<Izin> _allIzinler = new();
        
        // Sayfalama (Pagination) ayarları
        private int _currentPage = 1;
        private int _pageSize = 100;
        
        // İşlemleri hangi cihazın yaptığını loglamak için ortam bilgisayar adını alır
        private string _pcName = Environment.MachineName;

        // Düzenleme modlarının durum kontrolleri (Edit Modeler)
        private bool _isEditMode = false;
        private string _editOldUid = null;
        private string _editOldRol = null;

        private bool _isIzinEditMode = false;
        private int _editIzinId = -1;

        // Özel yapım Async Dialog pencereleri için TaskCompletionSource
        private TaskCompletionSource<string> _dialogTcs;

        public MainWindow()
        {
            InitializeComponent();
            InitializeSupabase();
            
            // Başlangıç tarihi olarak bugünü set et (UI komponentlerine)
            DpLogStart.SelectedDate = DateTime.Now;
            DpLogEnd.SelectedDate = DateTime.Now;
            DpIzinBas.SelectedDate = DateTime.Now;
            DpIzinBit.SelectedDate = DateTime.Now;
        }

        /// <summary>
        /// .env dosyasındaki API Key'leri okuyarak Supabase istemcisini asenkron olarak ayağa kaldırır
        /// ve ardından ana listeleri sunucudan çeker.
        /// </summary>
        private async void InitializeSupabase()
        {
            try
            {
                var basePath = System.AppDomain.CurrentDomain.BaseDirectory;
                var envPath = System.IO.Path.Combine(basePath, ".env");
                
                if (System.IO.File.Exists(envPath)) Env.Load(envPath);
                else Env.Load();

                var url = Environment.GetEnvironmentVariable("SUPABASE_URL");
                var key = Environment.GetEnvironmentVariable("SUPABASE_KEY");

                if (string.IsNullOrEmpty(url) || string.IsNullOrEmpty(key))
                {
                    await ShowDialogAsync("Kritik Hata", ".env dosyası bulunamadı veya içindeki SUPABASE_URL/KEY eksik!");
                    return;
                }

                // Supabase Realtime destekli bağlanır
                var options = new SupabaseOptions { AutoConnectRealtime = true };
                _supabase = new Client(url, key, options);
                await _supabase.InitializeAsync();
                
                LblStatus.Text = $"Sistem Hazır | Geçerli PC: {_pcName}";
                
                // Sistem açılır açılmaz verileri önbelleğe al
                await LoadUsers();
                await LoadLogs();
                await LoadIzinler();
            }
            catch (Exception ex)
            {
                await ShowDialogAsync("Bağlantı Hatası", $"Veritabanına bağlanılamadı:\n{ex.Message}");
            }
        }

        /// <summary>
        /// Arayüzü dondurmadan (non-blocking) çalışan özel Input/Mesaj dialog penceresi sistemi.
        /// </summary>
        private async Task<string> ShowDialogAsync(string title, string message, bool isInput = false, bool isConfirm = false, string defaultInput = "")
        {
            DialogTitle.Text = title;
            DialogMessage.Text = message;
            DialogInput.IsVisible = isInput;
            DialogInput.Text = defaultInput;
            BtnDialogCancel.IsVisible = isInput || isConfirm;
            
            OverlayDialog.IsVisible = true;
            _dialogTcs = new TaskCompletionSource<string>();
            var result = await _dialogTcs.Task; // Kullanıcı butona basana kadar bu satırda bekler (UI donmaz)
            OverlayDialog.IsVisible = false;
            
            return result;
        }

        // Dialog Buton Eventleri
        private void BtnDialogOk_Click(object sender, RoutedEventArgs e) => _dialogTcs?.TrySetResult(DialogInput.IsVisible ? DialogInput.Text : "OK");
        private void BtnDialogCancel_Click(object sender, RoutedEventArgs e) => _dialogTcs?.TrySetResult(null);

        // ====================================================================
        // LOG FONKSİYONLARI 
        // ====================================================================
        
        /// <summary>
        /// Supabase 'hareketler' tablosundan belirtilen tarih aralığındaki verileri çeker
        /// ve UI'a yansıtmak üzere LogDto listesine çevirir.
        /// </summary>
        private async Task LoadLogs()
        {
            var filterZaman = DpLogStart.SelectedDate?.ToString("yyyy-MM-dd") + " 00:00:00";
            var endZaman = DpLogEnd.SelectedDate?.ToString("yyyy-MM-dd") + " 23:59:59";
            var rolFiltresi = ((ComboBoxItem)CmbLogTur.SelectedItem).Content.ToString();

            // Veritabanı sorgusu (>= başlangıç tarihi, <= bitiş tarihi, Tarihe göre azalan sıralı)
            var logsRes = await _supabase.From<Hareket>()
                .Filter("zaman", Postgrest.Constants.Operator.GreaterThanOrEqual, filterZaman)
                .Filter("zaman", Postgrest.Constants.Operator.LessThanOrEqual, endZaman)
                .Order("zaman", Postgrest.Constants.Ordering.Descending)
                .Get();

            _allLogs.Clear();
            foreach (var log in logsRes.Models)
            {
                // İlişkisel mantığı veritabanı JOIN'i yerine RAM'de çözümlüyoruz (Performans artışı için)
                var user = _allUsers.FirstOrDefault(u => u.Uid == log.Uid);
                var ad = user?.AdSoyad ?? "-";
                var rol = user?.Rol ?? "-";

                // Rol filtresine uymuyorsa atla
                if (rolFiltresi != "TÜMÜ" && rol != rolFiltresi) continue;

                var islem = log.IslemTipi;
                // Eğer gece otomatik çıkartılan bir sistem varsa bunu belirt (Gece 23:59'daki çıkışlar)
                if (islem == "CIKIS" && log.Zaman != null && log.Zaman.Contains("23:59")) islem = "OTOMATIK_CIKIS";
                
                _allLogs.Add(new LogDto { 
                    Id = log.Id ?? 0, 
                    Zaman = log.Zaman, 
                    AdSoyad = ad, Rol = rol, IslemTipi = islem, 
                    GuncelleyenPc = string.IsNullOrEmpty(log.GuncelleyenPc) ? "-" : log.GuncelleyenPc 
                });
            }
            ApplyLogFilter();
        }

        private void TxtLogSearch_TextChanged(object sender, Avalonia.Controls.TextChangedEventArgs e) => ApplyLogFilter();

        /// <summary>
        /// Arama kutusuna girilen kelimeyi RAM'deki log listesinde filtreler (Veritabanını yormaz).
        /// </summary>
        private void ApplyLogFilter()
        {
            var text = TxtLogSearch.Text?.ToLower() ?? "";
            _filteredLogs = _allLogs.Where(l => l.AdSoyad.ToLower().Contains(text)).ToList();
            _currentPage = 1;
            RenderLogPage();
        }

        /// <summary>
        /// Filtrelenmiş listeyi sayfalara böler (Pagination) ve DataGrid'e gönderir.
        /// </summary>
        private void RenderLogPage()
        {
            GridLogs.ItemsSource = null; 
            var pagedLogs = _filteredLogs.Skip((_currentPage - 1) * _pageSize).Take(_pageSize).ToList();
            GridLogs.ItemsSource = pagedLogs;
            
            int totalPages = (int)Math.Ceiling(_filteredLogs.Count / (double)_pageSize);
            if (totalPages == 0) totalPages = 1;
            LblPageInfo.Text = $"Sayfa {_currentPage} / {totalPages}   |   Toplam: {_filteredLogs.Count} Kayıt";
        }

        /// <summary>
        /// Log DataGrid satırları çizilirken işlem tiplerine veya manuel değişikliğe göre renklendirme yapar.
        /// </summary>
        private void GridLogs_LoadingRow(object sender, DataGridRowEventArgs e)
        {
            if (e.Row.DataContext is LogDto log)
            {
                if (log.IslemTipi == "GIRIS") e.Row.Foreground = Avalonia.Media.SolidColorBrush.Parse("#27ae60"); // Yeşil
                else if (log.IslemTipi == "CIKIS") e.Row.Foreground = Avalonia.Media.SolidColorBrush.Parse("#c0392b"); // Kırmızı
                else if (log.IslemTipi == "OTOMATIK_CIKIS") e.Row.Foreground = Avalonia.Media.SolidColorBrush.Parse("#8e44ad"); // Mor
                else if (log.IslemTipi == "OFFLINE_SYNC") e.Row.Foreground = Avalonia.Media.SolidColorBrush.Parse("#e67e22"); // Turuncu

                // Eğer PC üzerinden saat güncellenmişse kalın puntolu siyah görünür (Manuel Müdahale Uyarısı)
                if (log.GuncelleyenPc != "-")
                {
                    e.Row.Foreground = Avalonia.Media.Brushes.Black;
                    e.Row.FontWeight = Avalonia.Media.FontWeight.Bold;
                }
            }
        }

        private async void MenuEditTime_Click(object sender, RoutedEventArgs e)
        {
            if (GridLogs.SelectedItem is LogDto log)
            {
                var yeniSaat = await ShowDialogAsync("Saat Düzenle", $"Eski Saat: {log.Zaman}\nYeni Saati Girin (YYYY-MM-DD HH:MM:SS):", true, false, log.Zaman);
                if (!string.IsNullOrEmpty(yeniSaat) && yeniSaat != log.Zaman)
                {
                    try {
                        await _supabase.From<Hareket>().Where(x => x.Id == log.Id).Set(x => x.Zaman, yeniSaat).Set(x => x.GuncelleyenPc, _pcName).Update();
                        await ShowDialogAsync("Başarılı", "Saat güncellendi.");
                        await LoadLogs();
                    } catch (Exception ex) { await ShowDialogAsync("Hata", ex.Message); }
                }
            }
        }

        private async void MenuShowPc_Click(object sender, RoutedEventArgs e) => await ShowPcInfo();
        private async void GridLogs_DoubleTapped(object sender, Avalonia.Input.TappedEventArgs e) => await ShowPcInfo();

        private async Task ShowPcInfo()
        {
            if (GridLogs.SelectedItem is LogDto log)
            {
                if (log.GuncelleyenPc != "-") await ShowDialogAsync("Düzenleyen PC", $"Bu kayıt şu bilgisayar tarafından güncellendi:\n\n{log.GuncelleyenPc}");
                else await ShowDialogAsync("Orijinal Kayıt", "Bu kayıt herhangi bir bilgisayar tarafından manuel olarak düzenlenmemiştir.");
            }
        }

        private async void BtnRefreshLogs_Click(object sender, RoutedEventArgs e) => await LoadLogs();
        private void BtnPrevPage_Click(object sender, RoutedEventArgs e) { if (_currentPage > 1) { _currentPage--; RenderLogPage(); } }
        private void BtnNextPage_Click(object sender, RoutedEventArgs e) { if (_currentPage * _pageSize < _filteredLogs.Count) { _currentPage++; RenderLogPage(); } }

        // ====================================================================
        // KULLANICI FONKSİYONLARI VE HAFTALIK SAAT RAPORU
        // ====================================================================
        
        /// <summary>
        /// Supabase üzerinden Personel ve Gönüllü tablolarını çekip tek bir listede birleştirir.
        /// </summary>
        private async Task LoadUsers()
        {
            var pRes = await _supabase.From<Personel>().Get();
            var gRes = await _supabase.From<Gonullu>().Get();

            _allUsers.Clear();
            foreach (var p in pRes.Models) _allUsers.Add(new UserDto { Uid = p.Uid, AdSoyad = p.AdSoyad, TC = string.IsNullOrEmpty(p.TC) ? "-" : p.TC, Rol = "PERSONEL", Durum = p.IcerideMi ? "İÇERİDE" : "DIŞARIDA", IcerideMi = p.IcerideMi });
            foreach (var g in gRes.Models) _allUsers.Add(new UserDto { Uid = g.Uid, AdSoyad = g.AdSoyad, TC = string.IsNullOrEmpty(g.TC) ? "-" : g.TC, Rol = "GONULLU", Durum = g.IcerideMi ? "İÇERİDE" : "DIŞARIDA", IcerideMi = g.IcerideMi });

            _allUsers = _allUsers.OrderByDescending(x => x.IcerideMi).ThenByDescending(x => x.Rol == "GONULLU").ThenBy(x => x.AdSoyad).ToList();
            for (int i = 0; i < _allUsers.Count; i++) _allUsers[i].Sira = i + 1;

            var isimListesi = _allUsers.Select(u => u.AdSoyad).ToList();
            TxtLogSearch.ItemsSource = isimListesi;
            TxtUserFilter.ItemsSource = isimListesi;
            TxtIzinAd.ItemsSource = isimListesi;
            TxtIzinFilter.ItemsSource = isimListesi;

            ApplyUserFilter();
        }

        private void GridUsers_LoadingRow(object sender, DataGridRowEventArgs e)
        {
            if (e.Row.DataContext is UserDto user)
            {
                if (user.IcerideMi) e.Row.Foreground = Avalonia.Media.SolidColorBrush.Parse("#27ae60"); 
                else e.Row.Foreground = Avalonia.Media.SolidColorBrush.Parse("#c0392b"); 
            }
        }

        private async void MenuForceGiris_Click(object sender, RoutedEventArgs e) => await ForceLog(true);
        private async void MenuForceCikis_Click(object sender, RoutedEventArgs e) => await ForceLog(false);

        /// <summary>
        /// Masaüstü uygulaması üzerinden manuel giriş/çıkış logu oluşturur.
        /// ESP32 üzerinden yapılmış gibi hareketler tablosuna işler ve durumu günceller.
        /// </summary>
        private async Task ForceLog(bool isGiris)
        {
            if (GridUsers.SelectedItem is UserDto user)
            {
                try {
                    var islemTipi = isGiris ? "GIRIS" : "CIKIS";
                    string anlikZamanESP32Formati = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss");

                    await _supabase.From<Hareket>().Insert(new Hareket { 
                        Uid = user.Uid, 
                        Zaman = anlikZamanESP32Formati, 
                        IslemTipi = islemTipi, 
                        GuncelleyenPc = _pcName 
                    });

                    if (user.Rol == "PERSONEL") await _supabase.From<Personel>().Where(x => x.Uid == user.Uid).Set(x => x.IcerideMi, isGiris).Update();
                    else await _supabase.From<Gonullu>().Where(x => x.Uid == user.Uid).Set(x => x.IcerideMi, isGiris).Update();

                    await LoadUsers();
                    await LoadLogs();
                    await ShowDialogAsync("Başarılı", $"{user.AdSoyad} için manuel {islemTipi} işlemi kaydedildi.");
                } catch (Exception ex) { await ShowDialogAsync("Hata", ex.Message); }
            }
        }

        /// <summary>
        /// Seçilen kullanıcının giriş/çıkış verilerini haftalara (ISO Week) bölerek çalışma saati raporu oluşturur.
        /// Giriş yapıp çıkış yapmayı unutma gibi eksik döngüleri filtreler.
        /// </summary>
        private async void MenuShowWeeklyHours_Click(object sender, RoutedEventArgs e)
        {
            if (GridUsers.SelectedItem is UserDto user)
            {
                try
                {
                    // Kişiye ait tüm hareketleri zamana göre artan (eskiden yeniye) şekilde getir
                    var res = await _supabase.From<Hareket>()
                        .Where(x => x.Uid == user.Uid)
                        .Order("zaman", Postgrest.Constants.Ordering.Ascending)
                        .Get();

                    var loglar = res.Models;
                    if (loglar.Count == 0)
                    {
                        await ShowDialogAsync("Bilgi", $"{user.AdSoyad} için sistemde henüz bir giriş-çıkış hareketi bulunamadı.");
                        return;
                    }

                    var haftalikSureler = new Dictionary<string, TimeSpan>();
                    DateTime? sonGiris = null;

                    foreach (var log in loglar)
                    {
                        if (DateTime.TryParse(log.Zaman, out DateTime dt))
                        {
                            if (log.IslemTipi == "GIRIS")
                            {
                                sonGiris = dt; // Girişi hafızaya al
                            }
                            else if (log.IslemTipi.Contains("CIKIS") && sonGiris != null)
                            {
                                // Çıkış varsa ve son giriş hafızadaysa farkı al
                                TimeSpan fark = dt - sonGiris.Value;
                                
                                // Olası yanlışlıkları engellemek için sadece 0 ile 24 saat arası mantıklı döngüleri topla
                                if (fark.TotalHours >= 0 && fark.TotalHours <= 24) 
                                {
                                    int yil = dt.Year;
                                    // Yılın kaçıncı haftası olduğunu uluslararası standarta göre hesapla
                                    int hafta = System.Globalization.ISOWeek.GetWeekOfYear(dt);
                                    string key = $"{yil} Yılı - {hafta}. Hafta";

                                    if (!haftalikSureler.ContainsKey(key))
                                        haftalikSureler[key] = TimeSpan.Zero;
                                    
                                    haftalikSureler[key] += fark; // İlgili haftanın toplamına ekle
                                }
                                sonGiris = null; // Döngüyü kapat
                            }
                        }
                    }

                    if (haftalikSureler.Count == 0)
                    {
                        await ShowDialogAsync("Bilgi", $"{user.AdSoyad} için hesaplanabilir tam bir giriş-çıkış (eşleşen) döngüsü bulunamadı.");
                        return;
                    }

                    // Hesaplanan veriyi UI Dialog için formatla
                    string rapor = "";
                    foreach (var kvp in haftalikSureler)
                    {
                        int toplamSaat = (int)kvp.Value.TotalHours;
                        int toplamDakika = kvp.Value.Minutes;
                        rapor += $"📅 {kvp.Key}:\nToplam: {toplamSaat} Saat {toplamDakika} Dakika\n\n";
                    }

                    await ShowDialogAsync($"Haftalık Çalışma Analizi: {user.AdSoyad}", rapor.Trim());
                }
                catch (Exception ex)
                {
                    await ShowDialogAsync("Hata", "Rapor oluşturulurken bir sorun yaşandı:\n" + ex.Message);
                }
            }
        }

        private async void BtnRefreshUsers_Click(object sender, RoutedEventArgs e) => await LoadUsers();

        private async void BtnSaveUser_Click(object sender, RoutedEventArgs e)
        {
            var uid = TxtUid.Text?.Trim();
            var ad = TxtAd.Text?.Trim();
            var tc = TxtTc.Text?.Trim() ?? "";
            var rol = ((ComboBoxItem)CmbRol.SelectedItem).Content.ToString();

            if (string.IsNullOrEmpty(uid) || string.IsNullOrEmpty(ad)) { await ShowDialogAsync("Hata", "UID ve Ad zorunlu."); return; }

            try
            {
                if (!_isEditMode)
                {
                    if (rol == "PERSONEL") await _supabase.From<Personel>().Insert(new Personel { Uid = uid, AdSoyad = ad, TC = tc });
                    else await _supabase.From<Gonullu>().Insert(new Gonullu { Uid = uid, AdSoyad = ad, TC = tc });
                    await ShowDialogAsync("Başarılı", "Kişi eklendi.");
                }
                else
                {
                    bool currentStatus = _allUsers.First(x => x.Uid == _editOldUid).IcerideMi;

                    if (_editOldRol != rol)
                    {
                        if (_editOldRol == "PERSONEL") await _supabase.From<Personel>().Where(x => x.Uid == _editOldUid).Delete();
                        else await _supabase.From<Gonullu>().Where(x => x.Uid == _editOldUid).Delete();

                        if (rol == "PERSONEL") await _supabase.From<Personel>().Insert(new Personel { Uid = uid, AdSoyad = ad, TC = tc, IcerideMi = currentStatus });
                        else await _supabase.From<Gonullu>().Insert(new Gonullu { Uid = uid, AdSoyad = ad, TC = tc, IcerideMi = currentStatus });
                    }
                    else
                    {
                        if (rol == "PERSONEL") await _supabase.From<Personel>().Where(x => x.Uid == _editOldUid).Set(x => x.Uid, uid).Set(x => x.AdSoyad, ad).Set(x => x.TC, tc).Update();
                        else await _supabase.From<Gonullu>().Where(x => x.Uid == _editOldUid).Set(x => x.Uid, uid).Set(x => x.AdSoyad, ad).Set(x => x.TC, tc).Update();
                    }

                    if (uid != _editOldUid)
                    {
                        await _supabase.From<Hareket>().Where(x => x.Uid == _editOldUid).Set(x => x.Uid, uid).Update();
                        await _supabase.From<Izin>().Where(x => x.Uid == _editOldUid).Set(x => x.Uid, uid).Update();
                    }
                    await ShowDialogAsync("Başarılı", "Kişi bilgileri güvenle güncellendi.");
                }
                ResetUserForm();
                await LoadUsers();
                await LoadLogs();
            }
            catch (Exception ex) { await ShowDialogAsync("Hata", $"İşlem tamamlanamadı:\n{ex.Message}"); }
        }

        private void MenuEditUser_Click(object sender, RoutedEventArgs e)
        {
            if (GridUsers.SelectedItem is UserDto user)
            {
                _isEditMode = true; _editOldUid = user.Uid; _editOldRol = user.Rol;
                LblUserFormTitle.Text = "Kişiyi Güncelle"; BtnSaveUser.Content = "GÜNCELLE"; BtnCancelEdit.IsVisible = true;
                TxtUid.Text = user.Uid; TxtAd.Text = user.AdSoyad; TxtTc.Text = user.TC == "-" ? "" : user.TC;
                CmbRol.SelectedIndex = user.Rol == "PERSONEL" ? 0 : 1;
            }
        }

        private async void MenuDeleteUser_Click(object sender, RoutedEventArgs e)
        {
            if (GridUsers.SelectedItem is UserDto user)
            {
                var onay = await ShowDialogAsync("Kritik İşlem", "Bu kişiyi silmek istiyor musunuz?\n(Geçmiş logları kalacaktır ancak isimleri '-' görünecektir.)", false, true);
                if (onay != null)
                {
                    if (user.Rol == "PERSONEL") await _supabase.From<Personel>().Where(x => x.Uid == user.Uid).Delete();
                    else await _supabase.From<Gonullu>().Where(x => x.Uid == user.Uid).Delete();
                    await LoadUsers();
                    await LoadLogs();
                }
            }
        }

        private void BtnCancelEdit_Click(object sender, RoutedEventArgs e) => ResetUserForm();

        private void ResetUserForm()
        {
            _isEditMode = false; _editOldUid = null; _editOldRol = null;
            LblUserFormTitle.Text = "Yeni Kişi Ekle"; BtnSaveUser.Content = "KİŞİYİ KAYDET"; BtnCancelEdit.IsVisible = false;
            TxtUid.Text = ""; TxtAd.Text = ""; TxtTc.Text = ""; CmbRol.SelectedIndex = 0;
        }

        private async void BtnCek_Click(object sender, RoutedEventArgs e)
        {
            try 
            {
                var res = await _supabase.From<AnlikKart>().Where(x => x.Id == 1).Get();
                
                if (res.Models.Count > 0) 
                {
                    var kart = res.Models[0];
                    
                    if (string.IsNullOrEmpty(kart.Uid)) 
                    {
                        await ShowDialogAsync("Bilgi", "Okunmuş yeni bir kart bulunamadı.");
                        return;
                    }

                    if (DateTime.TryParse(kart.Zaman, out DateTime okumaZamani))
                    {
                        TimeSpan fark = DateTime.Now - okumaZamani;
                        
                        if (fark.TotalMinutes > 5)
                        {
                            await _supabase.From<AnlikKart>().Where(x => x.Id == 1).Set(x => x.Uid, "").Set(x => x.Zaman, "").Update();
                            await ShowDialogAsync("Güvenlik Uyarısı", "Okutulan kartın üzerinden 5 dakikadan fazla zaman geçmiş. Güvenlik sebebiyle iptal edildi.\n\nLütfen kartı cihaza tekrar okutun.");
                            TxtUid.Text = "";
                            return;
                        }
                    }

                    TxtUid.Text = kart.Uid;
                    
                    await _supabase.From<AnlikKart>().Where(x => x.Id == 1).Set(x => x.Uid, "").Set(x => x.Zaman, "").Update();
                }
            } 
            catch (Exception ex) 
            {
                await ShowDialogAsync("Hata", "Kart bilgisi çekilemedi:\n" + ex.Message);
            }
        }

        private void TxtUserFilter_TextChanged(object sender, Avalonia.Controls.TextChangedEventArgs e) => ApplyUserFilter();
        
        private void ApplyUserFilter()
        {
            var text = TxtUserFilter.Text?.ToLower() ?? "";
            GridUsers.ItemsSource = null;
            GridUsers.ItemsSource = _allUsers.Where(u => u.AdSoyad.ToLower().Contains(text)).ToList();
        }

        // ====================================================================
        // İZİNLER, ARAMA, DÜZENLEME VE SİLME FONKSİYONLARI 
        // ====================================================================
        private async Task LoadIzinler()
        {
            try {
                var res = await _supabase.From<Izin>().Get();
                _allIzinler = res.Models.ToList();
                _allIzinler = _allIzinler.OrderByDescending(x => x.BaslangicTarihi).ToList();
                ApplyIzinFilter();
            } catch (Exception) { _allIzinler = new List<Izin>(); }
        }

        private void TxtIzinFilter_TextChanged(object sender, Avalonia.Controls.TextChangedEventArgs e) => ApplyIzinFilter();
        
        private void ApplyIzinFilter()
        {
            var text = TxtIzinFilter.Text?.ToLower() ?? "";
            GridIzinler.ItemsSource = null;
            GridIzinler.ItemsSource = _allIzinler.Where(i => i.AdSoyad.ToLower().Contains(text)).ToList();
        }

        private async void BtnAddIzin_Click(object sender, RoutedEventArgs e)
        {
             var secilenIsim = TxtIzinAd.Text?.Trim();
             var user = _allUsers.FirstOrDefault(u => u.AdSoyad == secilenIsim);
             
             if (user != null && DpIzinBas.SelectedDate.HasValue && DpIzinBit.SelectedDate.HasValue) 
             {
                 try {
                     if (!_isIzinEditMode) 
                     {
                         await _supabase.From<Izin>().Insert(new Izin { 
                             Uid = user.Uid, 
                             AdSoyad = user.AdSoyad, 
                             BaslangicTarihi = DpIzinBas.SelectedDate.Value, 
                             BitisTarihi = DpIzinBit.SelectedDate.Value,     
                             Aciklama = TxtAciklama.Text 
                         });
                         await ShowDialogAsync("Başarılı", "İzin başarıyla eklendi.");
                     }
                     else 
                     {
                         await _supabase.From<Izin>().Where(x => x.Id == _editIzinId)
                            .Set(x => x.Uid, user.Uid)
                            .Set(x => x.AdSoyad, user.AdSoyad)
                            .Set(x => x.BaslangicTarihi, DpIzinBas.SelectedDate.Value)
                            .Set(x => x.BitisTarihi, DpIzinBit.SelectedDate.Value)
                            .Set(x => x.Aciklama, TxtAciklama.Text)
                            .Update();
                         await ShowDialogAsync("Başarılı", "İzin başarıyla güncellendi.");
                     }
                     ResetIzinForm();
                     await LoadIzinler();
                 } catch (Exception ex) { await ShowDialogAsync("Hata", "İşlem başarısız oldu:\n" + ex.Message); }
             } else await ShowDialogAsync("Uyarı", "Lütfen listeden geçerli bir kişi seçin ve tarihleri eksiksiz doldurun.");
        }

        private void MenuEditIzin_Click(object sender, RoutedEventArgs e)
        {
            if (GridIzinler.SelectedItem is Izin izin)
            {
                _isIzinEditMode = true;
                _editIzinId = izin.Id ?? 0;

                TxtIzinAd.Text = izin.AdSoyad;
                DpIzinBas.SelectedDate = izin.BaslangicTarihi;
                DpIzinBit.SelectedDate = izin.BitisTarihi;
                TxtAciklama.Text = izin.Aciklama;

                BtnAddIzin.Content = "GÜNCELLE";
                BtnCancelIzinEdit.IsVisible = true;
            }
        }

        private async void MenuDeleteIzin_Click(object sender, RoutedEventArgs e)
        {
            if (GridIzinler.SelectedItem is Izin izin)
            {
                var onay = await ShowDialogAsync("İzni Sil", $"{izin.AdSoyad} adlı kişinin iznini silmek istiyor musunuz?", false, true);
                if (onay != null)
                {
                    try {
                        await _supabase.From<Izin>().Where(x => x.Id == izin.Id).Delete();
                        await LoadIzinler();
                        await ShowDialogAsync("Başarılı", "İzin kayıtları tablodan silindi.");
                    } catch (Exception ex) { await ShowDialogAsync("Hata", "Silme işlemi başarısız:\n" + ex.Message); }
                }
            }
        }

        private async void MenuShowIzinAciklama_Click(object sender, RoutedEventArgs e)
        {
            if (GridIzinler.SelectedItem is Izin izin)
            {
                await ShowDialogAsync($"Açıklama Detayı: {izin.AdSoyad}", izin.Aciklama);
            }
        }

        private void BtnCancelIzinEdit_Click(object sender, RoutedEventArgs e) => ResetIzinForm();

        private void ResetIzinForm()
        {
            _isIzinEditMode = false;
            _editIzinId = -1;
            TxtIzinAd.Text = "";
            DpIzinBas.SelectedDate = DateTime.Now;
            DpIzinBit.SelectedDate = DateTime.Now;
            TxtAciklama.Text = "";
            BtnAddIzin.Content = "➕ İzin Ekle";
            BtnCancelIzinEdit.IsVisible = false;
        }

        private async void BtnRefreshIzin_Click(object sender, RoutedEventArgs e) => await LoadIzinler();
    }
}