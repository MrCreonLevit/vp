// Viewpoints (MIT License) - See LICENSE file
#include "ControlPanel.h"
#include "Normalize.h"
#include "Brush.h"
#include "MainFrame.h"
#include "WebGPUCanvas.h"  // for SymbolName, SYMBOL_COUNT
#include "ColorMap.h"
#include <wx/statline.h>
#include <wx/colordlg.h>
#ifdef __WXMAC__
#include "ColorPanelHelper.h"
#endif
#include <cmath>

// ============================================================
// PlotTab
// ============================================================

PlotTab::PlotTab(wxWindow* parent, int plotIndex, int row, int col)
    : wxScrolledWindow(parent)
    , m_plotIndex(plotIndex)
{
    SetScrollRate(0, 10);
    CreateControls(row, col);
}

void PlotTab::CreateControls(int row, int col) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(this, wxID_ANY,
        wxString::Format("Plot %d,%d", row, col));
    auto font = title->GetFont();
    font.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(font);
    sizer->Add(title, 0, wxALL, 8);

    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    auto* randBtn = new wxButton(this, wxID_ANY, "Randomize Axes",
                                  wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    sizer->Add(randBtn, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    randBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (onRandomizeAxes) onRandomizeAxes(m_plotIndex);
    });

    auto* xRow = new wxBoxSizer(wxHORIZONTAL);
    xRow->Add(new wxStaticText(this, wxID_ANY, "X-axis"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_xLock = new wxCheckBox(this, wxID_ANY, "Link");
    xRow->Add(m_xLock, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    xRow->Add(new wxStaticText(this, wxID_ANY, "Norm"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_xNorm = new wxChoice(this, wxID_ANY);
    for (const auto& name : AllNormModeNames()) m_xNorm->Append(name);
    m_xNorm->SetSelection(0);
    xRow->Add(m_xNorm, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(xRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    m_xAxis = new wxChoice(this, wxID_ANY);
    sizer->Add(m_xAxis, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 4);

    auto* yRow = new wxBoxSizer(wxHORIZONTAL);
    yRow->Add(new wxStaticText(this, wxID_ANY, "Y-axis"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_yLock = new wxCheckBox(this, wxID_ANY, "Link");
    yRow->Add(m_yLock, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    yRow->Add(new wxStaticText(this, wxID_ANY, "Norm"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_yNorm = new wxChoice(this, wxID_ANY);
    for (const auto& name : AllNormModeNames()) m_yNorm->Append(name);
    m_yNorm->SetSelection(0);
    yRow->Add(m_yNorm, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(yRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4);
    m_yAxis = new wxChoice(this, wxID_ANY);
    sizer->Add(m_yAxis, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 4);

    // Z-axis (optional 3D)
    auto* zRow = new wxBoxSizer(wxHORIZONTAL);
    zRow->Add(new wxStaticText(this, wxID_ANY, "Z-axis"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_zNorm = new wxChoice(this, wxID_ANY);
    for (const auto& name : AllNormModeNames()) m_zNorm->Append(name);
    m_zNorm->SetSelection(0);
    zRow->Add(m_zNorm, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(zRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4);
    m_zAxis = new wxChoice(this, wxID_ANY);
    m_zAxis->Append("(None)");
    m_zAxis->SetSelection(0);
    sizer->Add(m_zAxis, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

    // Rotation sliders (for 3D) with zero/spin/rock buttons
    const wxSize zeroBtnSize(20, 20);
    m_rotationLabel = new wxStaticText(this, wxID_ANY, "screen y: 0\u00B0");
    sizer->Add(m_rotationLabel, 0, wxLEFT, 8);
    auto* rotRow = new wxBoxSizer(wxHORIZONTAL);
    auto* zeroYBtn = new wxButton(this, wxID_ANY, "0",
                                   wxDefaultPosition, zeroBtnSize, wxBU_EXACTFIT);
    rotRow->Add(zeroYBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
    m_rotationSlider = new wxSlider(this, wxID_ANY, 0, -360, 360);
    rotRow->Add(m_rotationSlider, 1, wxALIGN_CENTER_VERTICAL);
    m_spinButton = new wxToggleButton(this, wxID_ANY, "Spin",
                                       wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    rotRow->Add(m_spinButton, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 2);
    m_rockButton = new wxToggleButton(this, wxID_ANY, "Rock",
                                       wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    rotRow->Add(m_rockButton, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 2);
    sizer->Add(rotRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

    m_rotationXLabel = new wxStaticText(this, wxID_ANY, "screen x: 0\u00B0");
    sizer->Add(m_rotationXLabel, 0, wxLEFT, 8);
    auto* rotXRow = new wxBoxSizer(wxHORIZONTAL);
    auto* zeroXBtn = new wxButton(this, wxID_ANY, "0",
                                   wxDefaultPosition, zeroBtnSize, wxBU_EXACTFIT);
    rotXRow->Add(zeroXBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
    m_rotationXSlider = new wxSlider(this, wxID_ANY, 0, -360, 360);
    rotXRow->Add(m_rotationXSlider, 1, wxALIGN_CENTER_VERTICAL);
    m_spinXButton = new wxToggleButton(this, wxID_ANY, "Spin",
                                        wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    rotXRow->Add(m_spinXButton, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 2);
    m_rockXButton = new wxToggleButton(this, wxID_ANY, "Rock",
                                        wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    rotXRow->Add(m_rockXButton, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 2);
    sizer->Add(rotXRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    m_showUnselected = new wxCheckBox(this, wxID_ANY, "Show unselected");
    m_showUnselected->SetValue(true);
    sizer->Add(m_showUnselected, 0, wxLEFT | wxRIGHT | wxTOP, 8);

    m_showGridLines = new wxCheckBox(this, wxID_ANY, "Grid lines");
    m_showGridLines->SetValue(false);
    sizer->Add(m_showGridLines, 0, wxLEFT | wxRIGHT | wxTOP, 8);

    m_showHistograms = new wxCheckBox(this, wxID_ANY, "Histograms");
    m_showHistograms->SetValue(true);
    sizer->Add(m_showHistograms, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 8);

    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    m_pointSizeLabel = new wxStaticText(this, wxID_ANY, "Point Size: 6.0");
    sizer->Add(m_pointSizeLabel, 0, wxLEFT | wxTOP, 8);
    m_pointSizeSlider = new wxSlider(this, wxID_ANY, 60, 5, 300);
    sizer->Add(m_pointSizeSlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    m_opacityLabel = new wxStaticText(this, wxID_ANY, "Opacity: 5%");
    sizer->Add(m_opacityLabel, 0, wxLEFT | wxTOP, 8);
    m_opacitySlider = new wxSlider(this, wxID_ANY, 5, 1, 100);
    sizer->Add(m_opacitySlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    m_histBinsLabel = new wxStaticText(this, wxID_ANY, "Hist Bins: 64");
    sizer->Add(m_histBinsLabel, 0, wxLEFT | wxTOP, 8);
    m_histBinsSlider = new wxSlider(this, wxID_ANY, 64, 2, 512);
    sizer->Add(m_histBinsSlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    m_selectionLabel = new wxStaticText(this, wxID_ANY, "No selection");
    sizer->Add(m_selectionLabel, 0, wxLEFT | wxTOP, 8);

    auto* selBtnSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* clearBtn = new wxButton(this, wxID_ANY, "Clear (C)",
                                   wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* invertBtn = new wxButton(this, wxID_ANY, "Invert (I)",
                                    wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* killBtn = new wxButton(this, wxID_ANY, "Kill (K)",
                                  wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    selBtnSizer->Add(clearBtn, 1, wxRIGHT, 4);
    selBtnSizer->Add(invertBtn, 1, wxRIGHT, 4);
    selBtnSizer->Add(killBtn, 1);
    sizer->Add(selBtnSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    auto* saveSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* saveAllBtn = new wxButton(this, wxID_ANY, "Save All",
                                     wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* saveSelBtn = new wxButton(this, wxID_ANY, "Save Selected",
                                     wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    saveSizer->Add(saveAllBtn, 1, wxRIGHT, 4);
    saveSizer->Add(saveSelBtn, 1);
    sizer->Add(saveSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    sizer->AddStretchSpacer();
    SetSizer(sizer);

    clearBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (onClearSelection) onClearSelection();
    });
    invertBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (onInvertSelection) onInvertSelection();
    });
    killBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (onKillSelected) onKillSelected();
    });
    saveAllBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (onSaveData) onSaveData(false);
    });
    saveSelBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (onSaveData) onSaveData(true);
    });
    m_xAxis->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        if (!m_suppress && onAxisChanged)
            onAxisChanged(m_plotIndex, m_xAxis->GetSelection(), m_yAxis->GetSelection());
    });
    m_yAxis->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        if (!m_suppress && onAxisChanged)
            onAxisChanged(m_plotIndex, m_xAxis->GetSelection(), m_yAxis->GetSelection());
    });
    m_xNorm->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        if (!m_suppress && onNormChanged)
            onNormChanged(m_plotIndex, m_xNorm->GetSelection(), m_yNorm->GetSelection());
    });
    m_yNorm->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        if (!m_suppress && onNormChanged)
            onNormChanged(m_plotIndex, m_xNorm->GetSelection(), m_yNorm->GetSelection());
    });
    m_showUnselected->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        if (onShowUnselectedChanged)
            onShowUnselectedChanged(m_plotIndex, m_showUnselected->GetValue());
    });
    m_showGridLines->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        if (onGridLinesChanged)
            onGridLinesChanged(m_plotIndex, m_showGridLines->GetValue());
    });
    m_xLock->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        if (onAxisLockChanged)
            onAxisLockChanged(m_plotIndex, m_xLock->GetValue(), m_yLock->GetValue());
    });
    m_yLock->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        if (onAxisLockChanged)
            onAxisLockChanged(m_plotIndex, m_xLock->GetValue(), m_yLock->GetValue());
    });
    m_zAxis->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        if (!m_suppress && onZAxisChanged) {
            int sel = m_zAxis->GetSelection();
            int zCol = (sel == 0) ? -1 : sel - 1;  // 0="(None)", 1+=column index
            onZAxisChanged(m_plotIndex, zCol, m_zNorm->GetSelection());
        }
    });
    m_zNorm->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        if (!m_suppress && onZAxisChanged) {
            int sel = m_zAxis->GetSelection();
            int zCol = (sel == 0) ? -1 : sel - 1;
            onZAxisChanged(m_plotIndex, zCol, m_zNorm->GetSelection());
        }
    });
    m_rotationSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        if (m_suppress) return;
        float angle = static_cast<float>(m_rotationSlider->GetValue());
        m_spinAngle = angle;
        m_rotationLabel->SetLabel(wxString::Format("screen y: %d\u00B0", (int)angle));
        if (onRotationChanged) onRotationChanged(m_plotIndex, angle);
    });
    m_rotationXSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        if (m_suppress) return;
        float angle = static_cast<float>(m_rotationXSlider->GetValue());
        m_spinXAngle = angle;
        m_rotationXLabel->SetLabel(wxString::Format("screen x: %d\u00B0", (int)angle));
        if (onRotationXChanged) onRotationXChanged(m_plotIndex, angle);
    });
    m_spinButton->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) {
        m_spinning = m_spinButton->GetValue();
        if (m_spinning) {
            m_spinAngle = static_cast<float>(m_rotationSlider->GetValue());
            m_rocking = false;
            m_rockButton->SetValue(false);
        }
    });
    m_rockButton->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) {
        m_rocking = m_rockButton->GetValue();
        if (m_rocking) {
            m_spinAngle = static_cast<float>(m_rotationSlider->GetValue());
            m_rockCenter = m_spinAngle;
            m_rockPhase = 0.0f;
            m_spinning = false;
            m_spinButton->SetValue(false);
        }
    });
    zeroYBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        m_spinning = false; m_rocking = false;
        m_spinButton->SetValue(false); m_rockButton->SetValue(false);
        m_spinAngle = 0.0f;
        m_rotationSlider->SetValue(0);
        m_rotationLabel->SetLabel("screen y: 0\u00B0");
        if (onRotationZeroed) onRotationZeroed(m_plotIndex, true, false);
    });
    m_spinXButton->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) {
        m_spinningX = m_spinXButton->GetValue();
        if (m_spinningX) {
            m_spinXAngle = static_cast<float>(m_rotationXSlider->GetValue());
            m_rockingX = false;
            m_rockXButton->SetValue(false);
        }
    });
    m_rockXButton->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) {
        m_rockingX = m_rockXButton->GetValue();
        if (m_rockingX) {
            m_spinXAngle = static_cast<float>(m_rotationXSlider->GetValue());
            m_rockXCenter = m_spinXAngle;
            m_rockXPhase = 0.0f;
            m_spinningX = false;
            m_spinXButton->SetValue(false);
        }
    });
    zeroXBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        m_spinningX = false; m_rockingX = false;
        m_spinXButton->SetValue(false); m_rockXButton->SetValue(false);
        m_spinXAngle = 0.0f;
        m_rotationXSlider->SetValue(0);
        m_rotationXLabel->SetLabel("screen x: 0\u00B0");
        if (onRotationZeroed) onRotationZeroed(m_plotIndex, false, true);
    });
    m_showHistograms->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        if (onShowHistogramsChanged)
            onShowHistogramsChanged(m_plotIndex, m_showHistograms->GetValue());
    });
    m_pointSizeSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        if (m_suppress) return;
        float val = m_pointSizeSlider->GetValue() / 10.0f;
        m_pointSizeLabel->SetLabel(wxString::Format("Point Size: %.1f", val));
        if (onPointSizeChanged) onPointSizeChanged(m_plotIndex, val);
    });
    m_opacitySlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        if (m_suppress) return;
        int val = m_opacitySlider->GetValue();
        m_opacityLabel->SetLabel(wxString::Format("Opacity: %d%%", val));
        if (onOpacityChanged) onOpacityChanged(m_plotIndex, static_cast<float>(val) / 100.0f);
    });
    m_histBinsSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        if (m_suppress) return;
        int val = m_histBinsSlider->GetValue();
        m_histBinsLabel->SetLabel(wxString::Format("Hist Bins: %d", val));
        if (onHistBinsChanged) onHistBinsChanged(m_plotIndex, val);
    });
}

