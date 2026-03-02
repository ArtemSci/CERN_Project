# CERN-2026

This repository contains the CERN-2026 simulation pipeline (`SimOnly-1`) and local GUI backend (`GUI`).

The `Geant` folder is intentionally excluded from git because it contains large Geant4 data files.

## GUI Runtime Requirements

To run `GUI/server.py`:

- Python: `3.12.10` (exact version)
- pip packages: none
- Libraries used by the server:
  - Python standard library modules only (version is the Python runtime version above)
  - `base64`, `copy`, `datetime`, `json`, `mimetypes`, `os`, `re`, `subprocess`, `threading`, `traceback`, `uuid`, `http`, `pathlib`, `typing`, `urllib.parse`

## Geant4 Compatibility

This codebase is compatible with **Geant4 11.2.x** and expects the Geant data packages below:

- `G4EMLOW8.5`
- `G4ENSDFSTATE2.3`
- `G4NDL4.7.1`
- `PhotonEvaporation5.7`
- `RadioactiveDecay5.6`

## How To Install Compatible Geant Data

1. Download/install Geant4 11.2.x from the official CERN Geant4 page:  
   https://geant4.web.cern.ch/support/download
2. Make sure Geant4 datasets are installed (for source installs, set `-DGEANT4_INSTALL_DATA=ON`).
3. In this repo, create:
   - `Geant/data`
4. Copy these dataset folders into `Geant/data`:
   - `G4EMLOW8.5`
   - `G4ENSDFSTATE2.3`
   - `G4NDL4.7.1`
   - `PhotonEvaporation5.7`
   - `RadioactiveDecay5.6`

Expected final structure:

```text
CERN-2026/
  Geant/
    data/
      G4EMLOW8.5/
      G4ENSDFSTATE2.3/
      G4NDL4.7.1/
      PhotonEvaporation5.7/
      RadioactiveDecay5.6/
```

## Run GUI

From repository root:

```powershell
py -3 GUI\server.py
```

Open:

`http://127.0.0.1:8080`
