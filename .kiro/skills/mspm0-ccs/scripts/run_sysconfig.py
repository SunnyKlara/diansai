#!/usr/bin/env python3
"""Safely discover and run TI SysConfig CLI for an MSPM0 project."""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


BUILD_DIRS = {"Debug", "Release", "build", "out", "Objects", "Listings"}
SKIP_DIRS = {".git", ".svn", ".hg", "__pycache__", ".agents", ".claude", ".codex"}
TOOL_NAMES = ("sysconfig_cli.bat", "sysconfig_cli.sh", "sysconfig_cli")
PRODUCT_ENV_VARS = ("MSPM0_SDK_ROOT", "MSPM0_SDK_PATH")
TOOL_ENV_VARS = ("SYSCONFIG_CLI", "SYSCONFIG_ROOT")


class ResolutionError(RuntimeError):
    """Raised when a safe, unambiguous SysConfig choice cannot be made."""


@dataclass(frozen=True)
class ToolCandidate:
    path: str
    version: str
    source: str


@dataclass(frozen=True)
class ProductCandidate:
    path: str
    name: str
    version: str
    source: str


@dataclass(frozen=True)
class BuildEvidence:
    tool: str
    script: str
    product: str
    compiler: str
    source: str


@dataclass
class ProjectInfo:
    project: str
    script: str
    metadata: dict[str, object]
    ccs_products: dict[str, str]
    ccs_product_conflicts: dict[str, list[str]]
    ccs_compiler: str
    ccs_compiler_conflicts: list[str]
    build_evidence: list[BuildEvidence]


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def is_relative_to(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def is_build_path(path: Path, root: Path) -> bool:
    parts = path.relative_to(root).parts
    return bool(set(parts) & BUILD_DIRS) or any(part.startswith("cmake-build") for part in parts)


def iter_project_files(root: Path) -> Iterable[Path]:
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [
            name
            for name in dirnames
            if name not in SKIP_DIRS
            and name not in BUILD_DIRS
            and not name.startswith("cmake-build")
        ]
        for filename in filenames:
            yield Path(dirpath) / filename


def find_syscfg(project: Path, explicit: str | None) -> Path:
    if explicit:
        candidate = Path(explicit)
        if not candidate.is_absolute():
            candidate = project / candidate
        candidate = candidate.resolve()
        if not is_relative_to(candidate, project):
            raise ResolutionError("--script must stay inside the project directory")
        if candidate.suffix.lower() != ".syscfg" or not candidate.is_file():
            raise ResolutionError(f"SysConfig script not found: {candidate}")
        return candidate

    discovered = sorted(
        path
        for path in iter_project_files(project)
        if path.suffix.lower() == ".syscfg" and not is_build_path(path, project)
    )
    candidates: list[Path] = []
    for candidate in discovered:
        resolved = candidate.resolve()
        if not is_relative_to(resolved, project):
            relative = candidate.relative_to(project)
            raise ResolutionError(
                f"discovered SysConfig script resolves outside the project: {relative}"
            )
        candidates.append(resolved)
    if not candidates:
        raise ResolutionError(f"no .syscfg file found under {project}")
    if len(candidates) > 1:
        choices = ", ".join(str(path.relative_to(project)) for path in candidates)
        raise ResolutionError(f"multiple .syscfg files found ({choices}); select one with --script")
    return candidates[0]


def parse_cli_tokens(text: str) -> list[str]:
    try:
        return shlex.split(text, posix=True)
    except ValueError as exc:
        raise ResolutionError(f"invalid SysConfig metadata arguments: {exc}") from exc


def parse_known_args(tokens: list[str]) -> dict[str, str]:
    aliases = {
        "-b": "board",
        "--board": "board",
        "-c": "compiler",
        "--compiler": "compiler",
        "-d": "device",
        "--device": "device",
        "-o": "output",
        "--output": "output",
        "-p": "package",
        "--package": "package",
        "-r": "part",
        "--part": "part",
        "-s": "product",
        "--product": "product",
        "--script": "script",
        "-v": "variant",
        "--variant": "variant",
    }
    values: dict[str, str] = {}
    index = 0
    while index < len(tokens):
        key = aliases.get(tokens[index])
        if key and index + 1 < len(tokens):
            values[key] = tokens[index + 1]
            index += 2
        else:
            index += 1
    return values


def parse_syscfg_metadata(text: str) -> dict[str, object]:
    metadata: dict[str, object] = {}
    cli_args: dict[str, str] = {}
    v2_args: dict[str, str] = {}

    for line in text.splitlines():
        match = re.search(r"@(?P<name>v2CliArgs|cliArgs)\s+(?P<args>.*)$", line)
        if not match:
            continue
        parsed = parse_known_args(parse_cli_tokens(match.group("args").strip()))
        if match.group("name") == "v2CliArgs":
            v2_args.update(parsed)
        else:
            cli_args.update(parsed)

    merged_args = dict(cli_args)
    merged_args.update(v2_args)
    metadata["cli_args"] = cli_args
    metadata["v2_cli_args"] = v2_args
    metadata["effective_args"] = merged_args

    version_match = re.search(r"@versions\s+(\{[^\r\n]*\})", text)
    versions: dict[str, object] = {}
    if version_match:
        try:
            parsed_versions = json.loads(version_match.group(1))
        except json.JSONDecodeError as exc:
            raise ResolutionError(f"invalid @versions JSON: {exc}") from exc
        if isinstance(parsed_versions, dict):
            versions = parsed_versions
    metadata["versions"] = versions
    metadata["tool_version"] = str(versions.get("tool", ""))
    return metadata


def inspect_ccs_project(project: Path) -> dict[str, object]:
    cproject = project / ".cproject"
    if not cproject.is_file():
        return {
            "products": {},
            "product_conflicts": {},
            "compiler": "",
            "compiler_conflicts": [],
        }

    try:
        root = ET.parse(cproject).getroot()
    except ET.ParseError as exc:
        raise ResolutionError(f"cannot parse {cproject}: {exc}") from exc

    product_names: dict[str, str] = {}
    product_versions: dict[str, set[str]] = {}
    compilers: set[str] = set()
    for element in root.iter():
        value = element.attrib.get("value", "")
        if value.startswith("PRODUCTS="):
            for item in value.removeprefix("PRODUCTS=").split(";"):
                if ":" not in item:
                    continue
                name, version = item.split(":", 1)
                name = name.strip()
                version = version.strip()
                if not name or not version:
                    continue
                key = re.sub(r"[^a-z0-9]", "", name.lower())
                display_name = product_names.setdefault(key, name)
                product_versions.setdefault(display_name, set()).add(version)

        identifier = " ".join(
            (
                element.attrib.get("id", ""),
                element.attrib.get("superClass", ""),
                element.attrib.get("name", ""),
            )
        ).lower()
        if "ticlang" in identifier:
            compilers.add("ticlang")
        elif "arm-none-eabi" in identifier or "gnu" in identifier:
            compilers.add("gcc")

    products: dict[str, str] = {}
    product_conflicts: dict[str, list[str]] = {}
    for name, versions in product_versions.items():
        if len({version_key(version) for version in versions}) == 1:
            products[name] = sorted(versions)[0]
        else:
            product_conflicts[name] = sorted(versions)
    return {
        "products": products,
        "product_conflicts": product_conflicts,
        "compiler": next(iter(compilers)) if len(compilers) == 1 else "",
        "compiler_conflicts": sorted(compilers) if len(compilers) > 1 else [],
    }


def parse_build_rule(line: str, source: Path) -> BuildEvidence | None:
    match = re.search(
        r'"(?P<tool>[^"]*sysconfig_cli(?:\.bat|\.sh)?)"\s+(?P<args>.*)$',
        line.strip(),
        flags=re.IGNORECASE,
    )
    if not match:
        return None
    args = parse_known_args(parse_cli_tokens(match.group("args")))
    return BuildEvidence(
        tool=match.group("tool"),
        script=args.get("script", ""),
        product=args.get("product", ""),
        compiler=args.get("compiler", ""),
        source=str(source),
    )


def find_build_evidence(project: Path, script: Path) -> list[BuildEvidence]:
    evidence: list[BuildEvidence] = []
    seen: set[tuple[str, str, str, str]] = set()
    for build_dir in ("Debug", "Release"):
        rule_path = project / build_dir / "subdir_rules.mk"
        if not rule_path.is_file():
            continue
        for line in read_text(rule_path).splitlines():
            parsed = parse_build_rule(line, rule_path)
            if not parsed:
                continue
            if parsed.script:
                rule_script = Path(parsed.script)
                if not rule_script.is_absolute():
                    rule_script = rule_path.parent / rule_script
                rule_script = rule_script.resolve()
                if rule_script != script:
                    continue
            key = (
                os.path.normcase(parsed.tool),
                os.path.normcase(parsed.script),
                os.path.normcase(parsed.product),
                parsed.compiler,
            )
            if key not in seen:
                seen.add(key)
                evidence.append(parsed)
    return evidence


def inspect_project(project: Path, explicit_script: str | None = None) -> ProjectInfo:
    project = project.resolve()
    if not project.is_dir():
        raise ResolutionError(f"project directory not found: {project}")
    script = find_syscfg(project, explicit_script)
    metadata = parse_syscfg_metadata(read_text(script))
    ccs = inspect_ccs_project(project)
    return ProjectInfo(
        project=str(project),
        script=str(script),
        metadata=metadata,
        ccs_products=dict(ccs["products"]),
        ccs_product_conflicts=dict(ccs["product_conflicts"]),
        ccs_compiler=str(ccs["compiler"]),
        ccs_compiler_conflicts=list(ccs["compiler_conflicts"]),
        build_evidence=find_build_evidence(project, script),
    )


def resolve_tool_path(value: str) -> Path:
    candidate = Path(os.path.expandvars(os.path.expanduser(value))).resolve()
    if candidate.is_dir():
        for name in TOOL_NAMES:
            tool = candidate / name
            if tool.is_file():
                return tool.resolve()
        raise ResolutionError(f"SysConfig directory has no CLI executable: {candidate}")
    if not candidate.is_file():
        raise ResolutionError(f"SysConfig CLI not found: {candidate}")
    return candidate


def command_for_tool(tool: Path, args: list[str]) -> list[str]:
    # Python launches .bat files directly on Windows; cmd /c broke quoted paths with spaces.
    return [str(tool), *args]


def run_tool(
    tool: Path,
    args: list[str],
    *,
    cwd: Path | None = None,
    timeout: int = 120,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command_for_tool(tool, args),
        cwd=str(cwd) if cwd else None,
        text=True,
        capture_output=True,
        timeout=timeout,
        check=False,
    )


def query_tool_version(tool: Path) -> str:
    try:
        result = run_tool(tool, ["--version"], timeout=20)
    except (OSError, subprocess.SubprocessError) as exc:
        raise ResolutionError(f"cannot run SysConfig CLI {tool}: {exc}") from exc
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise ResolutionError(f"cannot query SysConfig version from {tool}: {detail}")
    return result.stdout.strip().splitlines()[0] if result.stdout.strip() else ""


def add_tool_candidate(
    candidates: dict[str, ToolCandidate],
    value: str | Path,
    source: str,
    *,
    ignore_missing: bool = True,
) -> None:
    try:
        path = resolve_tool_path(str(value))
    except ResolutionError:
        if ignore_missing:
            return
        raise
    key = os.path.normcase(str(path))
    if key in candidates:
        return
    candidates[key] = ToolCandidate(str(path), query_tool_version(path), source)


def common_tool_paths() -> Iterable[Path]:
    if os.name == "nt":
        ti_root = Path(os.environ.get("TI_ROOT", "C:/ti"))
        if ti_root.is_dir():
            yield from ti_root.glob("sysconfig_*/sysconfig_cli.bat")
            yield from ti_root.glob("ccs*/ccs/utils/sysconfig_*/sysconfig_cli.bat")
    else:
        for root in (Path("/opt/ti"), Path.home() / "ti"):
            if not root.is_dir():
                continue
            yield from root.glob("sysconfig_*/sysconfig_cli.sh")
            yield from root.glob("sysconfig_*/sysconfig_cli")
            yield from root.glob("ccs*/ccs/utils/sysconfig_*/sysconfig_cli.sh")


def discover_tools(build_evidence: list[BuildEvidence]) -> list[ToolCandidate]:
    candidates: dict[str, ToolCandidate] = {}
    for evidence in build_evidence:
        add_tool_candidate(candidates, evidence.tool, f"build rule: {evidence.source}")
    for variable in TOOL_ENV_VARS:
        value = os.environ.get(variable)
        if value:
            add_tool_candidate(candidates, value, f"environment: {variable}")
    for name in TOOL_NAMES:
        value = shutil.which(name)
        if value:
            add_tool_candidate(candidates, value, "PATH")
    for path in common_tool_paths():
        add_tool_candidate(candidates, path, "common install root")
    return sorted(candidates.values(), key=lambda item: (version_key(item.version), item.path))


def base_version(version: str) -> str:
    return version.strip().split("+", 1)[0]


def version_key(version: str) -> tuple[int, ...]:
    numbers = re.findall(r"\d+", base_version(version))
    return tuple(int(number) for number in numbers)


def versions_match(left: str, right: str) -> bool:
    return bool(left and right and version_key(left) == version_key(right))


def expected_tool_versions(info: ProjectInfo) -> list[tuple[str, str]]:
    versions: list[tuple[str, str]] = []
    metadata_version = str(info.metadata.get("tool_version", ""))
    if metadata_version:
        versions.append((metadata_version, ".syscfg @versions"))
    ccs_version = info.ccs_products.get("sysconfig", "")
    if ccs_version:
        versions.append((ccs_version, ".cproject PRODUCTS"))
    return versions


def ccs_conflicting_product_versions(
    info: ProjectInfo,
    normalized_name: str,
) -> tuple[str, list[str]] | None:
    for name, versions in info.ccs_product_conflicts.items():
        if normalize_product_name(name) == normalized_name:
            return name, versions
    return None


def select_tool(
    info: ProjectInfo,
    explicit: str | None,
) -> tuple[ToolCandidate, list[str], list[ToolCandidate]]:
    warnings: list[str] = []
    expected = expected_tool_versions(info)

    if explicit:
        path = resolve_tool_path(explicit)
        selected = ToolCandidate(str(path), query_tool_version(path), "explicit --tool")
        for version, source in expected:
            if not versions_match(selected.version, version):
                warnings.append(
                    f"explicit SysConfig {selected.version} differs from {source} {version}"
                )
        return selected, warnings, [selected]

    if info.build_evidence:
        tools = {
            os.path.normcase(str(Path(item.tool).resolve())): item
            for item in info.build_evidence
            if Path(item.tool).is_file()
        }
        if len(tools) > 1:
            raise ResolutionError("build rules reference multiple SysConfig tools; choose one with --tool")
        if tools:
            evidence = next(iter(tools.values()))
            path = resolve_tool_path(evidence.tool)
            selected = ToolCandidate(
                str(path),
                query_tool_version(path),
                f"build rule: {evidence.source}",
            )
            mismatches = [
                f"{source} {version}"
                for version, source in expected
                if not versions_match(selected.version, version)
            ]
            if mismatches:
                raise ResolutionError(
                    f"build-rule SysConfig {selected.version} conflicts with "
                    f"{', '.join(mismatches)}; use --tool for an intentional override"
                )
            return selected, warnings, [selected]

    ccs_conflict = ccs_conflicting_product_versions(info, "sysconfig")
    if ccs_conflict:
        name, versions = ccs_conflict
        raise ResolutionError(
            f"CCS configurations declare multiple {name} versions "
            f"({', '.join(versions)}); select one with --tool"
        )

    distinct_expected = {version_key(version) for version, _source in expected}
    if len(distinct_expected) > 1:
        detail = ", ".join(f"{source}={version}" for version, source in expected)
        raise ResolutionError(f"conflicting SysConfig version declarations ({detail}); use --tool")

    candidates = discover_tools(info.build_evidence)
    if not candidates:
        raise ResolutionError("no SysConfig CLI found; install it or pass --tool")

    if distinct_expected:
        expected_key = next(iter(distinct_expected))
        expected_version = expected[0][0]
        matches = [item for item in candidates if version_key(item.version) == expected_key]
        if len(matches) == 1:
            return matches[0], warnings, candidates
        if len(matches) > 1:
            choices = ", ".join(item.path for item in matches)
            raise ResolutionError(
                f"multiple SysConfig {expected_version} CLIs found ({choices}); use --tool"
            )
        available = ", ".join(f"{item.version} ({item.path})" for item in candidates)
        raise ResolutionError(
            f"project requires SysConfig {expected_version}, but it was not found; "
            f"available: {available}. Use --tool to override explicitly."
        )

    if len(candidates) == 1:
        return candidates[0], warnings, candidates
    available = ", ".join(f"{item.version} ({item.path})" for item in candidates)
    raise ResolutionError(
        f"project does not declare a SysConfig version and multiple tools are installed: "
        f"{available}. Select one with --tool."
    )


def product_from_json(path: Path, source: str) -> ProductCandidate:
    path = path.resolve()
    if path.is_dir():
        path = path / ".metadata" / "product.json"
    if not path.is_file():
        raise ResolutionError(f"SysConfig product.json not found: {path}")
    try:
        data = json.loads(read_text(path))
    except json.JSONDecodeError as exc:
        raise ResolutionError(f"invalid product.json {path}: {exc}") from exc
    return ProductCandidate(
        path=str(path),
        name=str(data.get("name", "")),
        version=str(data.get("version", "")),
        source=source,
    )


def add_product_candidate(
    candidates: dict[str, ProductCandidate],
    value: str | Path,
    source: str,
    *,
    ignore_missing: bool = True,
) -> None:
    try:
        candidate = product_from_json(
            Path(os.path.expandvars(os.path.expanduser(str(value)))),
            source,
        )
    except ResolutionError:
        if ignore_missing:
            return
        raise
    key = os.path.normcase(candidate.path)
    candidates.setdefault(key, candidate)


def common_product_paths() -> Iterable[Path]:
    roots: list[Path]
    if os.name == "nt":
        roots = [Path(os.environ.get("TI_ROOT", "C:/ti"))]
    else:
        roots = [Path("/opt/ti"), Path.home() / "ti"]
    for root in roots:
        if root.is_dir():
            yield from root.glob("mspm0_sdk_*/.metadata/product.json")


def discover_products(build_evidence: list[BuildEvidence]) -> list[ProductCandidate]:
    candidates: dict[str, ProductCandidate] = {}
    for evidence in build_evidence:
        if evidence.product and "@" not in evidence.product:
            add_product_candidate(
                candidates,
                evidence.product,
                f"build rule: {evidence.source}",
            )
    for variable in PRODUCT_ENV_VARS:
        value = os.environ.get(variable)
        if value:
            add_product_candidate(candidates, value, f"environment: {variable}")
    for path in common_product_paths():
        add_product_candidate(candidates, path, "common install root")
    return sorted(candidates.values(), key=lambda item: (version_key(item.version), item.path))


def normalize_product_name(name: str) -> str:
    return re.sub(r"[^a-z0-9]", "", name.lower())


def product_install_version(candidate: ProductCandidate) -> str:
    product_path = Path(candidate.path)
    install_dir = (
        product_path.parent.parent
        if product_path.parent.name == ".metadata"
        else product_path.parent
    )
    match = re.search(r"(\d+(?:[._-]\d+){2,})$", install_dir.name)
    if not match:
        return ""
    return re.sub(r"[._-]", ".", match.group(1))


def product_matches_requirement(
    candidate: ProductCandidate,
    name: str,
    version: str,
    source: str,
) -> bool:
    if normalize_product_name(candidate.name) != normalize_product_name(name):
        return False
    if versions_match(candidate.version, version):
        return True
    return source == ".cproject PRODUCTS" and versions_match(
        product_install_version(candidate),
        version,
    )


def is_product_path_value(value: str) -> bool:
    expanded = os.path.expandvars(os.path.expanduser(value.strip()))
    return (
        "/" in expanded
        or "\\" in expanded
        or expanded.lower().endswith("product.json")
        or Path(expanded).is_absolute()
    )


def metadata_product_requirement(info: ProjectInfo) -> tuple[str, str]:
    args = info.metadata.get("effective_args", {})
    if not isinstance(args, dict):
        return "", ""
    product = str(args.get("product", "")).strip()
    if is_product_path_value(product) or "@" not in product:
        return "", ""
    name, version = product.rsplit("@", 1)
    return name, version


def metadata_product_path(info: ProjectInfo) -> Path | None:
    args = info.metadata.get("effective_args", {})
    if not isinstance(args, dict):
        return None
    product = str(args.get("product", "")).strip()
    if not product or ("@" in product and not is_product_path_value(product)):
        return None
    path = Path(os.path.expandvars(os.path.expanduser(product)))
    if not path.is_absolute():
        path = Path(info.script).parent / path
    return path.resolve()


def ccs_product_requirement(info: ProjectInfo) -> tuple[str, str]:
    for name, version in info.ccs_products.items():
        if normalize_product_name(name) == "mspm0sdk":
            return name, version
    return "", ""


def select_product(
    info: ProjectInfo,
    explicit: str | None,
) -> tuple[ProductCandidate, list[str], list[ProductCandidate]]:
    warnings: list[str] = []
    requirements = [
        item
        for item in (
            (*metadata_product_requirement(info), ".syscfg metadata"),
            (*ccs_product_requirement(info), ".cproject PRODUCTS"),
        )
        if item[0] and item[1]
    ]
    metadata_candidate: ProductCandidate | None = None
    metadata_path = metadata_product_path(info)
    if metadata_path:
        try:
            metadata_candidate = product_from_json(
                metadata_path,
                ".syscfg metadata path",
            )
        except ResolutionError as exc:
            if not explicit:
                raise
            warnings.append(str(exc))
        if metadata_candidate:
            requirements.append(
                (
                    metadata_candidate.name,
                    metadata_candidate.version,
                    ".syscfg metadata path",
                )
            )

    if explicit:
        selected = product_from_json(Path(explicit), "explicit --product")
        for name, version, source in requirements:
            if not product_matches_requirement(selected, name, version, source):
                warnings.append(
                    f"explicit product {selected.name}@{selected.version} differs from "
                    f"{source} {name}@{version}"
                )
        return selected, warnings, [selected]

    build_products = {
        os.path.normcase(str(Path(item.product).resolve())): item
        for item in info.build_evidence
        if item.product and "@" not in item.product and Path(item.product).is_file()
    }
    if len(build_products) > 1:
        raise ResolutionError("build rules reference multiple SysConfig products; use --product")
    if build_products:
        evidence = next(iter(build_products.values()))
        selected = product_from_json(
            Path(evidence.product),
            f"build rule: {evidence.source}",
        )
        mismatches = [
            f"{source} {name}@{version}"
            for name, version, source in requirements
            if not product_matches_requirement(selected, name, version, source)
        ]
        if mismatches:
            raise ResolutionError(
                f"build-rule product {selected.name}@{selected.version} conflicts with "
                f"{', '.join(mismatches)}; use --product for an intentional override"
            )
        return selected, warnings, [selected]

    ccs_conflict = ccs_conflicting_product_versions(info, "mspm0sdk")
    if ccs_conflict:
        name, versions = ccs_conflict
        raise ResolutionError(
            f"CCS configurations declare multiple {name} versions "
            f"({', '.join(versions)}); select one with --product"
        )

    if metadata_candidate:
        mismatches = [
            f"{source} {name}@{version}"
            for name, version, source in requirements
            if not product_matches_requirement(
                metadata_candidate,
                name,
                version,
                source,
            )
        ]
        if mismatches:
            raise ResolutionError(
                f"metadata product {metadata_candidate.name}@{metadata_candidate.version} "
                f"conflicts with {', '.join(mismatches)}; "
                "use --product for an intentional override"
            )
        return metadata_candidate, warnings, [metadata_candidate]

    candidates = discover_products(info.build_evidence)
    if not candidates:
        raise ResolutionError("no MSPM0 SDK product.json found; pass --product")

    if requirements:
        required_version = requirements[0][1]
        matches = [
            item
            for item in candidates
            if all(
                product_matches_requirement(item, name, version, source)
                for name, version, source in requirements
            )
        ]
        if len(matches) == 1:
            return matches[0], warnings, candidates
        if len(matches) > 1:
            choices = ", ".join(item.path for item in matches)
            raise ResolutionError(
                f"multiple products satisfy project declarations ({choices}); use --product"
            )
        normalized_requirements = {
            (normalize_product_name(name), version_key(version))
            for name, version, _source in requirements
        }
        if len(normalized_requirements) > 1:
            detail = ", ".join(
                f"{source}={name}@{version}"
                for name, version, source in requirements
            )
            raise ResolutionError(
                f"conflicting SDK product declarations ({detail}); use --product"
            )
        required_name = normalize_product_name(requirements[0][0])
        available = ", ".join(
            f"{item.name}@{item.version} ({item.path})" for item in candidates
        )
        raise ResolutionError(
            f"project requires {required_name}@{required_version}, but it was not found; "
            f"available: {available}. Use --product to override explicitly."
        )

    if len(candidates) == 1:
        return candidates[0], warnings, candidates
    available = ", ".join(f"{item.name}@{item.version} ({item.path})" for item in candidates)
    raise ResolutionError(
        f"project does not declare an SDK product and multiple products are installed: "
        f"{available}. Select one with --product."
    )


def select_compiler(info: ProjectInfo, explicit: str | None) -> tuple[str, str]:
    if explicit:
        return explicit, "explicit --compiler"
    build_compilers = {item.compiler for item in info.build_evidence if item.compiler}
    if len(build_compilers) > 1:
        raise ResolutionError("build rules declare multiple SysConfig compilers; use --compiler")
    if build_compilers:
        return next(iter(build_compilers)), "build rule"
    if info.ccs_compiler_conflicts:
        raise ResolutionError(
            "CCS configurations declare multiple SysConfig compilers "
            f"({', '.join(info.ccs_compiler_conflicts)}); use --compiler"
        )
    if info.ccs_compiler:
        return info.ccs_compiler, ".cproject"
    metadata_args = info.metadata.get("effective_args", {})
    if isinstance(metadata_args, dict) and metadata_args.get("compiler"):
        return str(metadata_args["compiler"]), ".syscfg metadata"
    return "", "SysConfig default"


def format_command(command: list[str]) -> str:
    if os.name == "nt":
        return subprocess.list2cmdline(command)
    return shlex.join(command)


def collect_generated_files(output: Path) -> list[str]:
    if not output.is_dir():
        return []
    return sorted(str(path.relative_to(output)) for path in output.rglob("*") if path.is_file())


def build_report(
    info: ProjectInfo,
    tool: ToolCandidate,
    product: ProductCandidate,
    compiler: str,
    compiler_source: str,
    warnings: list[str],
    candidates: tuple[list[ToolCandidate], list[ProductCandidate]],
    output: Path,
    command: list[str],
    *,
    dry_run: bool,
    keep_output: bool,
) -> dict[str, object]:
    return {
        "status": "dry-run" if dry_run else "pending",
        "project": info.project,
        "script": info.script,
        "tool": asdict(tool),
        "product": asdict(product),
        "compiler": {"value": compiler, "source": compiler_source},
        "output": {"path": str(output), "temporary": True, "kept": keep_output},
        "command": command,
        "command_text": format_command(command),
        "warnings": warnings,
        "available_tools": [asdict(item) for item in candidates[0]],
        "available_products": [asdict(item) for item in candidates[1]],
        "returncode": None,
        "stdout": "",
        "stderr": "",
        "generated_files": [],
    }


def print_text(report: dict[str, object]) -> None:
    print(f"SysConfig validation: {report['project']}")
    print(f"- script: {report['script']}")
    tool = report["tool"]
    product = report["product"]
    compiler = report["compiler"]
    output = report["output"]
    assert isinstance(tool, dict)
    assert isinstance(product, dict)
    assert isinstance(compiler, dict)
    assert isinstance(output, dict)
    print(f"- tool: {tool['version']} ({tool['source']})")
    print(f"  {tool['path']}")
    print(f"- product: {product['name']}@{product['version']} ({product['source']})")
    print(f"  {product['path']}")
    print(f"- compiler: {compiler['value'] or '<SysConfig default>'} ({compiler['source']})")
    print(f"- output: {output['path']} (temporary={output['temporary']}, kept={output['kept']})")
    for warning in report.get("warnings", []):
        print(f"WARNING {warning}")
    print(f"- command: {report['command_text']}")
    if report["status"] == "dry-run":
        print("DRY-RUN SysConfig CLI was not executed.")
        return
    stdout = str(report.get("stdout", "")).strip()
    stderr = str(report.get("stderr", "")).strip()
    if stdout:
        print(stdout)
    if stderr:
        print(stderr, file=sys.stderr)
    print(
        f"RESULT status={report['status']} returncode={report['returncode']} "
        f"generated_files={len(report.get('generated_files', []))}"
    )


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Safely validate an MSPM0 .syscfg using TI SysConfig CLI."
    )
    parser.add_argument("project", nargs="?", default=".", help="MSPM0 project directory.")
    parser.add_argument("--script", help="Project-relative .syscfg path when more than one exists.")
    parser.add_argument("--tool", help="Explicit SysConfig CLI file or installation directory.")
    parser.add_argument("--product", help="Explicit MSPM0 SDK root or .metadata/product.json.")
    parser.add_argument("--compiler", help="Override the SysConfig compiler identifier.")
    parser.add_argument("--strict", action="store_true", help="Treat SysConfig warnings as errors.")
    parser.add_argument("--dry-run", action="store_true", help="Resolve and print without generating.")
    parser.add_argument(
        "--keep-output",
        action="store_true",
        help="Keep the newly created temporary output directory for inspection.",
    )
    parser.add_argument("--json", action="store_true", help="Print machine-readable JSON.")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        project = Path(args.project).resolve()
        info = inspect_project(project, args.script)
        tool, tool_warnings, tool_candidates = select_tool(info, args.tool)
        product, product_warnings, product_candidates = select_product(info, args.product)
        compiler, compiler_source = select_compiler(info, args.compiler)
        warnings = tool_warnings + product_warnings

        if args.dry_run:
            output = Path(tempfile.gettempdir()) / "mspm0-sysconfig-<temporary>"
            cleanup = None
        elif args.keep_output:
            output = Path(tempfile.mkdtemp(prefix="mspm0-sysconfig-")).resolve()
            cleanup = None
        else:
            cleanup = tempfile.TemporaryDirectory(prefix="mspm0-sysconfig-")
            output = Path(cleanup.name).resolve()

        cli_args = [
            "--script",
            info.script,
            "--output",
            str(output),
            "--product",
            product.path,
        ]
        if compiler:
            cli_args.extend(["--compiler", compiler])
        if args.strict:
            cli_args.append("--treatWarningsAsErrors")
        command = command_for_tool(Path(tool.path), cli_args)
        report = build_report(
            info,
            tool,
            product,
            compiler,
            compiler_source,
            warnings,
            (tool_candidates, product_candidates),
            output,
            command,
            dry_run=args.dry_run,
            keep_output=args.keep_output,
        )

        if args.dry_run:
            exit_code = 0
        else:
            try:
                result = run_tool(Path(tool.path), cli_args, cwd=project)
            except (OSError, subprocess.SubprocessError) as exc:
                report["status"] = "error"
                report["stderr"] = str(exc)
                report["returncode"] = 1
                exit_code = 1
            else:
                report["returncode"] = result.returncode
                report["stdout"] = result.stdout
                report["stderr"] = result.stderr
                report["generated_files"] = collect_generated_files(output)
                combined = f"{result.stdout}\n{result.stderr}"
                has_warning = bool(warnings) or bool(
                    re.search(r"\bwarning\b", combined, flags=re.IGNORECASE)
                )
                if result.returncode != 0:
                    report["status"] = "error"
                    exit_code = 1
                elif args.strict and has_warning:
                    report["status"] = "error"
                    exit_code = 1
                elif has_warning:
                    report["status"] = "warning"
                    exit_code = 0
                else:
                    report["status"] = "ok"
                    exit_code = 0

        if args.json:
            print(json.dumps(report, ensure_ascii=False, indent=2))
        else:
            print_text(report)
        if cleanup is not None:
            cleanup.cleanup()
        return exit_code
    except ResolutionError as exc:
        if args.json:
            print(json.dumps({"status": "resolution-error", "error": str(exc)}, ensure_ascii=False, indent=2))
        else:
            print(f"ERROR {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