void PlotTab::SetColumns(const std::vector<std::string>& names) {
    m_suppress = true;
    int xSel = m_xAxis->GetSelection();
    int ySel = m_yAxis->GetSelection();
    int zSel = m_zAxis->GetSelection();
    m_xAxis->Clear(); m_yAxis->Clear();
    m_zAxis->Clear(); m_zAxis->Append("(None)");
    for (const auto& name : names) {
        m_xAxis->Append(name); m_yAxis->Append(name); m_zAxis->Append(name);
    }
    if (xSel >= 0 && xSel < (int)names.size()) m_xAxis->SetSelection(xSel);
    else if (!names.empty()) m_xAxis->SetSelection(0);
    if (ySel >= 0 && ySel < (int)names.size()) m_yAxis->SetSelection(ySel);
    else if (names.size() > 1) m_yAxis->SetSelection(1);
    if (zSel >= 0 && zSel < (int)m_zAxis->GetCount()) m_zAxis->SetSelection(zSel);
    else m_zAxis->SetSelection(0);
    m_suppress = false;
}

void PlotTab::SyncFromConfig(const PlotConfig& cfg) {
    m_suppress = true;
    if ((int)cfg.xCol < m_xAxis->GetCount()) m_xAxis->SetSelection(cfg.xCol);
    if ((int)cfg.yCol < m_yAxis->GetCount()) m_yAxis->SetSelection(cfg.yCol);
    m_xLock->SetValue(cfg.xLocked);
    m_yLock->SetValue(cfg.yLocked);
    if ((int)cfg.xNorm < m_xNorm->GetCount()) m_xNorm->SetSelection(static_cast<int>(cfg.xNorm));
    if ((int)cfg.yNorm < m_yNorm->GetCount()) m_yNorm->SetSelection(static_cast<int>(cfg.yNorm));
    // Z-axis: -1="(None)" maps to dropdown index 0, column N maps to index N+1
    m_zAxis->SetSelection(cfg.zCol < 0 ? 0 : std::min(cfg.zCol + 1, (int)m_zAxis->GetCount() - 1));
    if ((int)cfg.zNorm < m_zNorm->GetCount()) m_zNorm->SetSelection(static_cast<int>(cfg.zNorm));
    if (!m_spinning && !m_rocking) {
        m_spinAngle = cfg.rotationY;
        m_rotationSlider->SetValue(static_cast<int>(cfg.rotationY));
        m_rotationLabel->SetLabel(wxString::Format("screen y: %d\u00B0", (int)cfg.rotationY));
    }
    if (!m_spinningX && !m_rockingX) {
        m_spinXAngle = cfg.rotationX;
        m_rotationXSlider->SetValue(static_cast<int>(cfg.rotationX));
        m_rotationXLabel->SetLabel(wxString::Format("screen x: %d\u00B0", (int)cfg.rotationX));
    }
    m_spinButton->SetValue(m_spinning);
    m_rockButton->SetValue(m_rocking);
    m_spinXButton->SetValue(m_spinningX);
    m_rockXButton->SetValue(m_rockingX);
    m_showUnselected->SetValue(cfg.showUnselected);
    m_showGridLines->SetValue(cfg.showGridLines);
    m_showHistograms->SetValue(cfg.showHistograms);
    m_pointSizeSlider->SetValue(static_cast<int>(cfg.pointSize * 10));
    m_pointSizeLabel->SetLabel(wxString::Format("Point Size: %.1f", cfg.pointSize));
    m_opacitySlider->SetValue(static_cast<int>(cfg.opacity * 100));
    m_opacityLabel->SetLabel(wxString::Format("Opacity: %d%%", (int)(cfg.opacity * 100)));
    m_histBinsSlider->SetValue(cfg.histBins);
    m_histBinsLabel->SetLabel(wxString::Format("Hist Bins: %d", cfg.histBins));
    m_suppress = false;
}

