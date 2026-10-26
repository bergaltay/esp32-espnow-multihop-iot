# Node Sensör Bağlantı Kılavuzu

Platform: **ESP32-C6 Super Mini** (3.3 V logic, 3.3 V rail)

---

## Pin Özeti

| GPIO | Fonksiyon            | Sensör / Devre                      |
|------|----------------------|-------------------------------------|
| GP1  | ADC1_CH1             | Kapasitif toprak nem sensörü (AOUT) |
| GP2  | ADC1_CH2             | Batarya voltaj bölücü (orta nokta)  |
| GP3  | Dijital I/O          | DHT22 DATA                          |
| GP4  | 1-Wire               | DS18B20 DQ                          |
| GP5  | Dijital çıkış        | MOSFET gate                         |
| GP6  | Dijital giriş        | SW-520D (tilt switch)               |
| GP18 | I2C SDA              | BMP180 SDA                          |
| GP19 | I2C SCL              | BMP180 SCL                          |

**Kullanılmayacak pinler:** GP0 (boot), GP8 (WS2812), GP9 (BOOT), GP15 (onboard LED)

---

## 1. Güç Anahtarlama — MOSFET Devresi

DHT22, kapasitif nem ve DS18B20 sensörlerinin VCC hattı MOSFET ile kesilip açılır.
Bu, her uyku döngüsünde sensor akım tüketimini sıfırlar.

```
3.3V ──────────────────────────── Sensor VCC Rayı
                                         |
                              DHT22 VCC, Nem VCC, DS18B20 VCC

ESP32 GP5 ──[100 Ω]──── Gate
                         │        (N-channel logic-level MOSFET)
                        Drain ─── Sensor GND Rayı
                         │
                        Source ── GND (board)
```

- N-channel MOSFET önerileri: **2N7000**, **BSS138**, **IRLML2502**
- Gate direnci (100 Ω) ringing'i önler, zorunlu değil ama önerilir
- Firmware: `gpio_set_level(GP5, 1)` → MOSFET iletimde → sensörler çalışır
- BMP180 ve SW-520D MOSFET'e bağlı **değil**; her zaman beslenir

---

## 2. Kapasitif Toprak Nem Sensörü

```
Sensor VCC Rayı ──── VCC
GND Rayı        ──── GND
GP1             ──── AOUT
```

- Sensör çıkışı: kuru toprak → yüksek ADC (yaklaşık 3000), ıslak → düşük ADC
- MOSFET devredeyken okunur (GP5 = HIGH)
- ADC aralığı: 0–4095 (12-bit), attenuation DB_12 (0–3.3 V)

---

## 3. Batarya Voltaj Bölücü

4.2 V LiPo bataryayı ESP32-C6'nın 3.3 V ADC girişine indirger.

```
Batarya (+) ──┬──[10 kΩ]──── GP2 ──[27 kΩ]──── GND
              │
           (şarj devresi veya doğrudan LiPo +)
```

- Bölücü oranı: 27 / (10 + 27) = 0.730
- Formül (firmware): `battery_mv = adc_mv × 37 / 27`
- Ölçüm aralığı: 3.2 V (boş) – 4.2 V (dolu)
- GP5 = LOW iken de batarya okunabilir (MOSFET devre dışı)

---

## 4. DHT22 — Hava Sıcaklığı ve Nem

```
Sensor VCC Rayı ──── VCC (1. pin)
GP3             ──── DATA (2. pin) ──[4.7 kΩ]──── Sensor VCC Rayı
                     NC  (3. pin)
GND Rayı        ──── GND (4. pin)
```

- 4.7 kΩ pull-up **zorunlu**; aksi hâlde bit okuma başarısız olur
- Protokol: bit-bang, interrupt kapatılarak okunur (firmware içi)
- MOSFET devredeyken okunur (GP5 = HIGH)
- Ardışık okumalar arası minimum 2 saniye bırakın

