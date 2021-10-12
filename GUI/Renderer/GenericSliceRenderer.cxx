/*=========================================================================

  Program:   ITK-SNAP
  Module:    $RCSfile: Filename.cxx,v $
  Language:  C++
  Date:      $Date: 2010/10/18 11:25:44 $
  Version:   $Revision: 1.12 $
  Copyright (c) 2011 Paul A. Yushkevich

  This file is part of ITK-SNAP

  ITK-SNAP is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

=========================================================================*/

#include "SNAPCommon.h"
#include "GenericSliceRenderer.h"
#include "GenericSliceModel.h"
#include "GlobalUIModel.h"
#include "SNAPAppearanceSettings.h"
#include "GenericImageData.h"
#include "ImageWrapper.h"
#include "IRISApplication.h"
#include "IntensityCurveModel.h"
#include "DisplayLayoutModel.h"
#include "ColorMapModel.h"
#include "LayerAssociation.txx"
#include "SliceWindowCoordinator.h"
#include "PaintbrushSettingsModel.h"

#include <vtkTexture.h>
#include <vtkTexturedActor2D.h>
#include <vtkRenderer.h>
#include <vtkActor2D.h>
#include <vtkActor.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkTextActor.h>
#include <vtkRenderWindow.h>
#include <vtkRendererCollection.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkPointData.h>
#include <vtkFloatArray.h>
#include <vtkQuad.h>
#include <vtkProperty.h>
#include <vtkCamera.h>
#include <itkRGBAPixel.h>
#include "SNAPExportITKToVTK.h"

GenericSliceRenderer
::GenericSliceRenderer()
{
  this->m_DrawingZoomThumbnail = false;
  this->m_DrawingLayerThumbnail = false;
  this->m_DrawingViewportIndex = -1;

  m_OverlayRenderer = vtkSmartPointer<vtkRenderer>::New();
  m_OverlayRenderer->SetLayer(1);

  vtkNew<vtkTextActor> txt;
  txt->SetInput("Hello World");
  txt->SetPosition(100, 100);
  m_OverlayRenderer->AddActor2D(txt);
}

void
GenericSliceRenderer::SetModel(GenericSliceModel *model)
{
  this->m_Model = model;

  // Record and rebroadcast changes in the model
  Rebroadcast(m_Model, ModelUpdateEvent(), ModelUpdateEvent());

  // Respond to changes in image dimension - these require big updates
  Rebroadcast(m_Model->GetDriver(), LayerChangeEvent(), AppearanceUpdateEvent());

  // Also listen to events on opacity
  Rebroadcast(m_Model->GetParentUI()->GetGlobalState()->GetSegmentationAlphaModel(),
              ValueChangedEvent(), AppearanceUpdateEvent());

  // Listen to changes in the appearance of any of the wrappers
  Rebroadcast(m_Model->GetDriver(), WrapperChangeEvent(), AppearanceUpdateEvent());

  // Listen to changes to the segmentation
  Rebroadcast(m_Model->GetDriver(), SegmentationChangeEvent(), AppearanceUpdateEvent());

  // Changes to cell layout also must be rebroadcast
  DisplayLayoutModel *dlm = m_Model->GetParentUI()->GetDisplayLayoutModel();
  Rebroadcast(dlm, DisplayLayoutModel::LayerLayoutChangeEvent(),
              AppearanceUpdateEvent());

  // Listen to changes in appearance
  Rebroadcast(m_Model->GetParentUI()->GetAppearanceSettings(),
              ChildPropertyChangedEvent(), AppearanceUpdateEvent());

  // Listen to overall visibility of overlaps
  Rebroadcast(m_Model->GetParentUI()->GetAppearanceSettings()->GetOverallVisibilityModel(),
              ValueChangedEvent(), AppearanceUpdateEvent());

  // Paintbrush appearance changes
  PaintbrushSettingsModel *psm = m_Model->GetParentUI()->GetPaintbrushSettingsModel();
  Rebroadcast(psm->GetBrushSizeModel(), ValueChangedEvent(), AppearanceUpdateEvent());

  // Which layer is currently selected
  Rebroadcast(m_Model->GetDriver()->GetGlobalState()->GetSelectedLayerIdModel(),
              ValueChangedEvent(), AppearanceUpdateEvent());

  Rebroadcast(m_Model->GetDriver()->GetGlobalState()->GetSelectedSegmentationLayerIdModel(),
              ValueChangedEvent(), AppearanceUpdateEvent());


  Rebroadcast(m_Model->GetHoveredImageLayerIdModel(), ValueChangedEvent(), AppearanceUpdateEvent());
  Rebroadcast(m_Model->GetHoveredImageIsThumbnailModel(), ValueChangedEvent(), AppearanceUpdateEvent());

  // Update the appearance settings
  this->AssignAppearanceSettingsToScene();
}

void GenericSliceRenderer::AssignAppearanceSettingsToScene()
{
  // Get the appearance settings pointer since we use it a lot
  SNAPAppearanceSettings *as =
      m_Model->GetParentUI()->GetAppearanceSettings();

  // Get the properties for the background color
  Vector3d clrBack = as->GetUIElement(
      SNAPAppearanceSettings::BACKGROUND_2D)->GetColor();

  // For each base renderer, set its background
  for(auto &bla : m_BaseLayerAssemblies)
    {
    bla.second.m_Renderer->SetBackground(clrBack.data_block());
    bla.second.m_ThumbRenderer->SetBackground(clrBack.data_block());
    }
}

