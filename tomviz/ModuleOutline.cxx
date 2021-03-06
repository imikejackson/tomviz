/******************************************************************************
  This source file is part of the tomviz project.

  Copyright Kitware, Inc.

  This source code is released under the New BSD License, (the "License").

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

******************************************************************************/
#include "ModuleOutline.h"

#include "DataSource.h"
#include "Utilities.h"
#include "pqProxiesWidget.h"
#include "vtkNew.h"
#include "vtkSMParaViewPipelineControllerWithRendering.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMSessionProxyManager.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMViewProxy.h"
#include "vtkSmartPointer.h"
#include <pqColorChooserButton.h>
#include <vtkGridAxes3DActor.h>
#include <vtkPVRenderView.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkTextProperty.h>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace tomviz {

using pugi::xml_attribute;
using pugi::xml_node;

ModuleOutline::ModuleOutline(QObject* parentObject) : Module(parentObject)
{
}

ModuleOutline::~ModuleOutline()
{
  finalize();
}

QIcon ModuleOutline::icon() const
{
  return QIcon(":/icons/pqProbeLocation.png");
}

bool ModuleOutline::initialize(DataSource* data, vtkSMViewProxy* vtkView)
{
  if (!Module::initialize(data, vtkView)) {
    return false;
  }

  vtkNew<vtkSMParaViewPipelineControllerWithRendering> controller;

  vtkSMSessionProxyManager* pxm = data->producer()->GetSessionProxyManager();

  // Create the outline filter.
  vtkSmartPointer<vtkSMProxy> proxy;
  proxy.TakeReference(pxm->NewProxy("filters", "OutlineFilter"));

  m_outlineFilter = vtkSMSourceProxy::SafeDownCast(proxy);
  Q_ASSERT(m_outlineFilter);
  controller->PreInitializeProxy(m_outlineFilter);
  vtkSMPropertyHelper(m_outlineFilter, "Input").Set(data->producer());
  controller->PostInitializeProxy(m_outlineFilter);
  controller->RegisterPipelineProxy(m_outlineFilter);

  // Create the representation for it.
  m_outlineRepresentation = controller->Show(m_outlineFilter, 0, vtkView);
  vtkSMPropertyHelper(m_outlineRepresentation, "Position")
    .Set(data->displayPosition(), 3);
  Q_ASSERT(m_outlineRepresentation);
  // vtkSMPropertyHelper(OutlineRepresentation,
  //                    "Representation").Set("Outline");
  m_outlineRepresentation->UpdateVTKObjects();

  // Give the proxy a friendly name for the GUI/Python world.
  if (auto p = convert<pqProxy*>(proxy)) {
    p->rename(label());
  }

  // Init the grid axes
  initializeGridAxes(data, vtkView);
  updateGridAxesColor(offWhite);

  return true;
}

bool ModuleOutline::finalize()
{
  vtkNew<vtkSMParaViewPipelineControllerWithRendering> controller;
  controller->UnRegisterProxy(m_outlineRepresentation);
  controller->UnRegisterProxy(m_outlineFilter);

  if (m_view) {
    m_view->GetRenderer()->RemoveActor(m_gridAxes.Get());
  }

  m_outlineFilter = nullptr;
  m_outlineRepresentation = nullptr;
  return true;
}

bool ModuleOutline::serialize(pugi::xml_node& ns) const
{
  xml_node rootNode = ns.append_child("properties");

  xml_node visibilityNode = rootNode.append_child("visibility");
  visibilityNode.append_attribute("enabled") = visibility();

  xml_node gridAxesNode = rootNode.append_child("grid_axes");
  gridAxesNode.append_attribute("enabled") = m_gridAxes->GetVisibility() > 0;
  gridAxesNode.append_attribute("grid") = m_gridAxes->GetGenerateGrid();

  xml_node color = gridAxesNode.append_child("color");
  double rgb[3];
  m_gridAxes->GetProperty()->GetDiffuseColor(rgb);
  color.append_attribute("r") = rgb[0];
  color.append_attribute("g") = rgb[1];
  color.append_attribute("b") = rgb[2];

  return true;
}

