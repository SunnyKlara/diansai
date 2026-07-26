#!/usr/bin/env python3
"""Detect connected debug probes without opening or modifying the target."""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
from dataclasses import asdict, dataclass


KNOWN_USB_IDS = {
    "EF1A:74E5": ("cmsis-dap", "Horco CMSIS-DAP"),
    "0D28:0204": ("cmsis-dap", "DAPLink CMSIS-DAP"),
    "0451:BEF3": ("xds110", "TI XDS110"),
}
KNOWN_USB_VENDORS = {
    "1366": ("jlink", "SEGGER J-Link"),
}
KNOWN_STLINK_PIDS = {"3744", "3748", "374B", "374D", "3752"}


@dataclass
class Probe:
    kind: str
    display_name: str
    manufacturer: str
    usb_id: str
    serial_ports: list[str]
    confidence: str
    recommended_backend: str
    recommended_config: str
    evidence: list[str]


def run_command(command: list[str]) -> str:
    try:
        completed = subprocess.run(
            command,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=15,
        )
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError(f"command timed out after {exc.timeout} seconds") from exc
    if completed.returncode != 0:
        message = (completed.stderr or completed.stdout).strip()
        raise RuntimeError(message or f"command failed: {' '.join(command)}")
    return completed.stdout


def normalize_usb_id(instance_id: str, hardware_ids: list[str]) -> str:
    for value in [instance_id, *hardware_ids]:
        match = re.search(r"VID_([0-9A-F]{4}).*PID_([0-9A-F]{4})", value, flags=re.IGNORECASE)
        if match:
            return f"{match.group(1).upper()}:{match.group(2).upper()}"
    return ""


def classify_probe(text: str, usb_id: str = "") -> tuple[str, str, str, str, str]:
    lowered = text.lower()
    if usb_id in KNOWN_USB_IDS:
        kind, mapped_name = KNOWN_USB_IDS[usb_id]
        return (*probe_defaults(kind), mapped_name)
    vid, _, pid = usb_id.partition(":")
    if vid in KNOWN_USB_VENDORS:
        kind, mapped_name = KNOWN_USB_VENDORS[vid]
        return (*probe_defaults(kind), mapped_name)
    if vid == "0483" and pid in KNOWN_STLINK_PIDS:
        return (*probe_defaults("stlink"), "ST-Link")
    if re.search(r"\b(j-?link|segger)\b", lowered):
        return (*probe_defaults("jlink"), "")
    if re.search(r"\b(xds[- ]?110|tixds110)\b", lowered):
        return (*probe_defaults("xds110"), "")
    if re.search(r"\b(st-?link|stmicroelectronics stlink)\b", lowered):
        return (*probe_defaults("stlink"), "")
    if re.search(r"\b(cmsis[- ]?dap|daplink)\b", lowered):
        return (*probe_defaults("cmsis-dap"), "")
    return ("unknown", "low", "confirm_with_user", "", "")


def probe_defaults(kind: str) -> tuple[str, str, str, str]:
    if kind == "jlink":
        return ("jlink", "high", "dslite_or_jlink", "")
    if kind == "xds110":
        return ("xds110", "high", "dslite_or_ccs_dss", "")
    if kind == "stlink":
        return ("stlink", "high", "openocd", "interface/stlink.cfg")
    if kind == "cmsis-dap":
        return ("cmsis-dap", "high", "openocd", "interface/cmsis-dap.cfg")
    return ("unknown", "low", "confirm_with_user", "")


def first_string(value: object) -> str:
    if isinstance(value, str):
        return value
    if isinstance(value, list):
        return " ".join(str(item) for item in value)
    return ""


def as_list(value: object) -> list[dict[str, object]]:
    if isinstance(value, dict):
        return [value]
    if isinstance(value, list):
        return [item for item in value if isinstance(item, dict)]
    return []


def normalize_windows_container_id(value: object) -> str:
    container_id = first_string(value).strip().strip("{}").upper()
    if container_id in {
        "",
        "00000000-0000-0000-0000-000000000000",
        "00000000-0000-0000-FFFF-FFFFFFFFFFFF",
    }:
        return ""
    return container_id


