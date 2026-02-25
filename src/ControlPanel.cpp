// Viewpoints (MIT License) - See LICENSE file
#include "ControlPanel.h"
#include "Normalize.h"
#include "Brush.h"
#include "MainFrame.h"
#include "WebGPUCanvas.h"  // for SymbolName, SYMBOL_COUNT
#include "ColorMap.h"
#include <wx/statline.h>
#include <wx/colordlg.h>
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
    m_xLock = new wxCheckBox(this, wxID_ANY, "Lock");
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
    m_yLock = new wxCheckBox(this, wxID_ANY, "Lock");
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

    // Rotation slider (for 3D) with spin button
    m_rotationLabel = new wxStaticText(this, wxID_ANY, "Rotation: 0");
    sizer->Add(m_rotationLabel, 0, wxLEFT, 8);
    auto* rotRow = new wxBoxSizer(wxHORIZONTAL);
    m_rotationSlider = new wxSlider(this, wxID_ANY, 0, 0, 360);
    rotRow->Add(m_rotationSlider, 1, wxALIGN_CENTER_VERTICAL);
    m_spinButton = new wxToggleButton(this, wxID_ANY, "Spin",
                                       wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    rotRow->Add(m_spinButton, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 2);
    m_rockButton = new wxToggleButton(this, wxID_ANY, "Rock",
                                       wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    rotRow->Add(m_rockButton, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 2);
    sizer->Add(rotRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

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

    sizer->AddStretchSpacer();
    SetSizer(sizer);

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
        m_rotationLabel->SetLabel(wxString::Format("Rotation: %d\u00B0", (int)angle));
        if (onRotationChanged) onRotationChanged(m_plotIndex, angle);
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
        m_rotationLabel->SetLabel(wxString::Format("Rotation: %d\u00B0", (int)cfg.rotationY));
    }
    m_spinButton->SetValue(m_spinning);
    m_rockButton->SetValue(m_rocking);
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

    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxALL, 4);

    auto* helpText = new wxStaticText(this, wxID_ANY,
        "Click plot: activate\n"
        "Drag: select (brush) points\n"
        "Opt+drag: move selection\n"
        "Cmd+drag: extend selection\n"
        "Shift+drag: pan\n"
        "Scroll: pan\n"
        "Pinch: zoom\n"
        "C: clear selection\n"
        "D: toggle deselected points\n"
        "I: invert selection\n"
        "K: kill selected points\n"
        "T: toggle hover details\n"
        "R: reset active view\n"
        "Shift+R: reset all views\n"
        "Cmd+S: save all data\n"
        "Cmd+Shift+S: save selected\n"
        "Q: quit");
    helpText->SetForegroundColour(wxColour(120, 120, 120));
    auto hFont = helpText->GetFont();
    hFont.SetPointSize(hFont.GetPointSize() - 1);
    helpText->SetFont(hFont);
    sizer->Add(helpText, 0, wxALL, 8);
    // Cap help text to at most 30% of panel height
    Bind(wxEVT_SIZE, [this, helpText](wxSizeEvent& evt) {
        int panelH = evt.GetSize().GetHeight();
        int contentH = helpText->GetBestSize().GetHeight() + 16;
        int maxH = panelH * 3 / 10;
        helpText->SetMaxSize(wxSize(-1, std::min(contentH, maxH)));
        evt.Skip();
    });

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
        if (tab->m_spinning) {
            tab->m_spinAngle += SPIN_SPEED * dt;
            if (tab->m_spinAngle >= 360.0f) tab->m_spinAngle -= 360.0f;
        } else if (tab->m_rocking) {
            // Sinusoidal: match ~1s full cycle (2π radians/s phase rate)
            tab->m_rockPhase += 2.0f * 3.14159265f * dt;
            if (tab->m_rockPhase >= 2.0f * 3.14159265f)
                tab->m_rockPhase -= 2.0f * 3.14159265f;
            tab->m_spinAngle = tab->m_rockCenter + ROCK_AMPLITUDE * std::sin(tab->m_rockPhase);
        } else {
            continue;
        }
        tab->m_rotationSlider->SetValue(static_cast<int>(tab->m_spinAngle));
        tab->m_rotationLabel->SetLabel(wxString::Format("Rotation: %d\u00B0", (int)tab->m_spinAngle));
        if (onRotationChanged) onRotationChanged(tab->m_plotIndex, tab->m_spinAngle);
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
    m_pointSizeSlider = nullptr;
    m_histBinsSlider = nullptr;
    m_selectionLabel = nullptr;
    m_brushSymbolChoice = nullptr;
    m_brushSizeSlider = nullptr;
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
            if (onRandomizeAxes) onRandomizeAxes(pi);
        };
        tab->onAxisChanged = [this](int pi, int x, int y) {
            if (onAxisChanged) onAxisChanged(pi, x, y);
        };
        tab->onAxisLockChanged = [this](int pi, bool xLock, bool yLock) {
            if (onAxisLockChanged) onAxisLockChanged(pi, xLock, yLock);
        };
        tab->onNormChanged = [this](int pi, int xn, int yn) {
            if (onNormChanged) onNormChanged(pi, xn, yn);
        };
        tab->onZAxisChanged = [this](int pi, int zCol, int zNorm) {
            if (onZAxisChanged) onZAxisChanged(pi, zCol, zNorm);
        };
        tab->onRotationChanged = [this](int pi, float angle) {
            if (onRotationChanged) onRotationChanged(pi, angle);
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

    // "Global" button row (below the plot grid)
    m_allButton = new wxButton(m_selectorPanel, wxID_ANY, "All Plots + Brush Controls",
                                wxDefaultPosition, wxSize(-1, 24), wxBU_EXACTFIT);
    m_allButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        int allIdx = static_cast<int>(m_plotTabs.size());
        SelectPage(allIdx);
        if (onAllSelected) onAllSelected();
    });
    sizer->Add(m_allButton, 0, wxEXPAND);

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
    if (m_allButton) {
        m_allButton->SetBackgroundColour(pageIndex == numPlots ? activeBg : normalBg);
        m_allButton->Refresh();
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
        tab->m_rotationLabel->SetLabel(wxString::Format("Rotation: 0\u00B0"));
    }
}

