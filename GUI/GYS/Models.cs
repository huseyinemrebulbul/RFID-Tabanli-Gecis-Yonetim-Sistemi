#nullable disable
using Postgrest.Attributes;
using Postgrest.Models;
using System;

namespace IpekYoluGYS
{
    /// <summary>
    /// Supabase üzerindeki "personel" tablosunun C# temsilidir.
    /// "BaseModel" den kalıtım alarak Supabase SDK'sının CRUD işlemlerini anlamasını sağlar.
    /// </summary>
    [Table("personel")]
    public class Personel : BaseModel
    {
        [Column("uid")] public string Uid { get; set; }
        [Column("ad_soyad")] public string AdSoyad { get; set; }
        [Column("TC")] public string TC { get; set; }
        
        // Cihaz tarafından anlık durum takibi (Giriş yaptı/Çıkış yaptı) için flag
        [Column("iceride_mi")] public bool IcerideMi { get; set; }
    }

    /// <summary>
    /// Gönüllüler için tasarlanan tablo. Personel ile yapısı aynıdır ancak
    /// istatistiksel ve organizasyonel filtrelemeler için ayrı bir tabloda tutulmuştur.
    /// </summary>
    [Table("Gonullu")]
    public class Gonullu : BaseModel
    {
        [Column("uid")] public string Uid { get; set; }
        [Column("ad_soyad")] public string AdSoyad { get; set; }
        [Column("TC")] public string TC { get; set; }
        [Column("iceride_mi")] public bool IcerideMi { get; set; }
    }

    /// <summary>
    /// Geçiş hareketlerinin tutulduğu ana log tablosudur.
    /// </summary>
    [Table("hareketler")]
    public class Hareket : BaseModel
    {
        [PrimaryKey("id", false)] public int? Id { get; set; }
        [Column("uid")] public string Uid { get; set; }
        
        // HAYAT KURTARAN DÜZELTME: DateTime yerine string yapıldı.
        // Neden? Çünkü ESP32-S3'teki C++ NTP kütüphanesi zamanı basit string gönderir. 
        // Supabase tarafında olası saat dilimi uyuşmazlıkları (UTC vs Local) veya dönüşüm hatalarını 
        // önlemek için bu değişken string olarak tutulur, analizler tarafında DateTime.Parse kullanılır.
        [Column("zaman")] public string Zaman { get; set; } 
        
        [Column("islem_tipi")] public string IslemTipi { get; set; }
        
        // Log manuel düzeltildiyse hangi masaüstü PC'den yapıldığının takibi
        [Column("guncelleyen_pc")] public string GuncelleyenPc { get; set; }
    }

    /// <summary>
    /// Cihazdan okutulan ancak sistemde kayıtlı olmayan (tanımsız) kartların düşeceği tablo.
    /// Arayüzdeki "Çek" butonu bu tablodaki ID=1 satırını okur.
    /// </summary>
    [Table("anlik_kart")]
    public class AnlikKart : BaseModel
    {
        [PrimaryKey("id", false)] public int? Id { get; set; }
        [Column("uid")] public string Uid { get; set; }
        [Column("zaman")] public string Zaman { get; set; } 
    }
    
    [Table("izinler")]
    public class Izin : BaseModel
    {
        [PrimaryKey("id", false)] public int? Id { get; set; }
        [Column("uid")] public string Uid { get; set; }
        [Column("ad_soyad")] public string AdSoyad { get; set; }
        [Column("baslangic_tarihi")] public DateTime BaslangicTarihi { get; set; }
        [Column("bitis_tarihi")] public DateTime BitisTarihi { get; set; }
        [Column("aciklama")] public string Aciklama { get; set; }
    }
}