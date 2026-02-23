#!/usr/bin/env python3
"""Local GUI backend for CERN-2026 simulation workspace.

Features:
- Project storage under Data/projectXXX
- Config save/load per project
- Run Phase I-IV pipeline and store artifacts per run
- Serve static GUI files from GUI/static
"""

from __future__ import annotations

import base64
import copy
import datetime as dt
import json
import mimetypes
import os
import re
import subprocess
import threading
import traceback
import uuid
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, unquote, urlparse


ROOT_DIR = Path(__file__).resolve().parents[1]
DATA_DIR = ROOT_DIR / "Data"
GUI_STATIC_DIR = ROOT_DIR / "GUI" / "static"
SIM_ROOT = ROOT_DIR / "SimOnly-1"
TEMPLATE_CONFIG_PATH = SIM_ROOT / "test_multilayer_config.json"

PHASE_EXECUTABLES = [
    ("phase1", SIM_ROOT / "build" / "PhaseI" / "Release" / "phase1_sim.exe"),
    ("phase2", SIM_ROOT / "build" / "PhaseII" / "Release" / "phase2_sim.exe"),
    ("phase3", SIM_ROOT / "build" / "PhaseIII" / "Release" / "phase3_sim.exe"),
    ("phase4", SIM_ROOT / "build" / "PhaseIV" / "Release" / "phase4_sim.exe"),
]

GEANT_DATA_ROOT = ROOT_DIR / "Geant" / "data"
GEANT_ENV = {
    "G4ENSDFSTATEDATA": str(GEANT_DATA_ROOT / "G4ENSDFSTATE2.3"),
    "G4LEDATA": str(GEANT_DATA_ROOT / "G4EMLOW8.5"),
    "G4LEVELGAMMADATA": str(GEANT_DATA_ROOT / "PhotonEvaporation5.7"),
    "G4RADIOACTIVEDATA": str(GEANT_DATA_ROOT / "RadioactiveDecay5.6"),
    "G4NEUTRONHPDATA": str(GEANT_DATA_ROOT / "G4NDL4.7.1"),
}

PROJECT_DIR_RE = re.compile(r"^project(\d{3,})$")


def utc_now() -> str:
    return (
        dt.datetime.now(dt.UTC)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )


def utc_stamp() -> str:
    return dt.datetime.now(dt.UTC).strftime("%Y%m%d-%H%M%S")


def ensure_dirs() -> None:
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    GUI_STATIC_DIR.mkdir(parents=True, exist_ok=True)


def read_json(path: Path, default: Any | None = None) -> Any:
    if not path.exists():
        return copy.deepcopy(default)
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2)
        handle.write("\n")


def load_template_config() -> dict[str, Any]:
    payload = read_json(TEMPLATE_CONFIG_PATH, default=None)
    if isinstance(payload, dict):
        return payload
    return {
        "phase1": {
            "geometry": {
                "aperture_shape": "box",
                "world_radius_mm": 400.0,
                "world_half_z_mm": 400.0,
                "aperture_half_x_mm": 100.0,
                "aperture_half_y_mm": 100.0,
                "stack_center_z_mm": 0.0,
                "wall_enabled": False,
                "layers": [
                    {
                        "name": "Radiator",
                        "material": "G4_SILICON_DIOXIDE",
                        "thickness_mm": 20.0,
                        "refractive_index": 1.12,
                    }
                ],
            },
            "tracking": {
                "record_secondaries": True,
                "enable_delta_rays": True,
                "store_step_trace": True,
                "range_out_energy_mev": 0.001,
                "delta_engineering": {
                    "strict_mode": True,
                    "electron_cut_mm": 0.005,
                    "positron_cut_mm": 0.005,
                    "gamma_cut_mm": 0.005,
                    "proton_cut_mm": 0.05,
                    "min_energy_mev": 0.001,
                    "max_energy_mev": 100000.0,
                },
            },
            "output": {"dir": "unused"},
            "tracks": [
                {
                    "track_id": 1,
                    "parent_id": 0,
                    "particle": "mu-",
                    "charge": -1.0,
                    "pos_mm": [0.0, 0.0, -20.0],
                    "mom_mev": [0.0, 0.0, 5000.0],
                    "time_ns": 0.0,
                }
            ],
        },
        "phase2": {
            "phase1_output": "unused",
            "phase1_input": "unused",
            "output": {"dir": "unused", "photons_csv": "photons.csv", "metrics_json": "metrics.json"},
            "generation": {"source_mode": "geant4", "enable_cherenkov": True},
        },
        "phase3": {
            "phase1_input": "unused",
            "phase2_output": "unused",
            "phase2_photons_csv": "photons.csv",
            "output": {
                "dir": "unused",
                "hits_csv": "detector_hits.csv",
                "boundary_csv": "boundary_events.csv",
                "transport_csv": "transport_events.csv",
                "metrics_json": "metrics.json",
                "write_boundary_csv": True,
                "write_transport_csv": True,
            },
            "detectors": [
                {
                    "id": "d0",
                    "shape": "box",
                    "material": "G4_Si",
                    "x_mm": 0.0,
                    "y_mm": 0.0,
                    "z_mm": 10.0,
                    "half_x_mm": 80.0,
                    "half_y_mm": 80.0,
                    "half_z_mm": 0.2,
                    "channel_mode": "single",
                }
            ],
        },
        "phase4": {
            "input": {
                "phase3_output": "unused",
                "hits_csv": "detector_hits.csv",
                "boundary_csv": "boundary_events.csv",
                "transport_csv": "transport_events.csv",
                "phase3_metrics_json": "metrics.json",
            },
            "output": {"dir": "unused", "metrics_json": "metrics.json"},
            "detectors": [{"id": "d0", "channels": 1, "topology": "none", "pde_scale": 1.0}],
        },
    }


