import os
import time
import serial
import glob
import sys
import subprocess
import argparse
import configparser
from shutil import which

# -------------------- CONFIG --------------------
DEFAULT_PIO_DIR = ".pio/build"

# Map PlatformIO 'board' → teensy_loader_cli -mmcu flag
MMCU_MAP = {
    "teensy40": "TEENSY40",
    "teensy41": "TEENSY41",
    "teensy36": "TEENSY36",
    "teensy35": "TEENSY35",
    "teensy31": "TEENSY31",
    "teensy30": "TEENSY30",
}

def resolve_cli_path():
    # Prefer PlatformIO packaged CLI
    pio_cli = os.path.expanduser("~/.platformio/packages/tool-teensy/teensy_loader_cli")
    if os.path.exists(pio_cli):
        return pio_cli
    # Fallback to PATH
    path_cli = which("teensy_loader_cli")
    if path_cli:
        return path_cli
    return pio_cli  # default expectation

def find_builds(pio_dir=DEFAULT_PIO_DIR):
    pattern = os.path.join(pio_dir, "*", "firmware.hex")
    return glob.glob(pattern)

def parse_platformio_ini(path="platformio.ini"):
    cfg = configparser.ConfigParser()
    cfg.read(path)
    envs = {}
    for section in cfg.sections():
        if section.startswith("env:"):
            env = section.split(":", 1)[1]
            board = cfg.get(section, "board", fallback=None)
            envs[env] = {"board": board}
    return envs

def pick_env_from_builds(build_hexes):
    if len(build_hexes) == 1:
        hex_path = build_hexes[0]
        env = os.path.basename(os.path.dirname(hex_path))
        return env, hex_path
    elif len(build_hexes) > 1:
        print("[SELECT] Multiple build outputs found:")
        for i, hp in enumerate(build_hexes):
            env = os.path.basename(os.path.dirname(hp))
            print(f"  {i+1}. {env} -> {hp}")
        while True:
            sel = input(f"Select build (1-{len(build_hexes)}): ").strip()
            try:
                idx = int(sel) - 1
                if 0 <= idx < len(build_hexes):
                    hex_path = build_hexes[idx]
                    env = os.path.basename(os.path.dirname(hex_path))
                    return env, hex_path
            except Exception:
                pass
            print("[ERROR] Invalid selection")
    return None, None

def resolve_env_hex_and_mmcu(args):
    envs = parse_platformio_ini()
    hex_path = None
    env = args.env

    if not env:
        # Try to detect from .pio/build
        builds = find_builds()
        env, hex_path = pick_env_from_builds(builds)
    else:
        hex_path = os.path.join(DEFAULT_PIO_DIR, env, "firmware.hex")

    if not env:
        # Fallback to first env from platformio.ini
        if envs:
            env = next(iter(envs.keys()))
            hex_path = os.path.join(DEFAULT_PIO_DIR, env, "firmware.hex")

    # Determine board and mmcu
    board = args.board or (envs.get(env, {}).get("board") if env else None)
    mmcu = args.mmcu or (MMCU_MAP.get(board) if board else None)

    return env, hex_path, board, mmcu

parser = argparse.ArgumentParser(description="Teensy Upload Tool")
parser.add_argument("--env", help="PlatformIO environment name (e.g., teensy40)")
parser.add_argument("--mmcu", help="Override mmcu flag (e.g., TEENSY40)")
parser.add_argument("--board", help="Override PlatformIO board ID (e.g., teensy40)")
parser.add_argument("--hex", help="Path to firmware hex (overrides autodetect)")
parser.add_argument("--verbose", action="store_true", help="Enable verbose uploader output (-v)")
args = parser.parse_args()

env, hex_file, board, mmcu = resolve_env_hex_and_mmcu(args)
if args.hex:
    hex_file = args.hex
teensy_cli = resolve_cli_path()

# ------------------ CHECK HEX ------------------
if not hex_file or not os.path.exists(hex_file):
    print(f"[RESET] Error: firmware not found at {hex_file}")
    sys.exit(1)

# ------------------ CHECK CLI ------------------
if not os.path.exists(teensy_cli):
    print(f"[RESET] Error: teensy_loader_cli not found at {teensy_cli}")
    print("[RESET] Make sure Teensy platform is installed in PlatformIO")
    sys.exit(1)
if not mmcu:
    if env in MMCU_MAP:
        mmcu = MMCU_MAP[env]
    else:
        print(f"[RESET] Could not determine mmcu (env={env}, board={board}). Use --mmcu.")
        sys.exit(1)

