import asyncio
import threading
import time
from collections import deque

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from bleak import BleakClient, BleakScanner

DEVICE_NAME = "Decibel_Devils"
SOUND_UUID = "e9ea0001-e19b-0065-02df-c7907585fc48"
WINDOW_SECONDS = 30
Y_MIN, Y_MAX = -10, 50
TARGET_LOW, TARGET_HIGH = 10, 20
readings = deque(maxlen=1000)

def on_data(_, data):
    db = int.from_bytes(data, "little", signed=True) / 10
    readings.append((time.monotonic(), db))

async def ble_task():
    print(f"Scanning for {DEVICE_NAME}...")
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=15)
    if device is None:
        print("Board not found. Is it powered, advertising, and not "
              "connected to your phone?")
        return

    async with BleakClient(device) as client:
        print("Connected. Close the graph window to quit.")
        await client.start_notify(SOUND_UUID, on_data)
        while client.is_connected:
            await asyncio.sleep(0.5)

    print("Disconnected.")

def update(_):
    now = time.monotonic()
    points = [(t - now, db) for t, db in list(readings)
              if now - t <= WINDOW_SECONDS]

    if points:
        xs, ys = zip(*points)
        line.set_data(xs, ys)
        ax.set_title(f"{ys[-1]:.1f} dB")

threading.Thread(target=lambda: asyncio.run(ble_task()), daemon=True).start()
fig, ax = plt.subplots()
(line,) = ax.plot([], [], linewidth=2)
ax.axhspan(TARGET_LOW, TARGET_HIGH, color="green", alpha=0.2)
ax.set_xlim(-WINDOW_SECONDS, 0)
ax.set_ylim(Y_MIN, Y_MAX)
ax.set_xlabel("Seconds ago")
ax.set_ylabel("Level (dB vs. quiet room)")
anim = FuncAnimation(fig, update, interval=100, cache_frame_data=False)
plt.show()