void ControlPanel::SetColumns(const std::vector<std::string>& names) {
    m_columnNames = names;
    for (auto* tab : m_plotTabs) tab->SetColumns(names);
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
    if (m_selectionLabel) {
        m_selectionLabel->SetLabel(selected > 0 ?
            wxString::Format("Selected: %d / %d", selected, total) : "No selection");
    }
}

void ControlPanel::CreateAllPage() {
    m_allPage = new wxScrolledWindow(m_book);
    static_cast<wxScrolledWindow*>(m_allPage)->SetScrollRate(0, 10);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* header = new wxStaticText(m_allPage, wxID_ANY, "All Plots + Brush Controls");
    auto font = header->GetFont();
    font.SetWeight(wxFONTWEIGHT_BOLD);
    header->SetFont(font);
    sizer->Add(header, 0, wxALL, 8);

    sizer->Add(new wxStaticLine(m_allPage), 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    // Brush controls (at the top for quick access)
    auto* brushLabel = new wxStaticText(m_allPage, wxID_ANY, "Brush (dbl-click: edit color)");
    auto bFont = brushLabel->GetFont();
    bFont.SetWeight(wxFONTWEIGHT_BOLD);
    brushLabel->SetFont(bFont);
    sizer->Add(brushLabel, 0, wxLEFT, 8);

    auto* brushSizer = new wxGridSizer(1, CP_NUM_BRUSHES, 2, 2);
    for (int i = 0; i < CP_NUM_BRUSHES; i++) {
        // Button label: "0" for unselected, "1"-"7" for selection brushes
        auto* btn = new wxButton(m_allPage, wxID_ANY, wxString::Format("%d", i),
                                  wxDefaultPosition, wxSize(26, 26), wxBU_EXACTFIT);
        wxColour col;
        if (i == 0) {
            // Brush 0: default unselected color (dark blue)
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
        // Single click: select brush
        btn->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
            SelectBrush(i);
        });
        // Double-click: open color picker
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
        // Right-click: reset brush to default (especially useful for brush 0)
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
    sizer->Add(brushSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    // "All" button below brush grid (affects all brushes)
    m_allBrushButton = new wxButton(m_allPage, wxID_ANY, "All Brushes",
                                     wxDefaultPosition, wxSize(-1, 24), wxBU_EXACTFIT);
    m_allBrushButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        SelectBrush(-1);
    });
    sizer->Add(m_allBrushButton, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    // Initialize per-brush defaults to match MainFrame defaults
    for (int i = 0; i < CP_NUM_BRUSHES; i++) {
        m_brushSymbols[i] = (i == 0) ? SYMBOL_CIRCLE : (i - 1) % SYMBOL_COUNT;
        m_brushSizeOffsets[i] = 0.0f;
    }
    SelectBrush(0);

    // Symbol chooser for active brush
    sizer->Add(new wxStaticText(m_allPage, wxID_ANY, "Brush Symbol"), 0, wxLEFT | wxTOP, 8);
    m_brushSymbolChoice = new wxChoice(m_allPage, wxID_ANY);
    for (int s = 0; s < SYMBOL_COUNT; s++)
        m_brushSymbolChoice->Append(SymbolName(s));
    m_brushSymbolChoice->SetSelection(0);
    sizer->Add(m_brushSymbolChoice, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
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

    // Per-brush size offset slider
    sizer->Add(new wxStaticText(m_allPage, wxID_ANY, "Brush Size +/-"), 0, wxLEFT | wxTOP, 8);
    m_brushSizeSlider = new wxSlider(m_allPage, wxID_ANY, 0, -1000, 2000);
    sizer->Add(m_brushSizeSlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
    m_brushSizeSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        float offset = m_brushSizeSlider->GetValue() / 100.0f;
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

    // Per-brush opacity offset slider
    sizer->Add(new wxStaticText(m_allPage, wxID_ANY, "Brush Opacity +/-"), 0, wxLEFT | wxTOP, 8);
    m_brushOpacitySlider = new wxSlider(m_allPage, wxID_ANY, 0, -100, 100);
    sizer->Add(m_brushOpacitySlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
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

    sizer->Add(new wxStaticLine(m_allPage), 0, wxEXPAND | wxALL, 8);

    m_pointSizeLabel = new wxStaticText(m_allPage, wxID_ANY, "Point Size: 6.0");
    sizer->Add(m_pointSizeLabel, 0, wxLEFT | wxTOP, 8);
    m_pointSizeSlider = new wxSlider(m_allPage, wxID_ANY, 60, 5, 300);
    sizer->Add(m_pointSizeSlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    m_histBinsLabel = new wxStaticText(m_allPage, wxID_ANY, "Hist Bins: 64");
    sizer->Add(m_histBinsLabel, 0, wxLEFT | wxTOP, 8);
    m_histBinsSlider = new wxSlider(m_allPage, wxID_ANY, 64, 2, 512);
    sizer->Add(m_histBinsSlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    sizer->Add(new wxStaticLine(m_allPage), 0, wxEXPAND | wxALL, 8);

    // Color map controls
    auto* colorHeader = new wxStaticText(m_allPage, wxID_ANY, "Color Map");
    auto cFont = colorHeader->GetFont();
    cFont.SetWeight(wxFONTWEIGHT_BOLD);
    colorHeader->SetFont(cFont);
    sizer->Add(colorHeader, 0, wxLEFT, 8);

    sizer->Add(new wxStaticText(m_allPage, wxID_ANY, "Map"), 0, wxLEFT | wxTOP, 8);
    auto* colorMapChoice = new wxChoice(m_allPage, wxID_ANY);
    for (const auto& name : AllColorMapNames())
        colorMapChoice->Append(name);
    colorMapChoice->SetSelection(0);
    sizer->Add(colorMapChoice, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    sizer->Add(new wxStaticText(m_allPage, wxID_ANY, "Color By"), 0, wxLEFT | wxTOP, 8);
    m_colorVarChoice = new wxChoice(m_allPage, wxID_ANY);
    m_colorVarChoice->Append("(density)");
    m_colorVarChoice->SetSelection(0);
    sizer->Add(m_colorVarChoice, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    sizer->Add(new wxStaticText(m_allPage, wxID_ANY, "Background"), 0, wxLEFT | wxTOP, 8);
    auto* bgSlider = new wxSlider(m_allPage, wxID_ANY, 0, 0, 50);
    sizer->Add(bgSlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    colorMapChoice->Bind(wxEVT_CHOICE, [this, colorMapChoice](wxCommandEvent&) {
        if (onColorMapChanged)
            onColorMapChanged(colorMapChoice->GetSelection(), m_colorVarChoice->GetSelection());
    });
    m_colorVarChoice->Bind(wxEVT_CHOICE, [this, colorMapChoice](wxCommandEvent&) {
        if (onColorMapChanged)
            onColorMapChanged(colorMapChoice->GetSelection(), m_colorVarChoice->GetSelection());
    });
    bgSlider->Bind(wxEVT_SLIDER, [this, bgSlider](wxCommandEvent&) {
        if (onBackgroundChanged)
            onBackgroundChanged(static_cast<float>(bgSlider->GetValue()) / 100.0f);
    });

    sizer->Add(new wxStaticLine(m_allPage), 0, wxEXPAND | wxALL, 8);

    // Display toggles (apply to all plots)
    auto* allShowUnselected = new wxCheckBox(m_allPage, wxID_ANY, "Show unselected");
    allShowUnselected->SetValue(true);
    sizer->Add(allShowUnselected, 0, wxLEFT, 8);

    auto* allGridLines = new wxCheckBox(m_allPage, wxID_ANY, "Grid lines");
    allGridLines->SetValue(false);
    sizer->Add(allGridLines, 0, wxLEFT, 8);

    auto* allHistograms = new wxCheckBox(m_allPage, wxID_ANY, "Histograms");
    allHistograms->SetValue(true);
    sizer->Add(allHistograms, 0, wxLEFT, 8);

    m_globalTooltipCheck = new wxCheckBox(m_allPage, wxID_ANY, "Hover shows datapoint details");
    m_globalTooltipCheck->SetValue(true);
    sizer->Add(m_globalTooltipCheck, 0, wxLEFT, 8);

    auto* deferRedraws = new wxCheckBox(m_allPage, wxID_ANY, "Defer redraws");
    deferRedraws->SetValue(false);
    sizer->Add(deferRedraws, 0, wxLEFT | wxBOTTOM, 8);

    m_globalTooltipCheck->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        if (onGlobalTooltipChanged) onGlobalTooltipChanged(m_globalTooltipCheck->GetValue());
    });
    deferRedraws->Bind(wxEVT_CHECKBOX, [this, deferRedraws](wxCommandEvent&) {
        if (onDeferRedrawsChanged) onDeferRedrawsChanged(deferRedraws->GetValue());
    });

    allShowUnselected->Bind(wxEVT_CHECKBOX, [this, allShowUnselected](wxCommandEvent&) {
        bool show = allShowUnselected->GetValue();
        for (int i = 0; i < (int)m_plotTabs.size(); i++) {
            if (onShowUnselectedChanged) onShowUnselectedChanged(i, show);
        }
    });
    allGridLines->Bind(wxEVT_CHECKBOX, [this, allGridLines](wxCommandEvent&) {
        bool show = allGridLines->GetValue();
        for (int i = 0; i < (int)m_plotTabs.size(); i++) {
            if (onGridLinesChanged) onGridLinesChanged(i, show);
        }
    });
    allHistograms->Bind(wxEVT_CHECKBOX, [this, allHistograms](wxCommandEvent&) {
        bool show = allHistograms->GetValue();
        for (int i = 0; i < (int)m_plotTabs.size(); i++) {
            if (onShowHistogramsChanged) onShowHistogramsChanged(i, show);
        }
    });

    sizer->Add(new wxStaticLine(m_allPage), 0, wxEXPAND | wxALL, 8);

    m_selectionLabel = new wxStaticText(m_allPage, wxID_ANY, "No selection");
    sizer->Add(m_selectionLabel, 0, wxLEFT | wxTOP, 8);

    auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* clearBtn = new wxButton(m_allPage, wxID_ANY, "Clear (C)",
                                   wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* invertBtn = new wxButton(m_allPage, wxID_ANY, "Invert (I)",
                                    wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* killBtn = new wxButton(m_allPage, wxID_ANY, "Kill (K)",
                                  wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    btnSizer->Add(clearBtn, 1, wxRIGHT, 4);
    btnSizer->Add(invertBtn, 1, wxRIGHT, 4);
    btnSizer->Add(killBtn, 1);
    sizer->Add(btnSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    auto* saveSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* saveAllBtn = new wxButton(m_allPage, wxID_ANY, "Save All",
                                     wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* saveSelBtn = new wxButton(m_allPage, wxID_ANY, "Save Selected",
                                     wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    saveSizer->Add(saveAllBtn, 1, wxRIGHT, 4);
    saveSizer->Add(saveSelBtn, 1);
    sizer->Add(saveSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    sizer->AddStretchSpacer();
    m_allPage->SetSizer(sizer);
    m_book->AddPage(m_allPage, "");

    m_pointSizeSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        float val = m_pointSizeSlider->GetValue() / 10.0f;
        m_pointSizeLabel->SetLabel(wxString::Format("Point Size: %.1f", val));
        if (onPointSizeChanged) onPointSizeChanged(val);
    });
    m_histBinsSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        int val = m_histBinsSlider->GetValue();
        m_histBinsLabel->SetLabel(wxString::Format("Hist Bins: %d", val));
        if (onHistBinsChanged) onHistBinsChanged(val);
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
    if (m_brushSizeSlider)
        m_brushSizeSlider->SetValue(static_cast<int>(m_brushSizeOffsets[displayBrush] * 100));
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