def windows_probe_key(instance_id: str, usb_id: str, container_id: object) -> str:
    normalized_container = normalize_windows_container_id(container_id)
    if normalized_container:
        return f"container:{normalized_container}"

    normalized_instance = instance_id.upper()
    segments = normalized_instance.split("\\")
    if usb_id and len(segments) >= 3 and re.search(r"&MI_[0-9A-F]{2}", segments[1]):
        physical_path = re.sub(r"&[0-9A-F]{4}$", "", segments[-1])
        if physical_path:
            return f"usb-path:{usb_id}:{physical_path}"
    return f"instance:{normalized_instance}"


def merge_probe(existing: Probe, candidate: Probe) -> None:
    existing.serial_ports = sorted(set(existing.serial_ports) | set(candidate.serial_ports))
    existing.evidence.extend(item for item in candidate.evidence if item not in existing.evidence)
    if not existing.manufacturer:
        existing.manufacturer = candidate.manufacturer
    if existing.display_name == existing.kind and candidate.display_name != candidate.kind:
        existing.display_name = candidate.display_name


def windows_pnp_devices() -> list[dict[str, object]]:
    powershell = shutil.which("powershell") or shutil.which("pwsh")
    if not powershell:
        raise RuntimeError("PowerShell is unavailable")
    script = r"""
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$devices = @(Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match '^(USB|HID)\\' })
$containerIds = @{}
if ($devices.Count -gt 0) {
    Get-PnpDeviceProperty -InputObject $devices -KeyName 'DEVPKEY_Device_ContainerId' -ThrottleLimit 32 -ErrorAction SilentlyContinue |
        ForEach-Object {
            if ($null -ne $_.Data) {
                $containerIds[$_.InstanceId] = [string]$_.Data
            }
        }
}
$items = foreach ($device in $devices) {
    [PSCustomObject]@{
        Class = $device.Class
        FriendlyName = $device.FriendlyName
        Manufacturer = $device.Manufacturer
        InstanceId = $device.InstanceId
        ContainerId = $containerIds[$device.InstanceId]
    }
}
@($items) | ConvertTo-Json -Depth 3 -Compress
"""
    output = run_command([powershell, "-NoProfile", "-Command", script]).strip()
    return as_list(json.loads(output or "[]"))


def windows_serial_ports() -> list[dict[str, str]]:
    powershell = shutil.which("powershell") or shutil.which("pwsh")
    if not powershell:
        return []
    script = r"""
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
@(
    Get-CimInstance Win32_SerialPort | ForEach-Object {
        [PSCustomObject]@{
            DeviceID = $_.DeviceID
            Name = $_.Name
            PNPDeviceID = $_.PNPDeviceID
        }
    }
) | ConvertTo-Json -Depth 3 -Compress
"""
    try:
        output = run_command([powershell, "-NoProfile", "-Command", script]).strip()
        return [
            {key: str(value or "") for key, value in item.items()}
            for item in as_list(json.loads(output or "[]"))
        ]
    except (RuntimeError, json.JSONDecodeError):
        return []


def detect_windows() -> list[Probe]:
    devices = windows_pnp_devices()
    serial_ports = windows_serial_ports()
    probes_by_device: dict[str, Probe] = {}
    for device in devices:
        instance_id = first_string(device.get("InstanceId"))
        usb_id = normalize_usb_id(instance_id, [])
        texts = [
            text
            for text in (
                first_string(device.get("FriendlyName")).strip(),
                first_string(device.get("Manufacturer")).strip(),
            )
            if text
        ]
        combined = " | ".join(texts)
        kind, confidence, backend, config, mapped_name = classify_probe(combined, usb_id)
        if kind == "unknown":
            continue
        ports: list[str] = []
        # Match VID/PID directly because Windows PNP IDs use VID_xxxx&PID_yyyy.
        if usb_id:
            vid, pid = usb_id.split(":", maxsplit=1)
            ports = sorted(
                {
                    port.get("DeviceID", "")
                    for port in serial_ports
                    if f"VID_{vid}&PID_{pid}" in port.get("PNPDeviceID", "").upper()
                    and port.get("DeviceID")
                }
            )
        candidate = Probe(
            kind=kind,
            display_name=mapped_name or (texts[0] if texts else kind),
            manufacturer=first_string(device.get("Manufacturer")).strip(),
            usb_id=usb_id,
            serial_ports=ports,
            confidence=confidence,
            recommended_backend=backend,
            recommended_config=config,
            evidence=texts,
        )
        key = windows_probe_key(instance_id, usb_id, device.get("ContainerId"))
        existing = probes_by_device.get(key)
        if existing is None:
            probes_by_device[key] = candidate
        else:
            merge_probe(existing, candidate)
    return list(probes_by_device.values())