// ============================================================
// ControlPanel
// ============================================================

ControlPanel::ControlPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(280, -1))
{
    SetMinSize(wxSize(280, -1));

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(this, wxID_ANY, "Plot Selection");
    auto titleFont = title->GetFont();
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    titleFont.SetPointSize(titleFont.GetPointSize() + 2);
    title->SetFont(titleFont);
    sizer->Add(title, 0, wxALL, 8);

    // Plot selector grid panel (rebuilt dynamically)
    m_selectorPanel = new wxPanel(this);
    sizer->Add(m_selectorPanel, 0, wxEXPAND | wxLEFT | wxRIGHT, 4);

    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4);

    // Simplebook for content pages (no built-in tab UI)
    m_book = new wxSimplebook(this);
    sizer->Add(m_book, 1, wxEXPAND | wxLEFT | wxRIGHT, 4);

    SetSizer(sizer);
    RebuildTabs(2, 2);
    m_ready = true;

    // Single spin timer drives all spinning PlotTabs
    m_spinTimer.SetOwner(this);
    Bind(wxEVT_TIMER, &ControlPanel::OnSpinTimer, this);
    m_lastSpinTime = wxGetLocalTimeMillis();
    m_spinTimer.Start(SPIN_INTERVAL_MS);
}

void ControlPanel::OnSpinTimer(wxTimerEvent&) {
    wxLongLong now = wxGetLocalTimeMillis();
    float dt = (now - m_lastSpinTime).ToDouble() / 1000.0;
    m_lastSpinTime = now;
    if (dt <= 0.0f || dt > 1.0f) dt = SPIN_INTERVAL_MS / 1000.0f; // sanity clamp
    for (auto* tab : m_plotTabs) {
        // Screen Y spin/rock
        if (tab->m_spinning) {
            tab->m_spinAngle += SPIN_SPEED * dt;
            if (tab->m_spinAngle > 360.0f) tab->m_spinAngle -= 720.0f;
        } else if (tab->m_rocking) {
            tab->m_rockPhase += 2.0f * 3.14159265f * dt;
            if (tab->m_rockPhase >= 2.0f * 3.14159265f)
                tab->m_rockPhase -= 2.0f * 3.14159265f;
            tab->m_spinAngle = tab->m_rockCenter + ROCK_AMPLITUDE * std::sin(tab->m_rockPhase);
        }
        if (tab->m_spinning || tab->m_rocking) {
            tab->m_rotationSlider->SetValue(static_cast<int>(tab->m_spinAngle));
            tab->m_rotationLabel->SetLabel(wxString::Format("screen y: %d\u00B0", (int)tab->m_spinAngle));
            if (onRotationChanged) onRotationChanged(tab->m_plotIndex, tab->m_spinAngle);
        }
        // Screen X spin/rock
        if (tab->m_spinningX) {
            tab->m_spinXAngle += SPIN_SPEED * dt;
            if (tab->m_spinXAngle > 360.0f) tab->m_spinXAngle -= 720.0f;
        } else if (tab->m_rockingX) {
            tab->m_rockXPhase += 2.0f * 3.14159265f * dt;
            if (tab->m_rockXPhase >= 2.0f * 3.14159265f)
                tab->m_rockXPhase -= 2.0f * 3.14159265f;
            tab->m_spinXAngle = tab->m_rockXCenter + ROCK_AMPLITUDE * std::sin(tab->m_rockXPhase);
        }
        if (tab->m_spinningX || tab->m_rockingX) {
            tab->m_rotationXSlider->SetValue(static_cast<int>(tab->m_spinXAngle));
            tab->m_rotationXLabel->SetLabel(wxString::Format("screen x: %d\u00B0", (int)tab->m_spinXAngle));
            if (onRotationXChanged) onRotationXChanged(tab->m_plotIndex, tab->m_spinXAngle);
        }
    }
}

void ControlPanel::RebuildTabs(int rows, int cols) {
    m_ready = false;  // suppress dialogs during rebuild

    float savedSize = m_pointSizeSlider ? m_pointSizeSlider->GetValue() : 6;
    int savedBins = m_histBinsSlider ? m_histBinsSlider->GetValue() : 64;
    int savedBrush = m_activeBrush;

    m_gridRows = rows;
    m_gridCols = cols;

    // Clear and destroy old book pages
    m_book->DeleteAllPages();
    m_plotTabs.clear();
    m_allPage = nullptr;
    m_allSubBook = nullptr;
    m_allPlotsBtn = nullptr;
    m_brushesBtn = nullptr;
    m_allXAxis = nullptr;
    m_allYAxis = nullptr;
    m_allZAxis = nullptr;
    m_allXNorm = nullptr;
    m_allYNorm = nullptr;
    m_allZNorm = nullptr;
    m_pointSizeSlider = nullptr;
    m_histBinsSlider = nullptr;
    m_selectionLabel = nullptr;
    m_brushSymbolChoice = nullptr;
    m_brushSizeSlider = nullptr;
    m_brushSizeLabel = nullptr;
    m_brushOpacitySlider = nullptr;
    m_allBrushButton = nullptr;

    int numPlots = rows * cols;
    for (int i = 0; i < numPlots; i++) {
        int r = i / cols;
        int c = i % cols;
        auto* tab = new PlotTab(m_book, i, r, c);
        m_book->AddPage(tab, "");
        m_plotTabs.push_back(tab);

        if (!m_columnNames.empty())
            tab->SetColumns(m_columnNames);

        tab->onRandomizeAxes = [this](int pi) {
            // Individual plot randomize → reset all "All Plots" axis dropdowns
            if (m_allXAxis) m_allXAxis->SetSelection(0);
            if (m_allYAxis) m_allYAxis->SetSelection(0);
            if (m_allZAxis) m_allZAxis->SetSelection(0);
            if (m_allXNorm) m_allXNorm->SetSelection(0);
            if (m_allYNorm) m_allYNorm->SetSelection(0);
            if (m_allZNorm) m_allZNorm->SetSelection(0);
            if (onRandomizeAxes) onRandomizeAxes(pi);
        };
        tab->onAxisChanged = [this](int pi, int x, int y) {
            if (m_allXAxis) m_allXAxis->SetSelection(0);
            if (m_allYAxis) m_allYAxis->SetSelection(0);
            if (onAxisChanged) onAxisChanged(pi, x, y);
        };
        tab->onAxisLockChanged = [this](int pi, bool xLock, bool yLock) {
            if (onAxisLockChanged) onAxisLockChanged(pi, xLock, yLock);
        };
        tab->onNormChanged = [this](int pi, int xn, int yn) {
            if (m_allXNorm) m_allXNorm->SetSelection(0);
            if (m_allYNorm) m_allYNorm->SetSelection(0);
            if (onNormChanged) onNormChanged(pi, xn, yn);
        };
        tab->onZAxisChanged = [this](int pi, int zCol, int zNorm) {
            if (m_allZAxis) m_allZAxis->SetSelection(0);
            if (m_allZNorm) m_allZNorm->SetSelection(0);
            if (onZAxisChanged) onZAxisChanged(pi, zCol, zNorm);
        };
        tab->onRotationChanged = [this](int pi, float angle) {
            if (onRotationChanged) onRotationChanged(pi, angle);
        };
        tab->onRotationXChanged = [this](int pi, float angle) {
            if (onRotationXChanged) onRotationXChanged(pi, angle);
        };
        tab->onRotationZeroed = [this](int pi, bool zeroY, bool zeroX) {
            if (onRotationZeroed) onRotationZeroed(pi, zeroY, zeroX);
        };
        tab->onShowUnselectedChanged = [this](int pi, bool show) {
            if (onShowUnselectedChanged) onShowUnselectedChanged(pi, show);
        };
        tab->onGridLinesChanged = [this](int pi, bool show) {
            if (onGridLinesChanged) onGridLinesChanged(pi, show);
        };
        tab->onShowHistogramsChanged = [this](int pi, bool show) {
            if (onShowHistogramsChanged) onShowHistogramsChanged(pi, show);
        };
        tab->onPointSizeChanged = [this](int pi, float size) {
            if (onPlotPointSizeChanged) onPlotPointSizeChanged(pi, size);
        };
        tab->onOpacityChanged = [this](int pi, float alpha) {
            if (onPlotOpacityChanged) onPlotOpacityChanged(pi, alpha);
        };
        tab->onHistBinsChanged = [this](int pi, int bins) {
            if (onPlotHistBinsChanged) onPlotHistBinsChanged(pi, bins);
        };
        tab->onClearSelection = [this]() {
            if (onClearSelection) onClearSelection();
        };
        tab->onInvertSelection = [this]() {
            if (onInvertSelection) onInvertSelection();
        };
        tab->onKillSelected = [this]() {
            if (onKillSelected) onKillSelected();
        };
        tab->onSaveData = [this](bool selectedOnly) {
            if (onSaveData) onSaveData(selectedOnly);
        };
    }

    // "Global" page (last)
    CreateAllPage();

    // Restore global state
    if (m_pointSizeSlider) m_pointSizeSlider->SetValue(savedSize);
    if (m_histBinsSlider) m_histBinsSlider->SetValue(savedBins);
    SelectBrush(savedBrush);

    // Rebuild selector buttons
    RebuildSelectorGrid();
    SelectPage(0);
    m_ready = true;
}

