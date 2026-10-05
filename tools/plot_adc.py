#!/usr/bin/env python3
import argparse
from collections import deque

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import serial


def parse_data_line(line: str):
    # Expected firmware line: DATA,<ms>,<raw>,<filtered>
    if not line.startswith("DATA,"):
        return None

    parts = line.strip().split(",")
    if len(parts) != 4:
        return None

    try:
        t_ms = int(parts[1])
        raw = int(parts[2])
        filt = float(parts[3])
    except ValueError:
        return None

    return t_ms, raw, filt


def main():
    parser = argparse.ArgumentParser(description="Live plot ESP32 ADC data from serial")
    parser.add_argument("--port", required=True, help="Serial port, e.g. /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200, help="Baudrate (default: 115200)")
    parser.add_argument("--window", type=int, default=800, help="Samples shown in plot window")
    parser.add_argument(
        "--ymax-filtered",
        type=float,
        default=200.0,
        help="Fixed +/- range for filtered plot (default: 200)",
    )
    args = parser.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.05)

    t_values = deque(maxlen=args.window)
    raw_values = deque(maxlen=args.window)
    filt_values = deque(maxlen=args.window)

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 6), sharex=True)
    fig.suptitle("ESP32 ADC Live Plot")

    line_raw, = ax1.plot([], [], lw=1.0, label="Raw ADC (scaled)")
    line_filt, = ax2.plot([], [], lw=1.0, color="tab:orange", label="Filtered")

    ax1.set_ylabel("Raw (scaled)")
    ax2.set_ylabel("Filtered")
    ax2.set_xlabel("Time (s)")
    ax1.set_ylim(-args.ymax_filtered, args.ymax_filtered)
    ax2.set_ylim(-args.ymax_filtered, args.ymax_filtered)
    ax1.grid(True, alpha=0.3)
    ax2.grid(True, alpha=0.3)
    ax1.legend(loc="upper right")
    ax2.legend(loc="upper right")

    def update(_frame):
        while ser.in_waiting:
            line = ser.readline().decode(errors="ignore").strip()
            parsed = parse_data_line(line)
            if parsed is None:
                continue

            t_ms, raw, filt = parsed
            t_values.append(t_ms / 1000.0)
            raw_centered = raw - 2048
            raw_scaled = raw_centered * (args.ymax_filtered / 2048.0)
            raw_values.append(raw_scaled)
            filt_values.append(filt)

        if len(t_values) < 2:
            return line_raw, line_filt

        line_raw.set_data(t_values, raw_values)
        line_filt.set_data(t_values, filt_values)

        ax2.set_xlim(t_values[0], t_values[-1])

        return line_raw, line_filt

    anim = FuncAnimation(fig, update, interval=50, blit=False, cache_frame_data=False)

    try:
        plt.tight_layout()
        plt.show()
    finally:
        ser.close()


if __name__ == "__main__":
    main()
