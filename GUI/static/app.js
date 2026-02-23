const state = {
  projects: [],
  projectId: null,
  projectMeta: null,
  config: null,
  runs: [],
  selected: { type: "world", index: -1 },
  mode: "simple",
  dirty: false,
  activeRunId: null,
  activeRun: null,
  hits: [],
  currentJobId: null,
  pollTimer: null,
  logs: [],
  view: { yaw: 0.35, pitch: -0.2, zoom: 1.0, dragging: false, x: 0, y: 0 },
  transportRows: [],
  stepRows: [],
  waveformRows: [],
  photonTracks: {},
  photonTrackKeys: [],
  selectedPhotonTrackKeys: [],
  particleTracks: [],
  trackSummaryMap: {},
  waveformChannels: [],
  selectedWaveformChannel: null,
  waveformMode: "current_pe",
  visualization: {
    showPhotonTracks: true,
    photonTrackCount: 25,
    showParticleTracks: true,
    showDeltaTracks: true,
    showDetectorHits: true,
    modelOpacity: 0.95,
    deltaTrackCount: 120,
  },
};

const SENSOR_TEMPLATES = {
  sipm_tile: {
    shape: "box",
    material: "G4_Si",
    half_x_mm: 3.0,
    half_y_mm: 3.0,
    half_z_mm: 0.25,
    channel_mode: "single",
    photosensor_shape: "rect",
    photosensor_offset_x_mm: 0.0,
    photosensor_offset_y_mm: 0.0,
    photosensor_half_x_mm: 2.8,
    photosensor_half_y_mm: 2.8,
    photosensor_radius_mm: 2.8,
    photosensor_inner_radius_mm: 0.0,
    photosensor_strip_width_mm: 0.4,
  },
  apd_strip: {
    shape: "box",
    material: "G4_Si",
    half_x_mm: 5.0,
    half_y_mm: 1.0,
    half_z_mm: 0.2,
    channel_mode: "grid_xy",
    channels_x: 8,
    channels_y: 1,
    photosensor_shape: "strip_x",
    photosensor_offset_x_mm: 0.0,
    photosensor_offset_y_mm: 0.0,
    photosensor_half_x_mm: 5.0,
    photosensor_half_y_mm: 0.5,
    photosensor_radius_mm: 0.8,
    photosensor_inner_radius_mm: 0.0,
    photosensor_strip_width_mm: 0.5,
  },
  pmt_disk: {
    shape: "cylinder",
    material: "G4_SILICON_DIOXIDE",
    radius_mm: 25.0,
    half_z_mm: 2.0,
    channel_mode: "single",
    photosensor_shape: "circle",
    photosensor_offset_x_mm: 0.0,
    photosensor_offset_y_mm: 0.0,
    photosensor_half_x_mm: 10.0,
    photosensor_half_y_mm: 10.0,
    photosensor_radius_mm: 22.0,
    photosensor_inner_radius_mm: 8.0,
    photosensor_strip_width_mm: 1.0,
  },
  generic_box: {
    shape: "box",
    material: "G4_Si",
    half_x_mm: 10.0,
    half_y_mm: 10.0,
    half_z_mm: 0.5,
    channel_mode: "single",
    photosensor_shape: "full",
    photosensor_offset_x_mm: 0.0,
    photosensor_offset_y_mm: 0.0,
    photosensor_half_x_mm: 10.0,
    photosensor_half_y_mm: 10.0,
    photosensor_radius_mm: 10.0,
    photosensor_inner_radius_mm: 0.0,
    photosensor_strip_width_mm: 1.0,
  },
  generic_cylinder: {
    shape: "cylinder",
    material: "G4_Si",
    radius_mm: 12.0,
    half_z_mm: 0.8,
    channel_mode: "single",
    photosensor_shape: "full",
    photosensor_offset_x_mm: 0.0,
    photosensor_offset_y_mm: 0.0,
    photosensor_half_x_mm: 8.0,
    photosensor_half_y_mm: 8.0,
    photosensor_radius_mm: 12.0,
    photosensor_inner_radius_mm: 0.0,
    photosensor_strip_width_mm: 1.0,
  },
};

function logLine(msg, level = "INFO") {
  const ts = new Date().toISOString().slice(11, 19);
  state.logs.push(`[${ts}] [${level}] ${msg}`);
  if (state.logs.length > 400) state.logs.shift();
  const el = document.getElementById("logBox");
  if (el) {
    el.textContent = state.logs.join("\n");
    el.scrollTop = el.scrollHeight;
  }
}

async function api(path, method = "GET", body = null) {
  const opts = { method, headers: {} };
  if (body !== null) {
    opts.headers["Content-Type"] = "application/json";
    opts.body = JSON.stringify(body);
  }
  const res = await fetch(path, opts);
  if (!res.ok) {
    let text = `${res.status} ${res.statusText}`;
    try {
      const payload = await res.json();
      if (payload && payload.error) text = payload.error;
    } catch (_) {}
    throw new Error(text);
  }
  if (res.headers.get("content-type")?.includes("application/json")) {
    return res.json();
  }
  return res.text();
}

function setStatus(text, cls = "") {
  const el = document.getElementById("statusBadge");
  if (!el) return;
  el.textContent = text;
  el.className = "status-badge";
  if (cls) el.classList.add(cls);
}

function setDirty(flag) {
  state.dirty = !!flag;
  if (state.projectMeta) {
    document.title = `${state.projectMeta.name}${state.dirty ? " *" : ""} - CERN-2026 GUI`;
  }
}

function ensureDefaults() {
  if (!state.config.phase1) state.config.phase1 = {};
  if (!state.config.phase1.geometry) state.config.phase1.geometry = {};
  if (!state.config.phase1.geometry.layers) state.config.phase1.geometry.layers = [];
  if (!state.config.phase1.tracks) state.config.phase1.tracks = [];
  if (!state.config.phase2) state.config.phase2 = { generation: {} };
  if (!state.config.phase2.generation) state.config.phase2.generation = {};
  if (!state.config.phase3) state.config.phase3 = {};
  if (!state.config.phase3.detectors) state.config.phase3.detectors = [];
  if (!state.config.phase3.tracking) state.config.phase3.tracking = {};
  if (!state.config.phase4) state.config.phase4 = {};
  if (!state.config.phase4.sensor) state.config.phase4.sensor = {};
  if (!state.config.phase4.noise) state.config.phase4.noise = {};
  if (!state.config.phase4.detection) state.config.phase4.detection = {};
  for (const layer of state.config.phase1.geometry.layers) {
    if (layer && typeof layer === "object" && typeof layer.visible !== "boolean") layer.visible = true;
  }
  for (const det of state.config.phase3.detectors) ensureDetectorDefaults(det);
}

function ensureDetectorDefaults(det) {
  if (!det || typeof det !== "object") return;
  if (!det.id) det.id = `det${state.config?.phase3?.detectors?.indexOf(det) ?? 0}`;
  if (!det.shape) det.shape = "box";
  if (!det.material) det.material = "G4_Si";
  if (!det.channel_mode) det.channel_mode = "single";
  if (typeof det.visible !== "boolean") det.visible = true;
  if (!Number.isFinite(Number(det.x_mm))) det.x_mm = 0;
  if (!Number.isFinite(Number(det.y_mm))) det.y_mm = 0;
  if (!Number.isFinite(Number(det.z_mm))) det.z_mm = 0;
  if ((det.shape || "box") === "cylinder") {
    if (!Number.isFinite(Number(det.radius_mm))) det.radius_mm = 10;
    if (!Number.isFinite(Number(det.half_z_mm))) det.half_z_mm = 1;
  } else {
    if (!Number.isFinite(Number(det.half_x_mm))) det.half_x_mm = 10;
    if (!Number.isFinite(Number(det.half_y_mm))) det.half_y_mm = 10;
    if (!Number.isFinite(Number(det.half_z_mm))) det.half_z_mm = 1;
  }
  if (!det.photosensor_shape) det.photosensor_shape = "full";
  if (!Number.isFinite(Number(det.photosensor_offset_x_mm))) det.photosensor_offset_x_mm = 0;
  if (!Number.isFinite(Number(det.photosensor_offset_y_mm))) det.photosensor_offset_y_mm = 0;
  if (!Number.isFinite(Number(det.photosensor_half_x_mm))) det.photosensor_half_x_mm = Math.max(0.1, num(det.half_x_mm, 10) * 0.9);
  if (!Number.isFinite(Number(det.photosensor_half_y_mm))) det.photosensor_half_y_mm = Math.max(0.1, num(det.half_y_mm, 10) * 0.9);
  if (!Number.isFinite(Number(det.photosensor_radius_mm))) det.photosensor_radius_mm = Math.max(0.1, num(det.radius_mm, 10) * 0.9);
  if (!Number.isFinite(Number(det.photosensor_inner_radius_mm))) det.photosensor_inner_radius_mm = 0;
  if (!Number.isFinite(Number(det.photosensor_strip_width_mm))) det.photosensor_strip_width_mm = 1.0;
}

async function loadProjects() {
  const payload = await api("/api/projects");
  state.projects = payload.projects || [];
  if (state.projects.length === 0) {
    await createProject("Default Project");
    return;
  }
  renderProjectSelect();
  if (!state.projectId) state.projectId = state.projects[0].id;
  await loadProject(state.projectId);
}

function renderProjectSelect() {
  const sel = document.getElementById("projectSelect");
  sel.innerHTML = "";
  for (const p of state.projects) {
    const opt = document.createElement("option");
    opt.value = p.id;
    opt.textContent = `${p.id} - ${p.name}`;
    if (p.id === state.projectId) opt.selected = true;
    sel.appendChild(opt);
  }
}

async function createProject(name) {
  const payload = await api("/api/projects", "POST", { name });
  const p = payload.project;
  logLine(`Created project ${p.id}`);
  state.projectId = p.id;
  await loadProjects();
}

async function loadProject(projectId) {
  const payload = await api(`/api/projects/${projectId}`);
  state.projectId = projectId;
  state.projectMeta = payload.project;
  state.config = payload.config;
  state.runs = payload.runs || [];
  state.activeRunId = state.projectMeta.last_run_id || (state.runs[0] ? state.runs[0].id : null);
  state.activeRun = null;
  state.hits = [];
  state.transportRows = [];
  state.stepRows = [];
  state.waveformRows = [];
  state.photonTracks = {};
  state.photonTrackKeys = [];
  state.selectedPhotonTrackKeys = [];
  state.particleTracks = [];
  state.trackSummaryMap = {};
  state.waveformChannels = [];
  state.selectedWaveformChannel = null;
  ensureDefaults();
  renderProjectSelect();
  renderAll();
  syncSetupForm();
  syncVisualizationControls();
  setDirty(false);
  if (state.activeRunId) {
    await loadRun(state.activeRunId);
  } else {
    renderStats();
    drawPlots([], []);
    syncWaveformControls();
    drawWaveformPlot();
  }
  logLine(`Loaded ${state.projectMeta.id}`);
}

async function saveCurrentProject() {
  if (!state.projectId || !state.config) return;
  await api(`/api/projects/${state.projectId}/config`, "PUT", { config: state.config });
  setDirty(false);
  logLine(`Saved ${state.projectId}`);
}

