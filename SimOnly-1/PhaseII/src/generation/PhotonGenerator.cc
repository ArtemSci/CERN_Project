#include "PhotonGenerator.hh"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>

#include "G4ParticleTable.hh"
#include "G4PhysicalConstants.hh"
#include "G4SystemOfUnits.hh"
#include "G4Alpha.hh"
#include "G4Electron.hh"
#include "G4Gamma.hh"
#include "G4MuonMinus.hh"
#include "G4MuonPlus.hh"
#include "G4PionMinus.hh"
#include "G4PionPlus.hh"
#include "G4Positron.hh"
#include "G4Proton.hh"

#include "nlohmann/json.hpp"
#include "util/CsvUtils.hh"

namespace phase2 {
namespace {

using json = nlohmann::json;
constexpr double kPi = 3.141592653589793;

void EnsureGeantParticles() {
  static bool initialized = false;
  if (initialized) {
    return;
  }
  initialized = true;
  G4Electron::ElectronDefinition();
  G4Positron::PositronDefinition();
  G4MuonMinus::MuonMinusDefinition();
  G4MuonPlus::MuonPlusDefinition();
  G4PionMinus::PionMinusDefinition();
  G4PionPlus::PionPlusDefinition();
  G4Proton::ProtonDefinition();
  G4Alpha::AlphaDefinition();
  G4Gamma::GammaDefinition();
}

ExtrapolationMode ParseExtrapolationMode(const std::string& value) {
  std::string lower;
  lower.reserve(value.size());
  for (char ch : value) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  if (lower == "clamp_zero") {
    return ExtrapolationMode::kClampZero;
  }
  if (lower == "clamp_edge") {
    return ExtrapolationMode::kClampEdge;
  }
  return ExtrapolationMode::kError;
}

bool ApplyBand(double band_min, double band_max, double* out_min, double* out_max) {
  if (band_max <= band_min) {
    return false;
  }
  if (out_min) {
    *out_min = std::max(*out_min, band_min);
  }
  if (out_max) {
    *out_max = std::min(*out_max, band_max);
  }
  return (*out_max > *out_min);
}

}  // namespace

void PhotonSoA::Clear() {
  event_id.clear();
  track_id.clear();
  parent_id.clear();
  node_id.clear();
  surface_id.clear();
  step_index.clear();
  origin_id.clear();
  material_id.clear();
  x_mm.clear();
  y_mm.clear();
  z_mm.clear();
  t_ns.clear();
  lambda_nm.clear();
  vgroup_mm_per_ns.clear();
  dir_x.clear();
  dir_y.clear();
  dir_z.clear();
  pol_x.clear();
  pol_y.clear();
  pol_z.clear();
  weight.clear();
  origin_table.clear();
  material_table.clear();
  origin_lookup.clear();
  material_lookup.clear();
}

int PhotonSoA::OriginIndex(const std::string& name) {
  auto it = origin_lookup.find(name);
  if (it != origin_lookup.end()) {
    return it->second;
  }
  const int id = static_cast<int>(origin_table.size());
  origin_table.push_back(name);
  origin_lookup[name] = id;
  return id;
}

int PhotonSoA::MaterialIndex(const std::string& name) {
  auto it = material_lookup.find(name);
  if (it != material_lookup.end()) {
    return it->second;
  }
  const int id = static_cast<int>(material_table.size());
  material_table.push_back(name);
  material_lookup[name] = id;
  return id;
}

PhotonGenerator::PhotonGenerator(const PhaseIIConfig& config)
    : config_(config), materials_(), rng_(config.rng.seed) {
  EnsureGeantParticles();
  bool ok = true;
  if (config_.generation.use_phase1_refractive_index) {
    if (config_.phase1_input.empty()) {
      last_error_ =
          "phase1_input is required when generation.use_phase1_refractive_index is enabled.";
      ok = false;
    } else {
      ok = BuildPhase1Overrides();
    }
  }
  if (ok) {
    materials_ = MaterialLibrary(config_);
    if (!config_.sensor.pde.lambda_nm.empty()) {
      sensor_pde_.SetTable(config_.sensor.pde.lambda_nm,
                           config_.sensor.pde.values,
                           ParseExtrapolationMode(config_.sensor.pde.extrapolation));
    }
    if (!config_.sensor.filter.lambda_nm.empty()) {
      sensor_filter_.SetTable(config_.sensor.filter.lambda_nm,
                              config_.sensor.filter.values,
                              ParseExtrapolationMode(config_.sensor.filter.extrapolation));
    }
    ok = ValidateTables();
  }
  valid_ = ok;
}

const std::vector<Photon>& PhotonGenerator::Photons() const {
  return photons_;
}

const PhotonSoA& PhotonGenerator::PhotonsSoA() const {
  return photons_soa_;
}

const std::vector<StepSummary>& PhotonGenerator::StepSummaries() const {
  return summaries_;
}

const Metrics& PhotonGenerator::GetMetrics() const {
  return metrics_;
}

const std::string& PhotonGenerator::LastError() const {
  return last_error_;
}

bool PhotonGenerator::BuildPhase1Overrides() {
  std::ifstream input(config_.phase1_input);
  if (!input) {
    last_error_ =
        "Failed to open phase1_input for refractive index override: " + config_.phase1_input;
    return false;
  }
  json j;
  try {
    input >> j;
  } catch (const std::exception& ex) {
    last_error_ = "Invalid JSON in phase1_input '" + config_.phase1_input + "': " + ex.what();
    return false;
  }
  const json& root = j.contains("phase1") ? j.at("phase1") : j;
  if (!root.contains("geometry") || !root.at("geometry").contains("layers")) {
    last_error_ =
        "phase1_input missing phase1.geometry.layers required for refractive index override: " +
        config_.phase1_input;
    return false;
  }
  const auto& layers = root.at("geometry").at("layers");
  if (!layers.is_array()) {
    last_error_ = "phase1.geometry.layers must be an array in phase1_input: " + config_.phase1_input;
    return false;
  }
  if (layers.empty()) {
    last_error_ =
        "phase1.geometry.layers is empty in phase1_input while refractive index override is enabled: " +
        config_.phase1_input;
    return false;
  }

  std::map<std::string, double> ref_index;
  std::map<std::string, std::string> override_base;
  for (const auto& layer : layers) {
    const std::string layer_name = layer.value("name", "Layer");
    const std::string material = layer.value("material", "G4_AIR");
    const double n_ref = layer.value("refractive_index", 1.0);
    std::string material_name = material;
    if (layer.contains("material_override")) {
      material_name = layer_name + "_Custom";
      override_base[material_name] = material;
    }
    ref_index[material_name] = n_ref;
  }

  for (auto& material : config_.materials) {
    auto it = ref_index.find(material.name);
    if (it == ref_index.end()) {
      continue;
    }
    MaterialConfig temp = material;
    temp.n_offset = 0.0;
    MaterialModel model(temp);
    const double base_n = model.RefractiveIndex(config_.wavelength.ref_nm);
    material.n_offset = it->second - base_n;
  }

  auto find_material_config = [&](const std::string& name, MaterialConfig* out) -> bool {
    auto it = std::find_if(config_.materials.begin(),
                           config_.materials.end(),
                           [&](const MaterialConfig& m) { return m.name == name; });
    if (it != config_.materials.end()) {
      if (out) {
        *out = *it;
      }
      return true;
    }
    auto alias_it = config_.material_aliases.find(name);
    if (alias_it != config_.material_aliases.end()) {
      auto alias_cfg = std::find_if(config_.materials.begin(),
                                    config_.materials.end(),
                                    [&](const MaterialConfig& m) { return m.name == alias_it->second; });
      if (alias_cfg != config_.materials.end()) {
        if (out) {
          *out = *alias_cfg;
        }
        return true;
      }
    }
    return false;
  };

  for (const auto& entry : ref_index) {
    const std::string& material_name = entry.first;
    const double n_ref = entry.second;
    const bool exists = std::any_of(config_.materials.begin(), config_.materials.end(),
                                    [&](const MaterialConfig& m) { return m.name == material_name; });
    if (exists) {
      continue;
    }
    MaterialConfig custom;
    bool seeded = false;
    auto base_it = override_base.find(material_name);
    if (base_it != override_base.end()) {
      seeded = find_material_config(base_it->second, &custom);
      if (!seeded) {
        last_error_ = "Missing material definition for custom Phase I layer base material: " + base_it->second;
        return false;
      }
    }
    if (!seeded) {
      last_error_ = "Missing material definition for Phase I override: " + material_name;
      return false;
    }
    custom.name = material_name;
    MaterialModel base(custom);
    const double base_n = base.RefractiveIndex(config_.wavelength.ref_nm);
    custom.n_offset = n_ref - base_n;
    config_.materials.push_back(custom);
  }
  return true;
}

bool PhotonGenerator::ValidateTables() {
  auto table_range = [](const TableConfig& table, double* out_min, double* out_max) -> bool {
    if (table.lambda_nm.empty() || table.lambda_nm.size() != table.values.size()) {
      return false;
    }
    double min_val = table.lambda_nm.front();
    double max_val = table.lambda_nm.front();
    for (double value : table.lambda_nm) {
      min_val = std::min(min_val, value);
      max_val = std::max(max_val, value);
    }
    if (out_min) {
      *out_min = min_val;
    }
    if (out_max) {
      *out_max = max_val;
    }
    return true;
  };

  const double band_min = config_.wavelength.min_nm;
  const double band_max = config_.wavelength.max_nm;
  bool ok = true;

  auto check_range = [&](const std::string& label, double min_nm, double max_nm) {
    if (band_min < min_nm || band_max > max_nm) {
      ok = false;
      if (config_.safety.warn_on_extrapolation) {
        std::cerr << "Phase II table range warning (" << label << "): "
                  << "[" << min_nm << ", " << max_nm << "] nm does not cover ["
                  << band_min << ", " << band_max << "] nm.\n";
      }
    }
  };

  if (sensor_pde_.IsValid()) {
    check_range("sensor_pde", sensor_pde_.MinLambda(), sensor_pde_.MaxLambda());
  }
  if (sensor_filter_.IsValid()) {
    check_range("sensor_filter", sensor_filter_.MinLambda(), sensor_filter_.MaxLambda());
  }

  for (const auto& material : config_.materials) {
    double min_nm = 0.0;
    double max_nm = 0.0;
    if (table_range(material.refractive_index_table, &min_nm, &max_nm)) {
      check_range(material.name + ":refractive_index", min_nm, max_nm);
    }
    if (table_range(material.absorption_length_table, &min_nm, &max_nm)) {
      check_range(material.name + ":absorption_length", min_nm, max_nm);
    }
    if (table_range(material.transmission_table, &min_nm, &max_nm)) {
      check_range(material.name + ":transmission", min_nm, max_nm);
    }
  }
  if (!config_.default_material.name.empty()) {
    double min_nm = 0.0;
    double max_nm = 0.0;
    if (table_range(config_.default_material.refractive_index_table, &min_nm, &max_nm)) {
      check_range("default_material:refractive_index", min_nm, max_nm);
    }
    if (table_range(config_.default_material.absorption_length_table, &min_nm, &max_nm)) {
      check_range("default_material:absorption_length", min_nm, max_nm);
    }
    if (table_range(config_.default_material.transmission_table, &min_nm, &max_nm)) {
      check_range("default_material:transmission", min_nm, max_nm);
    }
  }

  if (!ok && config_.safety.fail_on_extrapolation) {
    last_error_ = "Material_Safety_Check failed: table ranges do not cover the active wavelength band.";
    return false;
  }
  return true;
}

bool PhotonGenerator::LoadPhase1Nodes() {
  const std::string nodes_path = config_.phase1_output + "/" + config_.phase1_nodes_csv;
  std::ifstream nodes_file(nodes_path);
  if (!nodes_file) {
    last_error_ = "Missing Phase I nodes CSV at " + nodes_path;
    return false;
  }
  std::string line;
  if (!std::getline(nodes_file, line)) {
    last_error_ = "Empty Phase I nodes CSV at " + nodes_path;
    return false;
  }
  auto header = csvutil::SplitCsv(line);
  auto index_of = [&](const std::string& key) -> size_t {
    auto it = std::find(header.begin(), header.end(), key);
    return (it == header.end()) ? header.size() : static_cast<size_t>(it - header.begin());
  };
  const size_t event_id_idx = index_of("event_id");
  const size_t track_id_idx = index_of("track_id");
  const size_t step_idx = index_of("step_index");
  const size_t surface_idx = index_of("surface_id");
  if (event_id_idx == header.size() || track_id_idx == header.size() ||
      step_idx == header.size() || surface_idx == header.size()) {
    last_error_ = "Phase I nodes CSV missing required columns in " + nodes_path;
    return false;
  }

  int node_id = 0;
  int row_num = 1;
  while (std::getline(nodes_file, line)) {
    ++row_num;
    auto cols = csvutil::SplitCsv(line);
    int event_id = 0;
    int track_id = 0;
    int step_index = 0;
    int surface_id = -1;
    if (!csvutil::TryParseInt(cols, event_id_idx, event_id) ||
        !csvutil::TryParseInt(cols, track_id_idx, track_id) ||
        !csvutil::TryParseInt(cols, step_idx, step_index) ||
        !csvutil::TryParseInt(cols, surface_idx, surface_id)) {
      last_error_ = "Invalid numeric value in nodes CSV at row " + std::to_string(row_num);
      return false;
    }
    nodes_[{event_id, track_id, step_index}] = {node_id, surface_id};
    ++node_id;
  }
  return true;
}

void PhotonGenerator::RecordExtrapolation(int event_id) {
  metrics_.extrapolated_photons += 1;
  extrapolated_events_[event_id] = true;
}

bool PhotonGenerator::LoadPhase1Data() {
  const std::string summary_path = config_.phase1_output + "/track_summary.csv";
  std::ifstream summary_file(summary_path);
  if (!summary_file) {
    last_error_ = "Missing Phase I track_summary.csv at " + summary_path;
    return false;
  }
  std::string line;
  if (!std::getline(summary_file, line)) {
    last_error_ = "Empty track_summary.csv";
    return false;
  }
  auto header = csvutil::SplitCsv(line);
  auto find_index = [&](const std::string& key) -> size_t {
    auto it = std::find(header.begin(), header.end(), key);
    return (it == header.end()) ? header.size() : static_cast<size_t>(it - header.begin());
  };
  const size_t event_idx = find_index("event_id");
  const size_t track_idx = find_index("track_id");
  const size_t parent_idx = find_index("parent_id");
  const size_t particle_idx = find_index("particle");
  const size_t first_x_idx = find_index("first_x_mm");
  const size_t first_y_idx = find_index("first_y_mm");
  const size_t first_z_idx = find_index("first_z_mm");
  const size_t first_t_idx = find_index("first_t_ns");
  const size_t first_px_idx = find_index("first_px_mev");
  const size_t first_py_idx = find_index("first_py_mev");
  const size_t first_pz_idx = find_index("first_pz_mev");
  const size_t first_pol_x_idx = find_index("first_pol_x");
  const size_t first_pol_y_idx = find_index("first_pol_y");
  const size_t first_pol_z_idx = find_index("first_pol_z");
  const size_t first_material_idx = find_index("first_material");
  if (event_idx == header.size() || track_idx == header.size() ||
      parent_idx == header.size() || particle_idx == header.size()) {
    last_error_ = "Phase I track_summary.csv missing required columns at " + summary_path;
    return false;
  }

  int summary_row = 1;
  while (std::getline(summary_file, line)) {
    ++summary_row;
    auto cols = csvutil::SplitCsv(line);
    TrackInfo info;
    int event_id = 0;
    int track_id = 0;
    if (!csvutil::TryParseInt(cols, parent_idx, info.parent_id) ||
        !csvutil::TryParseInt(cols, event_idx, event_id) ||
        !csvutil::TryParseInt(cols, track_idx, track_id) ||
        particle_idx >= cols.size() || cols[particle_idx].empty()) {
      last_error_ = "Invalid row in track_summary.csv at row " + std::to_string(summary_row);
      return false;
    }
    info.particle = cols[particle_idx];
    auto parse_optional_double = [&](size_t idx, double* out) -> bool {
      if (!out) {
        return false;
      }
      if (idx >= cols.size()) {
        return false;
      }
      return csvutil::TryParseDouble(cols, idx, *out);
    };
    const bool has_birth_state =
        parse_optional_double(first_x_idx, &info.first_x_mm) &&
        parse_optional_double(first_y_idx, &info.first_y_mm) &&
        parse_optional_double(first_z_idx, &info.first_z_mm) &&
        parse_optional_double(first_t_idx, &info.first_t_ns) &&
        parse_optional_double(first_px_idx, &info.first_px_mev) &&
        parse_optional_double(first_py_idx, &info.first_py_mev) &&
        parse_optional_double(first_pz_idx, &info.first_pz_mev) &&
        parse_optional_double(first_pol_x_idx, &info.first_pol_x) &&
        parse_optional_double(first_pol_y_idx, &info.first_pol_y) &&
        parse_optional_double(first_pol_z_idx, &info.first_pol_z);
    if (has_birth_state &&
        std::isfinite(info.first_x_mm) && std::isfinite(info.first_y_mm) &&
        std::isfinite(info.first_z_mm) && std::isfinite(info.first_t_ns) &&
        std::isfinite(info.first_px_mev) && std::isfinite(info.first_py_mev) &&
        std::isfinite(info.first_pz_mev) && std::isfinite(info.first_pol_x) &&
        std::isfinite(info.first_pol_y) && std::isfinite(info.first_pol_z)) {
      info.has_birth_state = true;
      if (first_material_idx < cols.size() && !cols[first_material_idx].empty()) {
        info.first_material = cols[first_material_idx];
      }
    }
    tracks_[{event_id, track_id}] = info;
  }

  const std::string step_path = config_.phase1_output + "/step_trace.csv";
  std::ifstream step_file(step_path);
  if (!step_file) {
    last_error_ = "Missing Phase I step_trace.csv at " + step_path;
    return false;
  }
  if (!std::getline(step_file, line)) {
    last_error_ = "Empty step_trace.csv";
    return false;
  }
  header = csvutil::SplitCsv(line);
  auto index_of = [&](const std::string& key) -> size_t {
    auto it = std::find(header.begin(), header.end(), key);
    return (it == header.end()) ? header.size() : static_cast<size_t>(it - header.begin());
  };

  const size_t event_id_idx = index_of("event_id");
  const size_t track_id_idx = index_of("track_id");
  const size_t parent_id_idx = index_of("parent_id");
  const size_t step_idx = index_of("step_index");
  const size_t x_idx = index_of("x_mm");
  const size_t y_idx = index_of("y_mm");
  const size_t z_idx = index_of("z_mm");
  const size_t t_idx = index_of("t_ns");
  const size_t px_idx = index_of("px_mev");
  const size_t py_idx = index_of("py_mev");
  const size_t pz_idx = index_of("pz_mev");
  const size_t pol_x_idx = index_of("pol_x");
  const size_t pol_y_idx = index_of("pol_y");
  const size_t pol_z_idx = index_of("pol_z");
  const size_t len_idx = index_of("step_length_mm");
  const size_t edep_idx = index_of("edep_mev");
  const size_t pre_idx = index_of("pre_material");
  const size_t post_idx = index_of("post_material");
  if (event_id_idx == header.size() || track_id_idx == header.size() ||
      parent_id_idx == header.size() || step_idx == header.size() ||
      x_idx == header.size() || y_idx == header.size() || z_idx == header.size() ||
      t_idx == header.size() || px_idx == header.size() || py_idx == header.size() ||
      pz_idx == header.size() || pol_x_idx == header.size() || pol_y_idx == header.size() ||
      pol_z_idx == header.size() || len_idx == header.size() || pre_idx == header.size() ||
      post_idx == header.size()) {
    last_error_ = "Phase I step_trace.csv missing required columns at " + step_path;
    return false;
  }

  int step_row = 1;
  while (std::getline(step_file, line)) {
    ++step_row;
    auto cols = csvutil::SplitCsv(line);
    StepRecord record;
    if (!csvutil::TryParseInt(cols, event_id_idx, record.event_id) ||
        !csvutil::TryParseInt(cols, track_id_idx, record.track_id) ||
        !csvutil::TryParseInt(cols, parent_id_idx, record.parent_id) ||
        !csvutil::TryParseInt(cols, step_idx, record.step_index) ||
        !csvutil::TryParseDouble(cols, x_idx, record.x_mm) ||
        !csvutil::TryParseDouble(cols, y_idx, record.y_mm) ||
        !csvutil::TryParseDouble(cols, z_idx, record.z_mm) ||
        !csvutil::TryParseDouble(cols, t_idx, record.t_ns) ||
        !csvutil::TryParseDouble(cols, px_idx, record.px_mev) ||
        !csvutil::TryParseDouble(cols, py_idx, record.py_mev) ||
        !csvutil::TryParseDouble(cols, pz_idx, record.pz_mev) ||
        !csvutil::TryParseDouble(cols, pol_x_idx, record.pol_x) ||
        !csvutil::TryParseDouble(cols, pol_y_idx, record.pol_y) ||
        !csvutil::TryParseDouble(cols, pol_z_idx, record.pol_z) ||
        !csvutil::TryParseDouble(cols, len_idx, record.step_length_mm) ||
        pre_idx >= cols.size() || post_idx >= cols.size() ||
        cols[pre_idx].empty() || cols[post_idx].empty()) {
      last_error_ = "Invalid row in step_trace.csv at row " + std::to_string(step_row);
      return false;
    }
    if (edep_idx < cols.size()) {
      if (!csvutil::TryParseDouble(cols, edep_idx, record.edep_mev)) {
        last_error_ = "Invalid edep_mev in step_trace.csv at row " + std::to_string(step_row);
        return false;
      }
    } else {
      record.edep_mev = 0.0;
    }
    if (!std::isfinite(record.x_mm) || !std::isfinite(record.y_mm) || !std::isfinite(record.z_mm) ||
        !std::isfinite(record.t_ns) || !std::isfinite(record.px_mev) ||
        !std::isfinite(record.py_mev) || !std::isfinite(record.pz_mev) ||
        !std::isfinite(record.pol_x) || !std::isfinite(record.pol_y) || !std::isfinite(record.pol_z) ||
        !std::isfinite(record.step_length_mm) || !std::isfinite(record.edep_mev) ||
        record.step_length_mm < 0.0) {
      last_error_ = "Non-finite or negative step values in step_trace.csv at row " +
                    std::to_string(step_row);
      return false;
    }
    record.pre_material = cols[pre_idx];
    record.post_material = cols[post_idx];
    if (tracks_.find({record.event_id, record.track_id}) == tracks_.end()) {
      last_error_ = "Missing track metadata in track_summary.csv for event_id=" +
                    std::to_string(record.event_id) + ", track_id=" +
                    std::to_string(record.track_id) +
                    " referenced by step_trace.csv row " + std::to_string(step_row);
      return false;
    }
    steps_.push_back(std::move(record));
  }

  std::sort(steps_.begin(), steps_.end(),
            [](const StepRecord& a, const StepRecord& b) {
              if (a.event_id != b.event_id) {
                return a.event_id < b.event_id;
              }
              if (a.track_id != b.track_id) {
                return a.track_id < b.track_id;
              }
              return a.step_index < b.step_index;
            });

  if (!LoadPhase1Nodes()) {
    return false;
  }
  return true;
}


}  // namespace phase2
