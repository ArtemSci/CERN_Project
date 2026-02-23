#include "DetectorConstruction.hh"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "DetectorConstructionUtils.hh"
#include "DetectorSD.hh"

#include "G4AffineTransform.hh"
#include "G4Box.hh"
#include "G4Event.hh"
#include "G4EventManager.hh"
#include "G4LogicalVolume.hh"
#include "G4NistManager.hh"
#include "G4OpticalPhoton.hh"
#include "G4PVPlacement.hh"
#include "G4PhysicalConstants.hh"
#include "G4RotationMatrix.hh"
#include "G4SDManager.hh"
#include "G4Step.hh"
#include "G4StepPoint.hh"
#include "G4SubtractionSolid.hh"
#include "G4SystemOfUnits.hh"
#include "G4TessellatedSolid.hh"
#include "G4Track.hh"
#include "G4TriangularFacet.hh"
#include "G4Tubs.hh"
#include "G4VPhysicalVolume.hh"
#include "G4VSolid.hh"

namespace phase3 {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::string Normalize(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

double Clamp01(double value) {
  return std::max(0.0, std::min(1.0, value));
}

G4VSolid* LoadMeshFromStl(const std::string& solid_name, const std::string& mesh_path) {
  std::ifstream input(mesh_path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open STL mesh: " + mesh_path);
  }

  std::vector<char> bytes((std::istreambuf_iterator<char>(input)),
                          std::istreambuf_iterator<char>());
  if (bytes.size() < 84u) {
    throw std::runtime_error("STL mesh is too small: " + mesh_path);
  }

  auto read_u32 = [&](size_t off) -> uint32_t {
    if (off + 4u > bytes.size()) {
      throw std::runtime_error("Unexpected EOF while parsing STL mesh: " + mesh_path);
    }
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + off, 4u);
    return value;
  };
  auto read_f32 = [&](size_t off) -> double {
    if (off + 4u > bytes.size()) {
      throw std::runtime_error("Unexpected EOF while parsing STL mesh: " + mesh_path);
    }
    float value = 0.0f;
    std::memcpy(&value, bytes.data() + off, 4u);
    if (!std::isfinite(value)) {
      throw std::runtime_error("Non-finite STL coordinate encountered in: " + mesh_path);
    }
    return static_cast<double>(value);
  };

  const uint32_t tri_count = read_u32(80u);
  const uint64_t expected_binary_size = 84ull + (50ull * static_cast<uint64_t>(tri_count));
  const bool looks_binary = (expected_binary_size == static_cast<uint64_t>(bytes.size()));

  auto* solid = new G4TessellatedSolid(solid_name);
  uint64_t facets = 0;

  if (looks_binary) {
    size_t off = 84u;
    for (uint32_t i = 0; i < tri_count; ++i) {
      off += 12u;  // normal
      const double x1 = read_f32(off + 0u);
      const double y1 = read_f32(off + 4u);
      const double z1 = read_f32(off + 8u);
      off += 12u;
      const double x2 = read_f32(off + 0u);
      const double y2 = read_f32(off + 4u);
      const double z2 = read_f32(off + 8u);
      off += 12u;
      const double x3 = read_f32(off + 0u);
      const double y3 = read_f32(off + 4u);
      const double z3 = read_f32(off + 8u);
      off += 12u;
      off += 2u;  // attribute byte count

      solid->AddFacet(new G4TriangularFacet(
          G4ThreeVector(x1 * mm, y1 * mm, z1 * mm),
          G4ThreeVector(x2 * mm, y2 * mm, z2 * mm),
          G4ThreeVector(x3 * mm, y3 * mm, z3 * mm),
          ABSOLUTE));
      ++facets;
    }
  } else {
    std::ifstream ascii(mesh_path);
    if (!ascii) {
      throw std::runtime_error("Failed to reopen STL mesh in text mode: " + mesh_path);
    }

    std::vector<G4ThreeVector> tri;
    tri.reserve(3);
    std::string token;
    while (ascii >> token) {
      if (Normalize(token) != "vertex") {
        continue;
      }
      double x = 0.0;
      double y = 0.0;
      double z = 0.0;
      if (!(ascii >> x >> y >> z)) {
        throw std::runtime_error("Malformed ASCII STL vertex triplet in: " + mesh_path);
      }
      tri.emplace_back(x * mm, y * mm, z * mm);
      if (tri.size() == 3u) {
        solid->AddFacet(new G4TriangularFacet(tri[0], tri[1], tri[2], ABSOLUTE));
        tri.clear();
        ++facets;
      }
    }
  }

  if (facets == 0u) {
    throw std::runtime_error("STL mesh contains no valid triangular facets: " + mesh_path);
  }
  solid->SetSolidClosed(true);
  return solid;
}

int ComputeDetectorChannel(const DetectorConfig& detector, const G4ThreeVector& local_pos_mm) {
  if (detector.channel_mode == "single") {
    return 0;
  }

  if (detector.channel_mode == "grid_z") {
    const int nz = std::max(1, detector.channels_z);
    const double z_norm = (local_pos_mm.z() + detector.half_z_mm) / (2.0 * detector.half_z_mm);
    if (z_norm < 0.0 || z_norm > 1.0) {
      return -1;
    }
    const double z_clamped = std::min(1.0 - std::numeric_limits<double>::epsilon(), Clamp01(z_norm));
    int iz = static_cast<int>(std::floor(z_clamped * nz));
    iz = std::max(0, std::min(iz, nz - 1));
    return iz;
  }

  if (detector.channel_mode != "grid_xy") {
    return -1;
  }

  const int nx = std::max(1, detector.channels_x);
  const int ny = std::max(1, detector.channels_y);
  double u = 0.0;
  double v = 0.0;

  if (detector.shape == "box") {
    u = (local_pos_mm.x() + detector.half_x_mm) / (2.0 * detector.half_x_mm);
    v = (local_pos_mm.y() + detector.half_y_mm) / (2.0 * detector.half_y_mm);
  } else if (detector.shape == "cylinder") {
    double phi = std::atan2(local_pos_mm.y(), local_pos_mm.x());
    if (phi < 0.0) {
      phi += 2.0 * kPi;
    }
    u = phi / (2.0 * kPi);
    v = (local_pos_mm.z() + detector.half_z_mm) / (2.0 * detector.half_z_mm);
  } else {
    return -1;
  }

  if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0) {
    return -1;
  }
  const double uu = std::min(1.0 - std::numeric_limits<double>::epsilon(), Clamp01(u));
  const double vv = std::min(1.0 - std::numeric_limits<double>::epsilon(), Clamp01(v));
  int ix = static_cast<int>(std::floor(uu * nx));
  int iy = static_cast<int>(std::floor(vv * ny));
  ix = std::max(0, std::min(ix, nx - 1));
  iy = std::max(0, std::min(iy, ny - 1));
  return iy * nx + ix;
}

}  // namespace

