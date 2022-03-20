#include "InterpolateLabelModel.h"
#include "GlobalUIModel.h"
#include "IRISApplication.h"
#include "GenericImageData.h"
#include "ScalarImageWrapper.h"
#include "itkMorphologicalContourInterpolator.h"
#include "SegmentationUpdateIterator.h"
#include "itkBWAandRFinterpolation.h"
#include "itkImageRegionConstIterator.h"
#include "itkImageRegionIterator.h"


void InterpolateLabelModel::SetParentModel(GlobalUIModel *parent)
{
    this->m_Parent = parent;

    m_DrawingLabelModel->Initialize(parent->GetDriver()->GetColorLabelTable());
    m_InterpolateLabelModel->Initialize(parent->GetDriver()->GetColorLabelTable());
    m_DrawOverFilterModel->Initialize(parent->GetDriver()->GetColorLabelTable());
}

void InterpolateLabelModel::UpdateOnShow()
{
    // What we want to do here is syncronize labels with existing selections for active and draw over
    this->SetDrawingLabel(m_Parent->GetGlobalState()->GetDrawingColorLabel());
    this->SetInterpolateLabel(m_Parent->GetGlobalState()->GetDrawingColorLabel());
    this->SetDrawOverFilter(m_Parent->GetGlobalState()->GetDrawOverFilter());
}

void InterpolateLabelModel::Interpolate()
{
    // Get the segmentation wrapper
    LabelImageWrapper *liw = m_Parent->GetDriver()->GetSelectedSegmentationLayer();

    // Get the anatomical images
    m_CurrentImageData = m_Parent->GetDriver()->GetCurrentImageData();

    // Are we interpolating all labels?
    bool interp_all = this->GetInterpolateAll();

    // if Morphological ...
    bool interp_morphological = this->GetInterpolationApproach();

    //Morphological Interpolation
    if(interp_morphological){

        // Create the morphological interpolation filter
        typedef itk::MorphologicalContourInterpolator<GenericImageData::LabelImageType> MCIType;
        SmartPtr<MCIType> mci = MCIType::New();

        mci->SetInput(liw->GetImage());

        // Should we be interpolating a specific label?
        if (!interp_all)
            mci->SetLabel(this->GetInterpolateLabel());

        std::cout << this->GetMorphologyInterpolateOneAxis() << std::endl;
        // Should we interpolate only one axis?
        if (this->GetMorphologyInterpolateOneAxis())
        {
            int axis = this->m_Parent->GetDriver()->GetImageDirectionForAnatomicalDirection(this->GetMorphologyInterpolationAxis());
            mci->SetAxis(axis);
        }

        // Should we use the distance transform?
        mci->SetUseDistanceTransform(this->GetMorphologyUseDistance());

        // Should heuristic or optimal alignment be used?
        mci->SetHeuristicAlignment(!this->GetMorphologyUseOptimalAlignment());

        // Update the filter
        mci->Update();

        // Apply the labels back to the segmentation
        SegmentationUpdateIterator it_trg(liw->GetImage(), liw->GetImage()->GetBufferedRegion(),
                                          this->GetDrawingLabel(), this->GetDrawOverFilter());

        itk::ImageRegionConstIterator<GenericImageData::LabelImageType>
                it_src(mci->GetOutput(), mci->GetOutput()->GetBufferedRegion());

        // The way we paint back into the segmentation depends on whether all labels
        // or a specific label are being interpolated
        if(interp_all)
        {
            // Just replace the segmentation by the interpolation, respecting draw-over
            for(; !it_trg.IsAtEnd(); ++it_trg, ++it_src)
                it_trg.PaintLabel(it_src.Get());
        }
        else
        {
            LabelType l_replace = this->GetDrawingLabel();
            for(; !it_trg.IsAtEnd(); ++it_trg, ++it_src)
                it_trg.PaintLabelWithExtraProtection(it_src.Get(), l_replace);
        }

        // Finish the segmentation editing and create an undo point
        it_trg.Finalize();
        liw->StoreUndoPoint("Interpolate label", it_trg.RelinquishDelta());

    }

    // If Binary Weighted Averaging ...
    else{

        // Copy label image to short type from RLE
        ShortType::Pointer SegmentationImageShortType = ShortType::New();

        ShortType::RegionType ShortRegion = liw->GetImage()->GetBufferedRegion();
        SegmentationImageShortType->CopyInformation(liw->GetImage());
        SegmentationImageShortType->SetRegions( ShortRegion );
        SegmentationImageShortType->Allocate();

        itk::ImageRegionIterator< ShortType > itO( SegmentationImageShortType, SegmentationImageShortType->GetBufferedRegion() );
        itk::ImageRegionConstIterator< GenericImageData::LabelImageType > itI( liw->GetImage(), liw->GetImage()->GetBufferedRegion() );

        while ( !itI.IsAtEnd() )
        {
            typename GenericImageData::LabelImageType::PixelType val = itI.Get();
            itO.Set( val );
            ++itI;
            ++itO;
        }

        typedef itk::CombineBWAandRFFilter < GreyScalarType, GreyVectorType, ShortType > BinaryWeightedAverageType;
        typename BinaryWeightedAverageType::Pointer bwa =  BinaryWeightedAverageType::New();

        // Iterate through all of the relevant layers
        for(LayerIterator it = m_CurrentImageData->GetLayers(MAIN_ROLE | OVERLAY_ROLE);
            !it.IsAtEnd(); ++it)
        {
            if(it.GetLayerAsScalar())
            {
                AnatomicScalarImageWrapper *w = dynamic_cast<AnatomicScalarImageWrapper *>(it.GetLayer());
                bwa->AddScalarImage(w->GetImage());
            }
            else if (it.GetLayerAsVector())
            {
                AnatomicImageWrapper *w = dynamic_cast<AnatomicImageWrapper *>(it.GetLayer());
                bwa->AddVectorImage(w->GetImage());
            }
        }

        // Should we be interpolating a specific label?
        if (!interp_all)
            bwa->SetLabel(this->GetInterpolateLabel());

        bwa->SetOverwriteSegmentation(this->GetBWAOverwriteSegmentation());
        bwa->SetSegmentationImage(SegmentationImageShortType);
        bwa->SetContourInformationOnly(this->GetBWAUseContourOnly());
        bwa->SetIntermediateSlicesOnly(this->GetBWAInterpolateIntermediateOnly());

        // Did the user manually specify the slicing direction
        if (this->GetSliceDirection())
        {
            int axis = this->m_Parent->GetDriver()->GetImageDirectionForAnatomicalDirection(this->GetSliceDirectionAxis());
            bwa->SetUserAxis(axis);
        }

        bwa->Update();

        // Apply the labels back to the segmentation
        SegmentationUpdateIterator it_trg(liw->GetImage(), liw->GetImage()->GetBufferedRegion(),
                                          this->GetDrawingLabel(), this->GetDrawOverFilter());

        itk::ImageRegionConstIterator<ShortType>
                it_src(bwa->GetInterpolation(), bwa->GetInterpolation()->GetBufferedRegion());

        if(interp_all)
        {
            // Just replace the segmentation by the interpolation, respecting draw-over
            for(; !it_trg.IsAtEnd(); ++it_trg, ++it_src)
                it_trg.PaintLabel(it_src.Get());
        }
        else
        {
            LabelType l_replace = this->GetDrawingLabel();
            for(; !it_trg.IsAtEnd(); ++it_trg, ++it_src)
                it_trg.PaintLabelWithExtraProtection(it_src.Get(), l_replace);
        }

        // Finish the segmentation editing and create an undo point
        it_trg.Finalize();
        liw->StoreUndoPoint("Interpolate label", it_trg.RelinquishDelta());

    }


    // TODO: this should not be needed because this should propagate from wrapper
    // this->m_Parent->GetDriver()->InvokeEvent(SegmentationChangeEvent());
}


