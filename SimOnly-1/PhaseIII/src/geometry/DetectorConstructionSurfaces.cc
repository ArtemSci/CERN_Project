#include "DetectorConstruction.hh"
#include "DetectorConstructionUtils.hh"

#include <algorithm>

#include "G4LogicalBorderSurface.hh"
#include "G4LogicalSkinSurface.hh"
#include "G4LogicalVolume.hh"
#include "G4MaterialPropertiesTable.hh"
#include "G4OpticalSurface.hh"

namespace phase3 {

void DetectorConstruction::ApplySurface(G4LogicalVolume* volume,
                                        const SurfaceConfig& surface,
                                        bool metal) const {
  auto* optical = new G4OpticalSurface(volume->GetName() + "_Surface");
  optical->SetModel(unified);
  optical->SetType(metal ? dielectric_metal : dielectric_dielectric);
  optical->SetFinish(surface.roughness_sigma_alpha > 0.0 ? ground : polished);
  optical->SetSigmaAlpha(surface.roughness_sigma_alpha);

  auto* mpt = new G4MaterialPropertiesTable();
  const double reflectivity = std::max(0.0, std::min(1.0, surface.reflectivity));
  const double specular = std::max(0.0, std::min(1.0, 1.0 - surface.diffuse_fraction));
  auto add_probability = [&](const char* name,
                             const TableConfig& table,
                             double fallback) {
    if (!table.lambda_nm.empty()) {
      detector_utils::AddProperty(mpt, name, table.lambda_nm, table.values, 1.0);
      return;
    }
    mpt->AddConstProperty(name, std::max(0.0, std::min(1.0, fallback)), true);
  };
  add_probability("REFLECTIVITY", surface.reflectivity_table, reflectivity);
  add_probability("SPECULARLOBECONSTANT", surface.specular_lobe_table, specular);
  add_probability("SPECULARSPIKECONSTANT", surface.specular_spike_table, 0.0);
  add_probability("BACKSCATTERCONSTANT", surface.backscatter_table, 0.0);
  if (!surface.transmittance_table.lambda_nm.empty()) {
    detector_utils::AddProperty(mpt,
                                "TRANSMITTANCE",
                                surface.transmittance_table.lambda_nm,
                                surface.transmittance_table.values,
                                1.0);
  }
  if (!surface.efficiency_table.lambda_nm.empty()) {
    detector_utils::AddProperty(mpt,
                                "EFFICIENCY",
                                surface.efficiency_table.lambda_nm,
                                surface.efficiency_table.values,
                                1.0);
  }
  optical->SetMaterialPropertiesTable(mpt);

  new G4LogicalSkinSurface(volume->GetName() + "_Skin", volume, optical);
}

void DetectorConstruction::ApplyWindowSurface(G4LogicalVolume* volume,
                                              const WindowSurfaceConfig& surface) const {
  if (surface.roughness_sigma_alpha <= 0.0) {
    return;
  }
  auto* optical = new G4OpticalSurface(volume->GetName() + "_WindowSurface");
  optical->SetModel(unified);
  optical->SetType(dielectric_dielectric);
  optical->SetFinish(ground);
  optical->SetSigmaAlpha(surface.roughness_sigma_alpha);
  new G4LogicalSkinSurface(volume->GetName() + "_WindowSkin", volume, optical);
}

void DetectorConstruction::ApplyInterfaceSurface(const std::string& name,
                                                  const G4VPhysicalVolume* from,
                                                  const G4VPhysicalVolume* to,
                                                  const SurfaceConfig& surface) const {
  if (!from || !to) {
    return;
  }
  auto* optical = new G4OpticalSurface(name);
  optical->SetModel(unified);
  optical->SetType(dielectric_dielectric);
  optical->SetFinish(surface.roughness_sigma_alpha > 0.0 ? ground : polished);
  optical->SetSigmaAlpha(surface.roughness_sigma_alpha);

  auto* mpt = new G4MaterialPropertiesTable();
  const double reflectivity = std::max(0.0, std::min(1.0, surface.reflectivity));
  const double specular = std::max(0.0, std::min(1.0, 1.0 - surface.diffuse_fraction));
  auto add_probability = [&](const char* prop,
                             const TableConfig& table,
                             double fallback) {
    if (!table.lambda_nm.empty()) {
      detector_utils::AddProperty(mpt, prop, table.lambda_nm, table.values, 1.0);
      return;
    }
    mpt->AddConstProperty(prop, std::max(0.0, std::min(1.0, fallback)), true);
  };
  add_probability("REFLECTIVITY", surface.reflectivity_table, reflectivity);
  add_probability("SPECULARLOBECONSTANT", surface.specular_lobe_table, specular);
  add_probability("SPECULARSPIKECONSTANT", surface.specular_spike_table, 0.0);
  add_probability("BACKSCATTERCONSTANT", surface.backscatter_table, 0.0);
  if (!surface.transmittance_table.lambda_nm.empty()) {
    detector_utils::AddProperty(mpt,
                                "TRANSMITTANCE",
                                surface.transmittance_table.lambda_nm,
                                surface.transmittance_table.values,
                                1.0);
  }
  if (!surface.efficiency_table.lambda_nm.empty()) {
    detector_utils::AddProperty(mpt,
                                "EFFICIENCY",
                                surface.efficiency_table.lambda_nm,
                                surface.efficiency_table.values,
                                1.0);
  }
  optical->SetMaterialPropertiesTable(mpt);

  new G4LogicalBorderSurface(name + "_Forward",
                             const_cast<G4VPhysicalVolume*>(from),
                             const_cast<G4VPhysicalVolume*>(to),
                             optical);
  new G4LogicalBorderSurface(name + "_Backward",
                             const_cast<G4VPhysicalVolume*>(to),
                             const_cast<G4VPhysicalVolume*>(from),
                             optical);
}


}  // namespace phase3
