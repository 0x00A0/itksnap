#ifndef GENERIC3DRENDERER_H
#define GENERIC3DRENDERER_H

#include "AbstractVTKRenderer.h"
#include <vtkSmartPointer.h>

class Generic3DModel;
class vtkGenericOpenGLRenderWindow;
class vtkRenderer;
class vtkRenderWindow;
class vtkLineSource;
class vtkActor;
class vtkActor2D;
class vtkPropAssembly;
class vtkProperty;
class vtkTransform;
class vtkImplicitPlaneWidget;
class vtkGlyph3D;
class vtkTransformPolyDataFilter;
class vtkCubeSource;
class vtkCoordinate;
class vtkCamera;
class vtkScalarBarActor;
class vtkPolyDataMapper;
class Window3DPicker;
class ImageWrapperBase;
class VolumeAssembly;
class ImageMeshLayers;

/**
 * A struct representing the state of the VTK camera. This struct
 * can be used to communicate camera state between ITK-SNAP sessions
 */
struct CameraState
{
  Vector3d position, focal_point, view_up;
  Vector2d clipping_range;
  double view_angle, parallel_scale;
  int parallel_projection;

};

bool operator == (const CameraState &c1, const CameraState &c2);
bool operator != (const CameraState &c1, const CameraState &c2);

class ActorPool : public itk::Object
{
public:
  // Standard ITK macros
  irisITKObjectMacro(ActorPool, itk::Object)

  typedef std::map<LabelType, vtkActor*> ActorMap;
  typedef std::stack<vtkActor*> SpareActorStack;
  typedef std::stack<vtkSmartPointer<vtkActor>> ActorStorage;

  ActorMap &GetActorMap()
  { return m_ActorMap; }

  /** Get a spare actor from the pool */
  vtkActor* GetNewActor();

  /** Recycle an actor to the pool */
  void Recycle(vtkActor* actor);

protected:
  ActorPool() {}
  virtual ~ActorPool() {}

  // Create batch of new actors and add it to the reserve
  void CreateNewActors(unsigned int n);

  // Maps id to active actors
  ActorMap m_ActorMap;

  // Stores spare actors
  SpareActorStack m_SpareActors;

  // Actual Storage of all actors
  ActorStorage m_ActorStorage;
};


class Generic3DRenderer : public AbstractVTKRenderer
{
public:

  irisITKObjectMacro(Generic3DRenderer, AbstractVTKRenderer)

  /** An event fired when the camera state updates */
  itkEventMacro(CameraUpdateEvent, IRISEvent)

  FIRES(CameraUpdateEvent)

  void SetRenderWindow(vtkRenderWindow *rwin) override;

  void SetModel(Generic3DModel *model);

  virtual void OnUpdate() override;

  void ResetView();

  // Save the camera state
  void SaveCameraState();

  // Clear the rendering
  void ClearRendering();

  // Restore the camera state from saved
  void RestoreSavedCameraState();

  // Restore the camera state from saved
  void DeleteSavedCameraState();

  // Restore the camera state from saved
  bool IsSavedCameraStateAvailable();

  /** Access the VTK camera object (used for synchronization) */
  CameraState GetCameraState() const;

  /** Change the camera state */
  void SetCameraState(const CameraState &state);

  /** Get the normal to the scalpel plane in world coordinates */
  Vector3d GetScalpelPlaneNormal() const;

  /** Get the origin of the scalpel plane in world coordinates */
  Vector3d GetScalpelPlaneOrigin() const;

  /** Flip the direction of the cutplane */
  void FlipScalpelPlaneNormal();

  /** Compute the world coordinates of a click and a ray pointing inward (not normalized) */
  void ComputeRayFromClick(int x, int y, Vector3d &point, Vector3d &ray, Vector3d &dx, Vector3d &dy);

protected:
  Generic3DRenderer();
  virtual ~Generic3DRenderer() {}

  Generic3DModel *m_Model;

  // Update the actors and mappings for the renderer
  void UpdateSegmentationMeshAssembly();
  void UpdateSegmentationMeshAppearance();

  // Clear all the meshes being rendered
  void ResetSegmentationMeshAssembly();

  // Update the actors representing the axes
  void UpdateAxisRendering();

  // Update the spray paint glyph properties
  void UpdateSprayGlyphAppearanceAndShape();

  // Update the scalpel rendering
  void UpdateScalpelRendering();

  // Update the scalpel plane appearance (color, etc)
  void UpdateScalpelPlaneAppearance();

  // Update the camera
  void UpdateCamera(bool reset);

  // Configure the volume rendering
  void UpdateVolumeRendering();

  // Apply changes in the display mapping policy to the actors
  void ApplyDisplayMappingPolicyChange();

  typedef std::map<LabelType, vtkSmartPointer<vtkActor> > ActorMap;
  typedef ActorMap::iterator ActorMapIterator;
  typedef std::stack<vtkSmartPointer<vtkActor>> ActorStack;
  typedef std::stack<vtkSmartPointer<vtkPolyDataMapper>> MapperStack;


  SmartPtr<ActorPool> m_ActorPool;

  // Collection of actors for different color labels in use
  ActorMap m_ActorMap;

  // Collection of spare actors
  ActorStack m_FreeActors;

  // Collection of spare mappers
  MapperStack m_FreeMappers;

  // Line sources for drawing the crosshairs
  vtkSmartPointer<vtkLineSource> m_AxisLineSource[3];
  vtkSmartPointer<vtkActor> m_AxisActor[3];

  // Glyph filter used to render spray paint stuff
  vtkSmartPointer<vtkGlyph3D> m_SprayGlyphFilter;

  // The property controlling the spray paint
  vtkSmartPointer<vtkProperty> m_SprayProperty;

  // The transform applied to spray points
  vtkSmartPointer<vtkTransform> m_SprayTransform;
  vtkSmartPointer<vtkActor> m_SprayActor;

  // The actors for the scalpel drawing
  vtkSmartPointer<vtkLineSource> m_ScalpelLineSource;
  vtkSmartPointer<vtkActor2D> m_ScalpelLineActor;

  // The actors for the scalpel plane
  vtkSmartPointer<vtkCubeSource> m_ImageCubeSource;
  vtkSmartPointer<vtkTransformPolyDataFilter> m_ImageCubeTransform;
  vtkSmartPointer<vtkImplicitPlaneWidget> m_ScalpelPlaneWidget;

  // The actor for scalar bar
  vtkSmartPointer<vtkScalarBarActor> m_ScalarBarActor;

  // Coordinate mapper
  vtkSmartPointer<vtkCoordinate> m_CoordinateMapper;

  // Saved camera state
  vtkSmartPointer<vtkCamera> m_SavedCameraState;

  // Picker object
  vtkSmartPointer<Window3DPicker> m_Picker;

  void UpdateVolumeCurves(ImageWrapperBase *layer, VolumeAssembly *va);
  void UpdateVolumeTransform(ImageWrapperBase *layer, VolumeAssembly *va);

  ImageMeshLayers *m_MeshLayers;

  unsigned long m_CrntActorMapLayerId = 0;
  unsigned int m_CrntActorMapTimePoint = 0;
};

#endif // GENERIC3DRENDERER_H