function workspaceSwitch(workspace) {
  document.querySelectorAll(".nav-btn").forEach((b) => b.classList.remove("active"));
  document.querySelector(`.nav-btn[data-workspace='${workspace}']`)?.classList.add("active");
  document.querySelectorAll(".workspace").forEach((w) => w.classList.remove("active"));
  document.getElementById(`workspace-${workspace}`)?.classList.add("active");
  if (workspace === "design") renderViewport();
  if (workspace === "analysis") renderAnalysisPanel();
}

function renderAll() {
  renderHierarchy();
  renderRuns();
  renderInspector();
  renderViewport();
}

function renderHierarchy() {
  const root = document.getElementById("hierarchy");
  root.innerHTML = "";
  const items = [];
  items.push({ type: "world", index: -1, label: "World / Geometry" });
  for (let i = 0; i < state.config.phase1.geometry.layers.length; i += 1) {
    const layer = state.config.phase1.geometry.layers[i];
    const hidden = layer?.visible === false ? " (hidden)" : "";
    items.push({ type: "layer", index: i, label: `Layer ${i}: ${layer.name || "unnamed"}${hidden}` });
  }
  for (let i = 0; i < state.config.phase3.detectors.length; i += 1) {
    const det = state.config.phase3.detectors[i];
    const hidden = det?.visible === false ? " (hidden)" : "";
    items.push({ type: "detector", index: i, label: `Detector ${i}: ${det.id || "det"}${hidden}` });
  }
  for (let i = 0; i < state.config.phase1.tracks.length; i += 1) {
    const tr = state.config.phase1.tracks[i];
    items.push({ type: "track", index: i, label: `Track ${i}: ${tr.particle || "particle"}` });
  }

  for (const item of items) {
    const el = document.createElement("div");
    const selected = state.selected.type === item.type && state.selected.index === item.index;
    el.className = `hier-item${selected ? " selected" : ""}`;
    el.textContent = item.label;
    el.dataset.type = item.type;
    el.dataset.index = `${item.index}`;
    el.addEventListener("click", () => {
      state.selected = { type: item.type, index: item.index };
      renderHierarchy();
      renderInspector();
    });
    root.appendChild(el);
  }
}

function renderRuns() {
  const runList = document.getElementById("runList");
  runList.innerHTML = "";
  for (const run of state.runs) {
    const el = document.createElement("div");
    const selected = run.id === state.activeRunId;
    el.className = `run-item${selected ? " selected" : ""}`;
    el.innerHTML = `<div>${run.id}</div><small>${run.status || "unknown"} | ${run.started_at || "-"}</small>`;
    el.addEventListener("click", async () => {
      await loadRun(run.id);
      renderRuns();
    });
    runList.appendChild(el);
  }
  renderAnalysisPanel();
}

async function loadRun(runId) {
  const payload = await api(`/api/projects/${state.projectId}/runs/${encodeURIComponent(runId)}`);
  state.activeRunId = runId;
  state.activeRun = payload.run;
  renderStats();
  if (state.activeRun?.status === "completed") {
    await loadHitsForRun(runId);
    await loadTransportForRun(runId);
    await loadTrackSummaryForRun(runId);
    await loadStepTraceForRun(runId);
    rebuildParticleTracks();
    await loadWaveformForRun(runId);
    rebuildPhotonTracks();
    rerollPhotonTracks();
    syncWaveformControls();
    drawWaveformPlot();
    renderViewport();
  } else {
    state.hits = [];
    state.transportRows = [];
    state.stepRows = [];
    state.waveformRows = [];
    state.photonTracks = {};
    state.photonTrackKeys = [];
    state.selectedPhotonTrackKeys = [];
    state.particleTracks = [];
    state.trackSummaryMap = {};
    state.waveformChannels = [];
    state.selectedWaveformChannel = null;
    drawPlots([], []);
    syncWaveformControls();
    drawWaveformPlot();
    renderViewport();
    const failedPhase = (state.activeRun?.phases || []).find((p) => p.status === "failed");
    const detail = failedPhase?.failure_summary || state.activeRun?.error || "Run failed";
    logLine(`${runId} failed: ${detail}`, "ERROR");
  }
}

function renderStats() {
  const grid = document.getElementById("statsGrid");
  grid.innerHTML = "";
  const run = state.activeRun;
  if (!run) {
    grid.innerHTML = "<div class='stat-card'><div class='k'>Run</div><div class='v'>No run selected</div></div>";
    return;
  }
  const stats = [];
  stats.push(["run_id", run.id]);
  stats.push(["status", run.status]);
  if (run.metrics?.phase3) {
    stats.push(["phase3.total_photons", run.metrics.phase3.total_photons]);
    stats.push(["phase3.hit_count", run.metrics.phase3.hit_count]);
    stats.push(["phase3.absorbed_count", run.metrics.phase3.absorbed_count]);
    stats.push(["phase3.lost_count", run.metrics.phase3.lost_count]);
  }
  if (run.metrics?.phase4) {
    stats.push(["phase4.total_hits_in", run.metrics.phase4.total_hits_in]);
    stats.push(["phase4.detected_hits", run.metrics.phase4.detected_hits]);
    stats.push(["phase4.digi_hits", run.metrics.phase4.digi_hits]);
    stats.push(["phase4.waveform_samples", run.metrics.phase4.waveform_samples]);
  }
  for (const [k, v] of stats) {
    const card = document.createElement("div");
    card.className = "stat-card";
    card.innerHTML = `<div class="k">${k}</div><div class="v">${v}</div>`;
    grid.appendChild(card);
  }
}

function renderAnalysisPanel() {
  const list = document.getElementById("analysisRunList");
  const metrics = document.getElementById("analysisMetrics");
  list.innerHTML = "";
  metrics.innerHTML = "";
  for (const run of state.runs) {
    const item = document.createElement("div");
    item.className = `run-item${run.id === state.activeRunId ? " selected" : ""}`;
    item.textContent = `${run.id} | ${run.status}`;
    item.addEventListener("click", async () => {
      await loadRun(run.id);
      renderRuns();
    });
    list.appendChild(item);
  }
  if (state.activeRun?.metrics?.phase4) {
    const keys = ["total_hits_in", "detected_hits", "digi_hits", "waveform_samples"];
    for (const key of keys) {
      const card = document.createElement("div");
      card.className = "stat-card";
      card.innerHTML = `<div class="k">phase4.${key}</div><div class="v">${state.activeRun.metrics.phase4[key] ?? "-"}</div>`;
      metrics.appendChild(card);
    }
  }
}

function num(v, fallback = 0) {
  const n = Number(v);
  return Number.isFinite(n) ? n : fallback;
}

function listOptions(listId) {
  const list = document.getElementById(listId);
  if (!list) return [];
  return Array.from(list.querySelectorAll("option"))
    .map((o) => String(o.value || "").trim())
    .filter((v, i, a) => v && a.indexOf(v) === i);
}

function bindPickAndInput(pickId, inputId, listId, initialValue, onChange) {
  const pick = document.getElementById(pickId);
  const input = document.getElementById(inputId);
  if (!pick || !input) return;
  const options = listOptions(listId);
  pick.innerHTML = '<option value="">Quick pick...</option>';
  for (const v of options) {
    const opt = document.createElement("option");
    opt.value = v;
    opt.textContent = v;
    pick.appendChild(opt);
  }
  input.value = initialValue ?? "";
  pick.value = options.includes(String(initialValue ?? "")) ? String(initialValue) : "";

  pick.onchange = () => {
    if (!pick.value) return;
    input.value = pick.value;
    onChange(pick.value);
  };
  input.oninput = (e) => {
    const v = e.target.value;
    onChange(v);
    pick.value = options.includes(String(v)) ? String(v) : "";
  };
}

function deleteSelected() {
  const sel = state.selected;
  if (sel.type === "layer" && sel.index >= 0) state.config.phase1.geometry.layers.splice(sel.index, 1);
  if (sel.type === "detector" && sel.index >= 0) state.config.phase3.detectors.splice(sel.index, 1);
  if (sel.type === "track" && sel.index >= 0) state.config.phase1.tracks.splice(sel.index, 1);
  state.selected = { type: "world", index: -1 };
  setDirty(true);
  renderAll();
}

