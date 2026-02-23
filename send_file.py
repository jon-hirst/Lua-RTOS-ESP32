#!/usr/bin/env python3
import serial
import sys
import time

def send_file(port, filename, dest_path):
    ser = serial.Serial(port, 115200, timeout=3)
    time.sleep(0.5)

    # Send the io.receive() command to the device
    cmd = f'io.receive("{dest_path}")\r\n'
    ser.write(cmd.encode())
    print(f"Sent: {cmd.strip()}")

    # Wait for C\n prompt from device
    with open(filename, 'rb') as f:
        data = f.read()

    offset = 0
    chunk_num = 0
    while offset <= len(data):
        # Wait for C\n
        buf = b''
        deadline = time.time() + 5
        while time.time() < deadline:
            buf += ser.read(ser.in_waiting or 1)
            if b'C\n' in buf:
                break
        else:
            print("Timeout waiting for C\\n")
            return False

        # Send next chunk
        chunk = data[offset:offset+255]
        chunk_size = len(chunk)
        ser.write(bytes([chunk_size]))
        if chunk_size > 0:
            ser.write(chunk)
        offset += chunk_size
        chunk_num += 1
        print(f"Chunk {chunk_num}: {chunk_size} bytes")

        if chunk_size == 0:
            break

    print(f"Done. Sent {len(data)} bytes to {dest_path}")
    ser.close()
    return True

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <port> <local_file> <device_path>")
        print(f"  e.g.: {sys.argv[0]} /dev/ttyACM0 hello.lua /spiffs/hello.lua")
        sys.exit(1)
    send_file(sys.argv[1], sys.argv[2], sys.argv[3])