def list_project_dirs() -> list[Path]:
    ensure_dirs()
    dirs: list[Path] = []
    for entry in DATA_DIR.iterdir():
        if not entry.is_dir():
            continue
        if PROJECT_DIR_RE.match(entry.name):
            dirs.append(entry)
    dirs.sort(key=lambda p: p.name)
    return dirs


def next_project_number() -> int:
    max_num = 0
    for entry in list_project_dirs():
        match = PROJECT_DIR_RE.match(entry.name)
        if not match:
            continue
        max_num = max(max_num, int(match.group(1)))
    return max_num + 1


def make_project_id(number: int) -> str:
    return f"project{number:03d}"


def project_dir(project_id: str) -> Path:
    return DATA_DIR / project_id


def project_metadata_path(project_id: str) -> Path:
    return project_dir(project_id) / "project.json"


def project_config_path(project_id: str) -> Path:
    return project_dir(project_id) / "config.json"


def project_runs_dir(project_id: str) -> Path:
    return project_dir(project_id) / "runs"


def project_assets_dir(project_id: str) -> Path:
    return project_dir(project_id) / "assets"


def create_project(name: str | None = None, config: dict[str, Any] | None = None) -> dict[str, Any]:
    number = next_project_number()
    project_id = make_project_id(number)
    directory = project_dir(project_id)
    directory.mkdir(parents=True, exist_ok=False)
    project_runs_dir(project_id).mkdir(parents=True, exist_ok=True)
    project_assets_dir(project_id).mkdir(parents=True, exist_ok=True)

    now = utc_now()
    metadata = {
        "id": project_id,
        "name": name or f"Simulation Project {number:03d}",
        "created_at": now,
        "updated_at": now,
        "last_run_id": None,
    }
    write_json(project_metadata_path(project_id), metadata)
    write_json(project_config_path(project_id), config or load_template_config())
    return metadata


def read_project_metadata(project_id: str) -> dict[str, Any]:
    path = project_metadata_path(project_id)
    payload = read_json(path, default=None)
    if not isinstance(payload, dict):
        raise FileNotFoundError(f"Project metadata missing for {project_id}")
    return payload


def read_project_config(project_id: str) -> dict[str, Any]:
    path = project_config_path(project_id)
    payload = read_json(path, default=None)
    if not isinstance(payload, dict):
        raise FileNotFoundError(f"Project config missing for {project_id}")
    return payload


def list_projects() -> list[dict[str, Any]]:
    projects: list[dict[str, Any]] = []
    for directory in list_project_dirs():
        project_id = directory.name
        try:
            metadata = read_project_metadata(project_id)
        except FileNotFoundError:
            continue
        projects.append(metadata)
    projects.sort(key=lambda p: p.get("id", ""))
    return projects


def list_runs(project_id: str) -> list[dict[str, Any]]:
    runs: list[dict[str, Any]] = []
    runs_dir = project_runs_dir(project_id)
    if not runs_dir.exists():
        return runs
    for entry in runs_dir.iterdir():
        if not entry.is_dir():
            continue
        record = read_json(entry / "run.json", default=None)
        if isinstance(record, dict):
            runs.append(record)
    runs.sort(key=lambda r: r.get("started_at", ""), reverse=True)
    return runs


def update_project_metadata(project_id: str, **kwargs: Any) -> None:
    metadata = read_project_metadata(project_id)
    metadata.update(kwargs)
    metadata["updated_at"] = utc_now()
    write_json(project_metadata_path(project_id), metadata)