void ControlPanel::RebuildSelectorGrid() {
    m_selectorPanel->DestroyChildren();
    m_plotButtons.clear();

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    // Plot button grid
    auto* gridSizer = new wxGridSizer(m_gridRows, m_gridCols, 2, 2);
    int numPlots = m_gridRows * m_gridCols;
    for (int i = 0; i < numPlots; i++) {
        int r = i / m_gridCols;
        int c = i % m_gridCols;
        auto* btn = new wxButton(m_selectorPanel, wxID_ANY,
                                  wxString::Format("%d,%d", r, c),
                                  wxDefaultPosition, wxSize(-1, 24), wxBU_EXACTFIT);
        btn->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
            SelectPage(i);
            if (onTabSelected) onTabSelected(i);
        });
        gridSizer->Add(btn, 1, wxEXPAND);
        m_plotButtons.push_back(btn);
    }
    sizer->Add(gridSizer, 0, wxEXPAND | wxBOTTOM, 2);

    // Sub-page buttons (below the plot grid)
    m_allPlotsBtn = new wxButton(m_selectorPanel, wxID_ANY, "All Plots",
                                  wxDefaultPosition, wxSize(-1, 24), wxBU_EXACTFIT);
    m_allPlotsBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        int allIdx = static_cast<int>(m_plotTabs.size());
        SelectPage(allIdx);
        SelectAllSubPage(0);
        if (onAllSelected) onAllSelected();
    });
    sizer->Add(m_allPlotsBtn, 0, wxEXPAND | wxBOTTOM, 2);

    m_brushesBtn = new wxButton(m_selectorPanel, wxID_ANY, "Brushes && Colormaps",
                                 wxDefaultPosition, wxSize(-1, 24), wxBU_EXACTFIT);
    m_brushesBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        int allIdx = static_cast<int>(m_plotTabs.size());
        SelectPage(allIdx);
        SelectAllSubPage(1);
        if (onAllSelected) onAllSelected();
    });
    sizer->Add(m_brushesBtn, 0, wxEXPAND);

    m_selectorPanel->SetSizer(sizer);
    m_selectorPanel->Layout();
    GetSizer()->Layout();
}

void ControlPanel::SelectPage(int pageIndex) {
    m_selectedPage = pageIndex;
    int numPlots = static_cast<int>(m_plotTabs.size());

    if (pageIndex >= 0 && pageIndex <= numPlots) {
        m_book->SetSelection(pageIndex);
    }

    // Update button highlights
    wxColour activeBg(80, 120, 200);
    wxColour normalBg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);

    for (int i = 0; i < (int)m_plotButtons.size(); i++) {
        m_plotButtons[i]->SetBackgroundColour(i == pageIndex ? activeBg : normalBg);
        m_plotButtons[i]->Refresh();
    }
    // When switching away from the All page, reset both sub-page buttons
    if (pageIndex != numPlots) {
        if (m_allPlotsBtn) { m_allPlotsBtn->SetBackgroundColour(normalBg); m_allPlotsBtn->Refresh(); }
        if (m_brushesBtn) { m_brushesBtn->SetBackgroundColour(normalBg); m_brushesBtn->Refresh(); }
    }
}

void ControlPanel::SelectTab(int plotIndex) {
    SelectPage(plotIndex);
}

void ControlPanel::SetPlotConfig(int plotIndex, const PlotConfig& cfg) {
    if (plotIndex >= 0 && plotIndex < (int)m_plotTabs.size())
        m_plotTabs[plotIndex]->SyncFromConfig(cfg);
}

void ControlPanel::StopSpinRock(int plotIndex) {
    if (plotIndex >= 0 && plotIndex < (int)m_plotTabs.size()) {
        auto* tab = m_plotTabs[plotIndex];
        tab->m_spinning = false;
        tab->m_rocking = false;
        tab->m_spinAngle = 0.0f;
        tab->m_spinButton->SetValue(false);
        tab->m_rockButton->SetValue(false);
        tab->m_rotationSlider->SetValue(0);
        tab->m_rotationLabel->SetLabel("screen y: 0\u00B0");
        tab->m_spinningX = false;
        tab->m_rockingX = false;
        tab->m_spinXAngle = 0.0f;
        tab->m_spinXButton->SetValue(false);
        tab->m_rockXButton->SetValue(false);
        tab->m_rotationXSlider->SetValue(0);
        tab->m_rotationXLabel->SetLabel("screen x: 0\u00B0");
    }
}

void ControlPanel::SetColumns(const std::vector<std::string>& names) {
    m_columnNames = names;
    for (auto* tab : m_plotTabs) tab->SetColumns(names);
    // Update All Plots axis dropdowns (keep "(no change)" as first entry)
    if (m_allXAxis) {
        m_allXAxis->Clear(); m_allYAxis->Clear(); m_allZAxis->Clear();
        m_allXAxis->Append("(no change)");
        m_allYAxis->Append("(no change)");
        m_allZAxis->Append("(no change)");
        m_allZAxis->Append("(None)");
        for (const auto& name : names) {
            m_allXAxis->Append(name); m_allYAxis->Append(name); m_allZAxis->Append(name);
        }
        m_allXAxis->SetSelection(0);
        m_allYAxis->SetSelection(0);
        m_allZAxis->SetSelection(0);
    }
    // Update color variable dropdown
    if (m_colorVarChoice) {
        m_colorVarChoice->Clear();
        m_colorVarChoice->Append("(density)");
        for (const auto& name : names)
            m_colorVarChoice->Append(name);
        m_colorVarChoice->SetSelection(0);
    }
}

void ControlPanel::SetSelectionInfo(int selected, int total) {
    wxString text = selected > 0 ?
        wxString::Format("Selected: %d / %d", selected, total) : wxString("No selection");
    if (m_selectionLabel)
        m_selectionLabel->SetLabel(text);
    for (auto* tab : m_plotTabs) {
        if (tab->m_selectionLabel)
            tab->m_selectionLabel->SetLabel(text);
    }
}

