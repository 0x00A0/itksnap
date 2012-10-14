#ifndef QTREQUESTDELEGATES_H
#define QTREQUESTDELEGATES_H

#include <QWidget>
#include "UIReporterDelegates.h"

/**
  An implementation of a viewport reporter for Qt.

  CAVEAT: the reported must be destroyed before the client widget
  */
class QtViewportReporter : public ViewportSizeReporter
{
public:

  irisITKObjectMacro(QtViewportReporter, ViewportSizeReporter)

  /** Set the widget that we report on */
  void SetClientWidget(QWidget *widget);

  bool CanReportSize();

  Vector2ui GetViewportSize();

protected:

  QtViewportReporter();
  virtual ~QtViewportReporter();

  QWidget *m_ClientWidget;


  /** This helper class intercepts resize events from the widget */
  class EventFilter : public QObject
  {
  public:
    bool eventFilter(QObject *object, QEvent *event);
    QtViewportReporter *m_Owner;
  };

  EventFilter *m_Filter;

};

#endif // QTREQUESTDELEGATES_H