def is_subpath(root: Path, candidate: Path) -> bool:
    try:
        candidate.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


class JobStore:
    def __init__(self) -> None:
        self._jobs: dict[str, dict[str, Any]] = {}
        self._lock = threading.Lock()

    def create(self, project_id: str) -> dict[str, Any]:
        with self._lock:
            job_id = f"job-{uuid.uuid4().hex[:10]}"
            payload = {
                "id": job_id,
                "project_id": project_id,
                "status": "queued",
                "message": "Queued",
                "created_at": utc_now(),
                "started_at": None,
                "ended_at": None,
                "run_id": None,
                "phases": [],
                "error": None,
            }
            self._jobs[job_id] = payload
            return copy.deepcopy(payload)

    def update(self, job_id: str, **kwargs: Any) -> None:
        with self._lock:
            if job_id not in self._jobs:
                return
            self._jobs[job_id].update(kwargs)

    def append_phase(self, job_id: str, phase_record: dict[str, Any]) -> None:
        with self._lock:
            if job_id not in self._jobs:
                return
            self._jobs[job_id]["phases"].append(phase_record)

    def get(self, job_id: str) -> dict[str, Any] | None:
        with self._lock:
            payload = self._jobs.get(job_id)
            if payload is None:
                return None
            return copy.deepcopy(payload)

    def list(self) -> list[dict[str, Any]]:
        with self._lock:
            return [copy.deepcopy(v) for v in self._jobs.values()]

    def running_for_project(self, project_id: str) -> bool:
        with self._lock:
            for job in self._jobs.values():
                if job.get("project_id") != project_id:
                    continue
                if job.get("status") in {"queued", "running"}:
                    return True
            return False


JOBS = JobStore()


def phase_env() -> dict[str, str]:
    env = os.environ.copy()
    env.update(GEANT_ENV)
    return env


def default_optical_index(material_name: str, preferred: float | None = None) -> float:
    if isinstance(preferred, (float, int)) and preferred > 0:
        return float(preferred)
    name = material_name.lower()
    if "air" in name or "galactic" in name or "vacuum" in name:
        return 1.0003
    if name in {"g4_si", "g4_silicon"} or "silicon" in name:
        return 3.5
    if "water" in name:
        return 1.33
    return 1.5


def ensure_phase3_material(
    phase3: dict[str, Any],
    material_name: str,
    preferred_index: float | None = None,
) -> None:
    if not isinstance(material_name, str) or not material_name:
        return
    materials = phase3.get("materials")
    if not isinstance(materials, list):
        materials = []
        phase3["materials"] = materials
    existing = set()
    for item in materials:
        if isinstance(item, dict):
            name = item.get("name")
            if isinstance(name, str) and name:
                existing.add(name)
    if material_name in existing:
        return
    idx = default_optical_index(material_name, preferred_index)
    materials.append(
        {
            "name": material_name,
            "base_material": material_name,
            "constant_index": idx,
            "absorption_length_table": {
                "lambda_nm": [250.0, 650.0],
                "values": [1000000.0, 1000000.0],
            },
        }
    )


def ensure_phase2_material(
    phase2: dict[str, Any],
    material_name: str,
    preferred_index: float | None = None,
) -> None:
    if not isinstance(material_name, str) or not material_name:
        return
    materials = phase2.get("materials")
    if not isinstance(materials, list):
        materials = []
        phase2["materials"] = materials
    existing = set()
    for item in materials:
        if isinstance(item, dict):
            name = item.get("name")
            if isinstance(name, str) and name:
                existing.add(name)
    if material_name in existing:
        return
    idx = default_optical_index(material_name, preferred_index)
    materials.append(
        {
            "name": material_name,
            "model": "constant",
            "coeffs": [idx],
            "absorption_length_table": {
                "lambda_nm": [250.0, 650.0],
                "values": [1000000.0, 1000000.0],
                "extrapolation": "clamp_edge",
            },
        }
    )