bool ModuleOutline::deserialize(const pugi::xml_node& ns)
{
  xml_node rootNode = ns.child("properties");
  if (!rootNode) {
    return false;
  }

  xml_node node = rootNode.child("visibility");
  if (node) {
    xml_attribute att = node.attribute("enabled");
    if (att) {
      setVisibility(att.as_bool());
    }
  }

  node = rootNode.child("grid_axes");
  if (node) {
    xml_attribute att = node.attribute("enabled");
    if (att) {
      m_gridAxes->SetVisibility(att.as_bool() ? 1 : 0);
      m_axesVisibility = att.as_bool();
    }
    att = node.attribute("grid");
    if (att) {
      m_gridAxes->SetGenerateGrid(att.as_bool());
    }
    xml_node color = node.child("color");
    if (color) {
      double rgb[3];
      att = color.attribute("r");
      if (att) {
        rgb[0] = att.as_double();
      }
      att = color.attribute("g");
      if (att) {
        rgb[1] = att.as_double();
      }
      att = color.attribute("b");
      if (att) {
        rgb[2] = att.as_double();
      }
      updateGridAxesColor(rgb);
    }
  }

  return Module::deserialize(ns);
}

bool ModuleOutline::setVisibility(bool val)
{
  Q_ASSERT(m_outlineRepresentation);
  vtkSMPropertyHelper(m_outlineRepresentation, "Visibility").Set(val ? 1 : 0);
  m_outlineRepresentation->UpdateVTKObjects();
  if (!val || m_axesVisibility) {
    m_gridAxes->SetVisibility(val ? 1 : 0);
  }
  return true;
}

bool ModuleOutline::visibility() const
{
  if (m_outlineRepresentation) {
    return vtkSMPropertyHelper(m_outlineRepresentation, "Visibility")
             .GetAsInt() != 0;
  } else {
    return false;
  }
}

void ModuleOutline::addToPanel(QWidget* panel)
{
  Q_ASSERT(panel && m_outlineRepresentation);

  if (panel->layout()) {
    delete panel->layout();
  }

  QHBoxLayout* layout = new QHBoxLayout;
  QLabel* label = new QLabel("Color");
  layout->addWidget(label);
  layout->addStretch();
  pqColorChooserButton* colorSelector = new pqColorChooserButton(panel);
  colorSelector->setShowAlphaChannel(false);
  layout->addWidget(colorSelector);

  // Show Grid?
  QHBoxLayout* showGridLayout = new QHBoxLayout;
  QCheckBox* showGrid = new QCheckBox(QString("Show Grid"));
  showGrid->setChecked(m_gridAxes->GetGenerateGrid());

  connect(showGrid, &QCheckBox::stateChanged, this, [this](int state) {
    m_gridAxes->SetGenerateGrid(state == Qt::Checked);
    emit renderNeeded();
  });

  showGridLayout->addWidget(showGrid);

  // Show Axes?
  QHBoxLayout* showAxesLayout = new QHBoxLayout;
  QCheckBox* showAxes = new QCheckBox(QString("Show Axes"));
  showAxes->setChecked(m_gridAxes->GetVisibility());
  // Disable "Show Grid" if axes not enabled
  if (!showAxes->isChecked()) {
    showGrid->setEnabled(false);
  }
  connect(showAxes, &QCheckBox::stateChanged, this,
          [this, showGrid](int state) {
            m_gridAxes->SetVisibility(state == Qt::Checked);
            m_axesVisibility = state == Qt::Checked;
            // Uncheck "Show Grid" and disable it
            if (state == Qt::Unchecked) {
              showGrid->setChecked(false);
              showGrid->setEnabled(false);
            } else {
              showGrid->setEnabled(true);
            }

            emit renderNeeded();
          });
  showAxesLayout->addWidget(showAxes);

  QVBoxLayout* panelLayout = new QVBoxLayout;
  panelLayout->addItem(layout);
  panelLayout->addItem(showAxesLayout);
  panelLayout->addItem(showGridLayout);
  panelLayout->addStretch();
  panel->setLayout(panelLayout);

  m_links.addPropertyLink(colorSelector, "chosenColorRgbF",
                          SIGNAL(chosenColorChanged(const QColor&)),
                          m_outlineRepresentation,
                          m_outlineRepresentation->GetProperty("DiffuseColor"));

  connect(colorSelector, &pqColorChooserButton::chosenColorChanged,
          [this](const QColor& color) {
            double rgb[3];
            rgb[0] = color.redF();
            rgb[1] = color.greenF();
            rgb[2] = color.blueF();
            updateGridAxesColor(rgb);

          });
  connect(colorSelector, &pqColorChooserButton::chosenColorChanged, this,
          &ModuleOutline::dataUpdated);
}

void ModuleOutline::dataUpdated()
{
  m_links.accept();
  emit renderNeeded();
}

void ModuleOutline::dataSourceMoved(double newX, double newY, double newZ)
{
  double pos[3] = { newX, newY, newZ };
  vtkSMPropertyHelper(m_outlineRepresentation, "Position").Set(pos, 3);
  m_outlineRepresentation->UpdateVTKObjects();
  m_gridAxes->SetPosition(newX, newY, newZ);
}