InterpolateLabelModel::InterpolateLabelModel()
{
    m_InterpolateAllModel = NewSimpleConcreteProperty(false);

    m_DrawingLabelModel = ConcreteColorLabelPropertyModel::New();
    m_InterpolateLabelModel = ConcreteColorLabelPropertyModel::New();
    m_DrawOverFilterModel = ConcreteDrawOverFilterPropertyModel::New();
    m_RetainScaffoldModel = NewSimpleConcreteProperty(false);

    RegistryEnumMap<InterpolationType> emap_interp;
    emap_interp.AddPair(MORPHOLOGY,"Morphological");
    emap_interp.AddPair(LEVEL_SET,"Level set");
    emap_interp.AddPair(DISTANCE_MAP,"Distance map");
    emap_interp.AddPair(BINARY_WEIGHTED_AVERAGE,"Binary Weighted Average");
    m_InterpolationMethodModel = NewSimpleEnumProperty("InterpolationType", MORPHOLOGY, emap_interp);

    m_DefaultSmoothingModel = NewRangedConcreteProperty(3.0, 0.0, 20.0, 0.01);

    m_LevelSetSmoothingModel = NewRangedConcreteProperty(3.0, 0.0, 20.0, 0.01);
    m_LevelSetCurvatureModel = NewRangedConcreteProperty(0.2, 0.0, 1.0, 0.01);

    m_MorphologyUseDistanceModel = NewSimpleConcreteProperty(false);
    m_MorphologyUseOptimalAlignmentModel = NewSimpleConcreteProperty(false);
    m_MorphologyInterpolateOneAxisModel = NewSimpleConcreteProperty(false);

    RegistryEnumMap<AnatomicalDirection> emap_interp_axis;
    emap_interp_axis.AddPair(ANATOMY_AXIAL,"Axial");
    emap_interp_axis.AddPair(ANATOMY_SAGITTAL,"Sagittal");
    emap_interp_axis.AddPair(ANATOMY_CORONAL,"Coronal");
    m_MorphologyInterpolationAxisModel = NewSimpleEnumProperty("InterpolationAxis", ANATOMY_AXIAL, emap_interp_axis);

    // added by SR
    m_BWAInterpolateIntermediateOnlyModel = NewSimpleConcreteProperty(false);
    m_BWAUseContourOnlyModel = NewSimpleConcreteProperty(false);
    m_InterpolationApproachModel = NewSimpleConcreteProperty(false);
    m_BWAOverwriteSegmentationModel = NewSimpleConcreteProperty(false);
    m_SliceDirectionModel = NewSimpleConcreteProperty(false);
    m_SliceDirectionAxisModel = NewSimpleEnumProperty("InterpolationAxis", ANATOMY_AXIAL, emap_interp_axis);

}
