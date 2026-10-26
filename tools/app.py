import eventlet

eventlet.monkey_patch()

from flask import Flask, send_file, send_from_directory
from flask_socketio import SocketIO
import serial
import time
import threading
import json
import glob
import sys

app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*")


# ── macOS İçin Port Otomatik Bulma ────────────────────────────────────────────
def find_serial_ports():
    patterns = [
        "/dev/cu.usbserial*",
        "/dev/cu.SLAB_USBtoUART*",
        "/dev/cu.wchusbserial*",
        "/dev/cu.usbmodem*",
    ]
    ports = []
    for p in patterns:
        ports.extend(glob.glob(p))
    return sorted(ports)


# ── Seri Port Okuma ve WebSocket Yayın Döngüsü ────────────────────────────────
def read_serial_and_emit():
    ser = None
    baud_rate = 115200

    while True:
        # 1. Seri porta bağlı değilsek, cihazı ara
        if not ser:
            ports = find_serial_ports()
            if ports:
                port = ports[0]
                print(f"\n🔌 macOS Seri Port Bulundu: {port}. Bağlanılıyor...")
                try:
                    ser = serial.Serial(port, baud_rate, timeout=1)
                    print("✅ Bağlantı başarılı! Veriler bekleniyor...\n")
                except serial.SerialException as e:
                    print(f"⚠️ Bağlantı hatası ({port}): {e}. Tekrar deneniyor...")
                    time.sleep(2)
                    continue
            else:
                time.sleep(2)
                continue

        # 2. Bağlıysak verileri oku
        try:
            raw = ser.readline()
            if not raw:
                continue

            # Karakter setini esnek bir şekilde çöz
            line = raw.decode("utf-8", errors="replace").strip()

            # ANSI kodları veya başka prefixler olsa bile "DATA:{" kısmını yakala
            if "DATA:{" in line:
                # "DATA:" yazısını atlayıp sadece JSON "{...}" kısmını al
                start_idx = line.find("DATA:{") + 5
                json_str = line[start_idx:]

                # Sonda olabilecek çöp karakterleri/boşlukları temizle
                end_idx = json_str.rfind("}") + 1
                if end_idx > 0:
                    json_str = json_str[:end_idx]

                try:
                    payload = json.loads(json_str)

                    # --- GEÇİCİ KALİBRASYON (Toprak Nemi) ---
                    # 4000 = %100 mantığına göre ADC değerini % çevrimi
                    if "soil_moisture" in payload and payload["soil_moisture"] > 100:
                        payload["soil_moisture"] = round(min((payload["soil_moisture"] / 4000) * 100, 100.0), 1)

                    # Arayüzdeki "X saniye önce" için zaman damgası
                    payload["receivedAt"] = int(time.time() * 1000)

                    node_id = payload.get("node_id", "?")
                    print(
                        f"📡 İletiliyor -> Node {node_id} | Nem: %{payload.get('soil_moisture')} | Sıcaklık: {payload.get('air_temp_c')}°C")

                    # React Dashboard'a gönder
                    socketio.emit('node_data', payload)

                except json.JSONDecodeError as e:
                    print(f"⚠️ JSON Ayrıştırma Hatası: {e} \nKusurlu Veri: {json_str}")

        except serial.SerialException:
            print("\n❌ Seri port bağlantısı koptu! USB kablosu çıkmış olabilir. Yeniden aranıyor...")
            if ser:
                ser.close()
            ser = None
        except Exception as e:
            print(f"Bilinmeyen Hata: {e}")
            time.sleep(1)


# ── Flask Rotaları (Statik Dosyaları Sunmak İçin) ─────────────────────────────
@app.route('/')
def index():
    return send_file('Dashboard.html')


@app.route('/<path:filename>')
def serve_static(filename):
    return send_from_directory('.', filename)


if __name__ == '__main__':
    # Seri port okuma işlemini arka planda (Daemon Thread) başlat
    t = threading.Thread(target=read_serial_and_emit)
    t.daemon = True
    t.start()

    print("🚀 Sunucu başlatıldı! Dashboard için Safari veya Chrome'da şu adrese gidin:")
    print("👉 http://localhost:4120\n")

    # Sunucuyu çalıştır
    socketio.run(app, host='0.0.0.0', port=4120, debug=False, use_reloader=False)
