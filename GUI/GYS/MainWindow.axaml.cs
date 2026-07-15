#nullable disable
using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using ClosedXML.Excel;
using DotNetEnv;
using Supabase;
using System;
using System.Collections.Generic;
using System.IO;
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

        // Modal diyaloglarda Enter (Tamam) ve ESC (İptal) klavye kısayolu desteği
        private void DialogInput_KeyDown(object sender, Avalonia.Input.KeyEventArgs e)
        {
            if (e.Key == Avalonia.Input.Key.Enter)
            {
                BtnDialogOk_Click(sender, null);
                e.Handled = true;
            }
            else if (e.Key == Avalonia.Input.Key.Escape)
            {
                BtnDialogCancel_Click(sender, null);
                e.Handled = true;
            }
        }

        // ====================================================================
        // LOG FONKSİYONLARI 
        // ====================================================================

        /// <summary>
        /// Supabase 'hareketler' tablosundan belirtilen tarih aralığındaki verileri çeker
        /// ve UI'a yansıtmak üzere LogDto listesine çevirir.
        /// </summary>
        private async Task LoadLogs()
        {
            try
            {
                LblStatus.Text = "⏳ Log kayıtları Supabase sunucusundan çekiliyor...";

                var startDate = DpLogStart.SelectedDate ?? DateTime.Now;
                var endDate = DpLogEnd.SelectedDate ?? DateTime.Now;

                if (startDate > endDate)
                {
                    var temp = startDate;
                    startDate = endDate;
                    endDate = temp;
                    DpLogStart.SelectedDate = startDate;
                    DpLogEnd.SelectedDate = endDate;
                }

                var filterZaman = startDate.ToString("yyyy-MM-dd") + " 00:00:00";
                var endZaman = endDate.ToString("yyyy-MM-dd") + " 23:59:59";

                string rolFiltresi = "TÜMÜ";
                if (CmbLogTur?.SelectedItem is ComboBoxItem selectedRolItem && selectedRolItem.Content != null)
                {
                    rolFiltresi = selectedRolItem.Content.ToString();
                }

                var logsRes = await _supabase.From<Hareket>()
                    .Filter("zaman", Postgrest.Constants.Operator.GreaterThanOrEqual, filterZaman)
                    .Filter("zaman", Postgrest.Constants.Operator.LessThanOrEqual, endZaman)
                    .Order("zaman", Postgrest.Constants.Ordering.Descending)
                    .Get();

                _allLogs.Clear();
                if (logsRes?.Models != null)
                {
                    foreach (var log in logsRes.Models)
                    {
                        var user = _allUsers.FirstOrDefault(u => u.Uid == log.Uid);
                        var ad = user?.AdSoyad ?? "-";
                        var rol = user?.Rol ?? "-";

                        if (rolFiltresi != "TÜMÜ" && rol != rolFiltresi) continue;

                        var islem = log.IslemTipi;
                        if (islem == "CIKIS" && log.Zaman != null && log.Zaman.Contains("23:59")) islem = "OTOMATIK_CIKIS";

                        _allLogs.Add(new LogDto
                        {
                            Id = log.Id ?? 0,
                            Zaman = log.Zaman,
                            AdSoyad = ad,
                            Rol = rol,
                            IslemTipi = islem,
                            GuncelleyenPc = string.IsNullOrEmpty(log.GuncelleyenPc) ? "-" : log.GuncelleyenPc
                        });
                    }
                }
                ApplyLogFilter();
                LblStatus.Text = $"✅ Loglar güncellendi | Toplam Gösterilen: {_filteredLogs.Count} Kayıt";
            }
            catch (Exception ex)
            {
                LblStatus.Text = $"❌ Loglar çekilemedi: {ex.Message}";
                await ShowDialogAsync("Bağlantı Hatası", $"Log verileri yüklenirken bir sorun oluştu:\n{ex.Message}");
            }
        }

        private void TxtLogSearch_TextChanged(object sender, Avalonia.Controls.TextChangedEventArgs e) => ApplyLogFilter();

        private async void CmbLogTur_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (_supabase != null) await LoadLogs();
        }

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

            // Profesyonel Sayfalama UX: 1. sayfada önceki butonunu, son sayfada sonraki butonunu kilitliyoruz
            if (BtnPrevPage != null) BtnPrevPage.IsEnabled = (_currentPage > 1);
            if (BtnNextPage != null) BtnNextPage.IsEnabled = (_currentPage < totalPages);
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
                    if (!DateTime.TryParseExact(yeniSaat.Trim(), "yyyy-MM-dd HH:mm:ss", System.Globalization.CultureInfo.InvariantCulture, System.Globalization.DateTimeStyles.None, out _))
                    {
                        await ShowDialogAsync("Hatalı Format", "Girdiğiniz saat formatı geçersiz!\nLütfen 'YYYY-MM-DD HH:MM:SS' (Örn: 2026-07-15 14:30:00) formatında giriniz.");
                        return;
                    }

                    try
                    {
                        LblStatus.Text = "⏳ Saat kaydı güncelleniyor...";
                        await _supabase.From<Hareket>().Where(x => x.Id == log.Id).Set(x => x.Zaman, yeniSaat.Trim()).Set(x => x.GuncelleyenPc, _pcName).Update();
                        await ShowDialogAsync("Başarılı", "Saat güncellendi.");
                        await LoadLogs();
                    }
                    catch (Exception ex)
                    {
                        LblStatus.Text = "❌ Saat güncellenemedi.";
                        await ShowDialogAsync("Hata", ex.Message);
                    }
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
            try
            {
                LblStatus.Text = "⏳ Kullanıcı listeleri çekiliyor...";
                var pRes = await _supabase.From<Personel>().Get();
                var gRes = await _supabase.From<Gonullu>().Get();

                _allUsers.Clear();
                if (pRes?.Models != null)
                {
                    foreach (var p in pRes.Models) _allUsers.Add(new UserDto { Uid = p.Uid, AdSoyad = p.AdSoyad, TC = string.IsNullOrEmpty(p.TC) ? "-" : p.TC, Rol = "PERSONEL", Durum = p.IcerideMi ? "İÇERİDE" : "DIŞARIDA", IcerideMi = p.IcerideMi });
                }
                if (gRes?.Models != null)
                {
                    foreach (var g in gRes.Models) _allUsers.Add(new UserDto { Uid = g.Uid, AdSoyad = g.AdSoyad, TC = string.IsNullOrEmpty(g.TC) ? "-" : g.TC, Rol = "GONULLU", Durum = g.IcerideMi ? "İÇERİDE" : "DIŞARIDA", IcerideMi = g.IcerideMi });
                }

                _allUsers = _allUsers.OrderByDescending(x => x.IcerideMi).ThenByDescending(x => x.Rol == "GONULLU").ThenBy(x => x.AdSoyad).ToList();
                for (int i = 0; i < _allUsers.Count; i++) _allUsers[i].Sira = i + 1;

                var isimListesi = _allUsers.Select(u => u.AdSoyad).ToList();
                TxtLogSearch.ItemsSource = isimListesi;
                TxtUserFilter.ItemsSource = isimListesi;
                TxtIzinAd.ItemsSource = isimListesi;
                TxtIzinFilter.ItemsSource = isimListesi;

                ApplyUserFilter();
                LblStatus.Text = $"✅ Kullanıcılar yüklendi | Toplam: {_allUsers.Count} Kişi";
            }
            catch (Exception ex)
            {
                LblStatus.Text = $"❌ Kullanıcılar yüklenemedi: {ex.Message}";
                await ShowDialogAsync("Bağlantı Hatası", $"Kullanıcı listesi alınamadı:\n{ex.Message}");
            }
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
                try
                {
                    var islemTipi = isGiris ? "GIRIS" : "CIKIS";
                    string anlikZamanESP32Formati = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss");

                    await _supabase.From<Hareket>().Insert(new Hareket
                    {
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
                }
                catch (Exception ex) { await ShowDialogAsync("Hata", ex.Message); }
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
                    LblStatus.Text = $"⏳ {user.AdSoyad} için haftalık analiz hesaplanıyor...";
                    var res = await _supabase.From<Hareket>()
                        .Where(x => x.Uid == user.Uid)
                        .Order("zaman", Postgrest.Constants.Ordering.Ascending)
                        .Get();

                    var loglar = res.Models;
                    if (loglar == null || loglar.Count == 0)
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
                                sonGiris = dt;
                            }
                            else if (log.IslemTipi != null && log.IslemTipi.Contains("CIKIS") && sonGiris != null)
                            {
                                TimeSpan fark = dt - sonGiris.Value;
                                if (fark.TotalHours >= 0 && fark.TotalHours <= 24)
                                {
                                    int yil = dt.Year;
                                    int hafta = System.Globalization.ISOWeek.GetWeekOfYear(dt);
                                    string key = $"{yil} Yılı - {hafta,2:D2}. Hafta";

                                    if (!haftalikSureler.ContainsKey(key))
                                        haftalikSureler[key] = TimeSpan.Zero;

                                    haftalikSureler[key] += fark;
                                }
                                sonGiris = null;
                            }
                        }
                    }

                    if (haftalikSureler.Count == 0)
                    {
                        await ShowDialogAsync("Bilgi", $"{user.AdSoyad} için hesaplanabilir tam bir giriş-çıkış (eşleşen) döngüsü bulunamadı.");
                        return;
                    }

                    // Profesyonel Sunum: Haftaları rastgele değil, kronolojik yıla ve haftaya göre sıralıyoruz
                    string rapor = "";
                    foreach (var kvp in haftalikSureler.OrderBy(k => k.Key))
                    {
                        int toplamSaat = (int)kvp.Value.TotalHours;
                        int toplamDakika = kvp.Value.Minutes;
                        rapor += $"📅 {kvp.Key}:\nToplam Çalışma: {toplamSaat} Saat {toplamDakika} Dakika\n\n";
                    }

                    LblStatus.Text = $"✅ {user.AdSoyad} için haftalık analiz tamamlandı.";
                    await ShowDialogAsync($"Haftalık Çalışma Analizi: {user.AdSoyad}", rapor.Trim());
                }
                catch (Exception ex)
                {
                    LblStatus.Text = "❌ Analiz hatası.";
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

            if (string.IsNullOrEmpty(uid) || string.IsNullOrEmpty(ad)) { await ShowDialogAsync("Hata", "Kart UID ve Ad Soyad alanları zorunludur."); return; }
            if (ad.Length < 2) { await ShowDialogAsync("Hata", "Lütfen geçerli bir Ad Soyad giriniz (En az 2 karakter)."); return; }

            if (!string.IsNullOrEmpty(tc) && tc != "-")
            {
                if (tc.Length != 11 || !tc.All(char.IsDigit))
                {
                    await ShowDialogAsync("Uyarı", "TC Kimlik Numarası tam olarak 11 haneli olmalı ve sadece rakamlardan oluşmalıdır.");
                    return;
                }
            }
            else tc = "-";

            if (!_isEditMode || (_isEditMode && !string.Equals(uid, _editOldUid, StringComparison.OrdinalIgnoreCase)))
            {
                var existingUidUser = _allUsers.FirstOrDefault(u => string.Equals(u.Uid, uid, StringComparison.OrdinalIgnoreCase));
                if (existingUidUser != null) { await ShowDialogAsync("Uyarı", $"'{uid}' kart UID numarası zaten sistemde '{existingUidUser.AdSoyad}' adına kayıtlı!"); return; }
            }

            if (tc != "-" && (!_isEditMode || (_isEditMode && !string.Equals(tc, _allUsers.FirstOrDefault(u => u.Uid == _editOldUid)?.TC, StringComparison.OrdinalIgnoreCase))))
            {
                var existingTcUser = _allUsers.FirstOrDefault(u => u.TC != "-" && string.Equals(u.TC, tc, StringComparison.OrdinalIgnoreCase));
                if (existingTcUser != null) { await ShowDialogAsync("Uyarı", $"'{tc}' TC Kimlik Numarası zaten sistemde '{existingTcUser.AdSoyad}' adına kayıtlı!"); return; }
            }

            try
            {
                LblStatus.Text = "⏳ Kişi veritabanına kaydediliyor...";
                if (!_isEditMode)
                {
                    if (rol == "PERSONEL") await _supabase.From<Personel>().Insert(new Personel { Uid = uid, AdSoyad = ad, TC = tc });
                    else await _supabase.From<Gonullu>().Insert(new Gonullu { Uid = uid, AdSoyad = ad, TC = tc });
                    await ShowDialogAsync("Başarılı", "Kişi eklendi.");
                }
                else
                {
                    bool currentStatus = _allUsers.First(x => x.Uid == _editOldUid).IcerideMi;
                    string oldAdSoyad = _allUsers.First(x => x.Uid == _editOldUid).AdSoyad;

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

                    // Profesyonel Veri Bütünlüğü: UID veya Ad Soyad değiştiyse ilişkili tablolarda da hayalet veri kalmasın
                    if (uid != _editOldUid || !string.Equals(ad, oldAdSoyad, StringComparison.OrdinalIgnoreCase))
                    {
                        if (uid != _editOldUid)
                        {
                            await _supabase.From<Hareket>().Where(x => x.Uid == _editOldUid).Set(x => x.Uid, uid).Update();
                        }
                        await _supabase.From<Izin>().Where(x => x.Uid == _editOldUid)
                            .Set(x => x.Uid, uid)
                            .Set(x => x.AdSoyad, ad)
                            .Update();
                    }
                    await ShowDialogAsync("Başarılı", "Kişi bilgileri güvenle güncellendi.");
                }
                ResetUserForm();
                await LoadUsers();
                await LoadLogs();
            }
            catch (Exception ex)
            {
                LblStatus.Text = "❌ Kaydetme hatası.";
                await ShowDialogAsync("Hata", $"İşlem tamamlanamadı:\n{ex.Message}");
            }
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
                LblStatus.Text = "📡 Okutulan son kart sorgulanıyor...";
                var res = await _supabase.From<AnlikKart>().Where(x => x.Id == 1).Get();

                if (res?.Models != null && res.Models.Count > 0)
                {
                    var kart = res.Models[0];

                    if (string.IsNullOrEmpty(kart.Uid))
                    {
                        LblStatus.Text = "Sistem Hazır";
                        await ShowDialogAsync("Bilgi", "Okunmuş yeni bir kart bulunamadı.");
                        return;
                    }

                    if (DateTime.TryParse(kart.Zaman, out DateTime okumaZamani))
                    {
                        // NTP Saat dilimi kaymalarına karşı Math.Abs ile mutlak değer alıyoruz
                        TimeSpan fark = DateTime.Now - okumaZamani;
                        if (Math.Abs(fark.TotalMinutes) > 5)
                        {
                            await _supabase.From<AnlikKart>().Where(x => x.Id == 1).Set(x => x.Uid, "").Set(x => x.Zaman, "").Update();
                            LblStatus.Text = "Sistem Hazır";
                            await ShowDialogAsync("Güvenlik Uyarısı", "Okutulan kartın üzerinden 5 dakikadan fazla zaman geçmiş veya cihaz saati senkron değil. Güvenlik sebebiyle iptal edildi.\n\nLütfen kartı cihaza tekrar okutun.");
                            TxtUid.Text = "";
                            return;
                        }
                    }

                    TxtUid.Text = kart.Uid;
                    await _supabase.From<AnlikKart>().Where(x => x.Id == 1).Set(x => x.Uid, "").Set(x => x.Zaman, "").Update();
                    LblStatus.Text = $"✅ Kart başarıyla okundu: {kart.Uid}";
                }
            }
            catch (Exception ex)
            {
                LblStatus.Text = "❌ Kart çekme hatası.";
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
            try
            {
                var res = await _supabase.From<Izin>().Get();
                _allIzinler = res.Models.ToList();
                _allIzinler = _allIzinler.OrderByDescending(x => x.BaslangicTarihi).ToList();
                ApplyIzinFilter();
            }
            catch (Exception) { _allIzinler = new List<Izin>(); }
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
            // Büyük/küçük harf duyarlılığı olmadan esnek eşleştirme (OrdinalIgnoreCase)
            var user = _allUsers.FirstOrDefault(u => string.Equals(u.AdSoyad, secilenIsim, StringComparison.OrdinalIgnoreCase));

            if (user != null && DpIzinBas.SelectedDate.HasValue && DpIzinBit.SelectedDate.HasValue)
            {
                if (DpIzinBit.SelectedDate.Value.Date < DpIzinBas.SelectedDate.Value.Date)
                {
                    await ShowDialogAsync("Mantıksal Hata", "İzin bitiş tarihi, başlangıç tarihinden önce olamaz!");
                    return;
                }

                if (string.IsNullOrWhiteSpace(TxtAciklama.Text) || TxtAciklama.Text.Trim().Length < 3)
                {
                    await ShowDialogAsync("Uyarı", "Lütfen izin için en az 3 karakterden oluşan geçerli bir açıklama/neden belirtiniz.");
                    return;
                }

                try
                {
                    LblStatus.Text = "⏳ İzin veritabanına işleniyor...";
                    if (!_isIzinEditMode)
                    {
                        await _supabase.From<Izin>().Insert(new Izin
                        {
                            Uid = user.Uid,
                            AdSoyad = user.AdSoyad,
                            BaslangicTarihi = DpIzinBas.SelectedDate.Value,
                            BitisTarihi = DpIzinBit.SelectedDate.Value,
                            Aciklama = TxtAciklama.Text.Trim()
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
                           .Set(x => x.Aciklama, TxtAciklama.Text.Trim())
                           .Update();
                        await ShowDialogAsync("Başarılı", "İzin başarıyla güncellendi.");
                    }
                    ResetIzinForm();
                    await LoadIzinler();
                }
                catch (Exception ex)
                {
                    LblStatus.Text = "❌ İzin işlemi başarısız.";
                    await ShowDialogAsync("Hata", "İşlem başarısız oldu:\n" + ex.Message);
                }
            }
            else await ShowDialogAsync("Uyarı", "Lütfen listeden geçerli bir kişi seçin ve tarihleri eksiksiz doldurun.");
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
                    try
                    {
                        await _supabase.From<Izin>().Where(x => x.Id == izin.Id).Delete();
                        await LoadIzinler();
                        await ShowDialogAsync("Başarılı", "İzin kayıtları tablodan silindi.");
                    }
                    catch (Exception ex) { await ShowDialogAsync("Hata", "Silme işlemi başarısız:\n" + ex.Message); }
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

        // ==========================================
        // EXCEL AYRINTILI RAPORLAMA METOTLARI (ŞABLON BAZLI)
        // ==========================================

        private async void MenuExportExcel_Click(object sender, RoutedEventArgs e)
        {
            if (GridUsers.SelectedItem is UserDto selectedUser)
            {
                // Yıl ve Ayı esnek seçebilmek için akıllı yönlendirme yapıldı
                string defaultInput = DateTime.Now.ToString("yyyy-MM");
                string inputDate = await ShowDialogAsync("Excel Raporu - Tarih Seçimi", "Raporunu almak istediğiniz Yıl ve Ayı giriniz\n(Örn: 2026-07 veya sadece Ay: 7):", true, false, defaultInput);

                if (string.IsNullOrWhiteSpace(inputDate)) return;

                int targetYear = DateTime.Now.Year;
                int targetMonth = DateTime.Now.Month;

                if (inputDate.Contains("-") || inputDate.Contains("/"))
                {
                    var parts = inputDate.Split('-', '/');
                    if (parts.Length == 2 && int.TryParse(parts[0], out int pYear) && int.TryParse(parts[1], out int pMonth))
                    {
                        if (pYear > 2000 && pMonth >= 1 && pMonth <= 12) { targetYear = pYear; targetMonth = pMonth; }
                        else { await ShowDialogAsync("Hata", "Geçersiz Yıl veya Ay değeri girdiniz."); return; }
                    }
                    else { await ShowDialogAsync("Hata", "Tarih formatı algılanamadı. Örn: 2026-07"); return; }
                }
                else if (int.TryParse(inputDate.Trim(), out int justMonth) && justMonth >= 1 && justMonth <= 12)
                {
                    targetMonth = justMonth;
                }
                else
                {
                    await ShowDialogAsync("Hata", "Lütfen geçerli bir ay numarası (1-12) veya YYYY-MM formatında tarih giriniz.");
                    return;
                }

                await ExportUserMonthlyReportToExcelAsync(selectedUser, targetYear, targetMonth);
            }
        }

        private async Task ExportUserMonthlyReportToExcelAsync(UserDto user, int year, int month)
        {
            try
            {
                LblStatus.Text = $"⏳ {user.AdSoyad} için {year}/{month:D2} Excel raporu oluşturuluyor...";
                var queryResponse = await _supabase.From<Hareket>().Where(x => x.Uid == user.Uid).Get();
                List<Hareket> allLogs = queryResponse?.Models;

                if (allLogs == null || !allLogs.Any())
                {
                    LblStatus.Text = "Sistem Hazır";
                    await ShowDialogAsync("Bilgi", "Seçilen kişiye ait veritabanında geçiş logu bulunamadı.");
                    return;
                }

                var filteredLogs = allLogs
                    .Select(log => new
                    {
                        Item = log,
                        ParsedDate = DateTime.TryParse(log.Zaman, out var dt) ? dt : DateTime.MinValue
                    })
                    .Where(log => log.ParsedDate.Year == year && log.ParsedDate.Month == month && log.ParsedDate != DateTime.MinValue)
                    .OrderBy(log => log.ParsedDate)
                    .ToList();

                if (!filteredLogs.Any())
                {
                    LblStatus.Text = "Sistem Hazır";
                    await ShowDialogAsync("Bilgi", $"{year} Yılı {month}. Aya ait herhangi bir geçiş kaydı mevcut değil.");
                    return;
                }

                bool isVolunteer = user.Rol != null && (user.Rol.ToUpper() == "GONULLU" || user.Rol.ToUpper() == "GÖNÜLLÜ");
                string templateFileName = isVolunteer ? "GonulluSablonu.xlsx" : "PersonelSablonu.xlsx";
                string templatePath = System.IO.Path.Combine(System.AppContext.BaseDirectory, "Assets", templateFileName);

                if (!System.IO.File.Exists(templatePath))
                {
                    LblStatus.Text = "❌ Şablon dosyası bulunamadı.";
                    await ShowDialogAsync("Hata", $"Şablon dosyası bulunamadı: {templatePath}\nLütfen Assets klasörüne '{templateFileName}' şablonunu ekleyin.");
                    return;
                }

                using var workbook = new ClosedXML.Excel.XLWorkbook(templatePath);
                var sheetData = workbook.Worksheet("Veri Girişi");

                int dataRowIndex = 3;
                int daysInMonth = DateTime.DaysInMonth(year, month);
                var turkishCulture = new System.Globalization.CultureInfo("tr-TR");

                for (int day = 1; day <= daysInMonth; day++)
                {
                    DateTime currentDay = new DateTime(year, month, day);
                    var dayLogs = filteredLogs.Where(x => x.ParsedDate.Date == currentDay.Date).ToList();

                    sheetData.Cell(dataRowIndex, 1).Value = currentDay.ToString("dd.MM.yyyy dddd", turkishCulture);
                    sheetData.Cell(dataRowIndex, 2).Value = user.AdSoyad;

                    bool isWeekend = currentDay.DayOfWeek == DayOfWeek.Saturday || currentDay.DayOfWeek == DayOfWeek.Sunday;
                    bool isMissingOrInvalidLog = false;

                    if (!dayLogs.Any())
                    {
                        sheetData.Cell(dataRowIndex, 3).Value = "";
                        sheetData.Cell(dataRowIndex, 4).Value = "";
                        isMissingOrInvalidLog = true;
                    }
                    else
                    {
                        var firstEntry = dayLogs.FirstOrDefault(x => x.Item.IslemTipi == "GIRIS");
                        var lastExit = dayLogs.LastOrDefault(x => x.Item.IslemTipi == "CIKIS");

                        if (firstEntry == null || lastExit == null || (lastExit.ParsedDate.Hour == 23 && lastExit.ParsedDate.Minute >= 58))
                        {
                            sheetData.Cell(dataRowIndex, 3).Value = firstEntry != null ? firstEntry.ParsedDate.ToString("HH:mm") : "";
                            sheetData.Cell(dataRowIndex, 4).Value = lastExit != null ? lastExit.ParsedDate.ToString("HH:mm") : "";

                            string endColumn = isVolunteer ? "E" : "G";
                            sheetData.Range($"A{dataRowIndex}:{endColumn}{dataRowIndex}").Style.Fill.BackgroundColor = ClosedXML.Excel.XLColor.Yellow;
                            isMissingOrInvalidLog = true;
                        }
                        else
                        {
                            sheetData.Cell(dataRowIndex, 3).Value = firstEntry.ParsedDate.ToString("HH:mm");
                            sheetData.Cell(dataRowIndex, 4).Value = lastExit.ParsedDate.ToString("HH:mm");
                        }
                    }

                    if (!isVolunteer)
                    {
                        if (isWeekend || isMissingOrInvalidLog)
                        {
                            sheetData.Cell(dataRowIndex, 5).Value = 0;
                            sheetData.Cell(dataRowIndex, 6).Value = 0;
                        }
                        else
                        {
                            sheetData.Cell(dataRowIndex, 5).Value = 1;
                            sheetData.Cell(dataRowIndex, 6).Value = 9;
                        }
                    }

                    dataRowIndex++;
                }

                var storageProvider = Avalonia.Controls.TopLevel.GetTopLevel(this)?.StorageProvider;
                if (storageProvider != null)
                {
                    string rolPrefix = isVolunteer ? "Gonullu" : "Personel";
                    var fileSaveResult = await storageProvider.SaveFilePickerAsync(new Avalonia.Platform.Storage.FilePickerSaveOptions
                    {
                        Title = $"{rolPrefix} Takip Excel Raporunu Kaydet",
                        DefaultExtension = "xlsx",
                        SuggestedFileName = $"{user.AdSoyad.Replace(" ", "_")}_{rolPrefix}_Rapor_{year}_{month:D2}.xlsx",
                        FileTypeChoices = new[] { new Avalonia.Platform.Storage.FilePickerFileType("Excel Dosyası") { Patterns = new[] { "*.xlsx" } } }
                    });

                    if (fileSaveResult != null)
                    {
                        using var memoryStream = new System.IO.MemoryStream();
                        workbook.SaveAs(memoryStream);
                        memoryStream.Position = 0;

                        using var fileStream = await fileSaveResult.OpenWriteAsync();
                        await memoryStream.CopyToAsync(fileStream);

                        LblStatus.Text = $"✅ Excel raporu başarıyla kaydedildi: {fileSaveResult.Name}";
                        await ShowDialogAsync("Başarılı", "Veriler şablona başarıyla işlendi ve rapor kaydedildi.");
                    }
                    else LblStatus.Text = "Sistem Hazır";
                }
            }
            catch (Exception ex)
            {
                LblStatus.Text = "❌ Excel dışa aktarım hatası.";
                await ShowDialogAsync("Hata", "Excel dışa aktarım işlemi sırasında hata oluştu:\n" + ex.Message);
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