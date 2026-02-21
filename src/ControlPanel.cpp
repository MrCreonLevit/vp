#include "ControlPanel.h"
#include "Normalize.h"
#include "Brush.h"
#include "MainFrame.h"
#include "WebGPUCanvas.h"  // for SymbolName, SYMBOL_COUNT
#include "ColorMap.h"
#include <wx/statline.h>
#include <wx/colordlg.h>

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
    xRow->Add(new wxStaticText(this, wxID_ANY, "X Axis"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_xLock = new wxCheckBox(this, wxID_ANY, "Lock");
    xRow->Add(m_xLock, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(xRow, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    m_xAxis = new wxChoice(this, wxID_ANY);
    sizer->Add(m_xAxis, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    sizer->Add(new wxStaticText(this, wxID_ANY, "X Norm"), 0, wxLEFT | wxTOP, 8);
    m_xNorm = new wxChoice(this, wxID_ANY);
    for (const auto& name : AllNormModeNames()) m_xNorm->Append(name);
    m_xNorm->SetSelection(0);
    sizer->Add(m_xNorm, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    auto* yRow = new wxBoxSizer(wxHORIZONTAL);
    yRow->Add(new wxStaticText(this, wxID_ANY, "Y Axis"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_yLock = new wxCheckBox(this, wxID_ANY, "Lock");
    yRow->Add(m_yLock, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(yRow, 0, wxLEFT | wxRIGHT, 8);
    m_yAxis = new wxChoice(this, wxID_ANY);
    sizer->Add(m_yAxis, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    sizer->Add(new wxStaticText(this, wxID_ANY, "Y Norm"), 0, wxLEFT | wxTOP, 8);
    m_yNorm = new wxChoice(this, wxID_ANY);
    for (const auto& name : AllNormModeNames()) m_yNorm->Append(name);
    m_yNorm->SetSelection(0);
    sizer->Add(m_yNorm, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

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

    m_pointSizeLabel = new wxStaticText(this, wxID_ANY, "Point Size: 6");
    sizer->Add(m_pointSizeLabel, 0, wxLEFT | wxTOP, 8);
    m_pointSizeSlider = new wxSlider(this, wxID_ANY, 6, 1, 30);
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
    m_showHistograms->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        if (onShowHistogramsChanged)
            onShowHistogramsChanged(m_plotIndex, m_showHistograms->GetValue());
    });
    m_pointSizeSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        if (m_suppress) return;
        int val = m_pointSizeSlider->GetValue();
        m_pointSizeLabel->SetLabel(wxString::Format("Point Size: %d", val));
        if (onPointSizeChanged) onPointSizeChanged(m_plotIndex, static_cast<float>(val));
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
    m_xAxis->Clear(); m_yAxis->Clear();
    for (const auto& name : names) { m_xAxis->Append(name); m_yAxis->Append(name); }
    if (xSel >= 0 && xSel < (int)names.size()) m_xAxis->SetSelection(xSel);
    else if (!names.empty()) m_xAxis->SetSelection(0);
    if (ySel >= 0 && ySel < (int)names.size()) m_yAxis->SetSelection(ySel);
    else if (names.size() > 1) m_yAxis->SetSelection(1);
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
    m_showUnselected->SetValue(cfg.showUnselected);
    m_showGridLines->SetValue(cfg.showGridLines);
    m_showHistograms->SetValue(cfg.showHistograms);
    m_pointSizeSlider->SetValue(static_cast<int>(cfg.pointSize));
    m_pointSizeLabel->SetLabel(wxString::Format("Point Size: %d", (int)cfg.pointSize));
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

    m_infoLabel = new wxStaticText(this, wxID_ANY, "No data loaded");
    sizer->Add(m_infoLabel, 0, wxLEFT | wxRIGHT, 8);

    auto* helpText = new wxStaticText(this, wxID_ANY,
        "Click plot: activate\n"
        "Drag: select\n"
        "Opt+drag: move selection\n"
        "Cmd+drag: extend selection\n"
        "Shift+drag: pan\n"
        "Scroll: zoom\n"
        "C: clear selection\n"
        "I: invert selection\n"
        "D: kill selected points\n"
        "R: reset all views\n"
        "Q: quit");
    helpText->SetForegroundColour(wxColour(120, 120, 120));
    auto hFont = helpText->GetFont();
    hFont.SetPointSize(hFont.GetPointSize() - 1);
    helpText->SetFont(hFont);
    sizer->Add(helpText, 0, wxALL, 8);

    SetSizer(sizer);
    RebuildTabs(2, 2);
    m_ready = true;
}

void ControlPanel::RebuildTabs(int rows, int cols) {
    m_ready = false;  // suppress dialogs during rebuild

    float savedSize = m_pointSizeSlider ? m_pointSizeSlider->GetValue() : 6;
    float savedOpacity = m_opacitySlider ? m_opacitySlider->GetValue() : 5;
    int savedBins = m_histBinsSlider ? m_histBinsSlider->GetValue() : 64;
    int savedBrush = m_activeBrush;

    m_gridRows = rows;
    m_gridCols = cols;

    // Clear and destroy old book pages
    m_book->DeleteAllPages();
    m_plotTabs.clear();
    m_allPage = nullptr;
    m_pointSizeSlider = nullptr;
    m_opacitySlider = nullptr;
    m_histBinsSlider = nullptr;
    m_selectionLabel = nullptr;

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

    // "All" page (last)
    CreateAllPage();

    // Restore global state
    if (m_pointSizeSlider) m_pointSizeSlider->SetValue(savedSize);
    if (m_opacitySlider) m_opacitySlider->SetValue(savedOpacity);
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

    // "All" button row
    m_allButton = new wxButton(m_selectorPanel, wxID_ANY, "All",
                                wxDefaultPosition, wxSize(-1, 24), wxBU_EXACTFIT);
    m_allButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        int allIdx = static_cast<int>(m_plotTabs.size());
        SelectPage(allIdx);
        if (onAllSelected) onAllSelected();
    });
    sizer->Add(m_allButton, 0, wxEXPAND | wxBOTTOM, 2);

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
    sizer->Add(gridSizer, 0, wxEXPAND);

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

void ControlPanel::SetColumns(const std::vector<std::string>& names) {
    m_columnNames = names;
    for (auto* tab : m_plotTabs) tab->SetColumns(names);
    m_infoLabel->SetLabel(wxString::Format("%zu columns", names.size()));
    // Update color variable dropdown
    if (m_colorVarChoice) {
        m_colorVarChoice->Clear();
        m_colorVarChoice->Append("(position)");
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

    auto* header = new wxStaticText(m_allPage, wxID_ANY, "Global Settings");
    auto font = header->GetFont();
    font.SetWeight(wxFONTWEIGHT_BOLD);
    header->SetFont(font);
    sizer->Add(header, 0, wxALL, 8);

    sizer->Add(new wxStaticLine(m_allPage), 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    m_pointSizeLabel = new wxStaticText(m_allPage, wxID_ANY, "Point Size: 6");
    sizer->Add(m_pointSizeLabel, 0, wxLEFT | wxTOP, 8);
    m_pointSizeSlider = new wxSlider(m_allPage, wxID_ANY, 6, 1, 30);
    sizer->Add(m_pointSizeSlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    m_opacityLabel = new wxStaticText(m_allPage, wxID_ANY, "Opacity: 5%");
    sizer->Add(m_opacityLabel, 0, wxLEFT | wxTOP, 8);
    m_opacitySlider = new wxSlider(m_allPage, wxID_ANY, 5, 1, 100);
    sizer->Add(m_opacitySlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

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
    m_colorVarChoice->Append("(position)");
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
    sizer->Add(allHistograms, 0, wxLEFT | wxBOTTOM, 8);

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

    auto* brushLabel = new wxStaticText(m_allPage, wxID_ANY, "Brush (dbl-click: edit color)");
    auto bFont = brushLabel->GetFont();
    bFont.SetWeight(wxFONTWEIGHT_BOLD);
    brushLabel->SetFont(bFont);
    sizer->Add(brushLabel, 0, wxLEFT, 8);

    auto* brushSizer = new wxGridSizer(1, CP_NUM_BRUSHES, 2, 2);
    for (int i = 0; i < CP_NUM_BRUSHES; i++) {
        auto* btn = new wxButton(m_allPage, wxID_ANY, wxString::Format("%d", i + 1),
                                  wxDefaultPosition, wxSize(26, 26), wxBU_EXACTFIT);
        wxColour col(static_cast<unsigned char>(kDefaultBrushes[i].r * 255),
                     static_cast<unsigned char>(kDefaultBrushes[i].g * 255),
                     static_cast<unsigned char>(kDefaultBrushes[i].b * 255));
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
    }
    sizer->Add(brushSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    SelectBrush(0);

    // Symbol chooser for active brush
    sizer->Add(new wxStaticText(m_allPage, wxID_ANY, "Brush Symbol"), 0, wxLEFT | wxTOP, 8);
    auto* symbolChoice = new wxChoice(m_allPage, wxID_ANY);
    for (int s = 0; s < SYMBOL_COUNT; s++)
        symbolChoice->Append(SymbolName(s));
    symbolChoice->SetSelection(0);
    sizer->Add(symbolChoice, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
    symbolChoice->Bind(wxEVT_CHOICE, [this, symbolChoice](wxCommandEvent&) {
        int sym = symbolChoice->GetSelection();
        if (onBrushSymbolChanged)
            onBrushSymbolChanged(m_activeBrush, sym);
    });

    // Per-brush size offset slider
    sizer->Add(new wxStaticText(m_allPage, wxID_ANY, "Brush Size +/-"), 0, wxLEFT | wxTOP, 8);
    auto* brushSizeSlider = new wxSlider(m_allPage, wxID_ANY, 0, -10, 20);
    sizer->Add(brushSizeSlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
    brushSizeSlider->Bind(wxEVT_SLIDER, [this, brushSizeSlider](wxCommandEvent&) {
        float offset = static_cast<float>(brushSizeSlider->GetValue());
        if (onBrushSizeOffsetChanged)
            onBrushSizeOffsetChanged(m_activeBrush, offset);
    });

    m_selectionLabel = new wxStaticText(m_allPage, wxID_ANY, "No selection");
    sizer->Add(m_selectionLabel, 0, wxLEFT | wxTOP, 8);

    auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* clearBtn = new wxButton(m_allPage, wxID_ANY, "Clear (C)",
                                   wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* invertBtn = new wxButton(m_allPage, wxID_ANY, "Invert (I)",
                                    wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* killBtn = new wxButton(m_allPage, wxID_ANY, "Kill (D)",
                                  wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    btnSizer->Add(clearBtn, 1, wxRIGHT, 4);
    btnSizer->Add(invertBtn, 1, wxRIGHT, 4);
    btnSizer->Add(killBtn, 1);
    sizer->Add(btnSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    sizer->AddStretchSpacer();
    m_allPage->SetSizer(sizer);
    m_book->AddPage(m_allPage, "");

    m_pointSizeSlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        int val = m_pointSizeSlider->GetValue();
        m_pointSizeLabel->SetLabel(wxString::Format("Point Size: %d", val));
        if (onPointSizeChanged) onPointSizeChanged(static_cast<float>(val));
    });
    m_opacitySlider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        int val = m_opacitySlider->GetValue();
        m_opacityLabel->SetLabel(wxString::Format("Opacity: %d%%", val));
        if (onOpacityChanged) onOpacityChanged(static_cast<float>(val) / 100.0f);
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
}

void ControlPanel::SelectBrush(int index) {
    m_activeBrush = index;
    for (int i = 0; i < CP_NUM_BRUSHES; i++) {
        if (m_brushButtons[i])
            m_brushButtons[i]->SetLabel(
                i == index ? wxString::Format("[%d]", i + 1) : wxString::Format("%d", i + 1));
    }
    if (onBrushChanged) onBrushChanged(index);
}

float ControlPanel::GetPointSize() const {
    return m_pointSizeSlider ? static_cast<float>(m_pointSizeSlider->GetValue()) : 6.0f;
}

float ControlPanel::GetOpacity() const {
    return m_opacitySlider ? static_cast<float>(m_opacitySlider->GetValue()) / 100.0f : 0.05f;
}
