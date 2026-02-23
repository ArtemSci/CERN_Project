#include "DetectorConstruction.hh"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "G4Box.hh"
#include "G4LogicalVolume.hh"
#include "G4Material.hh"
#include "G4MaterialPropertiesTable.hh"
#include "G4NistManager.hh"
#include "G4PVPlacement.hh"
#include "G4PhysicalConstants.hh"
#include "G4SubtractionSolid.hh"
#include "G4SystemOfUnits.hh"
#include "G4TessellatedSolid.hh"
#include "G4TriangularFacet.hh"
#include "G4Tubs.hh"
#include "G4UserLimits.hh"
#include "G4VSolid.hh"

namespace phase1 {

namespace {

G4Material* CloneMaterial(const std::string& name, const G4Material* base) {
  if (!base) {
    return nullptr;
  }
  auto* cloned = new G4Material(name,
                                base->GetDensity(),
                                base->GetNumberOfElements(),
                                base->GetState(),
                                base->GetTemperature(),
                                base->GetPressure());
  for (size_t i = 0; i < base->GetNumberOfElements(); ++i) {
    cloned->AddElement(const_cast<G4Element*>(base->GetElement(i)), base->GetFractionVector()[i]);
  }
  return cloned;
}

void SetConstantRIndex(G4Material* material, double n0) {
  if (!material) {
    return;
  }
  if (!std::isfinite(n0) || n0 < 1.0) {
    throw std::runtime_error("Optical refractive index must be finite and >= 1.0.");
  }
  auto* mpt = material->GetMaterialPropertiesTable();
  if (!mpt) {
    mpt = new G4MaterialPropertiesTable();
    material->SetMaterialPropertiesTable(mpt);
  }
  constexpr int kPoints = 2;
  G4double photon_energy[kPoints] = {
      (h_Planck * c_light) / (700.0 * nm),
      (h_Planck * c_light) / (200.0 * nm)};
  G4double refractive_index[kPoints] = {n0, n0};
  mpt->AddProperty("RINDEX", photon_energy, refractive_index, kPoints);
}

double ComputeW0Mm(const LayerConfig& layer, double fallback_radius_mm) {
  if (!layer.bowing.enabled) {
    return 0.0;
  }
  if (layer.bowing.override_max_deflection_mm > 0.0) {
    return layer.bowing.override_max_deflection_mm;
  }
  const double radius_mm =
      (layer.bowing.radius_mm > 0.0) ? layer.bowing.radius_mm : fallback_radius_mm;
  return SurfaceRegistry::ComputeMaxDeflectionMm(layer.bowing.pressure_pa,
                                                 radius_mm,
                                                 layer.thickness_mm,
                                                 layer.bowing.youngs_modulus_pa,
                                                 layer.bowing.poisson);
}

BowedSurfaceField BuildBowedField(double radius_mm, double w0_mm, int radial_samples) {
  BowedSurfaceField field;
  const int samples = std::max(4, radial_samples);
  field.nx = 2 * samples + 1;
  field.ny = 2 * samples + 1;
  field.xmin = -radius_mm;
  field.xmax = radius_mm;
  field.ymin = -radius_mm;
  field.ymax = radius_mm;
  field.dx = (field.xmax - field.xmin) / (field.nx - 1);
  field.dy = (field.ymax - field.ymin) / (field.ny - 1);
  field.w_mm.assign(field.nx * field.ny, 0.0);

  for (int iy = 0; iy < field.ny; ++iy) {
    const double y = field.ymin + iy * field.dy;
    for (int ix = 0; ix < field.nx; ++ix) {
      const double x = field.xmin + ix * field.dx;
      const double r = std::sqrt(x * x + y * y);
      double w = 0.0;
      if (r <= radius_mm) {
        const double u = r / radius_mm;
        const double term = 1.0 - u * u;
        w = w0_mm * term * term;
      }
      field.w_mm[field.Index(ix, iy)] = w;
    }
  }
  return field;
}

G4TessellatedSolid* BuildBowedWindowSolid(const std::string& name,
                                          double radius_mm,
                                          double thickness_mm,
                                          double w0_mm,
                                          int radial_samples,
                                          int phi_samples) {
  constexpr double kPi = 3.141592653589793;
  const int nr = std::max(4, radial_samples);
  const int nphi = std::max(12, phi_samples);
  const double half_thickness = thickness_mm / 2.0;
  const auto w_at_r = [radius_mm, w0_mm](double r_mm) {
    if (radius_mm <= 0.0 || w0_mm == 0.0) {
      return 0.0;
    }
    const double u = std::min(std::abs(r_mm), radius_mm) / radius_mm;
    const double term = 1.0 - u * u;
    return w0_mm * term * term;
  };

  std::vector<G4ThreeVector> top_vertices;
  std::vector<G4ThreeVector> bottom_vertices;
  top_vertices.reserve(1 + nr * nphi);
  bottom_vertices.reserve(1 + nr * nphi);

  top_vertices.emplace_back(0.0, 0.0, (half_thickness + w_at_r(0.0)) * mm);
  bottom_vertices.emplace_back(0.0, 0.0, (-half_thickness + w_at_r(0.0)) * mm);

  for (int ir = 1; ir <= nr; ++ir) {
    const double r = radius_mm * ir / nr;
    const double w = w_at_r(r);
    for (int ip = 0; ip < nphi; ++ip) {
      const double phi = (2.0 * kPi * ip) / nphi;
      const double x = r * std::cos(phi);
      const double y = r * std::sin(phi);
      top_vertices.emplace_back(x * mm, y * mm, (half_thickness + w) * mm);
      bottom_vertices.emplace_back(x * mm, y * mm, (-half_thickness + w) * mm);
    }
  }

  const auto idx = [nphi](int ir, int ip) {
    if (ir == 0) {
      return 0;
    }
    return 1 + (ir - 1) * nphi + (ip % nphi);
  };

  auto* solid = new G4TessellatedSolid(name + "_Solid");

  for (int ip = 0; ip < nphi; ++ip) {
    const int ip_next = (ip + 1) % nphi;
    const auto& v0 = top_vertices[0];
    const auto& v1 = top_vertices[idx(1, ip)];
    const auto& v2 = top_vertices[idx(1, ip_next)];
    solid->AddFacet(new G4TriangularFacet(v0, v1, v2, ABSOLUTE));

    const auto& b0 = bottom_vertices[0];
    const auto& b1 = bottom_vertices[idx(1, ip)];
    const auto& b2 = bottom_vertices[idx(1, ip_next)];
    solid->AddFacet(new G4TriangularFacet(b0, b2, b1, ABSOLUTE));
  }

  for (int ir = 1; ir < nr; ++ir) {
    for (int ip = 0; ip < nphi; ++ip) {
      const int ip_next = (ip + 1) % nphi;
      const auto& t00 = top_vertices[idx(ir, ip)];
      const auto& t10 = top_vertices[idx(ir + 1, ip)];
      const auto& t11 = top_vertices[idx(ir + 1, ip_next)];
      const auto& t01 = top_vertices[idx(ir, ip_next)];
      solid->AddFacet(new G4TriangularFacet(t00, t10, t11, ABSOLUTE));
      solid->AddFacet(new G4TriangularFacet(t00, t11, t01, ABSOLUTE));

      const auto& b00 = bottom_vertices[idx(ir, ip)];
      const auto& b10 = bottom_vertices[idx(ir + 1, ip)];
      const auto& b11 = bottom_vertices[idx(ir + 1, ip_next)];
      const auto& b01 = bottom_vertices[idx(ir, ip_next)];
      solid->AddFacet(new G4TriangularFacet(b00, b11, b10, ABSOLUTE));
      solid->AddFacet(new G4TriangularFacet(b00, b01, b11, ABSOLUTE));
    }
  }

  const int ir = nr;
  for (int ip = 0; ip < nphi; ++ip) {
    const int ip_next = (ip + 1) % nphi;
    const auto& t0 = top_vertices[idx(ir, ip)];
    const auto& t1 = top_vertices[idx(ir, ip_next)];
    const auto& b0 = bottom_vertices[idx(ir, ip)];
    const auto& b1 = bottom_vertices[idx(ir, ip_next)];
    solid->AddFacet(new G4TriangularFacet(t0, b0, b1, ABSOLUTE));
    solid->AddFacet(new G4TriangularFacet(t0, b1, t1, ABSOLUTE));
  }

  solid->SetSolidClosed(true);
  return solid;
}

}  // namespace

DetectorConstruction::DetectorConstruction(const PhaseIConfig& config) : config_(config) {}

G4VPhysicalVolume* DetectorConstruction::Construct() {
  const auto& geom = config_.geometry;
  G4NistManager* nist = G4NistManager::Instance();

  const bool use_box = (geom.aperture_shape == GeometryConfig::ApertureShape::Box);
  const double world_radius = geom.world_radius_mm * mm;
  const double world_half_z = geom.world_half_z_mm * mm;
  auto* world_solid = new G4Tubs("WorldSolid", 0.0, world_radius, world_half_z, 0.0, 360.0 * deg);
  auto* world_base = nist->FindOrBuildMaterial("G4_Galactic");
  if (!world_base) {
    throw std::runtime_error("Failed to build world material G4_Galactic.");
  }
  auto* world_mat = CloneMaterial("World_Optical", world_base);
  if (!world_mat) {
    throw std::runtime_error("Failed to clone world material for optical properties.");
  }
  SetConstantRIndex(world_mat, 1.0);
  auto* world_log = new G4LogicalVolume(world_solid, world_mat, "WorldLogical");
  auto* world_phys = new G4PVPlacement(nullptr, G4ThreeVector(), world_log, "World", nullptr, false, 0);

  const double total_thickness = std::accumulate(
      geom.layers.begin(), geom.layers.end(), 0.0,
      [](double sum, const LayerConfig& layer) { return sum + layer.thickness_mm; });

  const double z_start = geom.stack_center_z_mm - total_thickness / 2.0;
  double z_cursor = z_start;
  layer_names_.clear();

  for (size_t i = 0; i < geom.layers.size(); ++i) {
    const auto& layer = geom.layers[i];
    const std::string layer_name = "Layer_" + std::to_string(i) + "_" + layer.name;
    layer_names_.push_back(layer_name);

    G4Material* material = nullptr;
    if (layer.custom_material) {
      material = new G4Material(layer.name + "_Custom",
                                layer.density_g_cm3 * g / cm3,
                                static_cast<int>(layer.elements.size()));
      for (const auto& element : layer.elements) {
        auto* g4_element = nist->FindOrBuildElement(element.symbol);
        if (!g4_element) {
          throw std::runtime_error("Unknown element symbol: " + element.symbol);
        }
        material->AddElement(g4_element, element.fraction);
      }
    } else {
      material = nist->FindOrBuildMaterial(layer.material);
      if (!material) {
        throw std::runtime_error("Unknown Geant material for layer '" + layer.name + "': " + layer.material);
      }
      material = CloneMaterial(layer_name + "_Material", material);
      if (!material) {
        throw std::runtime_error("Failed to clone Geant material for layer '" + layer.name + "'.");
      }
    }
    SetConstantRIndex(material, layer.refractive_index);

    G4VSolid* solid = nullptr;
    if (layer.is_window && layer.bowing.enabled) {
      if (use_box) {
        throw std::runtime_error("Bowed windows are only supported for cylindrical geometry.");
      }
      const double radius = (layer.bowing.radius_mm > 0.0) ? layer.bowing.radius_mm : geom.aperture_radius_mm;
      const double w0_mm = ComputeW0Mm(layer, geom.aperture_radius_mm);
      solid = BuildBowedWindowSolid(layer_name,
                                    radius,
                                    layer.thickness_mm,
                                    w0_mm,
                                    layer.bowing.radial_samples,
                                    layer.bowing.phi_samples);
    } else {
      const double half_thickness = (layer.thickness_mm * mm) / 2.0;
      if (use_box) {
        solid = new G4Box(layer_name + "_Solid",
                          geom.aperture_half_x_mm * mm,
                          geom.aperture_half_y_mm * mm,
                          half_thickness);
      } else {
        solid = new G4Tubs(layer_name + "_Solid",
                           0.0,
                           geom.aperture_radius_mm * mm,
                           half_thickness,
                           0.0,
                           360.0 * deg);
      }
    }
    auto* logical = new G4LogicalVolume(solid, material, layer_name + "_Logical");
    if (config_.tracking.max_step_mm > 0.0) {
      logical->SetUserLimits(new G4UserLimits(config_.tracking.max_step_mm * mm));
    }

    const double z_center = z_cursor + layer.thickness_mm / 2.0;
    new G4PVPlacement(nullptr,
                      G4ThreeVector(0.0, 0.0, z_center * mm),
                      logical,
                      layer_name,
                      world_log,
                      false,
                      static_cast<int>(i));

    z_cursor += layer.thickness_mm;

    if (i + 1 < geom.layers.size()) {
      SurfaceInfo surface;
      surface.id = static_cast<int>(surfaces_.Surfaces().size());
      surface.pre_layer = static_cast<int>(i);
      surface.post_layer = static_cast<int>(i + 1);
      surface.name = layer_name + "_to_" + "Layer_" + std::to_string(i + 1);
      surface.z_plane_mm = z_cursor;
      if (use_box) {
        surface.radius_mm = std::min(geom.aperture_half_x_mm, geom.aperture_half_y_mm);
      } else {
        surface.radius_mm = geom.aperture_radius_mm;
      }

      const LayerConfig* window_layer = nullptr;
      if (geom.layers[i].is_window && geom.layers[i].bowing.enabled) {
        window_layer = &geom.layers[i];
      } else if (geom.layers[i + 1].is_window && geom.layers[i + 1].bowing.enabled) {
        window_layer = &geom.layers[i + 1];
      }

      if (window_layer) {
        const double radius = (window_layer->bowing.radius_mm > 0.0)
                                  ? window_layer->bowing.radius_mm
                                  : geom.aperture_radius_mm;
        const double w0_mm = ComputeW0Mm(*window_layer, geom.aperture_radius_mm);
        if (w0_mm > 0.0) {
          surface.bowed = true;
          surface.radius_mm = radius;
          surface.w0_mm = w0_mm;
          surfaces_.AddBowedField(surface.id,
                                  BuildBowedField(radius,
                                                  w0_mm,
                                                  window_layer->bowing.radial_samples));
        }
      }

      surfaces_.AddSurface(surface);
    }
  }

  if (geom.wall_enabled) {
    const double wall_thickness_mm = geom.wall_thickness_mm;
    const double half_z_mm = std::max(1.0, geom.world_half_z_mm);
    G4VSolid* wall_solid = nullptr;
    if (use_box) {
      auto* outer = new G4Box("WallOuterSolid",
                              (geom.aperture_half_x_mm + wall_thickness_mm) * mm,
                              (geom.aperture_half_y_mm + wall_thickness_mm) * mm,
                              half_z_mm * mm);
      auto* inner = new G4Box("WallInnerSolid",
                              geom.aperture_half_x_mm * mm,
                              geom.aperture_half_y_mm * mm,
                              half_z_mm * mm);
      wall_solid = new G4SubtractionSolid("WallSolid", outer, inner);
    } else {
      wall_solid = new G4Tubs("WallSolid",
                              geom.aperture_radius_mm * mm,
                              (geom.aperture_radius_mm + wall_thickness_mm) * mm,
                              half_z_mm * mm,
                              0.0,
                              360.0 * deg);
    }

    auto* wall_material = nist->FindOrBuildMaterial(geom.wall_material);
    if (!wall_material) {
      throw std::runtime_error("Unknown Geant wall material: " + geom.wall_material);
    }
    wall_material = CloneMaterial("Wall_Material", wall_material);
    if (!wall_material) {
      throw std::runtime_error("Failed to clone wall material: " + geom.wall_material);
    }
    SetConstantRIndex(wall_material, 1.0);
    auto* wall_log = new G4LogicalVolume(wall_solid, wall_material, "Wall_Logical");
    new G4PVPlacement(nullptr,
                      G4ThreeVector(0.0, 0.0, geom.stack_center_z_mm * mm),
                      wall_log,
                      "Wall",
                      world_log,
                      false,
                      0);
  }

  return world_phys;
}

const SurfaceRegistry& DetectorConstruction::GetSurfaceRegistry() const {
  return surfaces_;
}

}  // namespace phase1