void GenericSliceRenderer::OnUpdate()
{
  std::cout << "OnUpdate() called in window " << m_Model->GetId() << std::endl;
  std::cout << *m_EventBucket << std::endl;

  // Make sure the model has been updated first
  m_Model->Update();

  // Also make sure to update the model zoom coordinator (this is confusing)
  m_Model->GetParentUI()->GetSliceCoordinator()->Update();

  // Also make sure to update the display layout model
  m_Model->GetParentUI()->GetDisplayLayoutModel()->Update();

  // Check what events have occurred
  bool appearance_settings_changed =
      m_EventBucket->HasEvent(ChildPropertyChangedEvent(),
                              m_Model->GetParentUI()->GetAppearanceSettings());

  bool segmentation_opacity_changed =
      m_EventBucket->HasEvent(ValueChangedEvent(),
                              m_Model->GetParentUI()->GetGlobalState()->GetSegmentationAlphaModel());

  bool layers_changed =
      m_EventBucket->HasEvent(LayerChangeEvent());

  bool layer_layout_changed =
      m_EventBucket->HasEvent(DisplayLayoutModel::LayerLayoutChangeEvent());

  bool layer_metadata_changed =
      m_EventBucket->HasEvent(WrapperMetadataChangeEvent());

  bool layer_mapping_changed =
      m_EventBucket->HasEvent(WrapperDisplayMappingChangeEvent());

  bool zoom_pan_changed =
      m_EventBucket->HasEvent(ModelUpdateEvent(), m_Model);

  if(layers_changed)
    {
    this->UpdateLayerAssemblies();
    }

  if(layers_changed || layer_layout_changed)
    {
    this->UpdateRendererLayout();
    }

  if(layers_changed || layer_mapping_changed || segmentation_opacity_changed)
    {
    this->UpdateLayerApperances();
    }

  if(appearance_settings_changed)
    {
    this->AssignAppearanceSettingsToScene();
    }

  if(layers_changed || layer_layout_changed || zoom_pan_changed)
    {
    this->UpdateRendererCameras();
    }
}

void GenericSliceRenderer::SetRenderWindow(vtkRenderWindow *rwin)
{
  m_RenderWindow = rwin;
  m_RenderWindow->SetNumberOfLayers(2);
  m_RenderWindow->AddRenderer(m_OverlayRenderer);
}

void GenericSliceRenderer::UpdateLayerAssemblies()
{
  // Synchronize the layer assemblies with available renderers
  std::map<unsigned long, LayerTextureAssembly> new_layer_texture_assemblies;
  GenericImageData *id = m_Model->GetDriver()->GetCurrentImageData();

  // For each layer either copy a reference to an existing assembly
  // or create a new assembly
  for(LayerIterator it = id->GetLayers(); !it.IsAtEnd(); ++it)
    {
    unsigned long layer_id = it.GetLayer()->GetUniqueId();

    // Every layer gets a texture assembly
    auto existing = m_LayerTextureAssemblies.find(layer_id);
    if(existing != m_LayerTextureAssemblies.end())
      new_layer_texture_assemblies[layer_id] = existing->second;
    else
      {
      LayerTextureAssembly lta;

      // Get the pointer to the display slice
      auto *ds = it.GetLayer()->GetDisplaySlice(m_Model->GetId()).GetPointer();

      // Configure the texture pipeline
      itk::SmartPointer<LayerTextureAssembly::VTKExporter> exporter = LayerTextureAssembly::VTKExporter::New();
      exporter->SetInput(ds);

      lta.m_Exporter = exporter.GetPointer();
      lta.m_Importer = vtkSmartPointer<vtkImageImport>::New();
      ConnectITKExporterToVTKImporter(exporter.GetPointer(), lta.m_Importer);

      lta.m_Texture = vtkSmartPointer<vtkTexture>::New();
      lta.m_Texture->SetInputConnection(lta.m_Importer->GetOutputPort());

      // Get the corners of the slice
      auto sc = m_Model->GetSliceCorners();
      auto c0 = sc.first, c1 = sc.second;

      // Create a polydata for the image
      lta.m_ImageRectPolyData = vtkSmartPointer<vtkPolyData>::New();
      lta.m_ImageRectPolyData->SetPoints(vtkSmartPointer<vtkPoints>::New());
      lta.m_ImageRectPolyData->GetPoints()->InsertNextPoint(c0[0], c0[1], 0);
      lta.m_ImageRectPolyData->GetPoints()->InsertNextPoint(c0[0], c1[1], 0);
      lta.m_ImageRectPolyData->GetPoints()->InsertNextPoint(c1[0], c1[1], 0);
      lta.m_ImageRectPolyData->GetPoints()->InsertNextPoint(c1[0], c0[1], 0);

      vtkNew<vtkQuad> quad;
      quad->GetPointIds()->SetId(0, 0);
      quad->GetPointIds()->SetId(1, 1);
      quad->GetPointIds()->SetId(2, 2);
      quad->GetPointIds()->SetId(3, 3);
      lta.m_ImageRectPolyData->SetPolys(vtkSmartPointer<vtkCellArray>::New());
      lta.m_ImageRectPolyData->GetPolys()->InsertNextCell(quad);

      // Set texture coordinates
      vtkNew<vtkFloatArray> tcoords;
      tcoords->SetNumberOfComponents(2);
      tcoords->InsertNextTuple2(0, 0);
      tcoords->InsertNextTuple2(0, 1);
      tcoords->InsertNextTuple2(1, 1);
      tcoords->InsertNextTuple2(1, 0);
      lta.m_ImageRectPolyData->GetPointData()->SetTCoords(tcoords);

      // Create the main image actor
      lta.m_ImageRectMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
      lta.m_ImageRectMapper->SetInputData(lta.m_ImageRectPolyData);
      lta.m_ImageRectActor = vtkSmartPointer<vtkActor>::New();
      lta.m_ImageRectActor->SetMapper(lta.m_ImageRectMapper);
      lta.m_ImageRectActor->SetTexture(lta.m_Texture);
      lta.m_ImageRectActor->GetProperty()->SetColor(1.0, 1.0, 1.0);

      new_layer_texture_assemblies[layer_id] = lta;
      }
    }

  // Replace the map (deleting the assemblies corresponding to removed layers)
  m_LayerTextureAssemblies = new_layer_texture_assemblies;

  // Now iterate over the base layers only and set up the base layer assemblies, which
  // consist of the base layer, sticky overlays, and segmentation layer
  std::map<unsigned long, BaseLayerAssembly> new_base_layer_assemblies;
  for(LayerIterator it = id->GetLayers(); !it.IsAtEnd(); ++it)
    {
    unsigned long layer_id = it.GetLayer()->GetUniqueId();

    // If this is a base layer (something drawn on its own), it gets a pair of renderers
    if(it.GetRole() == MAIN_ROLE || it.GetRole() == OVERLAY_ROLE || it.GetRole() == SNAP_ROLE)
      {
      auto existing = m_BaseLayerAssemblies.find(layer_id);
      if(existing != m_BaseLayerAssemblies.end())
        new_base_layer_assemblies[layer_id] = existing->second;
      else
        {
        BaseLayerAssembly la;

        // Create the renderers
        la.m_Renderer = vtkSmartPointer<vtkRenderer>::New();
        la.m_ThumbRenderer = vtkSmartPointer<vtkRenderer>::New();

        // Set camera properties
        la.m_Renderer->GetActiveCamera()->ParallelProjectionOn();

        new_base_layer_assemblies[layer_id] = la;
        }
      }
    }

  // Replace the map (deleting the assemblies corresponding to removed layers)
  m_BaseLayerAssemblies = new_base_layer_assemblies;
}

