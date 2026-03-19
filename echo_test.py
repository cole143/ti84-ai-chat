"""Echo test — verify serial connection between PC and TI-84 CE.
Run this BEFORE bridge.py to confirm the USB CDC link works.
Usage: python echo_test.py [COM_PORT]
"""
import sys
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM3"
BAUD = 9600

print(f"Echo test on {PORT}. Waiting for calc...")
print("Type something on your calculator and press ENTER.")
print("Press Ctrl+C to quit.\n")

try:
    with serial.Serial(PORT, BAUD, timeout=10) as ser:
        while True:
            line = ser.readline().decode("ascii", errors="replace").strip()
            if line:
                print(f"Received: {line}")
                ser.write((line + "\n").encode("ascii"))
                print(f"Echoed back: {line}")
except KeyboardInterrupt:
    print("\nDone.")
except serial.SerialException as e:
    print(f"Serial error: {e}")
    print("Make sure TI Connect CE is closed and the calc is running prgmAICHAT.")
