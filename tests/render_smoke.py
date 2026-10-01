"""Exercise the actual packaged host; isolate user data and retain QA captures.

Windows sends input to the exact child SDL window. Other platforms capture the
front without synthetic native input; model tests cover the backend-neutral loop.
"""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time


def bitmap(path):
    data = path.read_bytes()
    if data[:2] != b"BM" or len(data) < 54:
        raise RuntimeError("No valid host screenshot")
    offset = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    if w != 900 or abs(h) != 760:
        raise RuntimeError("Wrong screenshot dimensions")
    return data[offset:]


def find_window(pid):
    user = ctypes.WinDLL("user32", use_last_error=True)
    # Match SDL's physical-pixel coordinates on scaled Windows displays.
    user.SetProcessDpiAwarenessContext.argtypes = [wintypes.HANDLE]
    user.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user.PostMessageW.restype = wintypes.BOOL
    user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user.SendMessageW.restype = wintypes.LPARAM
    user.GetCursorPos.argtypes = [ctypes.POINTER(wintypes.POINT)]
    user.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
    window = []
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    @callback_type
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(256)
        user.GetClassNameW(hwnd, name, 256)
        if owner.value == pid and "SDL" in name.value:
            window.append(hwnd)
        return True
    user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    for _ in range(100):
        user.EnumWindows(visit, 0)
        if window:
            return user, window[0]
        time.sleep(0.05)
    raise RuntimeError("Child SDL window did not appear")


def capture(exe, out, name, env, keys=()):
    path = out / (name + ".bmp")
    process = subprocess.Popen([str(exe), "--plugin", "dev.draxul.flashcards",
        "--screenshot", str(path), "--screenshot-size", "900x760",
        "--screenshot-delay", "3500", "--log-file", str(out / (name + ".log"))],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        if keys and sys.platform == "win32":
            user, hwnd = find_window(process.pid)
            log = out / (name + ".log")
            for _ in range(140):
                if log.exists() and "Flashcards frame ready" in log.read_text(errors="replace"):
                    break
                if process.poll() is not None:
                    raise RuntimeError("Host exited before its first product frame")
                time.sleep(0.05)
            else:
                raise RuntimeError("Host did not render a ready product frame")
            for key in keys:
                if isinstance(key, tuple):
                    _, x, y = key
                    position = (y << 16) | x
                    old = wintypes.POINT()
                    target = wintypes.POINT(x, y)
                    user.GetCursorPos(ctypes.byref(old))
                    user.ClientToScreen(hwnd, ctypes.byref(target))
                    try:
                        user.SetCursorPos(target.x, target.y)
                        user.SendMessageW(hwnd, 0x200, 0, position)
                        time.sleep(0.1)
                        user.SendMessageW(hwnd, 0x201, 1, position)
                        time.sleep(0.12)
                        user.SendMessageW(hwnd, 0x202, 0, position)
                        time.sleep(0.12)
                        # The first synthetic click establishes SDL's mouse
                        # focus for a capture window; the second tests its button.
                        user.SendMessageW(hwnd, 0x201, 1, position)
                        time.sleep(0.12)
                        user.SendMessageW(hwnd, 0x202, 0, position)
                        time.sleep(0.12)
                    finally:
                        current = wintypes.POINT()
                        user.GetCursorPos(ctypes.byref(current))
                        if current.x == target.x and current.y == target.y:
                            user.SetCursorPos(old.x, old.y)
                    continue
                scan = user.MapVirtualKeyW(key, 0)
                if not user.PostMessageW(hwnd, 0x100, key, 1 | (scan << 16)):
                    raise RuntimeError("Could not send key press")
                time.sleep(0.12)
                user.PostMessageW(hwnd, 0x101, key, 1 | (scan << 16) | (1 << 30) | (1 << 31))
                time.sleep(0.12)
        process.wait(timeout=20)
        if process.returncode:
            raise RuntimeError("Host failed; inspect the retained " + name + ".log")
        return bitmap(path)
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate(timeout=5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="draxul-flashcards-review-") as temporary:
        env = dict(os.environ)
        if sys.platform == "win32":
            env["APPDATA"] = temporary
            env["LOCALAPPDATA"] = temporary
        front = capture(args.exe, args.out, "front", env)
        if len(set(front)) < 32:
            raise RuntimeError("Host is blank")
        if sys.platform != "win32":
            print("Front rendered; native keyboard lifecycle requires Windows in this harness.")
            return
        back = capture(args.exe, args.out, "back", env, [32])
        changed = sum(a != b for a, b in zip(front, back))
        if changed < 5000:
            raise RuntimeError("Flipping did not reveal the image/grade controls")
        capture(args.exe, args.out, "graded", env, [32, ord("2")])
        files = list(Path(temporary).rglob("recall-v1.json"))
        if len(files) != 1:
            raise RuntimeError("Grade did not reach actual SDK persistent storage")
        saved = files[0].read_bytes()
        state = json.loads(saved)
        if len(state["cards"]) != 1:
            raise RuntimeError("Expected exactly one explicitly graded card")
        record = next(iter(state["cards"].values()))
        if record["reviews"] != 1 or record["remembered"] != 1 or record["stage"] != 1:
            raise RuntimeError("Actual grade state is wrong")
        reopened = capture(args.exe, args.out, "reopened", env)
        if files[0].read_bytes() != saved:
            raise RuntimeError("Opening the host rewrote review progress")
        if sum(a != b for a, b in zip(front, reopened)) < 300:
            raise RuntimeError("Reopened host ignored the saved schedule")
        # Second fixture/current-journal card: actual mouse Forgot button.
        capture(args.exe, args.out, "mouse-graded", env, [32, ("click", 200, 545)])
        state = json.loads(files[0].read_bytes())
        if len(state["cards"]) != 2 or sum(p["forgotten"] for p in state["cards"].values()) != 1:
            raise RuntimeError("Mouse grade did not persist the second card")
        saved = files[0].read_bytes()
        capture(args.exe, args.out, "complete", env)
        if files[0].read_bytes() != saved:
            raise RuntimeError("Completion/reopen rewrote progress")
        files[0].write_text('{"schema_version":99,"cards":{}}')
        corrupt = files[0].read_bytes()
        capture(args.exe, args.out, "corrupt", env, [32, ord("2")])
        if files[0].read_bytes() != corrupt:
            raise RuntimeError("Corrupt progress was overwritten")
        print("Packaged host front/back, keyboard and mouse grades, reopen, completion and corrupt-state checks passed.")


if __name__ == "__main__":
    main()