void GenericSliceRenderer::SetDepth(vtkActor *actor, double z)
{
  double *p = actor->GetPosition();
  actor->SetPosition(p[0], p[1], z);
}

void GenericSliceRenderer::UpdateLayerDepth()
{
  double depth_ovl = DEPTH_OVERLAY_START;
  double depth_seg = DEPTH_SEGMENTATION_START;

  for(LayerIterator it = m_Model->GetImageData()->GetLayers(); !it.IsAtEnd(); ++it)
    {
    unsigned long layer_id = it.GetLayer()->GetUniqueId();
    auto &la = m_LayerTextureAssemblies[layer_id];
    double depth = 0.0;

    if(it.GetRole() == LABEL_ROLE)
      {
      // All segmentation layers get assigned the same depth because we are
      // currently not supporting rendering of multiple segmentation layers
      // at the same time with transparency
      depth = depth_seg;
      }
    else if(it.GetLayer()->IsSticky())
      {
      // Overlays are placed at increasing values of z
      depth = depth_ovl;
      depth_ovl += DEPTH_STEP;
      }

    SetDepth(la.m_ImageRectActor, depth);
    }
}

void GenericSliceRenderer::UpdateRendererLayout()
{
  if(!m_RenderWindow)
    return;

  std::cout << "Updating renderers in window " << m_Model->GetId() << std::endl;

  // Update the depths of the layers
  this->UpdateLayerDepth();

  // Create a sorted structure of layers that are rendered on top of the base
  std::map<double, vtkActor *> depth_map;
  for(auto it : m_LayerTextureAssemblies)
    {
    auto *actor = it.second.m_ImageRectActor.Get();
    double z = actor->GetPosition()[2];
    if(z > 0.0)
      depth_map[z] = actor;
    }

  // Get the viewport layout
  const SliceViewportLayout &vpl = m_Model->GetViewportLayout();

  // Remove all the renderers from the current window
  m_RenderWindow->GetRenderers()->RemoveAllItems();

  // Get the dimensions of the render window
  Vector2ui szWin = m_Model->GetSizeReporter()->GetViewportSize();

  // Draw each viewport in turn. For now, the number of z-layers is hard-coded at 2
  for(unsigned int k = 0; k < vpl.vpList.size(); k++)
    {
    // Get the current viewport
    const SliceViewportLayout::SubViewport &vp = m_Model->GetViewportLayout().vpList[k];

    // Get the assembly for this layer
    BaseLayerAssembly &la = m_BaseLayerAssemblies[vp.layer_id];

    // Get the renderer that is referenced by this viewport
    vtkRenderer *renderer = vp.isThumbnail ? la.m_ThumbRenderer : la.m_Renderer;

    // Create a VTK renderer for this viewport
    Vector2d rel_pos[2];
    for(unsigned int d = 0; d < 2; d++)
      {
      rel_pos[0][d] = vp.pos[d] * 1.0 / szWin[d];
      rel_pos[1][d] = rel_pos[0][d] + vp.size[d] * 1.0 / szWin[d];
      }

    // Set the renderer viewport
    renderer->SetViewport(rel_pos[0][0], rel_pos[0][1], rel_pos[1][0], rel_pos[1][1]);

    // Set up the actors shown in this renderer
    renderer->RemoveAllViewProps();

    // Add the base layer actor
    renderer->AddActor(m_LayerTextureAssemblies[vp.layer_id].m_ImageRectActor);

    // Add the overlay layer actors
    for(auto it : depth_map)
      renderer->AddActor(it.second);

    // Add the renderer to the window
    m_RenderWindow->AddRenderer(renderer);
    }

  // Add the overlay renderer
  m_RenderWindow->AddRenderer(m_OverlayRenderer);
}