def sync_phase4_detectors_from_phase3(resolved: dict[str, Any]) -> None:
    phase3 = resolved.setdefault("phase3", {})
    phase4 = resolved.setdefault("phase4", {})
    detectors3 = phase3.get("detectors")
    if not isinstance(detectors3, list):
        return

    existing4 = {}
    detectors4 = phase4.get("detectors")
    if isinstance(detectors4, list):
        for d in detectors4:
            if isinstance(d, dict):
                did = d.get("id")
                if isinstance(did, str) and did:
                    existing4[did] = d
    else:
        detectors4 = []

    rebuilt = []
    channel_limits: dict[str, int] = {}
    def to_int(value: Any, fallback: int) -> int:
        try:
            return int(value)
        except (TypeError, ValueError):
            return fallback
    def to_float(value: Any, fallback: float) -> float:
        try:
            return float(value)
        except (TypeError, ValueError):
            return fallback
    for det in detectors3:
        if not isinstance(det, dict):
            continue
        did = det.get("id")
        if not isinstance(did, str) or not did:
            continue
        mode = str(det.get("channel_mode", "single"))
        if mode == "grid_xy":
            channels = max(1, to_int(det.get("channels_x", 1), 1) * to_int(det.get("channels_y", 1), 1))
        elif mode == "grid_z":
            channels = max(1, to_int(det.get("channels_z", 1), 1))
        else:
            channels = 1
        channel_limits[did] = channels
        base = existing4.get(did, {})
        rebuilt.append(
            {
                "id": did,
                "channels": channels,
                "topology": base.get("topology", "none"),
                "pde_scale": to_float(base.get("pde_scale", 1.0), 1.0),
                "dcr_scale": to_float(base.get("dcr_scale", 1.0), 1.0),
                "gain_scale": to_float(base.get("gain_scale", 1.0), 1.0),
                "time_offset_ns": to_float(base.get("time_offset_ns", 0.0), 0.0),
                "grid_x": to_int(base.get("grid_x", 0), 0),
                "grid_y": to_int(base.get("grid_y", 0), 0),
            }
        )
    phase4["detectors"] = rebuilt

    overrides = phase4.get("channel_overrides")
    if not isinstance(overrides, list):
        phase4["channel_overrides"] = []
        return
    filtered = []
    for entry in overrides:
        if not isinstance(entry, dict):
            continue
        did = entry.get("detector_id")
        cid = entry.get("channel_id")
        if not isinstance(did, str) or did not in channel_limits:
            continue
        if not isinstance(cid, int) or cid < 0 or cid >= channel_limits[did]:
            continue
        filtered.append(entry)
    phase4["channel_overrides"] = filtered


def phase_output_paths(config: dict[str, Any], run_dir: Path, project_id: str) -> dict[str, Any]:
    resolved = copy.deepcopy(config)
    run_dir = run_dir.resolve()

    phase1_dir = run_dir / "phase1"
    phase2_dir = run_dir / "phase2"
    phase3_dir = run_dir / "phase3"
    phase4_dir = run_dir / "phase4"
    for directory in (phase1_dir, phase2_dir, phase3_dir, phase4_dir, run_dir / "logs"):
        directory.mkdir(parents=True, exist_ok=True)

    resolved.setdefault("phase1", {}).setdefault("output", {})["dir"] = str(phase1_dir)

    phase2 = resolved.setdefault("phase2", {})
    phase2["phase1_output"] = str(phase1_dir)
    phase2["phase1_input"] = str(project_config_path(project_id).resolve())
    phase2.setdefault("output", {})["dir"] = str(phase2_dir)
    phase2.setdefault("output", {}).setdefault("photons_csv", "photons.csv")
    phase2.setdefault("output", {}).setdefault("metrics_json", "metrics.json")
    aliases = phase2.get("material_aliases")
    if not isinstance(aliases, dict):
        aliases = {}
        phase2["material_aliases"] = aliases
    layers = resolved.get("phase1", {}).get("geometry", {}).get("layers", [])
    if isinstance(layers, list):
        for idx, layer in enumerate(layers):
            if not isinstance(layer, dict):
                continue
            layer_name = str(layer.get("name", f"Layer{idx + 1}"))
            material = layer.get("material")
            if isinstance(material, str) and material:
                aliases.setdefault(f"Layer_{idx}_{layer_name}_Material", material)
                ri = layer.get("refractive_index")
                ensure_phase2_material(phase2, material, float(ri) if isinstance(ri, (float, int)) else None)

    phase3 = resolved.setdefault("phase3", {})
    phase3["phase1_input"] = str(project_config_path(project_id).resolve())
    phase3["phase2_output"] = str(phase2_dir)
    phase3.setdefault("phase2_photons_csv", "photons.csv")
    phase3.setdefault("output", {})["dir"] = str(phase3_dir)
    phase3.setdefault("output", {}).setdefault("hits_csv", "detector_hits.csv")
    phase3.setdefault("output", {}).setdefault("boundary_csv", "boundary_events.csv")
    phase3.setdefault("output", {}).setdefault("transport_csv", "transport_events.csv")
    phase3.setdefault("output", {}).setdefault("metrics_json", "metrics.json")
    phase3.setdefault("output", {}).setdefault("write_boundary_csv", True)
    phase3.setdefault("output", {}).setdefault("write_transport_csv", True)
    phase3_aliases = phase3.get("material_aliases")
    if not isinstance(phase3_aliases, dict):
        phase3_aliases = {}
        phase3["material_aliases"] = phase3_aliases
    if isinstance(layers, list):
        for idx, layer in enumerate(layers):
            if not isinstance(layer, dict):
                continue
            layer_name = str(layer.get("name", f"Layer{idx + 1}"))
            material = layer.get("material")
            if not isinstance(material, str) or not material:
                continue
            ri = layer.get("refractive_index")
            ensure_phase3_material(phase3, material, float(ri) if isinstance(ri, (float, int)) else None)
            phase3_aliases.setdefault(f"Layer_{idx}_{layer_name}_Material", material)
    for det in phase3.get("detectors", []):
        if not isinstance(det, dict):
            continue
        material = det.get("material")
        if isinstance(material, str) and material:
            ensure_phase3_material(phase3, material, None)
    ensure_phase3_material(phase3, "G4_AIR", 1.0003)

    phase4 = resolved.setdefault("phase4", {})
    phase4_input = phase4.setdefault("input", {})
    phase4_input["phase3_output"] = str(phase3_dir)
    phase4_input.setdefault("hits_csv", "detector_hits.csv")
    phase4_input.setdefault("boundary_csv", "boundary_events.csv")
    phase4_input.setdefault("transport_csv", "transport_events.csv")
    phase4_input.setdefault("phase3_metrics_json", "metrics.json")
    phase4.setdefault("output", {})["dir"] = str(phase4_dir)
    phase4.setdefault("output", {}).setdefault("metrics_json", "metrics.json")
    phase4.setdefault("output", {}).setdefault("transport_meta_json", "transport_meta.json")
    sync_phase4_detectors_from_phase3(resolved)

    return resolved


