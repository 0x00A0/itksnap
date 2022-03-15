#ifndef STANDALONEMESHWRAPPER_H
#define STANDALONEMESHWRAPPER_H

#include "MeshWrapperBase.h"
#include "MeshDisplayMappingPolicy.h"

class StandaloneMeshWrapper : public MeshWrapperBase
{
public:
  irisITKObjectMacro(StandaloneMeshWrapper, MeshWrapperBase)

  //-------------------------------------------------
  // Begin virtual methods definition

  /**
   * Get the display mapping policy. This policy differs from wrapper to wrapper
   * and may involve using color labels or color maps.
   */
  AbstractDisplayMappingPolicy *GetDisplayMapping() override
  { return m_DisplayMapping.GetPointer(); }
  const AbstractDisplayMappingPolicy *GetDisplayMapping() const override
  { return m_DisplayMapping.GetPointer(); }

  MeshDisplayMappingPolicy *GetMeshDisplayMappingPolicy() const override
  { return m_DisplayMapping.GetPointer(); }

  bool IsMeshDirty(unsigned int timepoint) override;

  bool IsA(const char *type) const override;

  void SetMesh(vtkPolyData* mesh, unsigned int timepoint, LabelType id) override;


  // End of virtual methods definition
  //-------------------------------------------------


protected:
  StandaloneMeshWrapper();
  virtual ~StandaloneMeshWrapper() = default;

  SmartPtr<GenericMeshDisplayMappingPolicy> m_DisplayMapping;
};

#endif // STANDALONEMESHWRAPPER_H
