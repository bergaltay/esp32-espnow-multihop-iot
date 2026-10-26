import eventlet

eventlet.monkey_patch()

from flask import Flask, send_file
from flask_socketio import SocketIO
import time
import threading
import re
import random
import os

# static_folder='.' ve static_url_path='' sayesinde Flask,
# app.py'nin bulunduğu dizindeki .jsx dosyalarını otomatik olarak bulup sunabilecek.
app = Flask(__name__, static_folder='.', static_url_path='')
socketio = SocketIO(app, cors_allowed_origins="*")

LOG_PATTERN = re.compile(
    r"Node\s+(\d+)\s+soil=(\d+)\s+temp=([\d\.]+)°C\s+hum=(\d+)%\s+bat=(\d+)%\((\d+)mV\)\s+rssi=(-?\d+)dBm\s+hops=(\d+)\s+agg=(\d+)")


def read_serial_data():
    """
    Simüle edilmiş sensör verisi üreten döngü.
    Gerçek donanıma geçtiğinde burayı pyserial okumaları ile değiştirebilirsin.
    """
    while True:
        time.sleep(2)

        node_id = random.choice([1, 2])
        raw_line = f"[17:06:16] Node  {node_id}  soil={random.randint(2000, 4000)}  temp={random.uniform(21.0, 24.0):.1f}°C  hum={random.randint(60, 75)}%  bat=52%(3725mV)  rssi=-{random.randint(22, 50)}dBm  hops={node_id - 1}  agg=1"

        match = LOG_PATTERN.search(raw_line)
        if match:
            nid = int(match.group(1))
            soil_raw = int(match.group(2))
            temp = float(match.group(3))
            hum = int(match.group(4))
            bat_pct = int(match.group(5))
            bat_mv = int(match.group(6))
            rssi = int(match.group(7))
            hops = int(match.group(8))
            agg = int(match.group(9))

            # Kalibrasyon (4000 = %100)
            soil_pct = min((soil_raw / 4000) * 100, 100.0)

            # Uyarı bayrakları (Örnek algoritmalar)
            flags = {
                "frost": temp < 5.0,
                "brownout": bat_mv < 3300,
                "node_skip": hops == 0 and nid == 2
            }

            payload = {
                "node_id": nid,
                "timestamp_ms": int(time.time() * 1000),
                "soil_moisture": round(soil_pct, 1),
                "air_temp_c": temp,
                "air_humidity": hum,
                "battery_mv": bat_mv,
                "battery_pct": bat_pct,
                "rssi_dbm": rssi,
                "rx_rssi_dbm": rssi - random.randint(5, 10),
                "hop_count": hops,
                "agg_count": agg,
                "flags": flags,
                "receivedAt": int(time.time() * 1000)
            }

            print(f"Gönderiliyor: Node {nid} | N: {soil_pct:.1f}% | S: {temp}°C")
            socketio.emit('node_data', payload)


@app.route('/')
def index():
    """Ana dizindeki Dashboard.html dosyasını direkt renderla"""
    return send_file('Dashboard.html')


if __name__ == '__main__':
    t = threading.Thread(target=read_serial_data)
    t.daemon = True
    t.start()

    print("🚀 Dashboard başlatıldı! Tarayıcınızda şu adrese gidin: http://localhost:4120")
    # Portu 4120 olarak güncelledik
    socketio.run(app, host='0.0.0.0', port=4120, debug=True)