//-----------------------------------------------------------------------------
bool ModuleOutline::isProxyPartOfModule(vtkSMProxy* proxy)
{
  return (proxy == m_outlineFilter.Get()) ||
         (proxy == m_outlineRepresentation.Get());
}

std::string ModuleOutline::getStringForProxy(vtkSMProxy* proxy)
{
  if (proxy == m_outlineFilter.Get()) {
    return "Outline";
  } else if (proxy == m_outlineRepresentation.Get()) {
    return "Representation";
  } else {
    qWarning("Unknown proxy passed to module outline in save animation");
    return "";
  }
}

vtkSMProxy* ModuleOutline::getProxyForString(const std::string& str)
{
  if (str == "Outline") {
    return m_outlineFilter.Get();
  } else if (str == "Representation") {
    return m_outlineRepresentation.Get();
  } else {
    return nullptr;
  }
}

void ModuleOutline::updateGridAxesBounds(DataSource* dataSource)
{
  Q_ASSERT(dataSource);
  double bounds[6];
  dataSource->getBounds(bounds);
  m_gridAxes->SetGridBounds(bounds);
}
void ModuleOutline::initializeGridAxes(DataSource* data,
                                       vtkSMViewProxy* vtkView)
{

  updateGridAxesBounds(data);
  m_gridAxes->SetVisibility(0);
  m_gridAxes->SetGenerateGrid(false);

  // Work around a bug in vtkGridAxes3DActor. GetProperty() returns the
  // vtkProperty associated with a single face, so to get a property associated
  // with all the faces, we need to create a new one and set it.
  vtkNew<vtkProperty> prop;
  prop->DeepCopy(m_gridAxes->GetProperty());
  m_gridAxes->SetProperty(prop.Get());

  // Set mask to show labels on all axes
  m_gridAxes->SetLabelMask(vtkGridAxes3DActor::LabelMasks::MIN_X |
                           vtkGridAxes3DActor::LabelMasks::MIN_Y |
                           vtkGridAxes3DActor::LabelMasks::MIN_Z |
                           vtkGridAxes3DActor::LabelMasks::MAX_X |
                           vtkGridAxes3DActor::LabelMasks::MAX_Y |
                           vtkGridAxes3DActor::LabelMasks::MAX_Z);

  // Set mask to render all faces
  m_gridAxes->SetFaceMask(vtkGridAxes3DActor::FaceMasks::MAX_XY |
                          vtkGridAxes3DActor::FaceMasks::MAX_YZ |
                          vtkGridAxes3DActor::FaceMasks::MAX_ZX |
                          vtkGridAxes3DActor::FaceMasks::MIN_XY |
                          vtkGridAxes3DActor::FaceMasks::MIN_YZ |
                          vtkGridAxes3DActor::FaceMasks::MIN_ZX);

  // Enable front face culling
  prop->SetFrontfaceCulling(1);

  // Disable back face culling
  prop->SetBackfaceCulling(0);

  // Set the titles
  updateGridAxesUnit(data);

  m_view = vtkPVRenderView::SafeDownCast(vtkView->GetClientSideView());
  m_view->GetRenderer()->AddActor(m_gridAxes.Get());

  connect(data, &DataSource::dataPropertiesChanged, this, [this]() {
    auto dataSource = qobject_cast<DataSource*>(sender());
    updateGridAxesBounds(dataSource);
    updateGridAxesUnit(dataSource);
    dataSource->producer()->MarkModified(nullptr);
    dataSource->producer()->UpdatePipeline();
    emit renderNeeded();

  });
}

void ModuleOutline::updateGridAxesColor(double* color)
{
  for (int i = 0; i < 6; i++) {
    vtkNew<vtkTextProperty> prop;
    prop->SetColor(color);
    m_gridAxes->SetTitleTextProperty(i, prop.Get());
    m_gridAxes->SetLabelTextProperty(i, prop.Get());
  }
  m_gridAxes->GetProperty()->SetDiffuseColor(color);
  vtkSMPropertyHelper(m_outlineRepresentation, "DiffuseColor").Set(color, 3);
  m_outlineRepresentation->UpdateVTKObjects();
}

void ModuleOutline::updateGridAxesUnit(DataSource* dataSource)
{
  QString xTitle = QString("X (%1)").arg(dataSource->getUnits(0));
  QString yTitle = QString("Y (%1)").arg(dataSource->getUnits(1));
  QString zTitle = QString("Z (%1)").arg(dataSource->getUnits(2));
  m_gridAxes->SetXTitle(xTitle.toUtf8().data());
  m_gridAxes->SetYTitle(yTitle.toUtf8().data());
  m_gridAxes->SetZTitle(zTitle.toUtf8().data());
}

} // end of namespace tomviz