G4VPhysicalVolume* DetectorConstruction::Construct() {
  const GeometryInfo geom = LoadGeometry();
  const bool use_box = (geom.aperture_shape == "box");

  detectors_.clear();
  detector_index_.clear();
  wall_volume_ = nullptr;

  const double total_thickness_mm =
      std::accumulate(geom.layers.begin(), geom.layers.end(), 0.0,
                      [](double acc, const LayerInfo& layer) { return acc + layer.thickness_mm; });
  const double z_start_mm = geom.stack_center_z_mm - 0.5 * total_thickness_mm;

  bounds_.aperture_shape = geom.aperture_shape;
  bounds_.half_x_mm = geom.aperture_half_x_mm;
  bounds_.half_y_mm = geom.aperture_half_y_mm;
  bounds_.radius_mm = geom.aperture_radius_mm;
  bounds_.z_min_mm = z_start_mm;
  bounds_.z_max_mm = z_start_mm + total_thickness_mm;
  bounds_.wall_enabled = geom.wall_enabled;
  bounds_.wall_absorb = geom.wall_absorb;

  auto* world_material = ResolveMaterial("G4_Galactic");
  auto* world_solid = new G4Tubs("WorldSolid",
                                 0.0,
                                 geom.world_radius_mm * mm,
                                 geom.world_half_z_mm * mm,
                                 0.0,
                                 360.0 * deg);
  auto* world_log = new G4LogicalVolume(world_solid, world_material, "WorldLogical");
  auto* world_phys = new G4PVPlacement(nullptr,
                                       G4ThreeVector(),
                                       world_log,
                                       "World",
                                       nullptr,
                                       false,
                                       0,
                                       false);

  struct LayerBoundary {
    std::string layer_name;
    std::string material_name;
    G4VPhysicalVolume* first = nullptr;
    G4VPhysicalVolume* last = nullptr;
  };
  std::vector<LayerBoundary> boundaries;
  boundaries.reserve(geom.layers.size());
  std::vector<LayerPlacement> layer_placements;
  layer_placements.reserve(geom.layers.size() * 4);

  double z_cursor_mm = z_start_mm;
  for (size_t i = 0; i < geom.layers.size(); ++i) {
    const auto& layer = geom.layers[i];
    if (!(layer.thickness_mm > 0.0)) {
      throw std::runtime_error("Layer '" + layer.name + "' has invalid non-positive thickness.");
    }

    const GradientConfig* gradient = FindGradient(layer.name);
    const int slices = gradient ? std::max(1, gradient->slices) : 1;
    if (gradient && layer.bowing.enabled && slices > 1) {
      throw std::runtime_error(
          "Layer '" + layer.name +
          "' cannot combine bowing with material gradient slices > 1.");
    }

    LayerBoundary boundary;
    boundary.layer_name = layer.name;
    boundary.material_name = layer.material;

    const double slice_thickness_mm = layer.thickness_mm / static_cast<double>(slices);
    for (int s = 0; s < slices; ++s) {
      const std::string slice_name = (slices == 1)
                                         ? ("Layer_" + std::to_string(i) + "_" + layer.name)
                                         : ("Layer_" + std::to_string(i) + "_" + layer.name +
                                            "_Slice_" + std::to_string(s));
      const double z_center_mm = z_cursor_mm + 0.5 * slice_thickness_mm;
      const double slice_offset_mm =
          -0.5 * layer.thickness_mm + (static_cast<double>(s) + 0.5) * slice_thickness_mm;

      G4Material* material = nullptr;
      if (gradient) {
        const std::string grad_name = layer.material + "_grad_" + layer.name + "_" + std::to_string(s);
        material = ResolveGradientMaterial(layer.material,
                                           *gradient,
                                           layer.thickness_mm,
                                           slice_offset_mm,
                                           grad_name);
      } else {
        material = ResolveMaterial(layer.material);
      }

      G4VSolid* layer_solid = nullptr;
      if (layer.is_window && layer.bowing.enabled) {
        const double radius_mm = (layer.bowing.radius_mm > 0.0)
                                     ? layer.bowing.radius_mm
                                     : (use_box ? std::min(geom.aperture_half_x_mm, geom.aperture_half_y_mm)
                                                : geom.aperture_radius_mm);
        double w0_mm = layer.bowing.override_max_deflection_mm;
        if (!(w0_mm > 0.0)) {
          w0_mm = detector_utils::ComputeMaxDeflectionMm(layer.bowing.pressure_pa,
                                                         radius_mm,
                                                         layer.thickness_mm,
                                                         layer.bowing.youngs_modulus_pa,
                                                         layer.bowing.poisson);
        }
        if (use_box) {
          layer_solid = detector_utils::BuildBowedWindowSolidBox(slice_name,
                                                                 geom.aperture_half_x_mm,
                                                                 geom.aperture_half_y_mm,
                                                                 slice_thickness_mm,
                                                                 w0_mm,
                                                                 layer.bowing.radial_samples);
        } else {
          layer_solid = detector_utils::BuildBowedWindowSolidCylinder(slice_name,
                                                                      radius_mm,
                                                                      slice_thickness_mm,
                                                                      w0_mm,
                                                                      layer.bowing.radial_samples,
                                                                      layer.bowing.phi_samples);
        }
      } else {
        if (use_box) {
          layer_solid = new G4Box(slice_name + "_Solid",
                                  geom.aperture_half_x_mm * mm,
                                  geom.aperture_half_y_mm * mm,
                                  0.5 * slice_thickness_mm * mm);
        } else {
          layer_solid = new G4Tubs(slice_name + "_Solid",
                                   0.0,
                                   geom.aperture_radius_mm * mm,
                                   0.5 * slice_thickness_mm * mm,
                                   0.0,
                                   360.0 * deg);
        }
      }

      auto* logical = new G4LogicalVolume(layer_solid, material, slice_name + "_Logical");
      if (layer.is_window) {
        ApplyWindowSurface(logical, config_.window_surface);
      }

      auto* phys = new G4PVPlacement(nullptr,
                                     G4ThreeVector(0.0, 0.0, z_center_mm * mm),
                                     logical,
                                     slice_name,
                                     world_log,
                                     false,
                                     static_cast<int>(s),
                                     false);
      if (!boundary.first) {
        boundary.first = phys;
      }
      boundary.last = phys;
      LayerPlacement placement;
      placement.layer_name = layer.name;
      placement.material_name = layer.material;
      placement.volume = phys;
      placement.z_min_mm = z_center_mm - 0.5 * slice_thickness_mm;
      placement.z_max_mm = z_center_mm + 0.5 * slice_thickness_mm;
      layer_placements.push_back(placement);
      z_cursor_mm += slice_thickness_mm;
    }
    boundaries.push_back(boundary);
  }

  for (size_t i = 0; i + 1 < boundaries.size(); ++i) {
    const auto& from = boundaries[i];
    const auto& to = boundaries[i + 1];
    for (const auto& iface : config_.interfaces) {
      if (!iface.enabled) {
        continue;
      }
      bool match = true;
      if (!iface.from_layer.empty() && iface.from_layer != from.layer_name) {
        match = false;
      }
      if (!iface.to_layer.empty() && iface.to_layer != to.layer_name) {
        match = false;
      }
      if (!iface.material_a.empty() && iface.material_a != from.material_name) {
        match = false;
      }
      if (!iface.material_b.empty() && iface.material_b != to.material_name) {
        match = false;
      }
      if (!match) {
        continue;
      }
      ApplyInterfaceSurface("Interface_" + std::to_string(i) + "_" + std::to_string(i + 1),
                            from.last,
                            to.first,
                            iface.surface);
      break;
    }
  }

  if (geom.wall_enabled && geom.wall_thickness_mm > 0.0) {
    G4VSolid* wall_solid = nullptr;
    if (use_box) {
      auto* outer = new G4Box("WallOuterSolid",
                              (geom.aperture_half_x_mm + geom.wall_thickness_mm) * mm,
                              (geom.aperture_half_y_mm + geom.wall_thickness_mm) * mm,
                              geom.world_half_z_mm * mm);
      auto* inner = new G4Box("WallInnerSolid",
                              geom.aperture_half_x_mm * mm,
                              geom.aperture_half_y_mm * mm,
                              geom.world_half_z_mm * mm);
      wall_solid = new G4SubtractionSolid("WallSolid", outer, inner);
    } else {
      wall_solid = new G4Tubs("WallSolid",
                              geom.aperture_radius_mm * mm,
                              (geom.aperture_radius_mm + geom.wall_thickness_mm) * mm,
                              geom.world_half_z_mm * mm,
                              0.0,
                              360.0 * deg);
    }

    auto* wall_material = ResolveMaterial(geom.wall_material);
    auto* wall_log = new G4LogicalVolume(wall_solid, wall_material, "Wall_Logical");
    ApplySurface(wall_log, config_.wall_surface, true);

    wall_volume_ = new G4PVPlacement(nullptr,
                                     G4ThreeVector(0.0, 0.0, geom.stack_center_z_mm * mm),
                                     wall_log,
                                     "Wall",
                                     world_log,
                                     false,
                                     0,
                                     false);
  }

  for (size_t i = 0; i < config_.detectors.size(); ++i) {
    const auto& detector_cfg = config_.detectors[i];
    auto* solid = BuildDetectorSolid(detector_cfg);
    auto* material = ResolveMaterial(detector_cfg.material);
    auto* logical =
        new G4LogicalVolume(solid, material, "Detector_" + detector_cfg.id + "_Logical");
    ApplySurface(logical, detector_cfg.surface, false);

    auto* rotation = new G4RotationMatrix();
    rotation->rotateX(detector_cfg.rotation_deg[0] * deg);
    rotation->rotateY(detector_cfg.rotation_deg[1] * deg);
    rotation->rotateZ(detector_cfg.rotation_deg[2] * deg);

    G4LogicalVolume* mother_log = world_log;
    G4ThreeVector position(detector_cfg.x_mm * mm,
                           detector_cfg.y_mm * mm,
                           detector_cfg.z_mm * mm);
    auto inside_aperture_xy = [&](double x_mm, double y_mm) {
      if (use_box) {
        return std::abs(x_mm) <= geom.aperture_half_x_mm && std::abs(y_mm) <= geom.aperture_half_y_mm;
      }
      return (x_mm * x_mm + y_mm * y_mm) <= (geom.aperture_radius_mm * geom.aperture_radius_mm);
    };
    if (inside_aperture_xy(detector_cfg.x_mm, detector_cfg.y_mm)) {
      for (const auto& layer : layer_placements) {
        if (detector_cfg.z_mm < layer.z_min_mm || detector_cfg.z_mm > layer.z_max_mm) {
          continue;
        }
        const double layer_center_mm = 0.5 * (layer.z_min_mm + layer.z_max_mm);
        mother_log = layer.volume ? layer.volume->GetLogicalVolume() : world_log;
        position = G4ThreeVector(detector_cfg.x_mm * mm,
                                 detector_cfg.y_mm * mm,
                                 (detector_cfg.z_mm - layer_center_mm) * mm);
        break;
      }
    }

    auto* volume = new G4PVPlacement(rotation,
                                     position,
                                     logical,
                                     "Detector_" + detector_cfg.id,
                                     mother_log,
                                     false,
                                     static_cast<int>(i),
                                     false);

    DetectorPlacement placement;
    placement.config = detector_cfg;
    placement.logical = logical;
    placement.volume = volume;
    detector_index_[volume] = static_cast<int>(detectors_.size());
    detectors_.push_back(placement);
  }

  return world_phys;
}