def collect_metrics(run_dir: Path) -> dict[str, Any]:
    metrics: dict[str, Any] = {}
    paths = {
        "phase2": run_dir / "phase2" / "metrics.json",
        "phase3": run_dir / "phase3" / "metrics.json",
        "phase4": run_dir / "phase4" / "metrics.json",
        "phase4_transport_meta": run_dir / "phase4" / "transport_meta.json",
    }
    for key, path in paths.items():
        payload = read_json(path, default=None)
        if isinstance(payload, dict):
            metrics[key] = payload
    return metrics


def failure_summary(log_path: Path) -> str:
    if not log_path.exists():
        return ""
    try:
        with log_path.open("r", encoding="utf-8", errors="replace") as handle:
            lines = handle.readlines()
        for raw in reversed(lines):
            line = raw.strip()
            if not line:
                continue
            if "error" in line.lower() or "failed" in line.lower() or "exception" in line.lower():
                return line
        for raw in reversed(lines):
            line = raw.strip()
            if line:
                return line
    except Exception:
        return ""
    return ""


def run_pipeline_job(job_id: str, project_id: str) -> None:
    metadata = read_project_metadata(project_id)
    config = read_project_config(project_id)

    started = utc_now()
    run_id = f"run-{utc_stamp()}"
    run_dir = project_runs_dir(project_id) / run_id
    suffix = 1
    while run_dir.exists():
        run_id = f"run-{utc_stamp()}-{suffix:02d}"
        run_dir = project_runs_dir(project_id) / run_id
        suffix += 1
    run_dir.mkdir(parents=True, exist_ok=True)
    (run_dir / "logs").mkdir(parents=True, exist_ok=True)

    run_record: dict[str, Any] = {
        "id": run_id,
        "project_id": project_id,
        "project_name": metadata.get("name", project_id),
        "job_id": job_id,
        "status": "running",
        "started_at": started,
        "ended_at": None,
        "phases": [],
        "metrics": {},
        "error": None,
    }
    write_json(run_dir / "run.json", run_record)

    JOBS.update(job_id, status="running", started_at=started, run_id=run_id, message="Running phase1")

    resolved_config = phase_output_paths(config, run_dir, project_id)
    resolved_config_path = run_dir / "config.json"
    write_json(resolved_config_path, resolved_config)

    env = phase_env()
    try:
        for phase_name, exe in PHASE_EXECUTABLES:
            phase_started = utc_now()
            JOBS.update(job_id, message=f"Running {phase_name}")
            if not exe.exists():
                raise RuntimeError(f"Missing executable: {exe}")

            log_path = run_dir / "logs" / f"{phase_name}.log"
            with log_path.open("w", encoding="utf-8") as log_handle:
                process = subprocess.Popen(
                    [str(exe), str(resolved_config_path)],
                    cwd=str(SIM_ROOT),
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    env=env,
                )
                assert process.stdout is not None
                for line in process.stdout:
                    log_handle.write(line)
                return_code = process.wait()

            phase_record = {
                "phase": phase_name,
                "started_at": phase_started,
                "ended_at": utc_now(),
                "exit_code": return_code,
                "log_path": str(log_path.relative_to(run_dir)),
                "status": "completed" if return_code == 0 else "failed",
            }
            if return_code != 0:
                summary = failure_summary(log_path)
                if summary:
                    phase_record["failure_summary"] = summary
            run_record["phases"].append(phase_record)
            JOBS.append_phase(job_id, phase_record)
            write_json(run_dir / "run.json", run_record)

            if return_code != 0:
                error_msg = f"{phase_name} failed with exit code {return_code}"
                summary = phase_record.get("failure_summary")
                if isinstance(summary, str) and summary:
                    error_msg = f"{error_msg}: {summary}"
                JOBS.update(job_id, status="failed", message=error_msg, error=error_msg, ended_at=utc_now())
                run_record["status"] = "failed"
                run_record["error"] = error_msg
                run_record["ended_at"] = utc_now()
                write_json(run_dir / "run.json", run_record)
                update_project_metadata(project_id, last_run_id=run_id)
                return

        run_record["metrics"] = collect_metrics(run_dir)
        run_record["status"] = "completed"
        run_record["ended_at"] = utc_now()
        write_json(run_dir / "run.json", run_record)
        update_project_metadata(project_id, last_run_id=run_id)
        JOBS.update(
            job_id,
            status="completed",
            ended_at=run_record["ended_at"],
            message="Completed",
        )
    except Exception as exc:  # pylint: disable=broad-except
        error_msg = str(exc)
        run_record["status"] = "failed"
        run_record["error"] = error_msg
        run_record["ended_at"] = utc_now()
        write_json(run_dir / "run.json", run_record)
        update_project_metadata(project_id, last_run_id=run_id)
        JOBS.update(
            job_id,
            status="failed",
            error=error_msg,
            message=error_msg,
            ended_at=run_record["ended_at"],
        )