def detect_linux() -> list[Probe]:
    root = "/sys/bus/usb/devices"
    if not os.path.isdir(root):
        return []
    probes: list[Probe] = []
    for name in os.listdir(root):
        device_dir = os.path.join(root, name)
        values: dict[str, str] = {}
        for field in ("product", "manufacturer", "idVendor", "idProduct"):
            path = os.path.join(device_dir, field)
            try:
                with open(path, encoding="utf-8", errors="replace") as handle:
                    values[field] = handle.read().strip()
            except OSError:
                values[field] = ""
        usb_id = f"{values['idVendor'].upper()}:{values['idProduct'].upper()}" if values["idVendor"] and values["idProduct"] else ""
        combined = " | ".join((values["product"], values["manufacturer"]))
        kind, confidence, backend, config, mapped_name = classify_probe(combined, usb_id)
        if kind == "unknown":
            continue
        probes.append(
            Probe(
                kind=kind,
                display_name=mapped_name or values["product"] or kind,
                manufacturer=values["manufacturer"],
                usb_id=usb_id,
                serial_ports=[],
                confidence=confidence,
                recommended_backend=backend,
                recommended_config=config,
                evidence=[text for text in (values["product"], values["manufacturer"]) if text],
            )
        )
    return probes


def detect_probes() -> list[Probe]:
    system = platform.system()
    if system == "Windows":
        return detect_windows()
    if system == "Linux":
        return detect_linux()
    raise RuntimeError(f"probe detection is not implemented on {system or 'this operating system'}")


def print_text(probes: list[Probe]) -> None:
    if not probes:
        print("No supported debug probe was identified by the primary scan.")
        print("This result is inconclusive and is not proof that no probe is connected.")
        print("Inspect OS USB/PnP devices and serial ports, then try the selected backend's own probe command.")
        return
    if len(probes) > 1:
        print(f"Detected {len(probes)} supported debug probes. Ask the user which probe to use.")
        print()
    for index, probe in enumerate(probes, start=1):
        print(f"Probe {index}: {probe.kind}")
        print(f"  Device: {probe.display_name}")
        if probe.manufacturer:
            print(f"  Manufacturer: {probe.manufacturer}")
        if probe.usb_id:
            print(f"  USB ID: {probe.usb_id}")
        if probe.serial_ports:
            print(f"  Serial ports: {', '.join(probe.serial_ports)}")
        print(f"  Confidence: {probe.confidence}")
        print(f"  Recommended backend: {probe.recommended_backend}")
        if probe.recommended_config:
            print(f"  Recommended config: {probe.recommended_config}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="Print machine-readable JSON.")
    args = parser.parse_args()
    try:
        probes = detect_probes()
    except (RuntimeError, json.JSONDecodeError) as exc:
        if args.json:
            print(
                json.dumps(
                    {"status": "error", "probes": [], "error": str(exc)},
                    ensure_ascii=False,
                    indent=2,
                )
            )
        else:
            print(f"Probe detection failed: {exc}")
        return 2
    if args.json:
        payload: dict[str, object] = {
            "status": "detected" if probes else "inconclusive",
            "probes": [asdict(probe) for probe in probes],
        }
        if not probes:
            payload["message"] = (
                "No supported probe was identified. This is not proof that no probe is connected; "
                "inspect OS USB/PnP and serial devices and try the backend's own probe command."
            )
        print(json.dumps(payload, ensure_ascii=False, indent=2))
    else:
        print_text(probes)
    return 0 if probes else 1


if __name__ == "__main__":
    raise SystemExit(main())