void GenericSliceRenderer::UpdateRendererCameras()
{
  for(auto it : m_BaseLayerAssemblies)
    {
    auto ren = it.second.m_Renderer;
    auto vp = m_Model->GetViewPosition();
    ren->GetActiveCamera()->SetFocalPoint(vp[0], vp[1], 0.0);
    ren->GetActiveCamera()->SetPosition(vp[0], vp[1], 1.0);
    ren->GetActiveCamera()->SetViewUp(0.0, 1.0, 0.0);

    // ParallelScale is the height of the viewport in world coordinate distances.
    // ViewZoom is the number of display pixels per physical mm
    // pscale = v_height_in_pix / view_zoom
    int sz_logical = ren->GetSize()[1] / m_Model->GetSizeReporter()->GetViewportPixelRatio();
    double pscale = sz_logical / m_Model->GetViewZoom();
    ren->GetActiveCamera()->SetParallelScale(pscale);
    }
}

void GenericSliceRenderer::UpdateLayerApperances()
{
  // Iterate over the layers
  for(LayerIterator it = m_Model->GetImageData()->GetLayers(); !it.IsAtEnd(); ++it)
    {
    // Does this layer use transparency?
    double alpha = 1.0;
    if(it.GetRole() == LABEL_ROLE)
      alpha = m_Model->GetDriver()->GetGlobalState()->GetSegmentationAlpha();
    else if(it.GetLayer()->IsSticky())
      alpha = it.GetLayer()->GetAlpha();

    // Set the alpha for the actor
    auto &lta = m_LayerTextureAssemblies[it.GetLayer()->GetUniqueId()];
    std::cout << "Layer " << it.GetLayer()->GetUniqueId() << " setting alpha to " << alpha << std::endl;
    lta.m_ImageRectActor->GetProperty()->SetOpacity(alpha);
    }
}


