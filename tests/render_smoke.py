"""Exercise the actual packaged host; isolate user data and retain QA captures.

Windows sends input to the exact child SDL window. Other platforms capture the
front without synthetic native input; model tests cover the backend-neutral loop.
"""
import argparse
import importlib.util
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


def bitmap(path, dimensions=(900, 760)):
    data = path.read_bytes()
    if data[:2] != b"BM" or len(data) < 54:
        raise RuntimeError("No valid host screenshot")
    offset = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    if (w, abs(h)) != dimensions:
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


def capture(exe, out, name, env, keys=(), delay=5500, config=None, settle_flip=True, dimensions=(900, 760)):
    path = out / (name + ".bmp")
    log = out / (name + ".log")
    # A previous run's readiness/reveal diagnostics must never satisfy this
    # child process's input barriers before it has rendered its first frame.
    log.unlink(missing_ok=True)
    path.unlink(missing_ok=True)
    command = [str(exe), "--plugin", "dev.draxul.flashcards",
        "--screenshot", str(path), "--screenshot-size", f"{dimensions[0]}x{dimensions[1]}",
        "--screenshot-delay", str(delay), "--log-file", str(out / (name + ".log"))]
    if config is not None:
        command += ["--plugin-config", json.dumps(config)]
    process = subprocess.Popen(command,
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        if keys and sys.platform == "win32":
            user, hwnd = find_window(process.pid)
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
                if key == 32 and settle_flip:
                    # A posted key and a wall-clock delay do not establish that
                    # the host processed the event or finished its animation.
                    # Wait for the actual reveal before issuing a grade/replay.
                    for _ in range(80):
                        contents = log.read_text(errors="replace")
                        if "Flashcards reveal complete" in contents:
                            break
                        if "action flip revealed=0 image=0" in contents:
                            break # Empty/corrupt state has no card to reveal.
                        if process.poll() is not None:
                            raise RuntimeError("Host exited before reveal completed")
                        time.sleep(0.05)
                    else:
                        raise RuntimeError("Host did not finish its card reveal")
        process.wait(timeout=20)
        if process.returncode:
            raise RuntimeError("Host failed; inspect the retained " + name + ".log")
        return bitmap(path, dimensions)
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate(timeout=5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="draxul-flashcards-review-") as temporary:
        env = dict(os.environ)
        if sys.platform == "win32":
            env["APPDATA"] = temporary
            env["LOCALAPPDATA"] = temporary
            directory = Path(temporary) / "draxul/plugin-config/dev.draxul.flashcards/state"
            directory.mkdir(parents=True)
            (directory / "cue-guide-v1.json").write_text('{"schema_version":1,"acknowledged":true}')
        env["SDL_AUDIODRIVER"] = "dummy" # Queue verification without test noise.
        front = capture(args.exe, args.out, "front", env, [ord("R"), ord("2")])
        if len(set(front)) < 32:
            raise RuntimeError("Host is blank")
        if sys.platform != "win32":
            print("Front rendered; native keyboard lifecycle requires Windows in this harness.")
            return
        def queued(name):
            return (args.out / (name + ".log")).read_text(errors="replace").count("pronunciation queued after reveal")
        if queued("front") or list(Path(temporary).rglob("recall-v1.json")):
            raise RuntimeError("Front input leaked pronunciation or invented a grade")
        middle = capture(args.exe, args.out, "midflip", env, [32, ord("2"), ord("R")],
                         delay=1100, config={"flip_duration_ms": 3000}, settle_flip=False)
        if queued("midflip") or list(Path(temporary).rglob("recall-v1.json")):
            raise RuntimeError("Audio or grading ran before the turn completed")
        if sum(a != b for a, b in zip(front, middle)) < 5000:
            raise RuntimeError("Midflip did not draw a turning card")
        back = capture(args.exe, args.out, "back", env, [32, ord("R")])
        if queued("back") != 2:
            raise RuntimeError("Reveal and replay did not queue cached pronunciation")
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
        recognition_key = next(iter(state["cards"]))
        if not recognition_key.startswith("recognition:"):
            raise RuntimeError("First grade was not recognition")
        if record["reviews"] != 1 or record["remembered"] != 1 or record["stage"] != 1:
            raise RuntimeError("Actual grade state is wrong")
        reopened = capture(args.exe, args.out, "reopened", env)
        if files[0].read_bytes() != saved:
            raise RuntimeError("Opening the host rewrote review progress")
        if sum(a != b for a, b in zip(front, reopened)) < 300:
            raise RuntimeError("Reopened host ignored the saved schedule")
        if queued("reopened"):
            raise RuntimeError("Reopened front played its answer")
        # Simulate the already-tested ten-minute expiry without sleeping. The
        # actual opposite direction must remain independently ungraded.
        state["cards"][recognition_key]["last_reviewed"] -= 601
        record = state["cards"][recognition_key]
        spec = importlib.util.spec_from_file_location("embed_deck", Path(__file__).resolve().parents[1] / "tools/embed_deck.py")
        embed = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(embed)
        deck = embed.build_deck(args.source, Path(__file__).resolve().parents[1] / "assets/artwork.json")["cards"]
        # Other new words would otherwise precede this newly eligible opposite
        # direction. Seed them in the future in this isolated face fixture.
        others = {direction + ":" + card["id"]: {
            "stage": 0, "due": int(time.time()) + 86400, "last_reviewed": 0,
            "reviews": 0, "remembered": 0, "forgotten": 0, "assisted": 0}
            for card in deck if "recognition:" + card["id"] != recognition_key
            for direction in ("recognition", "production")}
        state["cards"].update(others)
        files[0].write_text(json.dumps(state))
        capture(args.exe, args.out, "production-back", env, [32])
        # The other direction of the same word: actual mouse Again button.
        capture(args.exe, args.out, "mouse-graded", env, [32, ("click", 440, 635)])
        state = json.loads(files[0].read_bytes())
        if len(state["cards"]) != len(others) + 2 or sum(p["forgotten"] for p in state["cards"].values()) != 1:
            raise RuntimeError("Mouse grade did not persist the second card")
        production_key = recognition_key.replace("recognition:", "production:", 1)
        if production_key not in state["cards"] or state["cards"][recognition_key] != record:
            raise RuntimeError("Directions did not retain independent progress")
        if any(state["cards"][key] != value for key, value in others.items()):
            raise RuntimeError("Opposite grade changed another word's schedule")
        saved = files[0].read_bytes()
        capture(args.exe, args.out, "complete", env)
        if files[0].read_bytes() != saved:
            raise RuntimeError("Completion/reopen rewrote progress")
        files[0].write_text('{"schema_version":99,"cards":{}}')
        corrupt = files[0].read_bytes()
        capture(args.exe, args.out, "corrupt", env, [32, ord("2")])
        if files[0].read_bytes() != corrupt:
            raise RuntimeError("Corrupt progress was overwritten")
        print("Packaged host front/midflip/back, reveal-only audio/replay, independent directions, keyboard/mouse grades and corrupt-state checks passed.")


if __name__ == "__main__":
    main()
