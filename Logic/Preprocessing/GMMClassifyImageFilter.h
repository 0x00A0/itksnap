#ifndef GMMCLASSIFYIMAGEFILTER_H
#define GMMCLASSIFYIMAGEFILTER_H

#include "itkImageToImageFilter.h"
#include "GaussianMixtureModel.h"

/**
 * @brief A class that takes multiple multi-component images and uses a
 * Gaussian mixture model to combine them into a single probability map.
 */
template <class TInputImage, class TOutputImage>
class GMMClassifyImageFilter :
    public itk::ImageToImageFilter<TInputImage, TOutputImage>
{
public:

  /** Pixel Type of the input image */
  typedef TInputImage                                    InputImageType;
  typedef typename InputImageType::PixelType             InputPixelType;
  typedef typename InputImageType::InternalPixelType InputComponentType;

  /** Pixel Type of the output image */
  typedef TOutputImage                                  OutputImageType;
  typedef typename OutputImageType::PixelType           OutputPixelType;
  typedef typename OutputImageType::RegionType    OutputImageRegionType;
  typedef typename OutputImageType::Pointer          OutputImagePointer;

  /** Standard class typedefs. */
  typedef GMMClassifyImageFilter                                   Self;
  typedef itk::ImageToImageFilter<InputImageType,
                                       OutputImageType>      Superclass;
  typedef itk::SmartPointer<Self>                               Pointer;
  typedef itk::SmartPointer<const Self>                    ConstPointer;


  /** Method for creation through the object factory. */
  itkNewMacro(Self)

  /** Image dimension. */
  itkStaticConstMacro(ImageDimension, unsigned int,
                      TInputImage::ImageDimension);

  /** Set the mixture model */
  void SetMixtureModel(GaussianMixtureModel *model);

protected:

  GMMClassifyImageFilter();
  virtual ~GMMClassifyImageFilter();

  void PrintSelf(std::ostream& os, itk::Indent indent) const;

  void ThreadedGenerateData(const OutputImageRegionType &outputRegionForThread,
                            itk::ThreadIdType threadId);

  GaussianMixtureModel *m_MixtureModel;
};

#ifndef ITK_MANUAL_INSTANTIATION
#include "GMMClassifyImageFilter.txx"
#endif


#endif // GMMCLASSIFYIMAGEFILTER_H