void
GenericSliceRenderer
::paintGL()
{
  // Get the current image data
  GenericImageData *id = m_Model->GetDriver()->GetCurrentImageData();

  // Get the appearance settings pointer since we use it a lot
  SNAPAppearanceSettings *as =
      m_Model->GetParentUI()->GetAppearanceSettings();

  // Get the properties for the background color
  Vector3d clrBack = as->GetUIElement(
      SNAPAppearanceSettings::BACKGROUND_2D)->GetColor();

  // Get the overall viewport
  Vector2ui vp_full = m_Model->GetSizeReporter()->GetViewportSize();
  int vppr = m_Model->GetSizeReporter()->GetViewportPixelRatio();

  // Set up lighting attributes
  glPushAttrib(GL_LIGHTING_BIT | GL_DEPTH_BUFFER_BIT |
               GL_PIXEL_MODE_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT);

  glDisable(GL_LIGHTING);

  glClearColor(clrBack[0], clrBack[1], clrBack[2], 1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Set up the viewports for individual 'cells' in the display. Each cell constitutes one
  // image with its various overlays.

  // Slice should be initialized before display
  if (m_Model->IsSliceInitialized())
    {
    // Draw each viewport in turn. For now, the number of z-layers is hard-coded at 2
    for(int k = 0; k < m_Model->GetViewportLayout().vpList.size(); k++)
      {
      const SliceViewportLayout::SubViewport &vp = m_Model->GetViewportLayout().vpList[k];

      // Set up the viewport for the current cell
      glViewport(vp.pos[0], vp.pos[1], vp.size[0], vp.size[1]);

      // Set up the projection
      glMatrixMode(GL_PROJECTION);
      glPushMatrix();
      glLoadIdentity();
      irisOrtho2D(0.0, vp.size[0], 0.0, vp.size[1]);

      // Establish the model view matrix
      glMatrixMode(GL_MODELVIEW);
      glPushMatrix();
      glLoadIdentity();

      glPushMatrix();

      // First set of transforms
      glTranslated(0.5 * vp.size[0], 0.5 * vp.size[1], 0.0);

      // Zoom by display zoom. The amount of zoom depends on whether we are in thumbnail
      // mode or in regular mode
      double zoom = m_Model->GetViewZoom();
      if(vp.isThumbnail)
        {
        double scale_x = vp.size[0] * 1.0 / m_Model->GetCanvasSize()[0];
        double scale_y = vp.size[1] * 1.0 / m_Model->GetCanvasSize()[1];
        zoom *= std::max(scale_x, scale_y);
        }

      // Apply the correct scaling
      glScalef(zoom, zoom, 1.0);

      // Panning
      glTranslated(-m_Model->GetViewPosition()[0], -m_Model->GetViewPosition()[1], 0.0);

      // Convert from voxel space to physical units
      glScalef(m_Model->GetSliceSpacing()[0], m_Model->GetSliceSpacing()[1], 1.0);

      // Draw the main layers for this row/column combination
      ImageWrapperBase *layer = id->FindLayer(vp.layer_id, false);
      if(layer && this->DrawImageLayers(layer, vp))
        {
        // Set the thumbnail flag
        m_DrawingLayerThumbnail = vp.isThumbnail;

        // Set the current vp index
        m_DrawingViewportIndex = k;

        // We don't want to draw segmentation over the speed image and other
        // SNAP-mode layers.
        this->DrawSegmentationTexture();

        // Draw the overlays
        if(as->GetOverallVisibility())
          {
          // Draw all the overlays added to this object
          this->DrawTiledOverlays();
          }

        glPopMatrix();

        // Determine if the current layer is hovered over by the mouse
        bool is_hover = layer->GetUniqueId() == m_Model->GetHoveredImageLayerId();
        bool is_thumb = vp.isThumbnail;
        bool is_selected = layer->GetUniqueId() == m_Model->GetDriver()->GetGlobalState()->GetSelectedLayerId();

        // Draw decoration around layer thumbnail. This is done when the thumbnail is hovered over
        // or currently selected
        if(is_thumb && (is_hover || is_selected))
          {
          // If the layer has positive z, draw a line
          glPushAttrib(GL_LINE_BIT | GL_COLOR_BUFFER_BIT);

          // The element used for highlighting thumbnails
          SmartPtr<OpenGLAppearanceElement> elt = OpenGLAppearanceElement::New();

          if(is_selected && is_hover)
            elt->SetColor(Vector3d(1.0, 1.0, 0.5));
          else if(is_selected)
            elt->SetColor(Vector3d(1.0, 0.9, 0.1));
          else if(is_hover)
            elt->SetColor(Vector3d(0.6, 0.54, 0.46));

          elt->SetLineThickness(1.5 * vppr);
          elt->SetVisible(true);
          elt->SetSmooth(false);
          elt->ApplyLineSettings();

          if(is_selected || is_hover)
            elt->ApplyColor();

          glBegin(GL_LINE_LOOP);
          glVertex2i(0,0);
          glVertex2i(0,vp.size[1]);
          glVertex2i(vp.size[0], vp.size[1]);
          glVertex2i(vp.size[0], 0);
          glEnd();

          glPopAttrib();
          }

        // Draw context menu indicator for the layer being hovered
        /*
         * // NOTE - this is now being done in the Qt code instead
        if(is_hover && is_thumb == m_Model->GetHoveredImageIsThumbnail())
          {
          // Load the texture for the icon
          static GLuint icon_texture_id = -1u;
          static Vector2ui icon_size;
          int vpratio = m_Model->GetSizeReporter()->GetViewportPixelRatio();
          if(icon_texture_id == -1u)
            {
            m_PlatformSupport->LoadTexture("context_gray_12", icon_texture_id, icon_size);
            }

          // Draw the icon in the corner of the view
          glPushAttrib(GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT);
          glEnable(GL_TEXTURE_2D);
          glEnable(GL_BLEND);
          glBindTexture(GL_TEXTURE_2D, icon_texture_id);
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

          glColor3d(1.0, 1.0, 1.0);
          glBegin(GL_QUADS);
          int margin = 4 * vpratio;
          int x0 = vp.size[0] - margin - icon_size[0], x1 = vp.size[0] - margin;
          int y1 = vp.size[1] - margin - icon_size[1], y0 = vp.size[1] - margin;
          glTexCoord2d(0.0, 0.0); glVertex2i(x0,y0);
          glTexCoord2d(0.0, 1.0); glVertex2i(x0,y1);
          glTexCoord2d(1.0, 1.0); glVertex2i(x1,y1);
          glTexCoord2d(1.0, 0.0); glVertex2i(x1,y0);
          glEnd();

          glPopAttrib();
          }
          */


        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        }
      }

    // No longer drawing thumbnails or viewports
    m_DrawingLayerThumbnail = false;
    m_DrawingViewportIndex = -1;

    // Set the viewport and projection to original dimensions
    glViewport(0, 0, vp_full[0], vp_full[1]);

    // Set up the projection
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    irisOrtho2D(0.0, vp_full[0], 0.0, vp_full[1]);

    // Establish the model view matrix
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    if(as->GetOverallVisibility())
      {
      // Draw the zoom locator
      if(m_Model->IsThumbnailOn())
        this->DrawThumbnail();

      // Draw the global overlays
      this->DrawGlobalOverlays();
      }

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    }

  // Draw the various decorations
  glPopAttrib();

  // Display!
  glFlush();
}

const GenericSliceRenderer::ViewportType *
GenericSliceRenderer
::GetDrawingViewport() const
{
  if(m_DrawingViewportIndex < 0)
    return NULL;
  else
    return &m_Model->GetViewportLayout().vpList[m_DrawingViewportIndex];

}

void
GenericSliceRenderer
::resizeGL(int w, int h, int device_pixel_ratio)
{
  // Set up projection matrix
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  irisOrtho2D(0.0,w,0.0,h);
  glViewport(0,0,w,h);

  // Establish the model view matrix
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
}

bool GenericSliceRenderer::DrawImageLayers(ImageWrapperBase *base_layer, const ViewportType &vp)
{
  // Get the image data
  GenericImageData *id = m_Model->GetImageData();

  // If drawing the thumbnail, only draw the main layer
  if(m_DrawingZoomThumbnail)
    {
    DrawTextureForLayer(base_layer, vp, false);
    return true;
    }

  // Is the display partitioned into rows and columns?
  if(!this->IsTiledMode())
    {
    // Draw the base layer without transparency
    DrawTextureForLayer(base_layer, vp, false);

    // Now draw all the sticky layers on top
    if(!vp.isThumbnail)
      {
        for(LayerIterator it(id); !it.IsAtEnd(); ++it)
        {
        ImageWrapperBase *layer = it.GetLayer();
        if(it.GetRole() != LABEL_ROLE
           && layer->IsDrawable()
           && layer->IsSticky()
           && layer->GetAlpha() > 0)
          {
          DrawTextureForLayer(layer, vp, true);
          }
        }
      }

    return true;
    }
  else
    {
    // Draw the particular layer
    DrawTextureForLayer(base_layer, vp, false);

    // Now draw all the non-sticky layers
    if(!vp.isThumbnail)
      {
      for(LayerIterator itov(id); !itov.IsAtEnd(); ++itov)
        {
        if(itov.GetRole() != MAIN_ROLE
           && itov.GetLayer()->IsSticky()
           && itov.GetLayer()->IsDrawable()
           && itov.GetLayer()->GetAlpha() > 0)
          {
          DrawTextureForLayer(itov.GetLayer(), vp, true);
          }
        }
      }

    return true;
  }
}

bool GenericSliceRenderer::IsTiledMode() const
{
  DisplayLayoutModel *dlm = m_Model->GetParentUI()->GetDisplayLayoutModel();
  Vector2ui layout = dlm->GetSliceViewLayerTilingModel()->GetValue();
  return layout[0] > 1 || layout[1] > 1;
}



GenericSliceRenderer::Texture *
GenericSliceRenderer
::GetTextureForLayer(ImageWrapperBase *layer)
{
  const char *user_data_ids[] = {
    "OpenGLTexture[0]",
    "OpenGLTexture[1]",
    "OpenGLTexture[2]"
  };
  const char *user_data_id = user_data_ids[m_Model->GetId()];

  // If layer uninitialized, return NULL
  if(!layer->IsInitialized())
    return NULL;

  // Retrieve the texture
  SmartPtr<Texture> tex = static_cast<Texture *>(layer->GetUserData(user_data_id));

  // Get the image that should be associated with the texture
  Texture::ImageType *slice = layer->GetDisplaySlice(m_Model->GetId()).GetPointer();

  // If the texture does not exist - or if the image has changed for some reason, update it
  if(!tex || tex->GetImage() != slice)
    {
    tex = Texture::New();
    tex->SetDepth(4, GL_RGBA);
    tex->SetImage(slice);

    layer->SetUserData(user_data_id, tex.GetPointer());
    }

  // Configure the texture parameters
  const GlobalDisplaySettings *gds = m_Model->GetParentUI()->GetGlobalDisplaySettings();
  GLint imode =
      (gds->GetGreyInterpolationMode() == GlobalDisplaySettings::LINEAR)
      ? GL_LINEAR : GL_NEAREST;
  tex->SetInterpolation(imode);

  // Set the mip-mapping behaviour depending on whether the image wrapper is rendering
  // in image space or in display space
  tex->SetMipMapping(layer->IsSlicingOrthogonal());

  return tex;
}

#include <itkImageLinearConstIteratorWithIndex.h>

Vector3d GenericSliceRenderer::ComputeGridPosition(
    const Vector3d &disp_pix,
    const itk::Index<2> &slice_index,
    ImageWrapperBase *vecimg)
{
  // The pixel must be mapped to native
  Vector3d disp;
  disp[0] = vecimg->GetNativeIntensityMapping()->MapInternalToNative(disp_pix[0]);
  disp[1] = vecimg->GetNativeIntensityMapping()->MapInternalToNative(disp_pix[1]);
  disp[2] = vecimg->GetNativeIntensityMapping()->MapInternalToNative(disp_pix[2]);

  // This is the physical coordinate of the current pixel - in LPS
  Vector3d xPhys;
  if(vecimg->IsSlicingOrthogonal())
    {
    // The pixel gives the displacement in LPS coordinates (by ANTS/Greedy convention)
    // We need to map it back into the slice domain. First, we need to know the 3D index
    // of the current pixel in the image space
    Vector3d xSlice;
    xSlice[0] = slice_index[0] + 0.5;
    xSlice[1] = slice_index[1] + 0.5;
    xSlice[2] = m_Model->GetSliceIndex();

    // For orthogonal slicing, the input coordinates are in units of image voxels
    xPhys = m_Model->MapSliceToImagePhysical(xSlice);
    }
  else
    {
    // Otherwise, the slice coordinates are relative to the rendered slice
    GenericImageData *gid = m_Model->GetImageData();
    GenericImageData::ImageBaseType *dispimg =
        gid->GetDisplayViewportGeometry(m_Model->GetId());

    // Use that image to transform coordinates
    itk::Point<double, 3> pPhys;
    itk::Index<3> index;
    index[0] = slice_index[0]; index[1] = slice_index[1]; index[2] = 0;
    dispimg->TransformIndexToPhysicalPoint(index, pPhys);
    xPhys = pPhys;
    }

  // Add displacement and map back to slice space
  itk::ContinuousIndex<double, 3> cix;
  itk::Point<double, 3> pt = to_itkPoint(xPhys + disp);

  m_Model->GetDriver()->GetCurrentImageData()->GetMain()->GetImageBase()
      ->TransformPhysicalPointToContinuousIndex(pt, cix);

  // The displaced location in slice coordinates
  Vector3d disp_slice = m_Model->MapImageToSlice(Vector3d(cix));

  // What we return also depends on whether slicing is ortho or not. For ortho
  // slicing, the renderer is configured in the "Slice" coordinate system (1 unit =
  // 1 image voxel) while for oblique slicing, the renderer uses the window coordinate
  // system (1 unit = 1 screen pixel). Whatever we return needs to be in those units.
  if(vecimg->IsSlicingOrthogonal())
    {
    return disp_slice;
    }
  else
    {
    Vector3d win3d;
    Vector2d win2d = m_Model->MapSliceToWindow(disp_slice);
    win3d[0] = win2d[0]; win3d[1] = win2d[1]; win3d[2] = disp_slice[2];
    return win3d;
    }
}


void GenericSliceRenderer::DrawTextureForLayer(
    ImageWrapperBase *layer, const ViewportType &vp, bool use_transparency)
{
  // Get the appearance settings pointer since we use it a lot
  SNAPAppearanceSettings *as =
      m_Model->GetParentUI()->GetAppearanceSettings();

  // Get the global display settings
  const GlobalDisplaySettings *gds =
      m_Model->GetParentUI()->GetGlobalDisplaySettings();

  // Get the interpolation mode
  GLenum interp =
      gds->GetGreyInterpolationMode() == GlobalDisplaySettings::LINEAR
      ? GL_LINEAR : GL_NEAREST;

  // Get the texture
  Texture *tex = this->GetTextureForLayer(layer);

  // Set up the drawing mode
  glPushMatrix();

  // If a layer is sliced orthogonally, it's sliced in its native voxel space
  // and we rely on OpenGL for scaling into display space
  // Otherwise there is a 1:1 mapping from slice pixels to display pixels
  if(!layer->IsSlicingOrthogonal())
    {
    glLoadIdentity();
    if(vp.isThumbnail)
      {
      double scale_x = vp.size[0] * 1.0 / m_Model->GetCanvasSize()[0];
      double scale_y = vp.size[1] * 1.0 / m_Model->GetCanvasSize()[1];
      double zoom = std::max(scale_x, scale_y);
      glScalef(zoom, zoom, 0);
      }

    }

  // Paint the texture with alpha
  if(tex)
    {
    tex->SetInterpolation(interp);
    if(use_transparency)
      {
      tex->DrawTransparent(layer->GetAlpha());
      }
    else
      {
      Vector3d clrBackground = m_DrawingZoomThumbnail
        ? as->GetUIElement(SNAPAppearanceSettings::ZOOM_THUMBNAIL)->GetColor()
        : Vector3d(1.0);
      tex->Draw(clrBackground);
      }
    }

  // TODO: move this somewhere
  AbstractMultiChannelDisplayMappingPolicy *dp = dynamic_cast<
      AbstractMultiChannelDisplayMappingPolicy *>(layer->GetDisplayMapping());
  if(dp && dp->GetDisplayMode().RenderAsGrid
     && !this->IsDrawingZoomThumbnail() && !this->IsDrawingLayerThumbnail())
    {
    // Draw the texture for the layer
    AnatomicImageWrapper *vecimg = dynamic_cast<AnatomicImageWrapper *>(layer);
    if(vecimg && vecimg->GetNumberOfComponents() == 3)
      {
      // Get the slice
      AnatomicImageWrapper::SliceType::Pointer slice = vecimg->GetSlice(m_Model->GetId());
      slice->GetSource()->UpdateLargestPossibleRegion();

      // Appearance settings for grid lines
      SNAPAppearanceSettings *as = m_Model->GetParentUI()->GetAppearanceSettings();
      const OpenGLAppearanceElement *elt =
          as->GetUIElement(SNAPAppearanceSettings::GRID_LINES);

      // Line properties
      glPushAttrib(GL_LINE_BIT | GL_COLOR_BUFFER_BIT);

      elt->ApplyLineSettings();


      // The mapping between (index, phi[index]) and on-screen coordinate for a grid
      // point is linear (combines a bunch of transforms). To save time, we can
      // compute this mapping once at the beginning of the loop. We also know that the
      // index will only be going up by one at each iteration
      itk::Index<2> ind;
      Vector3d phi, G0, d_grid_d_phi[3], d_grid_d_ind[2];

      // Compute the initial displacement G0
      ind.Fill(0); phi.fill(0.0f);
      G0 = ComputeGridPosition(phi, ind, vecimg);

      // Compute derivative of grid displacement wrt warp components
      for(int a = 0; a < 3; a++)
        {
        ind.Fill(0); phi.fill(0.0f);
        phi[a] = 1.0f;
        d_grid_d_phi[a] = ComputeGridPosition(phi, ind, vecimg) - G0;
        }

      // Compute derivative of grid displacement wrt index components
      for(int b = 0; b < 2; b++)
        {
        ind.Fill(0); phi.fill(0.0f);
        ind[b] = 1;
        d_grid_d_ind[b] = ComputeGridPosition(phi, ind, vecimg) - G0;
        }

      // Iterate line direction
      for(int d = 0; d < 2; d++)
        {

        // The current matrix is such that we should be drawing in pixel coordinates.
        typedef itk::ImageLinearConstIteratorWithIndex<AnatomicImageWrapper::SliceType> IterType;
        IterType it1(slice, slice->GetBufferedRegion());
        it1.SetDirection(d);
        it1.GoToBegin();

        int vox_increment;
        if(vecimg->IsSlicingOrthogonal())
          {
          // Figure out how frequently to sample lines. The spacing on the screen should be at
          // most every 4 pixels. Zoom is in units of px/mm. Spacing is in units of mm/vox, so
          // zoom * spacing is (display pixels) / (image voxels).
          double disp_pix_per_vox = m_Model->GetSliceSpacing()[d] * m_Model->GetViewZoom();
          vox_increment = (int) ceil(8.0 / disp_pix_per_vox);
          }
        else
          {
          // The slice is in screen pixel units already - so just 8!
          vox_increment = 8;
          }

        while( !it1.IsAtEnd() )
          {
          // Do we draw this line?
          if(it1.GetIndex()[1-d] % vox_increment == 0)
            {
            elt->ApplyColor();
            glBegin(GL_LINE_STRIP);

            // Set up the current position and increment
            Vector3d G1 = G0 +
                (d_grid_d_ind[0] * (double) (it1.GetIndex()[0])) +
                (d_grid_d_ind[1] * (double) (it1.GetIndex()[1]));

            while( !it1.IsAtEndOfLine() )
              {
              // Read the pixel
              AnatomicImageWrapper::SliceType::PixelType pix = it1.Get();

              // Alternative version
              Vector3d xDispSlice = G1 +
                  (d_grid_d_phi[0] * (double) (pix[0])) +
                  (d_grid_d_phi[1] * (double) (pix[1])) +
                  (d_grid_d_phi[2] * (double) (pix[2]));

              glVertex2d(xDispSlice[0], xDispSlice[1]);

              // Add the displacement
              ++it1;

              // Update the current position
              G1 += d_grid_d_ind[d];
              }

            glEnd();
            }

          it1.NextLine();
          }

        }

      glPopAttrib();
      }
    }

  // Pop the matrix
  glPopMatrix();
}


void GenericSliceRenderer::DrawSegmentationTexture()
  {
  GenericImageData *id = m_Model->GetImageData();
  double alpha = m_Model->GetParentUI()->GetDriver()->GetGlobalState()->GetSegmentationAlpha();

  if (alpha > 0)
    {
    // Search for the texture to draw
    LabelImageWrapper *seg_layer =
        dynamic_cast<LabelImageWrapper *>(
            id->FindLayer(m_Model->GetParentUI()->GetGlobalState()->GetSelectedSegmentationLayerId(), false, LABEL_ROLE));
    
    if(seg_layer)
      {
      // TODO: remove this, it's redundant
      if(seg_layer->GetImage()->GetMTime() > seg_layer->GetSlice(this->m_Model->GetId())->GetMTime())
        {
        seg_layer->GetSlice(this->m_Model->GetId())->Update();
        }
      Texture *texture = this->GetTextureForLayer(seg_layer);
      texture->DrawTransparent(alpha);
      }
    }
  }

void GenericSliceRenderer::DrawThumbnail()
  {
  // Get the thumbnail appearance properties
  SNAPAppearanceSettings *as = m_Model->GetParentUI()->GetAppearanceSettings();

  const OpenGLAppearanceElement *eltThumb =
      as->GetUIElement(SNAPAppearanceSettings::ZOOM_THUMBNAIL);

  const OpenGLAppearanceElement *eltViewport =
      as->GetUIElement(SNAPAppearanceSettings::ZOOM_VIEWPORT);

  // If thumbnail is not to be drawn, exit
  if(!eltThumb->GetVisible()) return;

  // Tell model to figure out the thumbnail size
  m_Model->ComputeThumbnailProperties();
  Vector2i tPos = m_Model->GetZoomThumbnailPosition();
  double tZoom = m_Model->GetThumbnailZoom();

  // Indicate the fact that we are currently drawing in thumbnail mode
  m_DrawingZoomThumbnail = true;

  // Set up the GL matrices
  glPushMatrix();
  glLoadIdentity();
  glTranslated((double) tPos[0], (double) tPos[1], 0.0);
  glScaled(tZoom, tZoom, 1.0);

  glPushMatrix();
  glScalef(m_Model->GetSliceSpacing()[0],m_Model->GetSliceSpacing()[1],1.0);

  // Draw the Main image (the background will be picked automatically)
  if (m_Model->GetImageData()->IsMainLoaded())
    {
    ViewportType vp = m_Model->GetViewportLayout().vpList.front();
    DrawTextureForLayer(m_Model->GetImageData()->GetMain(), vp, false);
    }

  // Draw the overlays that are shown on the thumbnail
  DrawTiledOverlays();

  // Line properties
  glPushAttrib(GL_LINE_BIT | GL_COLOR_BUFFER_BIT);

  // Apply the line settings
  eltThumb->ApplyLineSettings();

  // Draw the little version of the image in the corner of the window
  double w = m_Model->GetSliceSize()[0];
  double h = m_Model->GetSliceSize()[1];

  // Draw the line around the image
  eltThumb->ApplyColor();
  glBegin(GL_LINE_LOOP);
  glVertex2d(0,0);
  glVertex2d(0,h);
  glVertex2d(w,h);
  glVertex2d(w,0);
  glEnd();

  glPopAttrib();
  glPopMatrix();

  if(eltViewport->GetVisible())
    {
    // Line properties
    glPushAttrib(GL_LINE_BIT | GL_COLOR_BUFFER_BIT);

    // Apply the line settings
    eltViewport->ApplyLineSettings();

    // Draw a box representing the current zoom level
    glTranslated(m_Model->GetViewPosition()[0],
                 m_Model->GetViewPosition()[1],
                 0.0);
    w = m_Model->GetCanvasSize()[0] * 0.5 / m_Model->GetViewZoom();
    h = m_Model->GetCanvasSize()[1] * 0.5 / m_Model->GetViewZoom();

    eltViewport->ApplyColor();
    glBegin(GL_LINE_LOOP);
    glVertex2d(-w,-h);
    glVertex2d(-w, h);
    glVertex2d( w, h);
    glVertex2d( w,-h);
    glEnd();

    glPopAttrib();
    }

  glPopMatrix();

  // Indicate the fact that we are not drawing in thumbnail mode
  m_DrawingZoomThumbnail = false;
  }


void GenericSliceRenderer::initializeGL()
{
}

void GenericSliceRenderer::DrawTiledOverlays()
{
  // The renderer will contain a list of overlays that implement the
  // generic interface
  for(RendererDelegateList::iterator it = m_TiledOverlays.begin();
      it != m_TiledOverlays.end(); it++)
    {
    (*it)->paintGL();
    }
}

void GenericSliceRenderer::DrawGlobalOverlays()
{
  // The renderer will contain a list of overlays that implement the
  // generic interface
  for(RendererDelegateList::iterator it = m_GlobalOverlays.begin();
      it != m_GlobalOverlays.end(); it++)
    {
    (*it)->paintGL();
    }
}

