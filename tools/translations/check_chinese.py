"""Audit the shipped Simplified Chinese catalogs and current JSON metadata.

Run from tools/ with: python -m translations.check_chinese
Pass --lupdate /path/to/Qt/bin/lupdate to also check current C++/QML coverage.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import tempfile
from pathlib import Path

from defusedxml import ElementTree as ET

from translations import qgc_lupdate_json

REPO_ROOT = Path(__file__).resolve().parents[2]
CATALOGS = ("qgc_source_zh_CN.ts", "qgc_json_zh_CN.ts", "qgc_parameters_zh_CN.ts")
IDENTIFIER = re.compile(r"\b[A-Z][A-Z0-9]*_[A-Z][A-Z0-9_]*\b")
URL = re.compile(r"https?://[^\s<>\"']+")
TECHNICAL_NAMES = (
    "QGroundControl",
    "PX4",
    "ArduPilot",
    "MAVLink",
    "Mapbox",
    "Esri",
    "Discord",
    "OpenStreetMap",
    "u-blox",
    "Septentrio",
    "DroneCAN",
    "UAVCAN",
    "DataFlash",
    "DShot",
    "OneShot",
    "RunCam",
    "FPort",
    "NTRIP",
    "RTCM",
    "RTK",
    "GNSS",
)
QML_TR_LITERAL = re.compile(r'\bqsTr\(\s*("(?:[^"\\]|\\.)*")')
MODE_NAME_LITERAL = re.compile(
    r'\bconst\s+QString\s+\w*FlightMode\w*\s*=\s*tr\(\s*("(?:[^"\\]|\\.)*")\s*\)'
)


def embedded_qml_strings(value):
    """Find literal qsTr calls inside generated-page QML expression fields."""
    if isinstance(value, str):
        for match in QML_TR_LITERAL.finditer(value):
            yield json.loads(match.group(1))
    elif isinstance(value, dict):
        for child in value.values():
            yield from embedded_qml_strings(child)
    elif isinstance(value, list):
        for child in value:
            yield from embedded_qml_strings(child)


def message_keys(root) -> set[tuple[str, str, str]]:
    return {
        (
            context.findtext("name", ""),
            message.findtext("source", ""),
            message.findtext("comment", ""),
        )
        for context in root.findall("context")
        for message in context.findall("message")
        if message.findtext("source", "")
    }


def audit_catalog(path: Path) -> tuple[set[tuple[str, str, str]], list[str], int]:
    root = ET.parse(path).getroot()
    if root is None:
        raise ValueError(f"{path.name}: empty catalog")
    errors = []
    retained = 0
    if root.get("language") != "zh_CN":
        errors.append(f"{path.name}: expected language zh_CN")
    for context in root.findall("context"):
        name = context.findtext("name", "")
        for message in context.findall("message"):
            source = message.findtext("source", "")
            translation = message.find("translation")
            if not source or (
                translation is not None and translation.get("type") in {"vanished", "obsolete"}
            ):
                continue
            label = f"{path.name}:{name}: {source[:100]!r}"
            if translation is None or translation.get("type") == "unfinished":
                errors.append(f"{label}: missing finished translation")
                continue
            text = "".join(translation.itertext())
            if not text.strip() or "\ufffd" in text:
                errors.append(f"{label}: empty translation or Unicode replacement character")
            if re.search(r"[A-Za-z]", source) and not re.search(r"[\u3400-\u9fff]", text):
                if not message.findtext("translatorcomment", "").startswith("[keep-en]"):
                    errors.append(f"{label}: English text needs an explicit preservation note")
                elif text != source:
                    errors.append(f"{label}: preserved identifiers must remain unchanged")
                retained += 1
            for identifier in IDENTIFIER.findall(source):
                if identifier not in text:
                    errors.append(f"{label}: altered identifier {identifier}")
            for name in TECHNICAL_NAMES:
                if name in source and name not in text:
                    errors.append(f"{label}: altered technical name {name}")
            for url in URL.findall(source):
                if url.rstrip(".,;)") not in text:
                    errors.append(f"{label}: altered URL {url}")
            comment = message.findtext("extracomment", "")
            if ".enumStrings" in comment and source.count(",") != text.count(","):
                errors.append(f"{label}: changed enum choice count")
    return message_keys(root), errors, retained


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lupdate", type=Path)
    parser.add_argument(
        "--apm-dir", type=Path, help="ArduPilot ParameterRepository used by the build"
    )
    args = parser.parse_args()
    keys_by_file = {}
    errors = []
    retained = 0
    for name in CATALOGS:
        keys, failures, count = audit_catalog(REPO_ROOT / "translations" / name)
        keys_by_file[name] = keys
        errors.extend(failures)
        retained += count

    entries = []
    qgc_lupdate_json.walk_directory_tree_for_json_files(REPO_ROOT / "src", entries)
    for context, _, strings in entries:
        for source in strings:
            comment, source = qgc_lupdate_json._split_disambiguation(source, context)
            if (context, source, comment) not in keys_by_file["qgc_json_zh_CN.ts"]:
                errors.append(f"Untranslated JSON metadata: {context}: {source!r}")

    pages_dir = REPO_ROOT / "src/AppSettings/pages"
    pages = json.loads((pages_dir / "SettingsPages.json").read_text(encoding="utf-8"))
    available_keys = set().union(*keys_by_file.values())
    for page in pages["pages"]:
        if page.get("pageDefinition"):
            data = json.loads((pages_dir / page["pageDefinition"]).read_text(encoding="utf-8"))
            context = Path(page["qml"]).stem
            for source in embedded_qml_strings(data):
                if (context, source, "") not in available_keys:
                    errors.append(f"Untranslated generated QML expression: {context}: {source!r}")

    parameter_strings = set()
    for name in (
        "src/FirmwarePlugin/PX4/PX4ParameterFactMetaData.json",
        "src/Comms/MockLink/MockLink.Parameter.MetaData.json",
    ):
        data = json.loads((REPO_ROOT / name).read_text(encoding="utf-8"))
        for parameter in data.get("parameters", []):
            for key in ("shortDesc", "longDesc", "category", "group"):
                if parameter.get(key):
                    parameter_strings.add(parameter[key])
            for key in ("values", "bitmask"):
                parameter_strings.update(
                    value["description"]
                    for value in parameter.get(key, [])
                    if value.get("description")
                )
    if args.apm_dir:
        paths = list(args.apm_dir.glob("*/apm.pdef.json"))
        if not paths:
            errors.append(f"No ArduPilot parameter definitions found in {args.apm_dir}")
        for path in paths:
            if path.parent.name.startswith(
                ("AP_Periph-", "Blimp-", "Copter-3.", "Plane-3.", "Rover-3.")
            ):
                continue
            data = json.loads(path.read_text(encoding="utf-8"))
            for group in data.values():
                if not isinstance(group, dict):
                    continue
                for parameter in group.values():
                    if not isinstance(parameter, dict):
                        continue
                    for key in ("DisplayName", "Description", "User"):
                        if parameter.get(key):
                            parameter_strings.add(parameter[key])
                    for key in ("Values", "Bitmask"):
                        if isinstance(parameter.get(key), dict):
                            parameter_strings.update(
                                value for value in parameter[key].values() if value
                            )
    for source in sorted(parameter_strings):
        if ("ParameterMetaData", source, "") not in keys_by_file["qgc_parameters_zh_CN.ts"]:
            errors.append(f"Untranslated firmware parameter text: {source!r}")

    for path in (REPO_ROOT / "src/FirmwarePlugin").rglob("*"):
        if path.suffix not in {".cc", ".h"}:
            continue
        for match in MODE_NAME_LITERAL.finditer(path.read_text(encoding="utf-8")):
            name = json.loads(match.group(1))
            if ("FlightModes", name, "") not in keys_by_file["qgc_parameters_zh_CN.ts"]:
                errors.append(f"Untranslated available flight mode name: {name!r}")

    airframes = ET.parse(REPO_ROOT / "src/AutoPilotPlugins/PX4/AirframeFactMetaData.xml").getroot()
    if airframes is not None:
        for element in airframes.iter():
            if element.tag in {"airframe_group", "airframe"}:
                name = element.get("name", "")
                if name and ("Airframe", name, "") not in keys_by_file["qgc_parameters_zh_CN.ts"]:
                    errors.append(f"Untranslated airframe display name: {name!r}")

    if args.lupdate:
        with tempfile.TemporaryDirectory(prefix="qgc-chinese-audit-") as directory:
            template = Path(directory) / "source.ts"
            subprocess.run(
                [
                    str(args.lupdate),
                    str(REPO_ROOT / "src"),
                    "-ts",
                    str(template),
                    "-no-obsolete",
                    "-locations",
                    "none",
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            missing = (
                message_keys(ET.parse(template).getroot()) - keys_by_file["qgc_source_zh_CN.ts"]
            )
            errors.extend(f"Untranslated C++/QML: {key!r}" for key in sorted(missing))

    for error in errors:
        print(error)
    total = sum(len(keys) for keys in keys_by_file.values())
    print(
        f"Audited {total} Chinese messages; {retained} preserved technical strings; {len(errors)} errors."
    )
    return int(bool(errors))


if __name__ == "__main__":
    raise SystemExit(main())