# # ------------------ FIND PORT ------------------
# # Try both possible port patterns
# ports = glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*")
# if not ports:
#     print("[RESET] No Teensy serial port found!")
#     print("[RESET] Available ports:")
#     all_ports = glob.glob("/dev/cu.*") + glob.glob("/dev/tty.*")
#     for port in all_ports:
#         print(f"[RESET]   {port}")
#     print("[RESET] Make sure Teensy is connected and running your code")
#     sys.exit(1)

# ------------------ FIND PORT INCLUDING WINDOWS COM PORTS ------------------
# Try both possible port patterns
ports = glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*") + glob.glob("COM*")
if not ports:
    print("[RESET] No Teensy serial port found!")
    print("[RESET] Available ports:")
    all_ports = glob.glob("/dev/cu.*") + glob.glob("/dev/tty.*") + glob.glob("COM*")
    for port in all_ports:
        print(f"[RESET]   {port}")
    print("[RESET] Make sure Teensy is connected and running your code")
    sys.exit(1)

serial_port = ports[0]
print(f"[RESET] Using serial port: {serial_port}")

# ----------------- SEND REBOOT ------------------
try:
    print("[RESET] Connecting to Teensy...")
    with serial.Serial(port=serial_port, baudrate=115200, timeout=2) as ser:
        # Allow connection to stabilize
        time.sleep(0.5)
        
        # Clear any pending data
        ser.reset_input_buffer()
        
        print("[RESET] Sending REBOOT_BOOTLOADER command...")
        ser.write(b"REBOOT_BOOTLOADER\n")
        ser.flush()
        
        # Wait for response from Teensy
        response_received = False
        start_time = time.time()
        
        while time.time() - start_time < 3:
            if ser.in_waiting > 0:
                try:
                    response = ser.readline().decode('utf-8', errors='ignore').strip()
                    if response:
                        print(f"[RESET] Teensy: {response}")
                        if "Entering bootloader" in response or "REBOOT" in response:
                            response_received = True
                            break
                except Exception:
                    pass
            time.sleep(0.1)
        
        if response_received:
            print("[RESET] ✓ Reboot command confirmed")
        else:
            print("[RESET] ⚠ No response from Teensy (proceeding anyway)")
            
except Exception as e:
    print(f"[RESET] Failed to send reboot command: {e}")
    print("[RESET] ⚠ Proceeding with upload anyway...")

# Wait for bootloader to be ready (give it a bit more time)
print("[RESET] Waiting for bootloader to fully initialize May need to unplug and replug usb", end="", flush=True)
for i in range(50):  # 5 seconds worth of dots (0.1s each)
    time.sleep(0.1)
    print(".", end="", flush=True)

print(" ✓")
print("[RESET] Bootloader should now be ready for programming...")

# ------------------ UPLOAD HEX (WITH RETRY) ------------------
print(f"[UPLOAD] Uploading {hex_file} via teensy_loader_cli...")

for attempt in range(2):  # Try up to 2 times
    try:
        # Use subprocess for better error handling  
        cmd = [teensy_cli, f"-mmcu={mmcu}", "-w"]
        if args.verbose:
            cmd.append("-v")
        cmd.append(hex_file)
        print(f"[UPLOAD] Attempt {attempt + 1}: {' '.join(cmd)}")
        
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        
        if result.returncode == 0:
            print("[UPLOAD] ✓ Upload successful!")
            if result.stdout.strip():
                print(f"[UPLOAD] Output: {result.stdout.strip()}")
            break  # Success - exit retry loop
        else:
            print(f"[UPLOAD] ⚠ Attempt {attempt + 1} failed")
            if result.stderr.strip():
                print(f"[UPLOAD] Error: {result.stderr.strip()}")
            if result.stdout.strip():
                print(f"[UPLOAD] Output: {result.stdout.strip()}")
            if attempt == 0:  # First attempt failed
                print("[UPLOAD] This can be normal - trying again in 2 seconds...")
                time.sleep(2)  # Wait before retry
            else:  # Second attempt failed
                print("[UPLOAD] ✗ Both attempts failed!")
                print("[HINT] Try: close serial monitors, increase wait, use --verbose")
                print("[HINT] If stuck, press the Teensy button to force bootloader")
                sys.exit(1)
            
    except subprocess.TimeoutExpired:
        print(f"[UPLOAD] ✗ Attempt {attempt + 1} timed out")
        if attempt == 1:  # Last attempt
            sys.exit(1)
    except Exception as e:
        print(f"[UPLOAD] ✗ Attempt {attempt + 1} failed with exception: {e}")
        if attempt == 1:  # Last attempt
            sys.exit(1)

print("[UPLOAD] Success! Firmware uploaded successfully!")