void DetectorConstruction::ConstructSDandField() {
  if (detectors_.empty()) {
    return;
  }
  if (!output_) {
    throw std::runtime_error(
        "PhaseIII output is not attached to DetectorConstruction before initialization.");
  }

  auto* sd = new DetectorSD(this, output_);
  auto* sd_manager = G4SDManager::GetSDMpointer();
  sd_manager->AddNewDetector(sd);
  for (auto& detector : detectors_) {
    if (detector.logical) {
      SetSensitiveDetector(detector.logical, sd);
    }
  }
}

void DetectorConstruction::SetOutput(PhaseIIIOutput* output) {
  output_ = output;
}

bool DetectorConstruction::IsDetectorVolume(const G4VPhysicalVolume* volume) const {
  if (!volume) {
    return false;
  }
  return detector_index_.find(volume) != detector_index_.end();
}

bool DetectorConstruction::MakeDetectorHit(const G4Step* step, HitRecord* out) const {
  if (!step || !out) {
    return false;
  }
  auto* track = step->GetTrack();
  if (!track || track->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition()) {
    return false;
  }
  const auto* post = step->GetPostStepPoint();
  if (!post) {
    return false;
  }
  const auto* volume = post->GetPhysicalVolume();
  if (!volume) {
    return false;
  }
  auto it = detector_index_.find(volume);
  if (it == detector_index_.end()) {
    return false;
  }
  const DetectorPlacement& detector = detectors_.at(static_cast<size_t>(it->second));

  HitRecord hit;
  const auto* event = G4EventManager::GetEventManager()->GetConstCurrentEvent();
  hit.event_id = event ? event->GetEventID() : 0;
  hit.track_id = track->GetTrackID();
  hit.detector_id = detector.config.id;

  const G4ThreeVector global_pos = post->GetPosition();
  hit.x_mm = global_pos.x() / mm;
  hit.y_mm = global_pos.y() / mm;
  hit.z_mm = global_pos.z() / mm;
  hit.t_ns = post->GetGlobalTime() / ns;
  hit.lambda_nm = (h_Planck * c_light) / (track->GetTotalEnergy()) / nm;

  const auto touchable = post->GetTouchableHandle();
  if (!touchable) {
    return false;
  }
  const G4AffineTransform& global_to_local = touchable->GetHistory()->GetTopTransform();
  const G4ThreeVector local_pos = global_to_local.TransformPoint(global_pos) / mm;
  const int channel = ComputeDetectorChannel(detector.config, local_pos);
  if (channel < 0) {
    return false;
  }
  hit.channel_id = channel;

  G4ThreeVector normal_local(0.0, 0.0, -1.0);
  if (detector.logical && detector.logical->GetSolid()) {
    normal_local = detector.logical->GetSolid()->SurfaceNormal(local_pos * mm);
  }
  if (normal_local.mag2() <= 0.0) {
    normal_local = G4ThreeVector(0.0, 0.0, -1.0);
  } else {
    normal_local = normal_local.unit();
  }
  G4ThreeVector normal_global = global_to_local.Inverse().TransformAxis(normal_local);
  if (normal_global.mag2() <= 0.0) {
    normal_global = G4ThreeVector(0.0, 0.0, -1.0);
  } else {
    normal_global = normal_global.unit();
  }
  G4ThreeVector dir = track->GetMomentumDirection();
  const double cos_inc = std::max(-1.0, std::min(1.0, -dir.dot(normal_global)));
  hit.incidence_angle_deg = std::acos(cos_inc) * (180.0 / kPi);
  hit.dir_x = dir.x();
  hit.dir_y = dir.y();
  hit.dir_z = dir.z();
  const G4ThreeVector pol = track->GetPolarization();
  hit.pol_x = pol.x();
  hit.pol_y = pol.y();
  hit.pol_z = pol.z();

  *out = hit;
  return true;
}

const G4VPhysicalVolume* DetectorConstruction::WallVolume() const {
  return wall_volume_;
}

const DetectorBounds& DetectorConstruction::Bounds() const {
  return bounds_;
}

G4VSolid* DetectorConstruction::BuildDetectorSolid(const DetectorConfig& detector) const {
  if (detector.shape == "box") {
    return new G4Box("DetectorSolid_" + detector.id,
                     detector.half_x_mm * mm,
                     detector.half_y_mm * mm,
                     detector.half_z_mm * mm);
  }
  if (detector.shape == "cylinder") {
    return new G4Tubs("DetectorSolid_" + detector.id,
                      0.0,
                      detector.radius_mm * mm,
                      detector.half_z_mm * mm,
                      0.0,
                      360.0 * deg);
  }
  if (detector.shape == "mesh") {
    const std::filesystem::path mesh_path = std::filesystem::path(detector.mesh_path);
    return LoadMeshFromStl("DetectorSolid_" + detector.id, mesh_path.string());
  }
  throw std::runtime_error("Unsupported detector shape '" + detector.shape +
                           "' for detector '" + detector.id + "'.");
}

}  // namespace phase3