def start_run(project_id: str) -> dict[str, Any]:
    if JOBS.running_for_project(project_id):
        raise RuntimeError("A run is already active for this project.")
    job = JOBS.create(project_id)
    thread = threading.Thread(target=run_pipeline_job, args=(job["id"], project_id), daemon=True)
    thread.start()
    return job


class AppHandler(BaseHTTPRequestHandler):
    server_version = "CERN2026GUI/1.0"

    def do_GET(self) -> None:  # noqa: N802
        try:
            parsed = urlparse(self.path)
            path = parsed.path

            if path.startswith("/api/"):
                self.handle_api_get(path, parse_qs(parsed.query))
                return
            self.serve_static(path)
        except Exception as exc:  # pylint: disable=broad-except
            traceback.print_exc()
            self.send_error_json(HTTPStatus.INTERNAL_SERVER_ERROR, f"Internal server error: {exc}")

    def do_POST(self) -> None:  # noqa: N802
        try:
            parsed = urlparse(self.path)
            path = parsed.path
            if not path.startswith("/api/"):
                self.send_error_json(HTTPStatus.NOT_FOUND, "Unknown endpoint.")
                return
            payload = self.read_json_body()
            if payload is None:
                return
            self.handle_api_post(path, payload)
        except Exception as exc:  # pylint: disable=broad-except
            traceback.print_exc()
            self.send_error_json(HTTPStatus.INTERNAL_SERVER_ERROR, f"Internal server error: {exc}")

    def do_PUT(self) -> None:  # noqa: N802
        try:
            parsed = urlparse(self.path)
            path = parsed.path
            if not path.startswith("/api/"):
                self.send_error_json(HTTPStatus.NOT_FOUND, "Unknown endpoint.")
                return
            payload = self.read_json_body()
            if payload is None:
                return
            self.handle_api_put(path, payload)
        except Exception as exc:  # pylint: disable=broad-except
            traceback.print_exc()
            self.send_error_json(HTTPStatus.INTERNAL_SERVER_ERROR, f"Internal server error: {exc}")

    def do_OPTIONS(self) -> None:  # noqa: N802
        self.send_response(HTTPStatus.NO_CONTENT)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, PUT, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def log_message(self, fmt: str, *args: Any) -> None:
        super().log_message(fmt, *args)

    def send_json(self, status: HTTPStatus, payload: Any) -> None:
        raw = json.dumps(payload, ensure_ascii=True).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(raw)

    def send_error_json(self, status: HTTPStatus, message: str) -> None:
        self.send_json(status, {"error": message})

    def read_json_body(self) -> dict[str, Any] | list[Any] | None:
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self.send_error_json(HTTPStatus.BAD_REQUEST, "Invalid Content-Length.")
            return None
        body = self.rfile.read(length) if length > 0 else b"{}"
        try:
            return json.loads(body.decode("utf-8"))
        except json.JSONDecodeError:
            self.send_error_json(HTTPStatus.BAD_REQUEST, "Invalid JSON body.")
            return None

    def serve_static(self, raw_path: str) -> None:
        path = unquote(raw_path)
        if path == "/":
            path = "/static/index.html"
        if path == "/favicon.ico":
            path = "/static/favicon.svg"
        if not path.startswith("/static/"):
            self.send_error_json(HTTPStatus.NOT_FOUND, "Not found.")
            return
        rel = path[len("/static/") :]
        file_path = (GUI_STATIC_DIR / rel).resolve()
        if not is_subpath(GUI_STATIC_DIR, file_path) or not file_path.exists() or not file_path.is_file():
            self.send_error_json(HTTPStatus.NOT_FOUND, "File not found.")
            return
        content = file_path.read_bytes()
        ctype, _ = mimetypes.guess_type(str(file_path))
        if ctype is None:
            ctype = "application/octet-stream"
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        self.end_headers()
        self.wfile.write(content)

    def handle_api_get(self, path: str, query: dict[str, list[str]]) -> None:
        if path == "/api/status":
            payload = {
                "data_dir": str(DATA_DIR),
                "sim_root": str(SIM_ROOT),
                "projects": len(list_projects()),
                "jobs": JOBS.list(),
                "executables": {name: exe.exists() for name, exe in PHASE_EXECUTABLES},
                "geant_env": GEANT_ENV,
            }
            self.send_json(HTTPStatus.OK, payload)
            return

        if path == "/api/projects":
            self.send_json(HTTPStatus.OK, {"projects": list_projects()})
            return

        match = re.fullmatch(r"/api/projects/(project\d{3,})", path)
        if match:
            project_id = match.group(1)
            if not project_dir(project_id).exists():
                self.send_error_json(HTTPStatus.NOT_FOUND, "Project not found.")
                return
            payload = {
                "project": read_project_metadata(project_id),
                "config": read_project_config(project_id),
                "runs": list_runs(project_id),
            }
            self.send_json(HTTPStatus.OK, payload)
            return

        match = re.fullmatch(r"/api/projects/(project\d{3,})/runs", path)
        if match:
            project_id = match.group(1)
            if not project_dir(project_id).exists():
                self.send_error_json(HTTPStatus.NOT_FOUND, "Project not found.")
                return
            self.send_json(HTTPStatus.OK, {"runs": list_runs(project_id)})
            return

        match = re.fullmatch(r"/api/projects/(project\d{3,})/runs/([^/]+)", path)
        if match:
            project_id = match.group(1)
            run_id = match.group(2)
            run_path = project_runs_dir(project_id) / run_id / "run.json"
            record = read_json(run_path, default=None)
            if not isinstance(record, dict):
                self.send_error_json(HTTPStatus.NOT_FOUND, "Run not found.")
                return
            self.send_json(HTTPStatus.OK, {"run": record})
            return

        match = re.fullmatch(r"/api/projects/(project\d{3,})/runs/([^/]+)/file", path)
        if match:
            project_id = match.group(1)
            run_id = match.group(2)
            rel_values = query.get("path", [])
            if not rel_values:
                self.send_error_json(HTTPStatus.BAD_REQUEST, "Missing query parameter: path")
                return
            rel_path = Path(rel_values[0])
            run_root = (project_runs_dir(project_id) / run_id).resolve()
            file_path = (run_root / rel_path).resolve()
            if not is_subpath(run_root, file_path) or not file_path.exists() or not file_path.is_file():
                self.send_error_json(HTTPStatus.NOT_FOUND, "Artifact file not found.")
                return
            content = file_path.read_bytes()
            ctype, _ = mimetypes.guess_type(str(file_path))
            if ctype is None:
                ctype = "application/octet-stream"
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(content)))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
            self.send_header("Pragma", "no-cache")
            self.send_header("Expires", "0")
            self.end_headers()
            self.wfile.write(content)
            return

        match = re.fullmatch(r"/api/projects/(project\d{3,})/assets", path)
        if match:
            project_id = match.group(1)
            assets = []
            root = project_assets_dir(project_id)
            root.mkdir(parents=True, exist_ok=True)
            for item in root.rglob("*"):
                if item.is_file():
                    assets.append(str(item.relative_to(root)))
            assets.sort()
            self.send_json(HTTPStatus.OK, {"assets": assets})
            return

        match = re.fullmatch(r"/api/jobs/(job-[a-f0-9]+)", path)
        if match:
            job_id = match.group(1)
            job = JOBS.get(job_id)
            if job is None:
                self.send_error_json(HTTPStatus.NOT_FOUND, "Job not found.")
                return
            self.send_json(HTTPStatus.OK, {"job": job})
            return

        self.send_error_json(HTTPStatus.NOT_FOUND, "Unknown endpoint.")

    def handle_api_post(self, path: str, payload: Any) -> None:
        if path == "/api/projects":
            if not isinstance(payload, dict):
                self.send_error_json(HTTPStatus.BAD_REQUEST, "Expected JSON object.")
                return
            name = payload.get("name")
            template_cfg = payload.get("config")
            if template_cfg is not None and not isinstance(template_cfg, dict):
                self.send_error_json(HTTPStatus.BAD_REQUEST, "config must be an object when provided.")
                return
            metadata = create_project(name=name if isinstance(name, str) else None, config=template_cfg)
            self.send_json(HTTPStatus.CREATED, {"project": metadata})
            return

        match = re.fullmatch(r"/api/projects/(project\d{3,})/run", path)
        if match:
            project_id = match.group(1)
            if not project_dir(project_id).exists():
                self.send_error_json(HTTPStatus.NOT_FOUND, "Project not found.")
                return
            try:
                job = start_run(project_id)
            except RuntimeError as exc:
                self.send_error_json(HTTPStatus.CONFLICT, str(exc))
                return
            self.send_json(HTTPStatus.ACCEPTED, {"job": job})
            return

        match = re.fullmatch(r"/api/projects/(project\d{3,})/assets", path)
        if match:
            project_id = match.group(1)
            if not isinstance(payload, dict):
                self.send_error_json(HTTPStatus.BAD_REQUEST, "Expected JSON object.")
                return
            filename = payload.get("filename")
            data_b64 = payload.get("content_base64")
            if not isinstance(filename, str) or not filename:
                self.send_error_json(HTTPStatus.BAD_REQUEST, "filename is required.")
                return
            if not isinstance(data_b64, str) or not data_b64:
                self.send_error_json(HTTPStatus.BAD_REQUEST, "content_base64 is required.")
                return
            safe_name = Path(filename).name
            target = project_assets_dir(project_id) / safe_name
            try:
                raw = base64.b64decode(data_b64.encode("ascii"))
            except Exception:
                self.send_error_json(HTTPStatus.BAD_REQUEST, "Invalid base64 data.")
                return
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open("wb") as handle:
                handle.write(raw)
            self.send_json(
                HTTPStatus.CREATED,
                {
                    "asset": {
                        "filename": safe_name,
                        "relative_path": f"assets/{safe_name}",
                        "absolute_path": str(target.resolve()),
                    }
                },
            )
            return

        self.send_error_json(HTTPStatus.NOT_FOUND, "Unknown endpoint.")

    def handle_api_put(self, path: str, payload: Any) -> None:
        match = re.fullmatch(r"/api/projects/(project\d{3,})/config", path)
        if not match:
            self.send_error_json(HTTPStatus.NOT_FOUND, "Unknown endpoint.")
            return
        project_id = match.group(1)
        if not project_dir(project_id).exists():
            self.send_error_json(HTTPStatus.NOT_FOUND, "Project not found.")
            return
        if not isinstance(payload, dict):
            self.send_error_json(HTTPStatus.BAD_REQUEST, "Expected JSON object.")
            return

        config = payload.get("config", payload)
        if not isinstance(config, dict):
            self.send_error_json(HTTPStatus.BAD_REQUEST, "config must be a JSON object.")
            return

        write_json(project_config_path(project_id), config)
        update_project_metadata(project_id)
        self.send_json(HTTPStatus.OK, {"ok": True})


def bootstrap_default_project() -> None:
    if list_projects():
        return
    create_project(name="Default Project", config=load_template_config())


def main() -> None:
    ensure_dirs()
    bootstrap_default_project()
    host = os.environ.get("GUI_HOST", "127.0.0.1")
    port = int(os.environ.get("GUI_PORT", "8080"))
    server = ThreadingHTTPServer((host, port), AppHandler)
    print(f"GUI server listening on http://{host}:{port}")
    print(f"Static root: {GUI_STATIC_DIR}")
    print(f"Data root:   {DATA_DIR}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