---

## 5. DS18B20 — Toprak Sıcaklığı (1-Wire)

**Powered mode** kullanılır (parasite mode değil).

```
Sensor VCC Rayı ──── VDD (kırmızı)
GND Rayı        ──── GND (siyah)
GP4             ──── DQ  (sarı/beyaz) ──[4.7 kΩ]──── Sensor VCC Rayı
```

- 4.7 kΩ pull-up **zorunlu** (1-Wire bus gereksinimi)
- Dönüşüm süresi: 12-bit çözünürlükte 750 ms (firmware bu kadar bekler)
- MOSFET devredeyken okunur (GP5 = HIGH)
- Birden fazla DS18B20 bağlanacaksa ROM adresleme kodu güncellenmeli

---

## 6. BMP180 — Hava Basıncı ve Sıcaklık (I2C)

```
3.3V ──[4.7 kΩ]──┬──── GP18 (SDA)
                 └──── GP19 (SCL)

BMP180 VCC ──── 3.3V  (MOSFET'e bağlı değil — her zaman beslemeli)
BMP180 GND ──── GND
BMP180 SDA ──── GP18
BMP180 SCL ──── GP19
```

- I2C adresi: **0x77** (CSB = GND)
- Hız: 100 kHz (standart mode)
- Pull-up: firmware `flags.enable_internal_pullup = true` olarak ayarlı;
  harici 4.7 kΩ pull-up eklemek kararlılığı artırır (kablo uzunsa tercih edilir)
- MOSFET'e **bağlamayın** — güç kesilince kalibrasyon I2C bus'ı bloklayabilir

---

## 7. SW-520D — Eğim (Tilt) Sensörü

```
GP6  ──── Pin A
GND  ──── Pin B
```

- Firma kodu: `GPIO_PULLUP_ENABLE` (dahili pull-up aktif, harici direnç gerekmez)
- Normal/dik: iç top GND'ye bağlanmaz → GP6 = HIGH
- Eğik: top iki pini kısa devre yapar → GP6 = LOW → `FLAG_TILT_ALERT` set edilir
- SW-520D polaritesi yoktur; iki pinin hangisi GP6'ya, hangisi GND'ye gittiği farksız

---

## Tam Sistem Şeması

```
                       3.3V Rail
                          │
             ┌────────────┼───────────────────────┐
             │            │                       │
          BMP180        [4.7k]  [4.7k]           SW-520D
          VCC/SDA/SCL    │SDA    │SCL              │
          GP18──────────┘       └──────GP19        └──GP6
          GP19                                     └──GND

                       Sensor VCC Rayı  (MOSFET çıkışı)
                          │
             ┌────────────┼────────────┬───────────┐
             │            │            │           │
          DHT22         DHT22         Nem        DS18B20
          VCC           DATA          VCC         VDD
                     GP3──[4.7k]──VCC         GP4──[4.7k]──VCC
                                             DQ──GP4

                       Sensor GND Rayı
                          │
                        Drain
                          │ N-ch MOSFET
                        Gate ── GP5 (100Ω)
                          │
                        Source
                          │
                         GND

     GP2 ──[10k]──Batarya(+)    GP2──[27k]──GND   (voltaj bölücü)
```

---

## Kontrol Listesi

- [ ] 4.7 kΩ pull-up: DHT22 DATA
- [ ] 4.7 kΩ pull-up: DS18B20 DQ
- [ ] 4.7 kΩ pull-up (opsiyonel): BMP180 SDA ve SCL
- [ ] MOSFET source → board GND bağlı
- [ ] BMP180 VCC doğrudan 3.3V'a bağlı (MOSFET hattında değil)
- [ ] SW-520D herhangi bir pini GP6, diğeri GND
- [ ] Batarya bölücü dirençleri doğru sırada: 10 kΩ batarya tarafında, 27 kΩ GND tarafında
