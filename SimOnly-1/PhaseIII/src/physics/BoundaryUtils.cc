#include "BoundaryUtils.hh"

namespace phase3 {

std::string BoundaryStatusName(G4OpBoundaryProcessStatus status) {
  switch (status) {
    case Undefined:
      return "Undefined";
    case Transmission:
      return "Transmission";
    case FresnelRefraction:
      return "FresnelRefraction";
    case FresnelReflection:
      return "FresnelReflection";
    case TotalInternalReflection:
      return "TotalInternalReflection";
    case LambertianReflection:
      return "LambertianReflection";
    case LobeReflection:
      return "LobeReflection";
    case SpikeReflection:
      return "SpikeReflection";
    case BackScattering:
      return "BackScattering";
    case Absorption:
      return "Absorption";
    case Detection:
      return "Detection";
    case NotAtBoundary:
      return "NotAtBoundary";
    case SameMaterial:
      return "SameMaterial";
    case StepTooSmall:
      return "StepTooSmall";
    case NoRINDEX:
      return "NoRINDEX";
    default:
      return "Unknown";
  }
}

}  // namespace phase3