void ControlPanel::CreateAllPage() {
    m_allPage = new wxPanel(m_book);
    auto* topSizer = new wxBoxSizer(wxVERTICAL);

    // Sub-book with two pages (buttons are in the selector grid)
    m_allSubBook = new wxSimplebook(m_allPage);
    topSizer->Add(m_allSubBook, 1, wxEXPAND);

    // ---- Page 0: "All Plots" (mirrors PlotTab layout) ----
    auto* plotsPage = new wxScrolledWindow(m_allSubBook);
    plotsPage->SetScrollRate(0, 10);
    auto* pSizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(plotsPage, wxID_ANY, "All Plots");
    auto font = title->GetFont();
    font.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(font);
    pSizer->Add(title, 0, wxALL, 8);

    pSizer->Add(new wxStaticLine(plotsPage), 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    auto* randBtn = new wxButton(plotsPage, wxID_ANY, "Randomize Axes",
                                  wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    pSizer->Add(randBtn, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    randBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        for (int i = 0; i < (int)m_plotTabs.size(); i++)
            if (onRandomizeAxes) onRandomizeAxes(i);
        // Reset all axis dropdowns since plots now have different axes
        if (m_allXAxis) m_allXAxis->SetSelection(0);
        if (m_allYAxis) m_allYAxis->SetSelection(0);
        if (m_allZAxis) m_allZAxis->SetSelection(0);
        if (m_allXNorm) m_allXNorm->SetSelection(0);
        if (m_allYNorm) m_allYNorm->SetSelection(0);
        if (m_allZNorm) m_allZNorm->SetSelection(0);
    });

    // X-axis
    auto* xRow = new wxBoxSizer(wxHORIZONTAL);
    xRow->Add(new wxStaticText(plotsPage, wxID_ANY, "X-axis"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    auto* allXLock = new wxCheckBox(plotsPage, wxID_ANY, "Link");
    xRow->Add(allXLock, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    xRow->Add(new wxStaticText(plotsPage, wxID_ANY, "Norm"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_allXNorm = new wxChoice(plotsPage, wxID_ANY);
    m_allXNorm->Append("(no change)");
    for (const auto& name : AllNormModeNames()) m_allXNorm->Append(name);
    m_allXNorm->SetSelection(0);
    xRow->Add(m_allXNorm, 0, wxALIGN_CENTER_VERTICAL);
    pSizer->Add(xRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    m_allXAxis = new wxChoice(plotsPage, wxID_ANY);
    pSizer->Add(m_allXAxis, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 4);

    // Y-axis
    auto* yRow = new wxBoxSizer(wxHORIZONTAL);
    yRow->Add(new wxStaticText(plotsPage, wxID_ANY, "Y-axis"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    auto* allYLock = new wxCheckBox(plotsPage, wxID_ANY, "Link");
    yRow->Add(allYLock, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    yRow->Add(new wxStaticText(plotsPage, wxID_ANY, "Norm"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_allYNorm = new wxChoice(plotsPage, wxID_ANY);
    m_allYNorm->Append("(no change)");
    for (const auto& name : AllNormModeNames()) m_allYNorm->Append(name);
    m_allYNorm->SetSelection(0);
    yRow->Add(m_allYNorm, 0, wxALIGN_CENTER_VERTICAL);
    pSizer->Add(yRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4);
    m_allYAxis = new wxChoice(plotsPage, wxID_ANY);
    pSizer->Add(m_allYAxis, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 4);

    // Z-axis
    auto* zRow = new wxBoxSizer(wxHORIZONTAL);
    zRow->Add(new wxStaticText(plotsPage, wxID_ANY, "Z-axis"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_allZNorm = new wxChoice(plotsPage, wxID_ANY);
    m_allZNorm->Append("(no change)");
    for (const auto& name : AllNormModeNames()) m_allZNorm->Append(name);
    m_allZNorm->SetSelection(0);
    zRow->Add(m_allZNorm, 0, wxALIGN_CENTER_VERTICAL);
    pSizer->Add(zRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4);
    m_allZAxis = new wxChoice(plotsPage, wxID_ANY);
    m_allZAxis->Append("(no change)");
    m_allZAxis->Append("(None)");
    m_allZAxis->SetSelection(0);
    pSizer->Add(m_allZAxis, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

    // Rotation sliders
    const wxSize zeroBtnSize(20, 20);
    auto* allRotLabel = new wxStaticText(plotsPage, wxID_ANY, "screen y: 0\u00B0");
    pSizer->Add(allRotLabel, 0, wxLEFT, 8);
    auto* rotRow = new wxBoxSizer(wxHORIZONTAL);
    auto* zeroYBtn = new wxButton(plotsPage, wxID_ANY, "0",
                                   wxDefaultPosition, zeroBtnSize, wxBU_EXACTFIT);
    rotRow->Add(zeroYBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
    auto* allRotSlider = new wxSlider(plotsPage, wxID_ANY, 0, -360, 360);
    rotRow->Add(allRotSlider, 1, wxALIGN_CENTER_VERTICAL);
    auto* allSpinBtn = new wxToggleButton(plotsPage, wxID_ANY, "Spin",
                                           wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    rotRow->Add(allSpinBtn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 2);
    auto* allRockBtn = new wxToggleButton(plotsPage, wxID_ANY, "Rock",
                                           wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    rotRow->Add(allRockBtn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 2);
    pSizer->Add(rotRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

    auto* allRotXLabel = new wxStaticText(plotsPage, wxID_ANY, "screen x: 0\u00B0");
    pSizer->Add(allRotXLabel, 0, wxLEFT, 8);
    auto* rotXRow = new wxBoxSizer(wxHORIZONTAL);
    auto* zeroXBtn = new wxButton(plotsPage, wxID_ANY, "0",
                                   wxDefaultPosition, zeroBtnSize, wxBU_EXACTFIT);
    rotXRow->Add(zeroXBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
    auto* allRotXSlider = new wxSlider(plotsPage, wxID_ANY, 0, -360, 360);
    rotXRow->Add(allRotXSlider, 1, wxALIGN_CENTER_VERTICAL);
    auto* allSpinXBtn = new wxToggleButton(plotsPage, wxID_ANY, "Spin",
                                            wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    rotXRow->Add(allSpinXBtn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 2);
    auto* allRockXBtn = new wxToggleButton(plotsPage, wxID_ANY, "Rock",
                                            wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    rotXRow->Add(allRockXBtn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 2);
    pSizer->Add(rotXRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

    pSizer->Add(new wxStaticLine(plotsPage), 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    auto* allShowUnselected = new wxCheckBox(plotsPage, wxID_ANY, "Show unselected");
    allShowUnselected->SetValue(true);
    pSizer->Add(allShowUnselected, 0, wxLEFT | wxRIGHT | wxTOP, 8);

    auto* allGridLines = new wxCheckBox(plotsPage, wxID_ANY, "Grid lines");
    allGridLines->SetValue(false);
    pSizer->Add(allGridLines, 0, wxLEFT | wxRIGHT | wxTOP, 8);

    auto* allHistograms = new wxCheckBox(plotsPage, wxID_ANY, "Histograms");
    allHistograms->SetValue(true);
    pSizer->Add(allHistograms, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 8);

    pSizer->Add(new wxStaticLine(plotsPage), 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    m_pointSizeLabel = new wxStaticText(plotsPage, wxID_ANY, "Point Size: 6.0");
    pSizer->Add(m_pointSizeLabel, 0, wxLEFT | wxTOP, 8);
    m_pointSizeSlider = new wxSlider(plotsPage, wxID_ANY, 60, 5, 300);
    pSizer->Add(m_pointSizeSlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    auto* allOpacityLabel = new wxStaticText(plotsPage, wxID_ANY, "Opacity: 5%");
    pSizer->Add(allOpacityLabel, 0, wxLEFT | wxTOP, 8);
    auto* allOpacitySlider = new wxSlider(plotsPage, wxID_ANY, 5, 1, 100);
    pSizer->Add(allOpacitySlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    m_histBinsLabel = new wxStaticText(plotsPage, wxID_ANY, "Hist Bins: 64");
    pSizer->Add(m_histBinsLabel, 0, wxLEFT | wxTOP, 8);
    m_histBinsSlider = new wxSlider(plotsPage, wxID_ANY, 64, 2, 512);
    pSizer->Add(m_histBinsSlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    pSizer->Add(new wxStaticLine(plotsPage), 0, wxEXPAND | wxALL, 8);

    // Additional All-only controls
    m_globalTooltipCheck = new wxCheckBox(plotsPage, wxID_ANY, "Hover shows datapoint details");
    m_globalTooltipCheck->SetValue(false);
    pSizer->Add(m_globalTooltipCheck, 0, wxLEFT, 8);

    auto* deferRedraws = new wxCheckBox(plotsPage, wxID_ANY, "Defer redraws - fast w/big data");
    deferRedraws->SetValue(false);
    pSizer->Add(deferRedraws, 0, wxLEFT | wxBOTTOM, 8);

    pSizer->Add(new wxStaticLine(plotsPage), 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    m_selectionLabel = new wxStaticText(plotsPage, wxID_ANY, "No selection");
    pSizer->Add(m_selectionLabel, 0, wxLEFT | wxTOP, 8);

    auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* clearBtn = new wxButton(plotsPage, wxID_ANY, "Clear (C)",
                                   wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* invertBtn = new wxButton(plotsPage, wxID_ANY, "Invert (I)",
                                    wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* killBtn = new wxButton(plotsPage, wxID_ANY, "Kill (K)",
                                  wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    btnSizer->Add(clearBtn, 1, wxRIGHT, 4);
    btnSizer->Add(invertBtn, 1, wxRIGHT, 4);
    btnSizer->Add(killBtn, 1);
    pSizer->Add(btnSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    auto* saveSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* saveAllBtn = new wxButton(plotsPage, wxID_ANY, "Save All",
                                     wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* saveSelBtn = new wxButton(plotsPage, wxID_ANY, "Save Selected",
                                     wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    saveSizer->Add(saveAllBtn, 1, wxRIGHT, 4);
    saveSizer->Add(saveSelBtn, 1);
    pSizer->Add(saveSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    // --- Event bindings for All Plots page ---
    // Axis/norm dropdowns have "(no change)" at index 0; skip if unspecified,
    // and preserve each plot's current value for the paired control.
    // Snapshot all per-plot values upfront before calling callbacks,
    // so that callbacks (which may call SetPlotConfig/SyncFromConfig)
    // can't alter other tabs' dropdown state mid-iteration.
    m_allXAxis->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        int xSel = m_allXAxis->GetSelection();
        if (xSel == 0) return;  // "(no change)"
        int xCol = xSel - 1;
        int ySel = m_allYAxis->GetSelection();
        std::vector<int> yCols(m_plotTabs.size());
        for (int i = 0; i < (int)m_plotTabs.size(); i++)
            yCols[i] = (ySel == 0) ? m_plotTabs[i]->m_yAxis->GetSelection() : ySel - 1;
        for (int i = 0; i < (int)m_plotTabs.size(); i++)
            if (onAxisChanged) onAxisChanged(i, xCol, yCols[i]);
    });
    m_allYAxis->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        int ySel = m_allYAxis->GetSelection();
        if (ySel == 0) return;
        int yCol = ySel - 1;
        int xSel = m_allXAxis->GetSelection();
        std::vector<int> xCols(m_plotTabs.size());
        for (int i = 0; i < (int)m_plotTabs.size(); i++)
            xCols[i] = (xSel == 0) ? m_plotTabs[i]->m_xAxis->GetSelection() : xSel - 1;
        for (int i = 0; i < (int)m_plotTabs.size(); i++)
            if (onAxisChanged) onAxisChanged(i, xCols[i], yCol);
    });
    m_allXNorm->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        int xnSel = m_allXNorm->GetSelection();
        if (xnSel == 0) return;
        int xNorm = xnSel - 1;
        int ynSel = m_allYNorm->GetSelection();
        std::vector<int> yNorms(m_plotTabs.size());
        for (int i = 0; i < (int)m_plotTabs.size(); i++)
            yNorms[i] = (ynSel == 0) ? m_plotTabs[i]->m_yNorm->GetSelection() : ynSel - 1;
        for (int i = 0; i < (int)m_plotTabs.size(); i++)
            if (onNormChanged) onNormChanged(i, xNorm, yNorms[i]);
    });
    m_allYNorm->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        int ynSel = m_allYNorm->GetSelection();
        if (ynSel == 0) return;
        int yNorm = ynSel - 1;
        int xnSel = m_allXNorm->GetSelection();
        std::vector<int> xNorms(m_plotTabs.size());
        for (int i = 0; i < (int)m_plotTabs.size(); i++)
            xNorms[i] = (xnSel == 0) ? m_plotTabs[i]->m_xNorm->GetSelection() : xnSel - 1;
        for (int i = 0; i < (int)m_plotTabs.size(); i++)
            if (onNormChanged) onNormChanged(i, xNorms[i], yNorm);
    });
    allXLock->Bind(wxEVT_CHECKBOX, [this, allXLock](wxCommandEvent&) {
        bool xLock = allXLock->GetValue();
        for (int i = 0; i < (int)m_plotTabs.size(); i++) {
            bool yLock = m_plotTabs[i]->m_yLock->GetValue();
            if (onAxisLockChanged) onAxisLockChanged(i, xLock, yLock);
        }
    });
    allYLock->Bind(wxEVT_CHECKBOX, [this, allYLock](wxCommandEvent&) {
        bool yLock = allYLock->GetValue();
        for (int i = 0; i < (int)m_plotTabs.size(); i++) {
            bool xLock = m_plotTabs[i]->m_xLock->GetValue();
            if (onAxisLockChanged) onAxisLockChanged(i, xLock, yLock);
        }
    });
    m_allZAxis->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        int zSel = m_allZAxis->GetSelection();
        if (zSel == 0) return;  // "(no change)"
        // 1="(None)", 2+=column index
        int zCol = (zSel == 1) ? -1 : zSel - 2;
        int znSel = m_allZNorm->GetSelection();
        for (int i = 0; i < (int)m_plotTabs.size(); i++) {
            int zNorm = (znSel == 0) ? m_plotTabs[i]->m_zNorm->GetSelection() : znSel - 1;
            if (onZAxisChanged) onZAxisChanged(i, zCol, zNorm);
        }
    });
    m_allZNorm->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        int znSel = m_allZNorm->GetSelection();
        if (znSel == 0) return;
        int zNorm = znSel - 1;
        int zSel = m_allZAxis->GetSelection();
        for (int i = 0; i < (int)m_plotTabs.size(); i++) {
            int zCol;
            if (zSel == 0) {  // Z axis is "(no change)" — preserve per-plot
                int plotZSel = m_plotTabs[i]->m_zAxis->GetSelection();
                zCol = (plotZSel == 0) ? -1 : plotZSel - 1;
            } else {
                zCol = (zSel == 1) ? -1 : zSel - 2;
            }
            if (onZAxisChanged) onZAxisChanged(i, zCol, zNorm);
        }
    });
    allRotSlider->Bind(wxEVT_SLIDER, [this, allRotSlider, allRotLabel](wxCommandEvent&) {
        float angle = static_cast<float>(allRotSlider->GetValue());
        allRotLabel->SetLabel(wxString::Format("screen y: %d\u00B0", (int)angle));
        for (auto* tab : m_plotTabs) {
            tab->m_spinning = false; tab->m_rocking = false;
            tab->m_spinButton->SetValue(false); tab->m_rockButton->SetValue(false);
            tab->m_spinAngle = angle;
            tab->m_rotationSlider->SetValue((int)angle);
            tab->m_rotationLabel->SetLabel(wxString::Format("screen y: %d\u00B0", (int)angle));
            if (onRotationChanged) onRotationChanged(tab->m_plotIndex, angle);
        }
    });
    allRotXSlider->Bind(wxEVT_SLIDER, [this, allRotXSlider, allRotXLabel](wxCommandEvent&) {
        float angle = static_cast<float>(allRotXSlider->GetValue());
        allRotXLabel->SetLabel(wxString::Format("screen x: %d\u00B0", (int)angle));
        for (auto* tab : m_plotTabs) {
            tab->m_spinningX = false; tab->m_rockingX = false;
            tab->m_spinXButton->SetValue(false); tab->m_rockXButton->SetValue(false);
            tab->m_spinXAngle = angle;
            tab->m_rotationXSlider->SetValue((int)angle);
            tab->m_rotationXLabel->SetLabel(wxString::Format("screen x: %d\u00B0", (int)angle));
            if (onRotationXChanged) onRotationXChanged(tab->m_plotIndex, angle);
        }
    });
    allSpinBtn->Bind(wxEVT_TOGGLEBUTTON, [this, allSpinBtn, allRockBtn, allRotSlider](wxCommandEvent&) {
        bool spinning = allSpinBtn->GetValue();
        if (spinning) allRockBtn->SetValue(false);
        for (auto* tab : m_plotTabs) {
            tab->m_spinning = spinning;
            tab->m_spinButton->SetValue(spinning);
            if (spinning) {
                tab->m_spinAngle = static_cast<float>(allRotSlider->GetValue());
                tab->m_rocking = false;
                tab->m_rockButton->SetValue(false);
            }
        }
    });
    allRockBtn->Bind(wxEVT_TOGGLEBUTTON, [this, allSpinBtn, allRockBtn, allRotSlider](wxCommandEvent&) {
        bool rocking = allRockBtn->GetValue();
        if (rocking) allSpinBtn->SetValue(false);
        for (auto* tab : m_plotTabs) {
            tab->m_rocking = rocking;
            tab->m_rockButton->SetValue(rocking);
            if (rocking) {
                tab->m_spinAngle = static_cast<float>(allRotSlider->GetValue());
                tab->m_rockCenter = tab->m_spinAngle;
                tab->m_rockPhase = 0.0f;
                tab->m_spinning = false;
                tab->m_spinButton->SetValue(false);
            }
        }
    });
    zeroYBtn->Bind(wxEVT_BUTTON, [this, allSpinBtn, allRockBtn, allRotSlider, allRotLabel](wxCommandEvent&) {
        allSpinBtn->SetValue(false); allRockBtn->SetValue(false);
        allRotSlider->SetValue(0);
        allRotLabel->SetLabel("screen y: 0\u00B0");
        for (auto* tab : m_plotTabs) {
            tab->m_spinning = false; tab->m_rocking = false;
            tab->m_spinButton->SetValue(false); tab->m_rockButton->SetValue(false);
            tab->m_spinAngle = 0.0f;
            tab->m_rotationSlider->SetValue(0);
            tab->m_rotationLabel->SetLabel("screen y: 0\u00B0");
            if (onRotationZeroed) onRotationZeroed(tab->m_plotIndex, true, false);
        }
    });
    allSpinXBtn->Bind(wxEVT_TOGGLEBUTTON, [this, allSpinXBtn, allRockXBtn, allRotXSlider](wxCommandEvent&) {
        bool spinning = allSpinXBtn->GetValue();
        if (spinning) allRockXBtn->SetValue(false);
        for (auto* tab : m_plotTabs) {
            tab->m_spinningX = spinning;
            tab->m_spinXButton->SetValue(spinning);
            if (spinning) {
                tab->m_spinXAngle = static_cast<float>(allRotXSlider->GetValue());
                tab->m_rockingX = false;
                tab->m_rockXButton->SetValue(false);
            }
        }
    });
    allRockXBtn->Bind(wxEVT_TOGGLEBUTTON, [this, allSpinXBtn, allRockXBtn, allRotXSlider](wxCommandEvent&) {
        bool rocking = allRockXBtn->GetValue();
        if (rocking) allSpinXBtn->SetValue(false);
        for (auto* tab : m_plotTabs) {
            tab->m_rockingX = rocking;
            tab->m_rockXButton->SetValue(rocking);
            if (rocking) {
                tab->m_spinXAngle = static_cast<float>(allRotXSlider->GetValue());
                tab->m_rockXCenter = tab->m_spinXAngle;
                tab->m_rockXPhase = 0.0f;
                tab->m_spinningX = false;
                tab->m_spinXButton->SetValue(false);
            }
        }
    });
    zeroXBtn->Bind(wxEVT_BUTTON, [this, allSpinXBtn, allRockXBtn, allRotXSlider, allRotXLabel](wxCommandEvent&) {
        allSpinXBtn->SetValue(false); allRockXBtn->SetValue(false);
        allRotXSlider->SetValue(0);
        allRotXLabel->SetLabel("screen x: 0\u00B0");
        for (auto* tab : m_plotTabs) {
            tab->m_spinningX = false; tab->m_rockingX = false;
            tab->m_spinXButton->SetValue(false); tab->m_rockXButton->SetValue(false);
            tab->m_spinXAngle = 0.0f;
            tab->m_rotationXSlider->SetValue(0);
            tab->m_rotationXLabel->SetLabel("screen x: 0\u00B0");
            if (onRotationZeroed) onRotationZeroed(tab->m_plotIndex, false, true);
        }
    });
    allShowUnselected->Bind(wxEVT_CHECKBOX, [this, allShowUnselected](wxCommandEvent&) {
        bool show = allShowUnselected->GetValue();
        for (int i = 0; i < (int)m_plotTabs.size(); i++)
            if (onShowUnselectedChanged) onShowUnselectedChanged(i, show);
    });
    allGridLines->Bind(wxEVT_CHECKBOX, [this, allGridLines](wxCommandEvent&) {
        bool show = allGridLines->GetValue();
        for (int i = 0; i < (int)m_plotTabs.size(); i++)
            if (onGridLinesChanged) onGridLinesChanged(i, show);
    });
    allHistograms->Bind(wxEVT_CHECKBOX, [this, allHistograms](wxCommandEvent&) {
        bool show = allHistograms->GetValue();
        for (int i = 0; i < (int)m_plotTabs.size(); i++)
            if (onShowHistogramsChanged) onShowHistogramsChanged(i, show);
    });
    m_pointSizeSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        float val = m_pointSizeSlider->GetValue() / 10.0f;
        m_pointSizeLabel->SetLabel(wxString::Format("Point Size: %.1f", val));
        if (onPointSizeChanged) onPointSizeChanged(val);
    });
    allOpacitySlider->Bind(wxEVT_SLIDER, [this, allOpacitySlider, allOpacityLabel](wxCommandEvent&) {
        int val = allOpacitySlider->GetValue();
        allOpacityLabel->SetLabel(wxString::Format("Opacity: %d%%", val));
        float alpha = static_cast<float>(val) / 100.0f;
        for (int i = 0; i < (int)m_plotTabs.size(); i++)
            if (onPlotOpacityChanged) onPlotOpacityChanged(i, alpha);
    });
    m_histBinsSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        int val = m_histBinsSlider->GetValue();
        m_histBinsLabel->SetLabel(wxString::Format("Hist Bins: %d", val));
        if (onHistBinsChanged) onHistBinsChanged(val);
    });
    m_globalTooltipCheck->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        if (onGlobalTooltipChanged) onGlobalTooltipChanged(m_globalTooltipCheck->GetValue());
    });
    deferRedraws->Bind(wxEVT_CHECKBOX, [this, deferRedraws](wxCommandEvent&) {
        if (onDeferRedrawsChanged) onDeferRedrawsChanged(deferRedraws->GetValue());
    });
    clearBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (onClearSelection) onClearSelection();
    });
    invertBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (onInvertSelection) onInvertSelection();
    });
    killBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (onKillSelected) onKillSelected();
    });
    saveAllBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (onSaveData) onSaveData(false);
    });
    saveSelBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (onSaveData) onSaveData(true);
    });

    pSizer->AddStretchSpacer();
    plotsPage->SetSizer(pSizer);
    m_allSubBook->AddPage(plotsPage, "");

    // ---- Page 1: "Brushes & Colormaps" ----
    auto* brushPage = new wxScrolledWindow(m_allSubBook);
    brushPage->SetScrollRate(0, 10);
    auto* bSizer = new wxBoxSizer(wxVERTICAL);

    // Brush controls
    auto* brushLabel = new wxStaticText(brushPage, wxID_ANY, "Brush (dbl-click: edit color)");
    auto bFont = brushLabel->GetFont();
    bFont.SetWeight(wxFONTWEIGHT_BOLD);
    brushLabel->SetFont(bFont);
    bSizer->Add(brushLabel, 0, wxLEFT | wxTOP, 8);

    auto* brushSizer = new wxGridSizer(1, CP_NUM_BRUSHES, 2, 2);
    for (int i = 0; i < CP_NUM_BRUSHES; i++) {
        auto* btn = new wxButton(brushPage, wxID_ANY, wxString::Format("%d", i),
                                  wxDefaultPosition, wxSize(26, 26), wxBU_EXACTFIT);
        wxColour col;
        if (i == 0) {
            col = wxColour(38, 102, 255);
        } else {
            col = wxColour(static_cast<unsigned char>(kDefaultBrushes[i - 1].r * 255),
                           static_cast<unsigned char>(kDefaultBrushes[i - 1].g * 255),
                           static_cast<unsigned char>(kDefaultBrushes[i - 1].b * 255));
        }
        btn->SetBackgroundColour(col);
        btn->SetForegroundColour(*wxWHITE);
        m_brushButtons[i] = btn;
        brushSizer->Add(btn, 0, wxEXPAND);
        btn->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
            SelectBrush(i);
        });
#ifdef __WXMAC__
        btn->Bind(wxEVT_LEFT_DCLICK, [this, i](wxMouseEvent&) {
            wxColour bg = m_brushButtons[i]->GetBackgroundColour();
            ShowColorPanel(i, bg.Red() / 255.0f, bg.Green() / 255.0f,
                           bg.Blue() / 255.0f, bg.Alpha() / 255.0f,
                           [](int brushIndex, float r, float g, float b, float a, void* ud) {
                               static_cast<ControlPanel*>(ud)->ApplyBrushColor(brushIndex, r, g, b, a);
                           }, this);
        });
#else
        btn->Bind(wxEVT_LEFT_DCLICK, [this, i](wxMouseEvent&) {
            wxColourData colData;
            colData.SetChooseFull(true);
            colData.SetChooseAlpha(true);
            colData.SetColour(m_brushButtons[i]->GetBackgroundColour());
            wxColourDialog dlg(this, &colData);
            dlg.SetTitle(wxString::Format("Brush %d Color & Opacity", i + 1));
            if (dlg.ShowModal() == wxID_OK) {
                wxColour c = dlg.GetColourData().GetColour();
                float r = c.Red() / 255.0f;
                float g = c.Green() / 255.0f;
                float b = c.Blue() / 255.0f;
                float a = c.Alpha() / 255.0f;
                m_brushButtons[i]->SetBackgroundColour(wxColour(c.Red(), c.Green(), c.Blue()));
                m_brushButtons[i]->Refresh();
                if (onBrushColorEdited)
                    onBrushColorEdited(i, r, g, b, a);
            }
        });
#endif
        btn->Bind(wxEVT_RIGHT_DOWN, [this, i](wxMouseEvent&) {
            m_brushSymbols[i] = (i == 0) ? SYMBOL_CIRCLE : (i - 1) % SYMBOL_COUNT;
            m_brushSizeOffsets[i] = 0.0f;
            m_brushOpacityOffsets[i] = 0.0f;
            if (i == m_activeBrush) {
                if (m_brushSymbolChoice) m_brushSymbolChoice->SetSelection(m_brushSymbols[i]);
                if (m_brushSizeSlider) m_brushSizeSlider->SetValue(0);
                if (m_brushOpacitySlider) m_brushOpacitySlider->SetValue(0);
            }
            if (onBrushReset) onBrushReset(i);
        });
    }
    bSizer->Add(brushSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    m_allBrushButton = new wxButton(brushPage, wxID_ANY, "All Brushes",
                                     wxDefaultPosition, wxSize(-1, 24), wxBU_EXACTFIT);
    m_allBrushButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        SelectBrush(-1);
    });
    bSizer->Add(m_allBrushButton, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    // Initialize per-brush defaults
    for (int i = 0; i < CP_NUM_BRUSHES; i++) {
        m_brushSymbols[i] = (i == 0) ? SYMBOL_CIRCLE : (i - 1) % SYMBOL_COUNT;
        m_brushSizeOffsets[i] = 0.0f;
    }
    SelectBrush(0);

    // Symbol chooser
    bSizer->Add(new wxStaticText(brushPage, wxID_ANY, "Brush Symbol"), 0, wxLEFT | wxTOP, 8);
    m_brushSymbolChoice = new wxChoice(brushPage, wxID_ANY);
    for (int s = 0; s < SYMBOL_COUNT; s++)
        m_brushSymbolChoice->Append(SymbolName(s));
    m_brushSymbolChoice->SetSelection(0);
    bSizer->Add(m_brushSymbolChoice, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
    m_brushSymbolChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        int sym = m_brushSymbolChoice->GetSelection();
        if (m_activeBrush == -1) {
            for (int i = 0; i < CP_NUM_BRUSHES; i++) {
                m_brushSymbols[i] = sym;
                if (onBrushSymbolChanged) onBrushSymbolChanged(i, sym);
            }
        } else {
            m_brushSymbols[m_activeBrush] = sym;
            if (onBrushSymbolChanged) onBrushSymbolChanged(m_activeBrush, sym);
        }
    });

    // Brush size offset slider
    m_brushSizeLabel = new wxStaticText(brushPage, wxID_ANY, "Brush Size +/-: 0.00");
    bSizer->Add(m_brushSizeLabel, 0, wxLEFT | wxTOP, 8);
    m_brushSizeSlider = new wxSlider(brushPage, wxID_ANY, 0, -1000, 2000);
    bSizer->Add(m_brushSizeSlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
    m_brushSizeSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        float offset = m_brushSizeSlider->GetValue() / 100.0f;
        m_brushSizeLabel->SetLabel(wxString::Format("Brush Size +/-: %.2f", offset));
        if (m_activeBrush == -1) {
            for (int i = 0; i < CP_NUM_BRUSHES; i++) {
                m_brushSizeOffsets[i] = offset;
                if (onBrushSizeOffsetChanged) onBrushSizeOffsetChanged(i, offset);
            }
        } else {
            m_brushSizeOffsets[m_activeBrush] = offset;
            if (onBrushSizeOffsetChanged) onBrushSizeOffsetChanged(m_activeBrush, offset);
        }
    });

    // Brush opacity offset slider
    bSizer->Add(new wxStaticText(brushPage, wxID_ANY, "Brush Opacity +/-"), 0, wxLEFT | wxTOP, 8);
    m_brushOpacitySlider = new wxSlider(brushPage, wxID_ANY, 0, -100, 100);
    bSizer->Add(m_brushOpacitySlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
    m_brushOpacitySlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        float offset = static_cast<float>(m_brushOpacitySlider->GetValue());
        if (m_activeBrush == -1) {
            for (int i = 0; i < CP_NUM_BRUSHES; i++) {
                m_brushOpacityOffsets[i] = offset;
                if (onBrushOpacityOffsetChanged) onBrushOpacityOffsetChanged(i, offset);
            }
        } else {
            m_brushOpacityOffsets[m_activeBrush] = offset;
            if (onBrushOpacityOffsetChanged) onBrushOpacityOffsetChanged(m_activeBrush, offset);
        }
    });

    m_additiveSelectedCheck = new wxCheckBox(brushPage, wxID_ANY, "Blend Selected");
    m_additiveSelectedCheck->SetValue(false);
    bSizer->Add(m_additiveSelectedCheck, 0, wxLEFT | wxTOP, 8);
    m_additiveSelectedCheck->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        if (onAdditiveSelectedChanged) onAdditiveSelectedChanged(m_additiveSelectedCheck->GetValue());
    });

    bSizer->Add(new wxStaticLine(brushPage), 0, wxEXPAND | wxALL, 8);

    // Color map controls
    auto* colorHeader = new wxStaticText(brushPage, wxID_ANY, "Color Map");
    auto cFont = colorHeader->GetFont();
    cFont.SetWeight(wxFONTWEIGHT_BOLD);
    colorHeader->SetFont(cFont);
    bSizer->Add(colorHeader, 0, wxLEFT, 8);

    auto* mapRow = new wxBoxSizer(wxHORIZONTAL);
    mapRow->Add(new wxStaticText(brushPage, wxID_ANY, "Map"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    auto* colorMapChoice = new wxChoice(brushPage, wxID_ANY);
    for (const auto& name : AllColorMapNames())
        colorMapChoice->Append(name);
    colorMapChoice->SetSelection(0);
    mapRow->Add(colorMapChoice, 1, wxRIGHT, 4);
    auto* reversedBtn = new wxToggleButton(brushPage, wxID_ANY, "\u00B1");
    int btnH = colorMapChoice->GetBestSize().GetHeight();
    reversedBtn->SetMinSize(wxSize(btnH, btnH));
    mapRow->Add(reversedBtn, 0, wxALIGN_CENTER_VERTICAL);
    bSizer->Add(mapRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    bSizer->Add(new wxStaticText(brushPage, wxID_ANY, "Color By"), 0, wxLEFT | wxTOP, 8);
    m_colorVarChoice = new wxChoice(brushPage, wxID_ANY);
    m_colorVarChoice->Append("(density)");
    m_colorVarChoice->SetSelection(0);
    bSizer->Add(m_colorVarChoice, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    bSizer->Add(new wxStaticText(brushPage, wxID_ANY, "Background"), 0, wxLEFT | wxTOP, 8);
    auto* bgSlider = new wxSlider(brushPage, wxID_ANY, 0, 0, 50);
    bSizer->Add(bgSlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    auto fireColorMapChanged = [this, colorMapChoice, reversedBtn]() {
        if (onColorMapChanged)
            onColorMapChanged(colorMapChoice->GetSelection(),
                              m_colorVarChoice->GetSelection(),
                              reversedBtn->GetValue());
    };
    colorMapChoice->Bind(wxEVT_CHOICE, [fireColorMapChanged](wxCommandEvent&) {
        fireColorMapChanged();
    });
    m_colorVarChoice->Bind(wxEVT_CHOICE, [fireColorMapChanged](wxCommandEvent&) {
        fireColorMapChanged();
    });
    reversedBtn->Bind(wxEVT_TOGGLEBUTTON, [fireColorMapChanged](wxCommandEvent&) {
        fireColorMapChanged();
    });
    bgSlider->Bind(wxEVT_SLIDER, [this, bgSlider](wxCommandEvent&) {
        if (onBackgroundChanged)
            onBackgroundChanged(static_cast<float>(bgSlider->GetValue()) / 100.0f);
    });

    bSizer->AddStretchSpacer();
    brushPage->SetSizer(bSizer);
    m_allSubBook->AddPage(brushPage, "");

    // Default to "All Plots" sub-page
    SelectAllSubPage(0);

    m_allPage->SetSizer(topSizer);
    m_book->AddPage(m_allPage, "");
}

void ControlPanel::SelectAllSubPage(int idx) {
    if (m_allSubBook) m_allSubBook->SetSelection(idx);

    wxColour activeBg(80, 120, 200);
    wxColour normalBg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);

    if (m_allPlotsBtn) {
        m_allPlotsBtn->SetBackgroundColour(idx == 0 ? activeBg : normalBg);
        m_allPlotsBtn->Refresh();
    }
    if (m_brushesBtn) {
        m_brushesBtn->SetBackgroundColour(idx == 1 ? activeBg : normalBg);
        m_brushesBtn->Refresh();
    }
}

void ControlPanel::SelectBrush(int index) {
    m_activeBrush = index;
    if (index >= 0)
        m_lastIndividualBrush = index;

    for (int i = 0; i < CP_NUM_BRUSHES; i++) {
        if (m_brushButtons[i])
            m_brushButtons[i]->SetLabel(
                i == index ? wxString::Format("[%d]", i) : wxString::Format("%d", i));
    }
    if (m_allBrushButton)
        m_allBrushButton->SetLabel(index == -1 ? "[All Brushes]" : "All Brushes");

    // Show values from the last individually selected brush
    int displayBrush = (index >= 0) ? index : m_lastIndividualBrush;
    if (m_brushSymbolChoice)
        m_brushSymbolChoice->SetSelection(m_brushSymbols[displayBrush]);
    if (m_brushSizeSlider) {
        m_brushSizeSlider->SetValue(static_cast<int>(m_brushSizeOffsets[displayBrush] * 100));
        if (m_brushSizeLabel)
            m_brushSizeLabel->SetLabel(wxString::Format("Brush Size +/-: %.2f", m_brushSizeOffsets[displayBrush]));
    }
    if (m_brushOpacitySlider)
        m_brushOpacitySlider->SetValue(static_cast<int>(m_brushOpacityOffsets[displayBrush]));

    if (index >= 0 && onBrushChanged) onBrushChanged(index);
}

float ControlPanel::GetPointSize() const {
    return m_pointSizeSlider ? static_cast<float>(m_pointSizeSlider->GetValue()) : 6.0f;
}

void ControlPanel::SetGlobalPointSize(float size) {
    if (m_pointSizeSlider) m_pointSizeSlider->SetValue(static_cast<int>(size * 10));
    if (m_pointSizeLabel) m_pointSizeLabel->SetLabel(wxString::Format("Point Size: %.1f", size));
}

void ControlPanel::SetGlobalTooltip(bool on) {
    if (m_globalTooltipCheck) m_globalTooltipCheck->SetValue(on);
}

void ControlPanel::ShowBrushControls(int brushIndex) {
    int allIdx = static_cast<int>(m_plotTabs.size());
    SelectPage(allIdx);
    SelectAllSubPage(1);  // switch to "Brushes & Colormaps" sub-page
    if (brushIndex >= 0 && brushIndex < CP_NUM_BRUSHES)
        SelectBrush(brushIndex);
}

void ControlPanel::ApplyBrushColor(int brushIndex, float r, float g, float b, float a) {
    if (brushIndex < 0 || brushIndex >= CP_NUM_BRUSHES) return;
    m_brushButtons[brushIndex]->SetBackgroundColour(
        wxColour(static_cast<unsigned char>(r * 255),
                 static_cast<unsigned char>(g * 255),
                 static_cast<unsigned char>(b * 255)));
    m_brushButtons[brushIndex]->Refresh();
    if (onBrushColorEdited)
        onBrushColorEdited(brushIndex, r, g, b, a);
}