function renderInspector() {
  const pane = document.getElementById("inspector");
  const sel = state.selected;
  pane.innerHTML = "";

  const actionRow = document.createElement("div");
  actionRow.innerHTML = `<button class="btn" id="deleteSelectedBtn">Delete Selected</button>`;
  pane.appendChild(actionRow);
  document.getElementById("deleteSelectedBtn").onclick = deleteSelected;

  if (sel.type === "world") {
    const g = state.config.phase1.geometry;
    pane.innerHTML += `
      <h3>World / Geometry</h3>
      <div class="field"><label>Aperture Shape</label><select id="f_aperture"><option value="box">box</option><option value="cylinder">cylinder</option></select></div>
      <div class="field"><label>World Radius mm</label><input id="f_world_radius" type="number"></div>
      <div class="field"><label>World Half-Z mm</label><input id="f_world_halfz" type="number"></div>
      <div class="field"><label>Aperture Half-X mm</label><input id="f_halfx" type="number"></div>
      <div class="field"><label>Aperture Half-Y mm</label><input id="f_halfy" type="number"></div>
      <div class="field"><label>Stack Center-Z mm</label><input id="f_stackz" type="number"></div>
      <div class="field"><label>Wall Enabled</label><input id="f_wall_enabled" type="checkbox"></div>
      <div class="field"><label>Wall Material</label><div class="pick-with-input"><select id="f_wall_material_pick"></select><input id="f_wall_material" type="text"></div></div>
    `;
    document.getElementById("f_aperture").value = g.aperture_shape || "box";
    document.getElementById("f_world_radius").value = num(g.world_radius_mm, 400);
    document.getElementById("f_world_halfz").value = num(g.world_half_z_mm, 400);
    document.getElementById("f_halfx").value = num(g.aperture_half_x_mm, 100);
    document.getElementById("f_halfy").value = num(g.aperture_half_y_mm, 100);
    document.getElementById("f_stackz").value = num(g.stack_center_z_mm, 0);
    document.getElementById("f_wall_enabled").checked = !!g.wall_enabled;
    bindPickAndInput("f_wall_material_pick", "f_wall_material", "materialList", g.wall_material || "G4_Al", (v) => {
      g.wall_material = v;
      setDirty(true);
    });

    const bind = (id, key, type = "number") => {
      document.getElementById(id).addEventListener("input", (e) => {
        g[key] = type === "number" ? num(e.target.value, 0) : e.target.value;
        setDirty(true);
        renderViewport();
      });
    };
    bind("f_aperture", "aperture_shape", "string");
    bind("f_world_radius", "world_radius_mm");
    bind("f_world_halfz", "world_half_z_mm");
    bind("f_halfx", "aperture_half_x_mm");
    bind("f_halfy", "aperture_half_y_mm");
    bind("f_stackz", "stack_center_z_mm");
    document.getElementById("f_wall_enabled").addEventListener("change", (e) => {
      g.wall_enabled = !!e.target.checked;
      setDirty(true);
    });
    return;
  }

  if (sel.type === "layer" && sel.index >= 0) {
    const layer = state.config.phase1.geometry.layers[sel.index];
    pane.innerHTML += `
      <h3>Layer ${sel.index}</h3>
      <div class="field"><label>Name</label><input id="l_name" type="text"></div>
      <div class="field"><label>Visible in 3D</label><input id="l_visible" type="checkbox"></div>
      <div class="field"><label>Material</label><div class="pick-with-input"><select id="l_mat_pick"></select><input id="l_mat" type="text"></div></div>
      <div class="field"><label>Thickness mm</label><input id="l_thk" type="number"></div>
      <div class="field"><label>Refractive Index</label><input id="l_ri" type="number" step="0.001"></div>
    `;
    if (typeof layer.visible !== "boolean") layer.visible = true;
    document.getElementById("l_name").value = layer.name || "";
    document.getElementById("l_visible").checked = layer.visible !== false;
    bindPickAndInput("l_mat_pick", "l_mat", "materialList", layer.material || "", (v) => {
      layer.material = v;
      setDirty(true);
      renderViewport();
    });
    document.getElementById("l_thk").value = num(layer.thickness_mm, 1);
    document.getElementById("l_ri").value = num(layer.refractive_index, 1.0);
    document.getElementById("l_name").oninput = (e) => { layer.name = e.target.value; setDirty(true); renderHierarchy(); };
    document.getElementById("l_visible").onchange = (e) => {
      layer.visible = !!e.target.checked;
      setDirty(true);
      renderHierarchy();
      renderViewport();
    };
    document.getElementById("l_thk").oninput = (e) => { layer.thickness_mm = num(e.target.value, 1); setDirty(true); renderViewport(); };
    document.getElementById("l_ri").oninput = (e) => { layer.refractive_index = num(e.target.value, 1); setDirty(true); };
    return;
  }

  if (sel.type === "detector" && sel.index >= 0) {
    const det = state.config.phase3.detectors[sel.index];
    ensureDetectorDefaults(det);
    pane.innerHTML += `
      <h3>Detector ${sel.index}</h3>
      <div class="field"><label>ID</label><input id="d_id" type="text"></div>
      <div class="field"><label>Visible in 3D</label><input id="d_visible" type="checkbox"></div>
      <div class="field"><label>Shape</label><select id="d_shape"><option value="box">box</option><option value="cylinder">cylinder</option><option value="mesh">mesh</option></select></div>
      <div class="field"><label>Material</label><div class="pick-with-input"><select id="d_mat_pick"></select><input id="d_mat" type="text"></div></div>
      <div class="field"><label>Position (mm)</label><div class="field-row"><input id="d_x" type="number"><input id="d_y" type="number"><input id="d_z" type="number"></div></div>
      <div class="field"><label>Half XYZ (box)</label><div class="field-row"><input id="d_hx" type="number"><input id="d_hy" type="number"><input id="d_hz" type="number"></div></div>
      <div class="field"><label>Radius / HalfZ (cyl)</label><div class="field-row"><input id="d_r" type="number"><input id="d_cyl_hz" type="number"></div></div>
      <div class="field"><label>Mesh Path</label><input id="d_mesh" type="text"></div>
      <div class="field"><label>Channel Mode</label><select id="d_ch_mode"><option value="single">single</option><option value="grid_xy">grid_xy</option><option value="grid_z">grid_z</option></select></div>
      <div class="field"><label>Channels X/Y/Z</label><div class="field-row"><input id="d_cx" type="number"><input id="d_cy" type="number"><input id="d_cz" type="number"></div></div>
      <div class="field"><label>Photo Area Shape</label><select id="d_ps_shape"><option value="full">full</option><option value="rect">rect</option><option value="circle">circle</option><option value="annulus">annulus</option><option value="strip_x">strip_x</option><option value="strip_y">strip_y</option></select></div>
      <div class="field"><label>Photo Offset X/Y</label><div class="field-row"><input id="d_ps_x" type="number"><input id="d_ps_y" type="number"><input id="d_ps_strip" type="number"></div></div>
      <div class="field"><label>Photo Size HX/HY/R</label><div class="field-row"><input id="d_ps_hx" type="number"><input id="d_ps_hy" type="number"><input id="d_ps_r" type="number"></div></div>
      <div class="field"><label>Annulus Inner R</label><input id="d_ps_ir" type="number"></div>
    `;
    const setv = (id, v) => { document.getElementById(id).value = v ?? ""; };
    if (typeof det.visible !== "boolean") det.visible = true;
    setv("d_id", det.id || "");
    document.getElementById("d_visible").checked = det.visible !== false;
    setv("d_shape", det.shape || "box");
    bindPickAndInput("d_mat_pick", "d_mat", "materialList", det.material || "G4_Si", (v) => {
      det.material = v;
      setDirty(true);
      renderViewport();
    });
    setv("d_x", num(det.x_mm, 0));
    setv("d_y", num(det.y_mm, 0));
    setv("d_z", num(det.z_mm, 0));
    setv("d_hx", num(det.half_x_mm, 10));
    setv("d_hy", num(det.half_y_mm, 10));
    setv("d_hz", num(det.half_z_mm, 1));
    setv("d_r", num(det.radius_mm, 10));
    setv("d_cyl_hz", num(det.half_z_mm, 1));
    setv("d_mesh", det.mesh_path || "");
    setv("d_ch_mode", det.channel_mode || "single");
    setv("d_cx", num(det.channels_x, 1));
    setv("d_cy", num(det.channels_y, 1));
    setv("d_cz", num(det.channels_z, 1));
    setv("d_ps_shape", det.photosensor_shape || "full");
    setv("d_ps_x", num(det.photosensor_offset_x_mm, 0));
    setv("d_ps_y", num(det.photosensor_offset_y_mm, 0));
    setv("d_ps_strip", num(det.photosensor_strip_width_mm, 1));
    setv("d_ps_hx", num(det.photosensor_half_x_mm, 5));
    setv("d_ps_hy", num(det.photosensor_half_y_mm, 5));
    setv("d_ps_r", num(det.photosensor_radius_mm, 5));
    setv("d_ps_ir", num(det.photosensor_inner_radius_mm, 0));
    const bind = (id, key, mode = "number") => {
      document.getElementById(id).oninput = (e) => {
        det[key] = mode === "number" ? num(e.target.value, 0) : e.target.value;
        setDirty(true);
        if (key === "id") renderHierarchy();
        renderViewport();
      };
    };
    bind("d_id", "id", "string");
    bind("d_shape", "shape", "string");
    bind("d_x", "x_mm");
    bind("d_y", "y_mm");
    bind("d_z", "z_mm");
    bind("d_hx", "half_x_mm");
    bind("d_hy", "half_y_mm");
    bind("d_hz", "half_z_mm");
    bind("d_r", "radius_mm");
    bind("d_cyl_hz", "half_z_mm");
    bind("d_mesh", "mesh_path", "string");
    bind("d_ch_mode", "channel_mode", "string");
    bind("d_cx", "channels_x");
    bind("d_cy", "channels_y");
    bind("d_cz", "channels_z");
    bind("d_ps_shape", "photosensor_shape", "string");
    bind("d_ps_x", "photosensor_offset_x_mm");
    bind("d_ps_y", "photosensor_offset_y_mm");
    bind("d_ps_strip", "photosensor_strip_width_mm");
    bind("d_ps_hx", "photosensor_half_x_mm");
    bind("d_ps_hy", "photosensor_half_y_mm");
    bind("d_ps_r", "photosensor_radius_mm");
    bind("d_ps_ir", "photosensor_inner_radius_mm");
    document.getElementById("d_visible").onchange = (e) => {
      det.visible = !!e.target.checked;
      setDirty(true);
      renderHierarchy();
      renderViewport();
    };
    return;
  }

  if (sel.type === "track" && sel.index >= 0) {
    const tr = state.config.phase1.tracks[sel.index];
    pane.innerHTML += `
      <h3>Track ${sel.index}</h3>
      <div class="field"><label>Particle</label><div class="pick-with-input"><select id="t_part_pick"></select><input id="t_part" type="text"></div></div>
      <div class="field"><label>Charge</label><input id="t_charge" type="number" step="0.1"></div>
      <div class="field"><label>Position (mm)</label><div class="field-row"><input id="t_px" type="number"><input id="t_py" type="number"><input id="t_pz" type="number"></div></div>
      <div class="field"><label>Momentum (MeV)</label><div class="field-row"><input id="t_mx" type="number"><input id="t_my" type="number"><input id="t_mz" type="number"></div></div>
      <div class="field"><label>Time ns</label><input id="t_time" type="number"></div>
    `;
    tr.pos_mm = tr.pos_mm || [0, 0, 0];
    tr.mom_mev = tr.mom_mev || [0, 0, 1000];
    bindPickAndInput("t_part_pick", "t_part", "particleList", tr.particle || "mu-", (v) => {
      tr.particle = v;
      setDirty(true);
      renderHierarchy();
    });
    document.getElementById("t_charge").value = num(tr.charge, -1);
    document.getElementById("t_px").value = num(tr.pos_mm[0], 0);
    document.getElementById("t_py").value = num(tr.pos_mm[1], 0);
    document.getElementById("t_pz").value = num(tr.pos_mm[2], 0);
    document.getElementById("t_mx").value = num(tr.mom_mev[0], 0);
    document.getElementById("t_my").value = num(tr.mom_mev[1], 0);
    document.getElementById("t_mz").value = num(tr.mom_mev[2], 1000);
    document.getElementById("t_time").value = num(tr.time_ns, 0);
    document.getElementById("t_charge").oninput = (e) => { tr.charge = num(e.target.value, -1); setDirty(true); };
    ["x", "y", "z"].forEach((axis, idx) => {
      document.getElementById(`t_p${axis}`).oninput = (e) => { tr.pos_mm[idx] = num(e.target.value, 0); setDirty(true); renderViewport(); };
      document.getElementById(`t_m${axis}`).oninput = (e) => { tr.mom_mev[idx] = num(e.target.value, 0); setDirty(true); renderViewport(); };
    });
    document.getElementById("t_time").oninput = (e) => { tr.time_ns = num(e.target.value, 0); setDirty(true); };
  }
}

function addLayer() {
  state.config.phase1.geometry.layers.push({
    name: `Layer${state.config.phase1.geometry.layers.length + 1}`,
    material: "G4_SILICON_DIOXIDE",
    thickness_mm: 10.0,
    refractive_index: 1.1,
    visible: true,
  });
  state.selected = { type: "layer", index: state.config.phase1.geometry.layers.length - 1 };
  setDirty(true);
  renderAll();
}

function addDetector() {
  const det = {
    id: `det${state.config.phase3.detectors.length + 1}`,
    shape: "box",
    material: "G4_Si",
    visible: true,
    x_mm: 0,
    y_mm: 0,
    z_mm: 0,
    half_x_mm: 20,
    half_y_mm: 20,
    half_z_mm: 0.5,
    channel_mode: "single",
    photosensor_shape: "full",
    photosensor_offset_x_mm: 0,
    photosensor_offset_y_mm: 0,
    photosensor_half_x_mm: 18,
    photosensor_half_y_mm: 18,
    photosensor_radius_mm: 18,
    photosensor_inner_radius_mm: 0,
    photosensor_strip_width_mm: 1,
  };
  ensureDetectorDefaults(det);
  state.config.phase3.detectors.push(det);
  state.selected = { type: "detector", index: state.config.phase3.detectors.length - 1 };
  setDirty(true);
  renderAll();
}

