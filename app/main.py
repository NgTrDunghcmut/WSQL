import os
import json
import time
import paho.mqtt.client as mqtt
import clickhouse_connect
import traceback

# Cấu hình biến môi trường (Lấy từ Docker Compose)
MQTT_BROKER = os.getenv("MQTT_BROKER", "mosquitto")
MQTT_PORT = int(os.getenv("MQTT_PORT", 1883))
MQTT_TOPIC = os.getenv("MQTT_TOPIC", "data")

CLICKHOUSE_HOST = os.getenv("CLICKHOUSE_HOST", "clickhouse")
CLICKHOUSE_PORT = int(os.getenv("CLICKHOUSE_PORT", 8123))

# Khởi tạo kết nối ClickHouse với cơ chế retry khi khởi động container
ch_client = None
print(CLICKHOUSE_HOST)
while not ch_client:
    try:
        print('LONG gay')

        ch_client = clickhouse_connect.get_client(host=CLICKHOUSE_HOST, port=CLICKHOUSE_PORT, username='default', password='',secure=False,compress=False)
        print("[INIT] Kết nối ClickHouse thành công!")
    except Exception as e:
        traceback.print_exc()
        print(f"[INIT] Đang chờ ClickHouse khởi động... ({e})")
        time.sleep(3)
with open('/app/clickhouseinit.sql', 'r') as f:
    init_sql = f.read()
statements = [stmt.strip() for stmt in init_sql.split(';') if stmt.strip()]
for stmt in statements:
    try:
        ch_client.command(stmt)
    except Exception as e:
        print(f"[INIT] Lỗi khi thực hiện câu lệnh SQL: {e}")
# Logic Baseline và Phát hiện Cảnh báo (Task 3 & Task 4)
baselines = {}
ALPHA = 0.1
RSSI_THRESHOLD_DROP = 15.0

def update_baseline_and_detect(data):
    node = data['node']
    tech = data['technology']
    current_rssi = float(data['rssi'])
    current_snr = float(data['snr']) if data.get('snr') is not None else None
    
    key = f"{node}_{tech}"
    if key not in baselines:
        baselines[key] = {
            "rssi_base": current_rssi,
            "snr_base": current_snr if current_snr is not None else 0.0
        }
    else:
        old_rssi = baselines[key]["rssi_base"]
        baselines[key]["rssi_base"] = ALPHA * current_rssi + (1 - ALPHA) * old_rssi
        
        if current_snr is not None:
            old_snr = baselines[key]["snr_base"]
            baselines[key]["snr_base"] = ALPHA * current_snr + (1 - ALPHA) * old_snr

    base_rssi = baselines[key]["rssi_base"]
    drop_amount = base_rssi - current_rssi
    is_degraded = drop_amount >= RSSI_THRESHOLD_DROP
    
    if is_degraded:
        print(f"[ALERT] Cảnh báo suy giảm tín hiệu: Node={node}, Tech={tech}, Drop={drop_amount:.1f}dBm")
        
    return is_degraded

# Ghi dữ liệu vào ClickHouse (Task 2)
def insert_to_clickhouse(data):
    try:
        data_row = [
            data.get("node"),
            data.get("technology"),
            float(data.get("rssi", 0)),
            float(data["snr"]) if data.get("snr") is not None else None,
            int(data["jitter"]) if data.get("jitter") is not None else None,
            int(data["latency"]) if data.get("latency") is not None else None
        ]
        
        ch_client.insert(
            'embedded_db.sensor_measurements',
            [data_row],
            column_names=['node', 'technology', 'rssi', 'snr', 'jitter', 'latency']
        )
        print(f"[DB] Saved: Node={data.get('node')} | Tech={data.get('technology')}")
    except Exception as e:
        print(f"[ERROR DB] Lỗi ghi dữ liệu: {e}")

# Callbacks MQTT (Task 1)
def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode("utf-8")
        data = json.loads(payload)
        
        if all(k in data for k in ["node", "technology", "rssi"]):
            update_baseline_and_detect(data)
            insert_to_clickhouse(data)
    except Exception as e:
        print(f"[ERROR MQTT] Lỗi xử lý gói tin: {e}")

def main():
    client = mqtt.Client()
    client.on_message = on_message
    
    while True:
        try:
            client.connect(MQTT_BROKER, MQTT_PORT, 60)
            break
        except Exception as e:
            print(f"[INIT] Đang chờ Mosquitto MQTT Broker... ({e})")
            time.sleep(3)
            
    client.subscribe(MQTT_TOPIC)
    print(f"[READY] Đang lắng nghe MQTT Topic: {MQTT_TOPIC}")
    client.loop_forever()

if __name__ == "__main__":
    main()