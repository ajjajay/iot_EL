import math
import os
import time
from dataclasses import dataclass

SENSOR_MOCK = os.environ.get("SENSOR_MOCK", "0") == "1"


@dataclass
class SensorReading:
    temperature_c: float = 0.0
    humidity_pct: float  = 0.0
    light_raw: int       = 0
    light_norm: float    = 0.0
    valid: bool          = False
    timestamp_ms: float  = 0.0


class SensorManager:
    _MAX_RETRIES    = 3
    _RETRY_DELAY_S  = 0.1

    def __init__(self, dht_pin: int, ldr_spi_channel: int, ema_alpha: float = 0.3):
        self._dht_pin     = dht_pin
        self._ldr_channel = ldr_spi_channel
        self._ema_alpha   = ema_alpha
        self._last        = SensorReading()
        self._fail_count  = 0
        self._last_read_s = 0.0
        self._ema_temp    = 0.0
        self._ema_hum     = 0.0
        self._ema_light   = 0.0
        self._ema_init    = False
        self._dht         = None
        self._ldr         = None   # AnalogIn from adafruit_mcp3xxx

    def begin(self):
        if SENSOR_MOCK:
            print("[SENS] Mock mode active")
            return

        # DHT22 via adafruit-circuitpython-dht + blinka
        try:
            import adafruit_dht, board
            pin = getattr(board, f"D{self._dht_pin}")
            self._dht = adafruit_dht.DHT22(pin, use_pulseio=False)
            print(f"[SENS] DHT22 on GPIO {self._dht_pin}")
        except Exception as e:
            print(f"[SENS] DHT22 init failed: {e}")

        # LDR via MCP3008 SPI ADC (RPi has no onboard ADC)
        try:
            import busio, board, digitalio
            import adafruit_mcp3xxx.mcp3008 as MCP
            from adafruit_mcp3xxx.analog_in import AnalogIn
            spi = busio.SPI(clock=board.SCK, MISO=board.MISO, MOSI=board.MOSI)
            cs  = digitalio.DigitalInOut(board.CE0)
            mcp = MCP.MCP3008(spi, cs)
            self._ldr = AnalogIn(mcp, self._ldr_channel)
            print(f"[SENS] MCP3008 LDR on SPI ch{self._ldr_channel}")
        except Exception as e:
            print(f"[SENS] MCP3008 init failed (LDR will read 0): {e}")

    def read(self, interval_ms: float = 0) -> SensorReading:
        elapsed_s = time.monotonic() - self._last_read_s
        if interval_ms == 0 or elapsed_s >= interval_ms / 1000:
            return self.read_now()
        return self._last

    def read_now(self) -> SensorReading:
        if SENSOR_MOCK:
            t = time.monotonic()
            r = SensorReading(
                temperature_c = 25.0 + 10.0 * math.sin(t / 30.0),
                humidity_pct  = 55.0 + 20.0 * math.cos(t / 45.0),
                light_raw     = int(2048 + 2000 * math.sin(t / 20.0)),
                valid         = True,
                timestamp_ms  = time.time() * 1000,
            )
            r.light_norm   = r.light_raw / 4095.0
            self._last     = r
            self._fail_count = 0
            self._last_read_s = time.monotonic()
            return r

        for attempt in range(self._MAX_RETRIES):
            r = self._do_read()
            if r.valid:
                self._fail_count   = 0
                self._last         = r
                self._last_read_s  = time.monotonic()
                return r
            print(f"[SENS] Read attempt {attempt + 1}/{self._MAX_RETRIES} failed")
            time.sleep(self._RETRY_DELAY_S)

        self._fail_count += 1
        print(f"[SENS] All retries failed (total failures: {self._fail_count})")
        self._last.valid = False
        return self._last

    def last(self) -> SensorReading:
        return self._last

    def fail_count(self) -> int:
        return self._fail_count

    # ── Private ───────────────────────────────────────────────────────────────

    def _do_read(self) -> SensorReading:
        r = SensorReading(timestamp_ms=time.time() * 1000)
        if self._dht is None:
            return r
        try:
            temp = self._dht.temperature
            hum  = self._dht.humidity
        except Exception:
            return r
        if temp is None or hum is None:
            return r
        if not (-40.0 <= temp <= 80.0) or not (0.0 <= hum <= 100.0):
            return r

        # MCP3008 returns 16-bit scaled value (0–65535); normalise to 12-bit to match ESP32
        ldr_raw = 0
        if self._ldr is not None:
            try:
                ldr_raw = self._ldr.value >> 4   # 65535 → 4095
            except Exception:
                pass

        if not self._ema_init:
            self._ema_temp  = temp
            self._ema_hum   = hum
            self._ema_light = float(ldr_raw)
            self._ema_init  = True

        r.temperature_c = self._ema("temp",  temp)
        r.humidity_pct  = self._ema("hum",   hum)
        r.light_raw     = int(self._ema("light", float(ldr_raw)))
        r.light_norm    = r.light_raw / 4095.0
        r.valid         = True
        return r

    def _ema(self, key: str, new_val: float) -> float:
        α = self._ema_alpha
        if key == "temp":
            self._ema_temp  = α * new_val + (1 - α) * self._ema_temp
            return self._ema_temp
        if key == "hum":
            self._ema_hum   = α * new_val + (1 - α) * self._ema_hum
            return self._ema_hum
        self._ema_light = α * new_val + (1 - α) * self._ema_light
        return self._ema_light