function addTrack() {
  state.config.phase1.tracks.push({
    track_id: state.config.phase1.tracks.length + 1,
    parent_id: 0,
    particle: "mu-",
    charge: -1,
    pos_mm: [0, 0, -20],
    mom_mev: [0, 0, 5000],
    time_ns: 0,
  });
  state.selected = { type: "track", index: state.config.phase1.tracks.length - 1 };
  setDirty(true);
  renderAll();
}

function addTemplateSensor() {
  const sel = document.getElementById("sensorTemplateSelect");
  const key = sel ? sel.value : "sipm_tile";
  const tpl = SENSOR_TEMPLATES[key] || SENSOR_TEMPLATES.sipm_tile;
  const det = {
    id: `det${state.config.phase3.detectors.length + 1}`,
    x_mm: num(document.getElementById("templateX")?.value, 0),
    y_mm: num(document.getElementById("templateY")?.value, 0),
    z_mm: num(document.getElementById("templateZ")?.value, 0),
    ...tpl,
  };
  ensureDetectorDefaults(det);
  state.config.phase3.detectors.push(det);
  state.selected = { type: "detector", index: state.config.phase3.detectors.length - 1 };
  setDirty(true);
  renderAll();
  logLine(`Added detector template '${key}' at (${det.x_mm}, ${det.y_mm}, ${det.z_mm})`);
}

function syncSetupForm() {
  document.getElementById("setupCherenkov").checked = !!state.config.phase2.generation.enable_cherenkov;
  document.getElementById("setupScint").checked = !!state.config.phase2.generation.enable_scintillation;
  document.getElementById("setupRecordSteps").checked = !!state.config.phase3.tracking.record_steps;
  document.getElementById("setupTraceMax").value = num(state.config.phase3.tracking.max_trace_photons, 500);
  document.getElementById("setupDirectPde").checked = !!state.config.phase4.detection.use_direct_pde;
  document.getElementById("setupDcr").value = num(state.config.phase4.noise.dcr_hz, 80000);
  document.getElementById("setupVbias").value = num(state.config.phase4.sensor.v_bias, 31);
  document.getElementById("setupVbd").value = num(state.config.phase4.sensor.v_bd, 28);
}

function applySetupForm() {
  state.config.phase2.generation.enable_cherenkov = document.getElementById("setupCherenkov").checked;
  state.config.phase2.generation.enable_scintillation = document.getElementById("setupScint").checked;
  state.config.phase3.tracking.record_steps = document.getElementById("setupRecordSteps").checked;
  state.config.phase3.tracking.max_trace_photons = num(document.getElementById("setupTraceMax").value, 500);
  state.config.phase4.detection.use_direct_pde = document.getElementById("setupDirectPde").checked;
  state.config.phase4.noise.dcr_hz = num(document.getElementById("setupDcr").value, 80000);
  state.config.phase4.sensor.v_bias = num(document.getElementById("setupVbias").value, 31);
  state.config.phase4.sensor.v_bd = num(document.getElementById("setupVbd").value, 28);
  setDirty(true);
  logLine("Applied setup form to config");
}

async function runProject() {
  if (!state.projectId) return;
  try {
    if (state.dirty) await saveCurrentProject();
    const payload = await api(`/api/projects/${state.projectId}/run`, "POST", {});
    state.currentJobId = payload.job.id;
    setStatus("Running", "status-running");
    logLine(`Started ${payload.job.id}`);
    pollJob();
  } catch (err) {
    setStatus("Run Failed", "status-failed");
    logLine(err.message, "ERROR");
  }
}

function pollJob() {
  if (state.pollTimer) clearInterval(state.pollTimer);
  state.pollTimer = setInterval(async () => {
    if (!state.currentJobId) return;
    try {
      const payload = await api(`/api/jobs/${state.currentJobId}`);
      const job = payload.job;
      if (job.status === "running" || job.status === "queued") {
        setStatus(job.message || job.status, "status-running");
        return;
      }
      clearInterval(state.pollTimer);
      state.pollTimer = null;
      if (job.status === "completed") {
        setStatus("Completed", "status-ok");
        logLine(`Run completed (${job.run_id})`);
      } else {
        setStatus("Failed", "status-failed");
        logLine(`Run failed: ${job.error || "unknown error"}`, "ERROR");
      }
      await loadProject(state.projectId);
      if (job.run_id && state.activeRunId !== job.run_id) {
        await loadRun(job.run_id);
      }
    } catch (err) {
      clearInterval(state.pollTimer);
      state.pollTimer = null;
      setStatus("Polling error", "status-failed");
      logLine(err.message, "ERROR");
    }
  }, 2000);
}

async function loadHitsForRun(runId) {
  try {
    const text = await api(`/api/projects/${state.projectId}/runs/${encodeURIComponent(runId)}/file?path=phase3/detector_hits.csv`);
    const rows = parseCsv(text);
    state.hits = rows;
    const times = rows.map((r) => num(r.t_ns, 0));
    const lambdas = rows.map((r) => num(r.lambda_nm, 0));
    drawPlots(times, lambdas);
    renderViewport();
    logLine(`Loaded ${rows.length} detector hits from ${runId}`);
  } catch (err) {
    state.hits = [];
    drawPlots([], []);
    logLine(`No detector_hits.csv for ${runId}`, "WARN");
  }
}

async function loadTransportForRun(runId) {
  try {
    const text = await api(`/api/projects/${state.projectId}/runs/${encodeURIComponent(runId)}/file?path=phase3/transport_events.csv`);
    state.transportRows = parseCsv(text);
    logLine(`Loaded ${state.transportRows.length} transport rows from ${runId}`);
  } catch (_) {
    state.transportRows = [];
    logLine(`No transport_events.csv for ${runId}`, "WARN");
  }
}

async function loadStepTraceForRun(runId) {
  try {
    const text = await api(`/api/projects/${state.projectId}/runs/${encodeURIComponent(runId)}/file?path=phase1/step_trace.csv`);
    state.stepRows = parseCsv(text);
    logLine(`Loaded ${state.stepRows.length} step-trace rows from ${runId}`);
  } catch (_) {
    state.stepRows = [];
    state.particleTracks = [];
    logLine(`No step_trace.csv for ${runId}`, "WARN");
  }
}

async function loadTrackSummaryForRun(runId) {
  try {
    const text = await api(`/api/projects/${state.projectId}/runs/${encodeURIComponent(runId)}/file?path=phase1/track_summary.csv`);
    const rows = parseCsv(text);
    if (rows.length > 0) {
      const probe = rows[0];
      if (!Object.prototype.hasOwnProperty.call(probe, "creator_process") ||
          !Object.prototype.hasOwnProperty.call(probe, "is_delta_secondary")) {
        throw new Error("track_summary.csv is missing strict delta columns: creator_process,is_delta_secondary");
      }
    }
    const map = {};
    const parseDeltaFlag = (rawValue, key) => {
      const raw = String(rawValue ?? "").trim().toLowerCase();
      if (raw === "1" || raw === "true") return true;
      if (raw === "0" || raw === "false") return false;
      throw new Error(`track_summary.csv has invalid is_delta_secondary='${rawValue}' for ${key}`);
    };
    for (const row of rows) {
      const eventId = Math.trunc(num(row.event_id, 0));
      const trackId = Math.trunc(num(row.track_id, -1));
      const key = `${eventId}:${trackId}`;
      if (trackId < 0) continue;
      if (!Object.prototype.hasOwnProperty.call(row, "is_delta_secondary")) {
        throw new Error(`track_summary.csv row missing is_delta_secondary for ${key}`);
      }
      map[key] = {
        creatorProcess: String(row.creator_process || "").trim(),
        isDeltaSecondary: parseDeltaFlag(row.is_delta_secondary, key),
        particle: String(row.particle || ""),
        parentId: num(row.parent_id, 0),
      };
    }
    state.trackSummaryMap = map;
    logLine(`Loaded ${rows.length} track-summary rows from ${runId}`);
  } catch (err) {
    state.trackSummaryMap = {};
    logLine(`Strict delta classification unavailable for ${runId}: ${err.message}`, "ERROR");
  }
}

async function loadWaveformForRun(runId) {
  try {
    const text = await api(`/api/projects/${state.projectId}/runs/${encodeURIComponent(runId)}/file?path=phase4/pulse_waveform.csv`);
    state.waveformRows = parseCsv(text);
    logLine(`Loaded ${state.waveformRows.length} waveform rows from ${runId}`);
  } catch (_) {
    state.waveformRows = [];
    logLine(`No pulse_waveform.csv for ${runId}`, "WARN");
  }
}

function clampInt(value, minValue, maxValue) {
  const n = Number.isFinite(Number(value)) ? Math.trunc(Number(value)) : minValue;
  return Math.max(minValue, Math.min(maxValue, n));
}

function simplifyTrackPoints(points, maxPoints = 250) {
  if (!Array.isArray(points) || points.length <= maxPoints) return points;
  const step = Math.ceil(points.length / maxPoints);
  const out = [];
  for (let i = 0; i < points.length; i += step) out.push(points[i]);
  const last = points[points.length - 1];
  if (out[out.length - 1] !== last) out.push(last);
  return out;
}

function rebuildPhotonTracks() {
  const grouped = {};
  for (const row of state.transportRows) {
    const key = String(row.track_id ?? "");
    if (!key) continue;
    const x = num(row.x_mm, NaN);
    const y = num(row.y_mm, NaN);
    const z = num(row.z_mm, NaN);
    if (!Number.isFinite(x) || !Number.isFinite(y) || !Number.isFinite(z)) continue;
    if (!grouped[key]) grouped[key] = [];
    grouped[key].push({ x, y, z, t: num(row.t_ns, 0) });
  }

  state.photonTracks = {};
  state.photonTrackKeys = [];
  for (const key of Object.keys(grouped)) {
    const pts = grouped[key].sort((a, b) => a.t - b.t);
    if (pts.length < 2) continue;
    state.photonTracks[key] = simplifyTrackPoints(pts, 180);
    state.photonTrackKeys.push(key);
  }
  state.photonTrackKeys.sort((a, b) => {
    const na = Number(a);
    const nb = Number(b);
    if (Number.isFinite(na) && Number.isFinite(nb)) return na - nb;
    return a.localeCompare(b);
  });
  if (state.selectedPhotonTrackKeys.length === 0 && state.photonTrackKeys.length > 0) rerollPhotonTracks(false);
  syncVisualizationControls();
}

