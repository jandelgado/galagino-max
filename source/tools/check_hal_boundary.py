#!/usr/bin/env python3
"""Fail if code outside src/hal/ uses an SDK or Arduino API.

Usage: check_hal_boundary.py [--root SRC_DIR] [--selftest]
Prints file:line for each hit, exits 1 if there is any.
"""
import argparse
import os
import re
import sys
import tempfile

_EXEMPT_DIRS = ("hal",)
_EXTENSIONS = (".c", ".cpp", ".h", ".hpp")


_INCLUDE = re.compile(
    r'#\s*include\s*[<"]('
    r'Arduino\.h|Esp\.h|FastLED\.h|Wire\.h|FreeRTOS\.h|WiFi[\w.]*|'
    r'esp_[\w/.]*|driver/[\w/.]*|freertos/[\w/.]*|soc/[\w/.]*|rom/[\w/.]*|'
    r'mbedtls/[\w/.]*|hardware/[\w/.]*|pico/[\w/.]*)[>"]'
)
_SYMBOL = re.compile(
    r'(?<![\w:.>])('
    r'millis|micros|delay|delayMicroseconds|pinMode|digitalRead|digitalWrite|'
    r'vTaskDelay|vTaskDelete|xTaskCreate\w*|xTaskNotify\w*|ulTaskNotifyTake|'
    r'xPortGetCoreID|heap_caps_\w+)\s*\(|'
    r'(?<![\w:.>])(Serial|ESP)\.|'
    r'\b(IRAM_ATTR|DRAM_ATTR)\b'
)


def _strip_comment(line):
    idx = line.find("//")
    return line if idx < 0 else line[:idx]


def _scan_file(path):
    hits = []
    in_block = False
    with open(path, encoding="utf-8", errors="replace") as f:
        for number, raw in enumerate(f, 1):
            line = raw
            if in_block:
                end = line.find("*/")
                if end < 0:
                    continue
                line = line[end + 2:]
                in_block = False
            while "/*" in line:
                start = line.find("/*")
                end = line.find("*/", start + 2)
                if end < 0:
                    line = line[:start]
                    in_block = True
                    break
                line = line[:start] + " " + line[end + 2:]
            line = _strip_comment(line)
            if _INCLUDE.search(line) or _SYMBOL.search(line):
                hits.append((number, raw.rstrip()))
    return hits


def _scan(root):
    result = []
    for dirpath, dirnames, filenames in os.walk(root):
        rel = os.path.relpath(dirpath, root)
        if rel == ".":
            dirnames[:] = [d for d in dirnames if d not in _EXEMPT_DIRS]
        for name in sorted(filenames):
            if not name.endswith(_EXTENSIONS):
                continue
            path = os.path.join(dirpath, name)
            relpath = os.path.relpath(path, root)
            for number, text in _scan_file(path):
                result.append((relpath, number, text))
    return result


def _write(root, relpath, text):
    path = os.path.join(root, relpath)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)


def _selftest():
    with tempfile.TemporaryDirectory() as clean, tempfile.TemporaryDirectory() as dirty:
        _write(clean, "a.cpp", '#include "hal/hal.h"\nuint32_t t = hal::millis();\n// delay(5)\n/* digitalRead(1) */\n')
        _write(clean, "hal/esp/x.cpp", "#include <driver/gpio.h>\nvoid f() { delay(1); }\n")
        assert _scan(clean) == [], _scan(clean)
        _write(dirty, "a.cpp", "#include <Arduino.h>\n")
        _write(dirty, "m/b.h", "void IRAM_ATTR f() { pinMode(1, 2); }\n")
        _write(dirty, "c.c", "int x = millis();\n")
        _write(dirty, "net/y.cpp", "#include <esp_wifi.h>\n")
        hits = _scan(dirty)
        assert len(hits) == 4, hits
    print("selftest ok")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        _selftest()
        return 0
    hits = _scan(os.path.normpath(args.root))
    for relpath, number, text in hits:
        print("%s:%d: %s" % (relpath, number, text.strip()))
    if hits:
        print("%d HAL boundary violation(s)" % len(hits), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
