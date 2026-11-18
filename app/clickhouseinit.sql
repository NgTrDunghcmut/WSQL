CREATE DATABASE IF NOT EXISTS embedded_db;

CREATE TABLE IF NOT EXISTS embedded_db.sensor_measurements
(
    timestamp DateTime DEFAULT now(),
    node String,
    technology Enum8('LoRa' = 1, 'Wi-Fi' = 2, 'BLE' = 3),
    rssi Float32,
    snr Nullable(Float32),
    jitter Nullable(UInt16),
    latency Nullable(UInt32)
)
ENGINE = MergeTree()
ORDER BY (technology, node, timestamp);