function rebuildParticleTracks() {
  const grouped = {};
  for (const row of state.stepRows) {
    const eventId = Math.trunc(num(row.event_id, 0));
    const trackIdNum = num(row.track_id, NaN);
    if (!Number.isFinite(trackIdNum)) continue;
    const trackId = String(Math.trunc(trackIdNum));
    const key = `${eventId}:${trackId}`;
    const x = num(row.x_mm, NaN);
    const y = num(row.y_mm, NaN);
    const z = num(row.z_mm, NaN);
    if (!Number.isFinite(x) || !Number.isFinite(y) || !Number.isFinite(z)) continue;
    if (!grouped[key]) {
      grouped[key] = {
        eventId,
        trackId,
        parentId: num(row.parent_id, 0),
        points: [],
      };
    }
    grouped[key].points.push({
      x,
      y,
      z,
      step: num(row.step_index, 0),
      t: num(row.t_ns, 0),
    });
  }

  const missing = [];
  let skippedOptical = 0;
  const rebuilt = Object.values(grouped)
    .map((track) => {
      track.points.sort((a, b) => {
        if (a.step === b.step) return a.t - b.t;
        return a.step - b.step;
      });
      track.points = simplifyTrackPoints(track.points, 220);
      const summaryKey = `${track.eventId}:${track.trackId}`;
      const summary = state.trackSummaryMap[summaryKey];
      if (!summary || typeof summary.isDeltaSecondary !== "boolean") {
        missing.push(summaryKey);
      }
      const proc = String(summary?.creatorProcess || "").toLowerCase();
      const particle = String(summary?.particle || "").toLowerCase();
      if (particle === "opticalphoton") {
        skippedOptical += 1;
        return null;
      }
      const deltaByProcess = (particle === "e-" || particle === "e+") && /ioni|delta/.test(proc);
      track.creatorProcess = summary?.creatorProcess || "";
      track.particle = summary?.particle || "";
      track.parentId = summary ? num(summary.parentId, track.parentId) : track.parentId;
      track.isDelta = summary ? (!!summary.isDeltaSecondary || deltaByProcess) : false;
      return track;
    })
    .filter((track) => track && track.points.length > 1)
    .sort((a, b) => {
      const na = Number(a.trackId);
      const nb = Number(b.trackId);
      if (Number.isFinite(na) && Number.isFinite(nb)) return na - nb;
      return String(a.trackId).localeCompare(String(b.trackId));
    });
  if (missing.length > 0) {
    state.particleTracks = [];
    logLine(
      `Strict delta mode: missing track_summary delta metadata for ${missing.length} tracks. Rendering aborted.`,
      "ERROR"
    );
    return;
  }
  state.particleTracks = rebuilt;
  const deltaCount = rebuilt.reduce((acc, tr) => acc + (tr.isDelta ? 1 : 0), 0);
  logLine(`Particle tracks rebuilt: total=${rebuilt.length}, delta=${deltaCount}, skipped_optical=${skippedOptical}`);
}

function rerollPhotonTracks(redraw = true) {
  const allKeys = state.photonTrackKeys.slice();
  const total = allKeys.length;
  if (total === 0) {
    state.selectedPhotonTrackKeys = [];
    syncVisualizationControls();
    if (redraw) renderViewport();
    return;
  }
  const want = clampInt(state.visualization.photonTrackCount, 1, total);
  for (let i = allKeys.length - 1; i > 0; i -= 1) {
    const j = Math.floor(Math.random() * (i + 1));
    const tmp = allKeys[i];
    allKeys[i] = allKeys[j];
    allKeys[j] = tmp;
  }
  state.selectedPhotonTrackKeys = allKeys.slice(0, want);
  syncVisualizationControls();
  if (redraw) renderViewport();
}

function syncVisualizationControls() {
  const showPhoton = document.getElementById("vizShowPhoton");
  const photonCount = document.getElementById("vizPhotonCount");
  const showParticle = document.getElementById("vizShowParticle");
  const showDelta = document.getElementById("vizShowDelta");
  const showHits = document.getElementById("vizShowHits");
  const modelOpacity = document.getElementById("vizModelOpacity");
  const modelOpacityValue = document.getElementById("vizModelOpacityValue");
  const deltaCount = document.getElementById("vizDeltaCount");
  const info = document.getElementById("vizPhotonInfo");
  if (!showPhoton || !photonCount || !showParticle || !showDelta || !showHits || !modelOpacity || !modelOpacityValue || !deltaCount || !info) return;

  const total = state.photonTrackKeys.length;
  const deltaTotal = state.particleTracks.reduce((acc, tr) => acc + (tr.isDelta ? 1 : 0), 0);
  state.visualization.photonTrackCount = clampInt(state.visualization.photonTrackCount, 1, Math.max(1, total || 1));
  if (deltaTotal > 0) {
    state.visualization.deltaTrackCount = clampInt(state.visualization.deltaTrackCount, 1, deltaTotal);
  } else {
    state.visualization.deltaTrackCount = 0;
  }
  state.visualization.modelOpacity = Math.max(0.05, Math.min(1.0, num(state.visualization.modelOpacity, 0.95)));

  showPhoton.checked = !!state.visualization.showPhotonTracks;
  photonCount.value = state.visualization.photonTrackCount;
  photonCount.max = String(Math.max(1, total));
  showParticle.checked = !!state.visualization.showParticleTracks;
  showDelta.checked = !!state.visualization.showDeltaTracks;
  showDelta.disabled = !state.visualization.showParticleTracks;
  showHits.checked = !!state.visualization.showDetectorHits;
  modelOpacity.value = state.visualization.modelOpacity.toFixed(2);
  modelOpacityValue.textContent = state.visualization.modelOpacity.toFixed(2);
  deltaCount.value = deltaTotal > 0 ? state.visualization.deltaTrackCount : 0;
  deltaCount.max = String(Math.max(1, deltaTotal));
  deltaCount.disabled = !state.visualization.showParticleTracks || !state.visualization.showDeltaTracks || deltaTotal === 0;
  const shown = state.selectedPhotonTrackKeys.length;
  const shownDelta = deltaTotal > 0 ? Math.min(state.visualization.deltaTrackCount, deltaTotal) : 0;
  info.textContent = `${shown}/${total} photon tracks | ${shownDelta}/${deltaTotal} delta tracks`;
}

function syncWaveformControls() {
  const select = document.getElementById("waveformChannelSelect");
  const mode = document.getElementById("waveformModeSelect");
  if (!select || !mode) return;

  const uniq = new Set();
  for (const row of state.waveformRows) {
    if (row.channel_id === undefined) continue;
    uniq.add(String(row.channel_id));
  }
  state.waveformChannels = Array.from(uniq).sort((a, b) => {
    const na = Number(a);
    const nb = Number(b);
    if (Number.isFinite(na) && Number.isFinite(nb)) return na - nb;
    return a.localeCompare(b);
  });

  if (!state.waveformChannels.includes(String(state.selectedWaveformChannel))) {
    state.selectedWaveformChannel = state.waveformChannels.length ? state.waveformChannels[0] : null;
  }

  select.innerHTML = "";
  for (const ch of state.waveformChannels) {
    const opt = document.createElement("option");
    opt.value = ch;
    opt.textContent = `Ch ${ch}`;
    if (String(state.selectedWaveformChannel) === ch) opt.selected = true;
    select.appendChild(opt);
  }
  select.disabled = state.waveformChannels.length === 0;

  if (state.waveformMode !== "voltage" && state.waveformMode !== "current_pe") {
    state.waveformMode = "current_pe";
  }
  mode.value = state.waveformMode;
}

function drawWaveformPlot() {
  const canvas = document.getElementById("waveformPlot");
  if (!canvas) return;
  if (!state.waveformRows.length || state.selectedWaveformChannel === null) {
    drawLinePlot(canvas, [], "waveform", "#f59e0b");
    return;
  }

  const channel = String(state.selectedWaveformChannel);
  const key = state.waveformMode === "voltage" ? "voltage" : "current_pe";
  const points = [];
  for (const row of state.waveformRows) {
    if (String(row.channel_id) !== channel) continue;
    const x = num(row.t_ns, NaN);
    const y = num(row[key], NaN);
    if (!Number.isFinite(x) || !Number.isFinite(y)) continue;
    points.push({ x, y });
  }
  points.sort((a, b) => a.x - b.x);
  const simplified = simplifyTrackPoints(points, 1800);
  const label = `${key === "voltage" ? "U(t)" : "I(t)"} | Ch ${channel}`;
  const color = key === "voltage" ? "#38bdf8" : "#f59e0b";
  drawLinePlot(canvas, simplified, label, color);
}

function parseCsv(text) {
  const lines = text.split(/\r?\n/).filter((line) => line.trim().length > 0);
  if (lines.length < 2) return [];
  const header = lines[0].split(",");
  const out = [];
  for (let i = 1; i < lines.length; i += 1) {
    const cols = lines[i].split(",");
    const row = {};
    for (let j = 0; j < header.length; j += 1) row[header[j]] = cols[j];
    out.push(row);
  }
  return out;
}

function drawPlots(times, lambdas) {
  drawHistogram(document.getElementById("timePlot"), times, "t_ns", "#10b981");
  drawHistogram(document.getElementById("lambdaPlot"), lambdas, "lambda_nm", "#3b82f6");
}

function drawHistogram(canvas, values, label, color) {
  const ctx = canvas.getContext("2d");
  const dpr = window.devicePixelRatio || 1;
  const w = canvas.clientWidth || 300;
  const h = canvas.clientHeight || 220;
  canvas.width = Math.floor(w * dpr);
  canvas.height = Math.floor(h * dpr);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = "#111827";
  ctx.fillRect(0, 0, w, h);
  ctx.strokeStyle = "#374151";
  ctx.strokeRect(0, 0, w, h);
  ctx.fillStyle = "#9ca3af";
  ctx.font = "12px Consolas";
  ctx.fillText(label, 8, 14);
  if (!values.length) {
    ctx.fillText("no data", 8, 30);
    return;
  }
  const min = Math.min(...values);
  const max = Math.max(...values);
  const bins = 30;
  const hist = new Array(bins).fill(0);
  const span = max - min || 1;
  for (const v of values) {
    const idx = Math.max(0, Math.min(bins - 1, Math.floor(((v - min) / span) * bins)));
    hist[idx] += 1;
  }
  const top = Math.max(...hist);
  const margin = 26;
  const plotW = w - margin * 2;
  const plotH = h - margin * 2;
  ctx.strokeStyle = "#6b7280";
  ctx.beginPath();
  ctx.moveTo(margin, margin);
  ctx.lineTo(margin, margin + plotH);
  ctx.lineTo(margin + plotW, margin + plotH);
  ctx.stroke();
  const barW = plotW / bins;
  ctx.fillStyle = color;
  for (let i = 0; i < bins; i += 1) {
    const bh = top > 0 ? (hist[i] / top) * plotH : 0;
    ctx.fillRect(margin + i * barW, margin + plotH - bh, Math.max(1, barW - 1), bh);
  }
  ctx.fillStyle = "#9ca3af";
  ctx.fillText(min.toFixed(2), margin, h - 8);
  ctx.fillText(max.toFixed(2), margin + plotW - 40, h - 8);
}

