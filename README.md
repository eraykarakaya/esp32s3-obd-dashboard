# 🚗 Jetta 1.4 TSI — OBD2 Sportif Dashboard

ESP32-S3 tabanlı, araç içi OBD2 dashboard projesi. Vgate iCar Pro BLE adaptörü üzerinden araç verilerini okuyarak 5 inç dokunmatik ekranda gerçek zamanlı gösterir.

---

## 📸 Ekran Görüntüsü

> *(Yakında eklenecek)*

---

## 🔧 Donanım

| Parça | Model |
|-------|-------|
| Mikrodenetleyici | Waveshare ESP32-S3 Touch LCD 5" |
| Ekran | 1024×600 RGB Paralel Panel |
| Dokunmatik | TAMC GT911 Kapasitif |
| OBD2 Adaptör | Vgate iCar Pro (BLE) |
| Araç | VW Jetta 1.4 TSI (7 Vites DSG) |

---

## ✨ Özellikler

- **Hız Göstergesi** — 0–240 km/h yay ark, tepe hız göstergesi
- **Turbo Basınç** — Motor yüküne göre hesaplanan boost (renk bölgeli: yeşil / turuncu / kırmızı)
- **Devir Saati** — Analog iğneli, 0–8000 RPM
- **Shift Light** — Üst bar, RPM'e göre renk geçişli (yeşil → sarı → kırmızı)
- **Motor Yükü** — Alt çubukta animasyonlu bar
- **Sport Modu** — Hız >80 km/h veya RPM >3200 olduğunda otomatik aktif, kırmızı flash animasyonu
- **0-100 Zamanlayıcı** — Otomatik algılar, en iyi süreyi kaydeder
- **HP Tahmini** — RPM ve motor yüküne göre anlık güç hesabı
- **BLE Otomatik Bağlantı** — Adaptörü arar, bağlanır, kesilince tekrar dener

---

## 📚 Kullanılan Kütüphaneler

- [LVGL v8](https://lvgl.io) — UI framework
- [Arduino_GFX_Library](https://github.com/moononournation/Arduino_GFX) — RGB panel sürücüsü
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) — BLE
- [TAMC_GT911](https://github.com/TAMCTec/gt911-arduino) — Dokunmatik

---

## ⚡ Performans Optimizasyonları

- `full_refresh = 1` + tam PSRAM frame buffer (1024×600×2 byte) → tearing yok
- Piksel saati 16MHz → 12MHz → araçta elektriksel gürültüye dayanıklı
- Shift light: 20 ayrı obje yerine tek `lv_canvas` → çok daha hızlı render
- Arc güncelleme: 40ms / Label güncelleme: 150ms (ayrı döngüler)
- BLE görevi Core 0'a sabitlenmiş, UI Core 1'de

---

## 🚀 Kurulum

1. [PlatformIO](https://platformio.org) kur
2. Repoyu klonla:
   ```bash
   git clone https://github.com/eraykarakaya/esp32s3-obd-dashboard.git
   ```
3. `platformio.ini` ayarlarını kontrol et (PSRAM etkin olmalı)
4. ESP32-S3'ü bağla, **Upload** tıkla
5. Vgate iCar Pro adaptörünü araca tak, cihazı başlat

---

## 📝 Notlar

- Turbo basıncı gerçek MAP sensörü yerine motor yüküne göre tahmin edilir
- HP tahmini yaklaşıktır (stok 1.4 TSI = ~122 HP referans alınır)
- Araçta titreme varsa 5V hattına **1000–2200µF** elektrolitik kapasitör önerilir

---

## 👤 Geliştirici

**Eray Karakaya** — [github.com/eraykarakaya](https://github.com/eraykarakaya)