function drawLinePlot(canvas, points, label, color) {
  const ctx = canvas.getContext("2d");
  const dpr = window.devicePixelRatio || 1;
  const w = canvas.clientWidth || 300;
  const h = canvas.clientHeight || 220;
  canvas.width = Math.floor(w * dpr);
  canvas.height = Math.floor(h * dpr);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = "#111827";
  ctx.fillRect(0, 0, w, h);
  ctx.strokeStyle = "#374151";
  ctx.strokeRect(0, 0, w, h);

  ctx.fillStyle = "#9ca3af";
  ctx.font = "12px Consolas";
  ctx.fillText(label, 8, 14);
  if (!points.length) {
    ctx.fillText("no data", 8, 30);
    return;
  }

  const minX = Math.min(...points.map((p) => p.x));
  const maxX = Math.max(...points.map((p) => p.x));
  const minY = Math.min(...points.map((p) => p.y));
  const maxY = Math.max(...points.map((p) => p.y));
  const spanX = Math.max(1e-9, maxX - minX);
  const spanY = Math.max(1e-9, maxY - minY);
  const margin = 32;
  const plotW = w - margin * 2;
  const plotH = h - margin * 2;

  ctx.strokeStyle = "#6b7280";
  ctx.beginPath();
  ctx.moveTo(margin, margin);
  ctx.lineTo(margin, margin + plotH);
  ctx.lineTo(margin + plotW, margin + plotH);
  ctx.stroke();

  ctx.strokeStyle = color;
  ctx.lineWidth = 1.2;
  ctx.beginPath();
  points.forEach((p, idx) => {
    const x = margin + ((p.x - minX) / spanX) * plotW;
    const y = margin + plotH - ((p.y - minY) / spanY) * plotH;
    if (idx === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.stroke();
  ctx.lineWidth = 1.0;

  ctx.fillStyle = "#9ca3af";
  ctx.fillText(minX.toFixed(2), margin, h - 8);
  ctx.fillText(maxX.toFixed(2), margin + plotW - 40, h - 8);
  ctx.fillText(minY.toFixed(3), 4, margin + plotH);
  ctx.fillText(maxY.toFixed(3), 4, margin + 10);
}

function rotatePoint(p) {
  const cy = Math.cos(state.view.yaw);
  const sy = Math.sin(state.view.yaw);
  let x = p.x * cy - p.z * sy;
  let z = p.x * sy + p.z * cy;
  const cx = Math.cos(state.view.pitch);
  const sx = Math.sin(state.view.pitch);
  const y = p.y * cx - z * sx;
  z = p.y * sx + z * cx;
  return { x, y, z };
}

function project(p, w, h) {
  const pr = rotatePoint(p);
  const depth = pr.z + 1200;
  if (depth <= 10) return null;
  const s = (420 * state.view.zoom) / depth;
  return { x: w * 0.5 + pr.x * s, y: h * 0.5 - pr.y * s, depth };
}

function line3(ctx, a, b, w, h, color = "#9ca3af") {
  const pa = project(a, w, h);
  const pb = project(b, w, h);
  if (!pa || !pb) return;
  ctx.strokeStyle = color;
  ctx.beginPath();
  ctx.moveTo(pa.x, pa.y);
  ctx.lineTo(pb.x, pb.y);
  ctx.stroke();
}

function polyline3(ctx, points, w, h, color = "#9ca3af", lineWidth = 1.0) {
  if (!Array.isArray(points) || points.length < 2) return;
  ctx.strokeStyle = color;
  ctx.lineWidth = lineWidth;
  let started = false;
  ctx.beginPath();
  for (const p of points) {
    const pp = project(p, w, h);
    if (!pp) continue;
    if (!started) {
      ctx.moveTo(pp.x, pp.y);
      started = true;
    } else {
      ctx.lineTo(pp.x, pp.y);
    }
  }
  if (started) ctx.stroke();
  ctx.lineWidth = 1.0;
}

const MATERIAL_COLOR_OVERRIDES = {
  "g4_si": "#2d9cdb",
  "g4_silicon": "#2d9cdb",
  "g4_silicon_dioxide": "#6aaed6",
  "g4_air": "#87a8c4",
  "g4_galactic": "#525d6d",
  "g4_al": "#b6bcc8",
  "g4_cu": "#c47a4f",
  "g4_pb": "#66738a",
  "g4_fe": "#8f97a6",
  "g4_w": "#6d7a8d",
  "g4_water": "#3f9ad9",
  "g4_plastic_sc_vinyltoluene": "#3aa56e",
  "g4_bgo": "#66b9a8",
  "g4_pbwo4": "#5f8db7",
};
const materialColorCache = {};

function clampByte(v) {
  return Math.max(0, Math.min(255, Math.round(v)));
}

function hexToRgb(hex) {
  const raw = String(hex || "").trim();
  const m = raw.match(/^#([0-9a-f]{6})$/i);
  if (!m) return null;
  const n = parseInt(m[1], 16);
  return { r: (n >> 16) & 255, g: (n >> 8) & 255, b: n & 255 };
}

function hslToRgb(h, s, l) {
  const c = (1 - Math.abs((2 * l) - 1)) * s;
  const hp = (h % 360) / 60;
  const x = c * (1 - Math.abs((hp % 2) - 1));
  let r1 = 0;
  let g1 = 0;
  let b1 = 0;
  if (hp >= 0 && hp < 1) [r1, g1, b1] = [c, x, 0];
  else if (hp < 2) [r1, g1, b1] = [x, c, 0];
  else if (hp < 3) [r1, g1, b1] = [0, c, x];
  else if (hp < 4) [r1, g1, b1] = [0, x, c];
  else if (hp < 5) [r1, g1, b1] = [x, 0, c];
  else [r1, g1, b1] = [c, 0, x];
  const m = l - c / 2;
  return {
    r: clampByte((r1 + m) * 255),
    g: clampByte((g1 + m) * 255),
    b: clampByte((b1 + m) * 255),
  };
}

function shadeRgb(rgb, factor) {
  return {
    r: clampByte(rgb.r * factor),
    g: clampByte(rgb.g * factor),
    b: clampByte(rgb.b * factor),
  };
}

function rgba(rgb, alpha = 1.0) {
  return `rgba(${rgb.r}, ${rgb.g}, ${rgb.b}, ${Math.max(0, Math.min(1, alpha))})`;
}

function materialColorRgb(materialName, fallbackHex = "#64748b") {
  const raw = String(materialName || "").trim();
  const key = raw.toLowerCase();
  if (Object.prototype.hasOwnProperty.call(materialColorCache, key)) {
    return materialColorCache[key];
  }
  let rgb = null;
  if (Object.prototype.hasOwnProperty.call(MATERIAL_COLOR_OVERRIDES, key)) {
    rgb = hexToRgb(MATERIAL_COLOR_OVERRIDES[key]);
  }
  if (!rgb) {
    if (!raw) {
      rgb = hexToRgb(fallbackHex) || { r: 100, g: 116, b: 139 };
    } else {
      let hash = 2166136261;
      for (let i = 0; i < raw.length; i += 1) {
        hash ^= raw.charCodeAt(i);
        hash = Math.imul(hash, 16777619);
      }
      hash >>>= 0;
      const hue = hash % 360;
      const sat = 58 + ((hash >>> 9) % 18);
      const lit = 42 + ((hash >>> 17) % 16);
      rgb = hslToRgb(hue, sat / 100, lit / 100);
    }
  }
  materialColorCache[key] = rgb;
  return rgb;
}

function polygonDepth(points, w, h) {
  let sum = 0;
  for (const p of points) {
    const pp = project(p, w, h);
    if (!pp) return null;
    sum += pp.depth;
  }
  return sum / points.length;
}

function faceLightFactor(points) {
  if (!points || points.length < 3) return 0.85;
  const a = rotatePoint(points[0]);
  const b = rotatePoint(points[1]);
  const c = rotatePoint(points[2]);
  const ux = b.x - a.x;
  const uy = b.y - a.y;
  const uz = b.z - a.z;
  const vx = c.x - a.x;
  const vy = c.y - a.y;
  const vz = c.z - a.z;
  const nx = (uy * vz) - (uz * vy);
  const ny = (uz * vx) - (ux * vz);
  const nz = (ux * vy) - (uy * vx);
  const nlen = Math.sqrt((nx * nx) + (ny * ny) + (nz * nz));
  if (nlen <= 1e-9) return 0.85;
  const lx = -0.35;
  const ly = -0.45;
  const lz = -0.82;
  const llen = Math.sqrt((lx * lx) + (ly * ly) + (lz * lz));
  const dot = ((nx * lx) + (ny * ly) + (nz * lz)) / (nlen * llen);
  return 0.55 + (0.45 * Math.abs(dot));
}

function fillPolygon3(ctx, points, w, h, fillStyle, strokeStyle, lineWidth = 1.0) {
  const pr = [];
  for (const p of points) {
    const pp = project(p, w, h);
    if (!pp) return false;
    pr.push(pp);
  }
  ctx.beginPath();
  ctx.moveTo(pr[0].x, pr[0].y);
  for (let i = 1; i < pr.length; i += 1) {
    ctx.lineTo(pr[i].x, pr[i].y);
  }
  ctx.closePath();
  if (fillStyle) {
    ctx.fillStyle = fillStyle;
    ctx.fill();
  }
  if (strokeStyle) {
    ctx.strokeStyle = strokeStyle;
    ctx.lineWidth = lineWidth;
    ctx.stroke();
    ctx.lineWidth = 1.0;
  }
  return true;
}

function drawFaceRect(ctx, cx, cy, cz, hx, hy, w, h, color) {
  const a = { x: cx - hx, y: cy - hy, z: cz };
  const b = { x: cx + hx, y: cy - hy, z: cz };
  const d = { x: cx - hx, y: cy + hy, z: cz };
  const e = { x: cx + hx, y: cy + hy, z: cz };
  line3(ctx, a, b, w, h, color);
  line3(ctx, b, e, w, h, color);
  line3(ctx, e, d, w, h, color);
  line3(ctx, d, a, w, h, color);
}

function drawFaceCircle(ctx, cx, cy, cz, r, w, h, color) {
  const seg = 36;
  for (let i = 0; i < seg; i += 1) {
    const a0 = (Math.PI * 2 * i) / seg;
    const a1 = (Math.PI * 2 * (i + 1)) / seg;
    const p0 = { x: cx + r * Math.cos(a0), y: cy + r * Math.sin(a0), z: cz };
    const p1 = { x: cx + r * Math.cos(a1), y: cy + r * Math.sin(a1), z: cz };
    line3(ctx, p0, p1, w, h, color);
  }
}

function drawSensorAreaBox(ctx, c, hx, hy, hz, det, w, h) {
  const shape = String(det.photosensor_shape || "full");
  const ox = num(det.photosensor_offset_x_mm, 0);
  const oy = num(det.photosensor_offset_y_mm, 0);
  const activeHx = Math.max(0.1, num(det.photosensor_half_x_mm, hx * 0.9));
  const activeHy = Math.max(0.1, num(det.photosensor_half_y_mm, hy * 0.9));
  const stripW = Math.max(0.1, num(det.photosensor_strip_width_mm, 1.0));
  const r = Math.max(0.1, num(det.photosensor_radius_mm, Math.min(hx, hy) * 0.9));
  const zFace = c.z + hz;
  const color = "#22c55e";
  if (shape === "circle") {
    drawFaceCircle(ctx, c.x + ox, c.y + oy, zFace, Math.min(r, Math.min(hx, hy)), w, h, color);
    return;
  }
  if (shape === "strip_x") {
    drawFaceRect(ctx, c.x + ox, c.y + oy, zFace, Math.min(hx, activeHx), Math.min(hy, stripW), w, h, color);
    return;
  }
  if (shape === "strip_y") {
    drawFaceRect(ctx, c.x + ox, c.y + oy, zFace, Math.min(hx, stripW), Math.min(hy, activeHy), w, h, color);
    return;
  }
  drawFaceRect(
    ctx,
    c.x + ox,
    c.y + oy,
    zFace,
    shape === "full" ? hx : Math.min(hx, activeHx),
    shape === "full" ? hy : Math.min(hy, activeHy),
    w,
    h,
    color
  );
}

function drawSensorAreaCylinder(ctx, c, r, hz, det, w, h) {
  const shape = String(det.photosensor_shape || "full");
  const ox = num(det.photosensor_offset_x_mm, 0);
  const oy = num(det.photosensor_offset_y_mm, 0);
  const outer = Math.max(0.1, Math.min(r, num(det.photosensor_radius_mm, r * 0.9)));
  const inner = Math.max(0.0, Math.min(outer - 0.05, num(det.photosensor_inner_radius_mm, 0)));
  const stripW = Math.max(0.1, num(det.photosensor_strip_width_mm, 1.0));
  const zFace = c.z + hz;
  const color = "#22c55e";
  if (shape === "rect") {
    const hx = Math.min(r, Math.max(0.1, num(det.photosensor_half_x_mm, outer * 0.8)));
    const hy = Math.min(r, Math.max(0.1, num(det.photosensor_half_y_mm, outer * 0.8)));
    drawFaceRect(ctx, c.x + ox, c.y + oy, zFace, hx, hy, w, h, color);
    return;
  }
  if (shape === "strip_x") {
    drawFaceRect(ctx, c.x + ox, c.y + oy, zFace, outer, Math.min(outer, stripW), w, h, color);
    return;
  }
  if (shape === "strip_y") {
    drawFaceRect(ctx, c.x + ox, c.y + oy, zFace, Math.min(outer, stripW), outer, w, h, color);
    return;
  }
  drawFaceCircle(ctx, c.x + ox, c.y + oy, zFace, outer, w, h, color);
  if (shape === "annulus" && inner > 0) drawFaceCircle(ctx, c.x + ox, c.y + oy, zFace, inner, w, h, color);
}

function drawBox(ctx, c, hx, hy, hz, w, h, baseColor, det = null) {
  const rgb = typeof baseColor === "string" ? (hexToRgb(baseColor) || { r: 100, g: 116, b: 139 }) : baseColor;
  const modelAlpha = Math.max(0.05, Math.min(1.0, num(state.visualization.modelOpacity, 0.95)));
  const edgeAlpha = Math.min(1.0, modelAlpha + 0.25);
  const v = [
    { x: c.x - hx, y: c.y - hy, z: c.z - hz },
    { x: c.x + hx, y: c.y - hy, z: c.z - hz },
    { x: c.x + hx, y: c.y + hy, z: c.z - hz },
    { x: c.x - hx, y: c.y + hy, z: c.z - hz },
    { x: c.x - hx, y: c.y - hy, z: c.z + hz },
    { x: c.x + hx, y: c.y - hy, z: c.z + hz },
    { x: c.x + hx, y: c.y + hy, z: c.z + hz },
    { x: c.x - hx, y: c.y + hy, z: c.z + hz },
  ];
  const faces = [
    [0, 1, 2, 3],
    [4, 5, 6, 7],
    [0, 1, 5, 4],
    [1, 2, 6, 5],
    [2, 3, 7, 6],
    [0, 3, 7, 4],
  ];
  const sorted = [];
  for (const f of faces) {
    const pts = f.map((idx) => v[idx]);
    const depth = polygonDepth(pts, w, h);
    if (depth === null) continue;
    sorted.push({ pts, depth, light: faceLightFactor(pts) });
  }
  sorted.sort((a, b) => b.depth - a.depth);
  for (const face of sorted) {
    const lit = shadeRgb(rgb, face.light);
    const edge = shadeRgb(rgb, Math.max(0.35, face.light * 0.62));
    fillPolygon3(ctx, face.pts, w, h, rgba(lit, modelAlpha), rgba(edge, edgeAlpha), 1.0);
  }
  if (det) drawSensorAreaBox(ctx, c, hx, hy, hz, det, w, h);
}

function drawCylinder(ctx, c, r, hz, w, h, baseColor, det = null) {
  const rgb = typeof baseColor === "string" ? (hexToRgb(baseColor) || { r: 100, g: 116, b: 139 }) : baseColor;
  const modelAlpha = Math.max(0.05, Math.min(1.0, num(state.visualization.modelOpacity, 0.95)));
  const edgeAlpha = Math.min(1.0, modelAlpha + 0.25);
  const seg = 24;
  const top = [];
  const bottom = [];
  for (let i = 0; i < seg; i += 1) {
    const a = (Math.PI * 2 * i) / seg;
    const x = c.x + r * Math.cos(a);
    const y = c.y + r * Math.sin(a);
    bottom.push({ x, y, z: c.z - hz });
    top.push({ x, y, z: c.z + hz });
  }

  const sideFaces = [];
  for (let i = 0; i < seg; i += 1) {
    const j = (i + 1) % seg;
    const quad = [bottom[i], bottom[j], top[j], top[i]];
    const depth = polygonDepth(quad, w, h);
    if (depth === null) continue;
    sideFaces.push({ pts: quad, depth, light: faceLightFactor(quad) });
  }
  sideFaces.sort((a, b) => b.depth - a.depth);

  const topDepth = polygonDepth(top, w, h);
  const bottomDepth = polygonDepth(bottom, w, h);
  const topFace = { pts: top, depth: topDepth ?? 0, light: faceLightFactor(top) * 1.03 };
  const bottomFace = { pts: bottom, depth: bottomDepth ?? 0, light: faceLightFactor(bottom) * 0.92 };

  if (topDepth !== null && bottomDepth !== null) {
    const farCap = topFace.depth > bottomFace.depth ? topFace : bottomFace;
    const nearCap = topFace.depth > bottomFace.depth ? bottomFace : topFace;
    fillPolygon3(
      ctx,
      farCap.pts,
      w,
      h,
      rgba(shadeRgb(rgb, farCap.light), modelAlpha),
      rgba(shadeRgb(rgb, Math.max(0.35, farCap.light * 0.62)), edgeAlpha),
      1.0
    );
    for (const f of sideFaces) {
      fillPolygon3(
        ctx,
        f.pts,
        w,
        h,
        rgba(shadeRgb(rgb, f.light), modelAlpha),
        rgba(shadeRgb(rgb, Math.max(0.35, f.light * 0.62)), edgeAlpha),
        1.0
      );
    }
    fillPolygon3(
      ctx,
      nearCap.pts,
      w,
      h,
      rgba(shadeRgb(rgb, nearCap.light), modelAlpha),
      rgba(shadeRgb(rgb, Math.max(0.35, nearCap.light * 0.62)), edgeAlpha),
      1.0
    );
  } else {
    for (const f of sideFaces) {
      fillPolygon3(
        ctx,
        f.pts,
        w,
        h,
        rgba(shadeRgb(rgb, f.light), modelAlpha),
        rgba(shadeRgb(rgb, Math.max(0.35, f.light * 0.62)), edgeAlpha),
        1.0
      );
    }
  }
  if (det) drawSensorAreaCylinder(ctx, c, r, hz, det, w, h);
}

function renderViewport() {
  const canvas = document.getElementById("viewportCanvas");
  if (!canvas || !state.config) return;
  const ctx = canvas.getContext("2d");
  const dpr = window.devicePixelRatio || 1;
  const w = canvas.clientWidth || 600;
  const h = canvas.clientHeight || 400;
  canvas.width = Math.floor(w * dpr);
  canvas.height = Math.floor(h * dpr);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = "#111827";
  ctx.fillRect(0, 0, w, h);

  const solidDrawQueue = [];
  const enqueueSolid = (center, drawFn) => {
    const pr = project(center, w, h);
    const depth = pr ? pr.depth : (rotatePoint(center).z + 1200);
    solidDrawQueue.push({ depth, draw: drawFn });
  };

  const g = state.config.phase1.geometry;
  const layers = state.config.phase1.geometry.layers;
  let z = num(g.stack_center_z_mm, 0) - layers.reduce((acc, l) => acc + num(l.thickness_mm, 0), 0) / 2;
  for (const layer of layers) {
    const th = num(layer.thickness_mm, 1);
    const cz = z + th / 2;
    const center = { x: 0, y: 0, z: cz };
    if (layer?.visible === false) {
      z += th;
      continue;
    }
    const layerColor = materialColorRgb(layer.material || layer.name || "", "#334155");
    enqueueSolid(center, () => {
      drawBox(
        ctx,
        center,
        num(g.aperture_half_x_mm, 80),
        num(g.aperture_half_y_mm, 80),
        th / 2,
        w,
        h,
        layerColor
      );
    });
    z += th;
  }

  for (const det of state.config.phase3.detectors) {
    ensureDetectorDefaults(det);
    if (det.visible === false) continue;
    const bodyColor = materialColorRgb(det.material || det.id || "", "#64748b");
    const center = { x: num(det.x_mm, 0), y: num(det.y_mm, 0), z: num(det.z_mm, 0) };
    if ((det.shape || "box") === "cylinder") {
      enqueueSolid(center, () => {
        drawCylinder(
          ctx,
          center,
          num(det.radius_mm, 10),
          num(det.half_z_mm, 1),
          w,
          h,
          bodyColor,
          det
        );
      });
    } else {
      enqueueSolid(center, () => {
        drawBox(
          ctx,
          center,
          num(det.half_x_mm, 10),
          num(det.half_y_mm, 10),
          num(det.half_z_mm, 1),
          w,
          h,
          bodyColor,
          det
        );
      });
    }
  }
  solidDrawQueue.sort((a, b) => b.depth - a.depth);
  for (const item of solidDrawQueue) item.draw();

  if (state.visualization.showParticleTracks) {
    if (state.particleTracks.length > 0) {
      const nonDeltaTracks = [];
      const deltaTracks = [];
      for (const track of state.particleTracks) {
        if (track.isDelta) deltaTracks.push(track);
        else nonDeltaTracks.push(track);
      }

      ctx.save();
      ctx.globalAlpha = 0.32;
      for (const track of nonDeltaTracks) {
        polyline3(ctx, track.points, w, h, "#f59e0b", 1.2);
      }
      ctx.restore();
      if (state.visualization.showDeltaTracks) {
        const deltaLimit = Math.max(0, state.visualization.deltaTrackCount || 0);
        let shownDelta = 0;
        for (const track of deltaTracks) {
          if (shownDelta >= deltaLimit) break;
          polyline3(ctx, track.points, w, h, "#ff2d55", 2.1);
          shownDelta += 1;
        }
      }
    } else if (state.stepRows.length === 0) {
      for (const tr of state.config.phase1.tracks) {
        const p = tr.pos_mm || [0, 0, 0];
        const m = tr.mom_mev || [0, 0, 1];
        const norm = Math.sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]) || 1;
        const len = 120;
        const a = { x: num(p[0], 0), y: num(p[1], 0), z: num(p[2], 0) };
        const b = { x: a.x + (m[0] / norm) * len, y: a.y + (m[1] / norm) * len, z: a.z + (m[2] / norm) * len };
        line3(ctx, a, b, w, h, "#f59e0b");
      }
    }
  }

  if (state.visualization.showPhotonTracks && state.selectedPhotonTrackKeys.length > 0) {
    for (const key of state.selectedPhotonTrackKeys) {
      const pts = state.photonTracks[key];
      if (!pts || pts.length < 2) continue;
      polyline3(ctx, pts, w, h, "#22d3ee", 1.0);
    }
  }

  if (state.visualization.showDetectorHits) {
    const maxHits = 1500;
    for (let i = 0; i < Math.min(maxHits, state.hits.length); i += 1) {
      const hit = state.hits[i];
      const p = project({ x: num(hit.x_mm, 0), y: num(hit.y_mm, 0), z: num(hit.z_mm, 0) }, w, h);
      if (!p) continue;
      ctx.fillStyle = "#10b981";
      ctx.fillRect(p.x - 1, p.y - 1, 2, 2);
    }
  }

  const info = document.getElementById("cameraInfo");
  info.textContent = `Yaw ${state.view.yaw.toFixed(2)} | Pitch ${state.view.pitch.toFixed(2)} | Zoom ${state.view.zoom.toFixed(2)}`;
}

function setupViewportControls() {
  const canvas = document.getElementById("viewportCanvas");
  canvas.addEventListener("mousedown", (e) => {
    state.view.dragging = true;
    state.view.x = e.clientX;
    state.view.y = e.clientY;
  });
  window.addEventListener("mouseup", () => { state.view.dragging = false; });
  window.addEventListener("mousemove", (e) => {
    if (!state.view.dragging) return;
    const dx = e.clientX - state.view.x;
    const dy = e.clientY - state.view.y;
    state.view.yaw += dx * 0.005;
    state.view.pitch += dy * 0.005;
    state.view.pitch = Math.max(-1.3, Math.min(1.3, state.view.pitch));
    state.view.x = e.clientX;
    state.view.y = e.clientY;
    renderViewport();
  });
  canvas.addEventListener("wheel", (e) => {
    e.preventDefault();
    const s = e.deltaY < 0 ? 1.08 : 0.92;
    state.view.zoom = Math.max(0.2, Math.min(5.0, state.view.zoom * s));
    renderViewport();
  }, { passive: false });
  window.addEventListener("resize", () => {
    renderViewport();
    drawPlots(
      state.hits.map((r) => num(r.t_ns, 0)),
      state.hits.map((r) => num(r.lambda_nm, 0))
    );
    drawWaveformPlot();
  });
}

function selectBottomTab(tabName) {
  document.querySelectorAll(".tab-bar .tab").forEach((t) => {
    if (t.dataset.bottom) t.classList.toggle("active", t.dataset.bottom === tabName);
  });
  document.querySelectorAll(".bottom-content").forEach((c) => c.classList.remove("active"));
  document.getElementById(`bottom-${tabName}`)?.classList.add("active");
  if (tabName === "plots") {
    drawPlots(
      state.hits.map((r) => num(r.t_ns, 0)),
      state.hits.map((r) => num(r.lambda_nm, 0))
    );
    drawWaveformPlot();
  }
}

async function importConfigFile(file) {
  const text = await file.text();
  const cfg = JSON.parse(text);
  state.config = cfg;
  ensureDefaults();
  state.selected = { type: "world", index: -1 };
  setDirty(true);
  renderAll();
  syncSetupForm();
  syncVisualizationControls();
  syncWaveformControls();
  drawWaveformPlot();
  logLine(`Imported config from ${file.name}`);
}

function exportConfigFile() {
  const raw = JSON.stringify(state.config, null, 2);
  const blob = new Blob([raw], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `${state.projectId || "project"}_config.json`;
  a.click();
  URL.revokeObjectURL(url);
}

async function uploadAsset(file) {
  const base64 = await fileToBase64(file);
  const payload = await api(`/api/projects/${state.projectId}/assets`, "POST", {
    filename: file.name,
    content_base64: base64,
  });
  const abs = payload.asset.absolute_path;
  logLine(`Uploaded asset: ${payload.asset.filename}`);
  if (state.selected.type === "detector" && state.selected.index >= 0) {
    const det = state.config.phase3.detectors[state.selected.index];
    det.shape = "mesh";
    det.mesh_path = abs;
    setDirty(true);
    renderInspector();
    renderViewport();
    logLine(`Detector ${det.id} set to mesh path ${abs}`);
  }
}

function fileToBase64(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => {
      const s = String(reader.result);
      const idx = s.indexOf(",");
      resolve(idx >= 0 ? s.slice(idx + 1) : s);
    };
    reader.onerror = reject;
    reader.readAsDataURL(file);
  });
}

async function importPdeCsv(file) {
  const text = await file.text();
  const x = [];
  const values = [];
  for (const rawLine of text.split(/\r?\n/)) {
    const line = rawLine.trim();
    if (!line || line.startsWith("#")) continue;
    const cols = line.split(/[,\s;]+/);
    if (cols.length < 2) continue;
    const xx = Number(cols[0]);
    const yy = Number(cols[1]);
    if (Number.isFinite(xx) && Number.isFinite(yy)) {
      x.push(xx);
      values.push(yy);
    }
  }
  if (x.length < 2) throw new Error("PDE CSV needs at least two numeric rows");
  state.config.phase4.detection.use_direct_pde = true;
  state.config.phase4.detection.pde_table = { x, values, extrapolation: "clamp" };
  setDirty(true);
  syncSetupForm();
  logLine(`Imported PDE table (${x.length} points)`);
}

function refreshAdvancedText() {
  const ta = document.getElementById("advancedConfig");
  ta.value = JSON.stringify(state.config, null, 2);
}

function applyAdvancedText() {
  const ta = document.getElementById("advancedConfig");
  const parsed = JSON.parse(ta.value);
  state.config = parsed;
  ensureDefaults();
  setDirty(true);
  renderAll();
  syncSetupForm();
  syncVisualizationControls();
  syncWaveformControls();
  drawWaveformPlot();
  logLine("Applied advanced JSON");
}

function bindEvents() {
  document.querySelectorAll(".nav-btn").forEach((b) => {
    b.addEventListener("click", () => workspaceSwitch(b.dataset.workspace));
  });
  document.querySelectorAll(".tab").forEach((b) => {
    if (!b.dataset.bottom) return;
    b.addEventListener("click", () => selectBottomTab(b.dataset.bottom));
  });
  document.getElementById("projectSelect").addEventListener("change", async (e) => {
    if (state.dirty && !confirm("Discard unsaved changes and switch project?")) {
      renderProjectSelect();
      return;
    }
    await loadProject(e.target.value);
  });
  document.getElementById("newProjectBtn").addEventListener("click", async () => {
    const name = prompt("Project name:", `Simulation Project ${String(state.projects.length + 1).padStart(3, "0")}`);
    if (!name) return;
    await createProject(name);
  });
  document.getElementById("saveBtn").addEventListener("click", async () => {
    try { await saveCurrentProject(); setStatus("Saved", "status-ok"); } catch (err) { logLine(err.message, "ERROR"); }
  });
  document.getElementById("runBtn").addEventListener("click", runProject);
  document.getElementById("addLayerBtn").addEventListener("click", addLayer);
  document.getElementById("addDetectorBtn").addEventListener("click", addDetector);
  document.getElementById("addTrackBtn").addEventListener("click", addTrack);
  document.getElementById("addTemplateSensorBtn").addEventListener("click", addTemplateSensor);
  document.getElementById("applySetupBtn").addEventListener("click", () => { applySetupForm(); renderViewport(); });

  document.getElementById("vizShowPhoton").addEventListener("change", (e) => {
    state.visualization.showPhotonTracks = !!e.target.checked;
    syncVisualizationControls();
    renderViewport();
  });
  document.getElementById("vizPhotonCount").addEventListener("change", (e) => {
    state.visualization.photonTrackCount = clampInt(e.target.value, 1, Math.max(1, state.photonTrackKeys.length || 1));
    rerollPhotonTracks();
  });
  document.getElementById("vizRerollBtn").addEventListener("click", () => rerollPhotonTracks());
  document.getElementById("vizShowParticle").addEventListener("change", (e) => {
    state.visualization.showParticleTracks = !!e.target.checked;
    syncVisualizationControls();
    renderViewport();
  });
  document.getElementById("vizShowDelta").addEventListener("change", (e) => {
    state.visualization.showDeltaTracks = !!e.target.checked;
    syncVisualizationControls();
    renderViewport();
  });
  document.getElementById("vizShowHits").addEventListener("change", (e) => {
    state.visualization.showDetectorHits = !!e.target.checked;
    syncVisualizationControls();
    renderViewport();
  });
  document.getElementById("vizModelOpacity").addEventListener("input", (e) => {
    state.visualization.modelOpacity = Math.max(0.05, Math.min(1.0, num(e.target.value, 0.95)));
    syncVisualizationControls();
    renderViewport();
  });
  document.getElementById("vizDeltaCount").addEventListener("change", (e) => {
    const deltaTotal = state.particleTracks.reduce((acc, tr) => acc + (tr.isDelta ? 1 : 0), 0);
    if (deltaTotal <= 0) {
      state.visualization.deltaTrackCount = 0;
    } else {
      state.visualization.deltaTrackCount = clampInt(e.target.value, 1, deltaTotal);
    }
    syncVisualizationControls();
    renderViewport();
  });

  document.getElementById("waveformChannelSelect").addEventListener("change", (e) => {
    state.selectedWaveformChannel = e.target.value;
    drawWaveformPlot();
  });
  document.getElementById("waveformModeSelect").addEventListener("change", (e) => {
    state.waveformMode = e.target.value === "voltage" ? "voltage" : "current_pe";
    drawWaveformPlot();
  });

  document.getElementById("modeSimple").addEventListener("click", () => {
    state.mode = "simple";
    document.getElementById("modeSimple").classList.add("active");
    document.getElementById("modeAdvanced").classList.remove("active");
    document.getElementById("advancedPane").classList.add("hidden");
  });
  document.getElementById("modeAdvanced").addEventListener("click", () => {
    state.mode = "advanced";
    document.getElementById("modeAdvanced").classList.add("active");
    document.getElementById("modeSimple").classList.remove("active");
    document.getElementById("advancedPane").classList.remove("hidden");
    refreshAdvancedText();
  });
  document.getElementById("applyAdvancedBtn").addEventListener("click", () => {
    try { applyAdvancedText(); } catch (err) { logLine(err.message, "ERROR"); }
  });

  const cfgInput = document.getElementById("configFileInput");
  document.getElementById("importConfigBtn").addEventListener("click", () => cfgInput.click());
  cfgInput.addEventListener("change", async () => {
    if (!cfgInput.files?.length) return;
    try { await importConfigFile(cfgInput.files[0]); } catch (err) { logLine(err.message, "ERROR"); }
    cfgInput.value = "";
  });
  document.getElementById("exportConfigBtn").addEventListener("click", exportConfigFile);

  const assetInput = document.getElementById("assetFileInput");
  document.getElementById("uploadAssetBtn").addEventListener("click", () => assetInput.click());
  assetInput.addEventListener("change", async () => {
    if (!assetInput.files?.length) return;
    try { await uploadAsset(assetInput.files[0]); } catch (err) { logLine(err.message, "ERROR"); }
    assetInput.value = "";
  });

  const pdeInput = document.getElementById("pdeFileInput");
  document.getElementById("importPdeBtn").addEventListener("click", () => pdeInput.click());
  pdeInput.addEventListener("change", async () => {
    if (!pdeInput.files?.length) return;
    try { await importPdeCsv(pdeInput.files[0]); } catch (err) { logLine(err.message, "ERROR"); }
    pdeInput.value = "";
  });
}

async function init() {
  bindEvents();
  setupViewportControls();
  setStatus("Loading...");
  try {
    await loadProjects();
    setStatus("Ready", "status-ok");
  } catch (err) {
    setStatus("Backend Error", "status-failed");
    logLine(err.message, "ERROR");
  }
}

init();
