#include "ControlPanel.h"
#include "Normalize.h"
#include "Brush.h"
#include "MainFrame.h"  // for PlotConfig
#include <wx/statline.h>

// ============================================================
// PlotTab — one per plot, lives as a page in the notebook
// ============================================================

PlotTab::PlotTab(wxNotebook* parent, int plotIndex, int row, int col)
    : wxPanel(parent)
    , m_plotIndex(plotIndex)
{
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

    // X Axis
    sizer->Add(new wxStaticText(this, wxID_ANY, "X Axis"), 0, wxLEFT | wxTOP, 8);
    m_xAxis = new wxChoice(this, wxID_ANY);
    sizer->Add(m_xAxis, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    sizer->Add(new wxStaticText(this, wxID_ANY, "X Norm"), 0, wxLEFT | wxTOP, 8);
    m_xNorm = new wxChoice(this, wxID_ANY);
    for (const auto& name : AllNormModeNames())
        m_xNorm->Append(name);
    m_xNorm->SetSelection(0);
    sizer->Add(m_xNorm, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    // Y Axis
    sizer->Add(new wxStaticText(this, wxID_ANY, "Y Axis"), 0, wxLEFT, 8);
    m_yAxis = new wxChoice(this, wxID_ANY);
    sizer->Add(m_yAxis, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    sizer->Add(new wxStaticText(this, wxID_ANY, "Y Norm"), 0, wxLEFT | wxTOP, 8);
    m_yNorm = new wxChoice(this, wxID_ANY);
    for (const auto& name : AllNormModeNames())
        m_yNorm->Append(name);
    m_yNorm->SetSelection(0);
    sizer->Add(m_yNorm, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    // Display toggles
    m_showUnselected = new wxCheckBox(this, wxID_ANY, "Show unselected");
    m_showUnselected->SetValue(true);
    sizer->Add(m_showUnselected, 0, wxALL, 8);

    m_showGridLines = new wxCheckBox(this, wxID_ANY, "Grid lines");
    m_showGridLines->SetValue(false);
    sizer->Add(m_showGridLines, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    sizer->AddStretchSpacer();
    SetSizer(sizer);

    // Bind events
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
}

void PlotTab::SetColumns(const std::vector<std::string>& names) {
    m_suppress = true;
    int xSel = m_xAxis->GetSelection();
    int ySel = m_yAxis->GetSelection();
    m_xAxis->Clear();
    m_yAxis->Clear();
    for (const auto& name : names) {
        m_xAxis->Append(name);
        m_yAxis->Append(name);
    }
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
    if ((int)cfg.xNorm < m_xNorm->GetCount()) m_xNorm->SetSelection(static_cast<int>(cfg.xNorm));
    if ((int)cfg.yNorm < m_yNorm->GetCount()) m_yNorm->SetSelection(static_cast<int>(cfg.yNorm));
    m_showUnselected->SetValue(cfg.showUnselected);
    m_showGridLines->SetValue(cfg.showGridLines);
    m_suppress = false;
}

// ============================================================
// ControlPanel — outer container with wxNotebook
// ============================================================

ControlPanel::ControlPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(240, -1))
{
    SetMinSize(wxSize(240, -1));

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(this, wxID_ANY, "Plot Controls");
    auto titleFont = title->GetFont();
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    titleFont.SetPointSize(titleFont.GetPointSize() + 2);
    title->SetFont(titleFont);
    sizer->Add(title, 0, wxALL, 8);

    // Notebook fills most of the panel
    m_notebook = new wxNotebook(this, wxID_ANY);
    sizer->Add(m_notebook, 1, wxEXPAND | wxLEFT | wxRIGHT, 4);

    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxALL, 4);

    // Info and help below the notebook
    m_infoLabel = new wxStaticText(this, wxID_ANY, "No data loaded");
    sizer->Add(m_infoLabel, 0, wxLEFT | wxRIGHT, 8);

    auto* helpText = new wxStaticText(this, wxID_ANY,
        "Drag: select | Shift+drag: pan\n"
        "Scroll: zoom | Cmd+drag: extend");
    helpText->SetForegroundColour(wxColour(120, 120, 120));
    auto hFont = helpText->GetFont();
    hFont.SetPointSize(hFont.GetPointSize() - 1);
    helpText->SetFont(hFont);
    sizer->Add(helpText, 0, wxALL, 8);

    SetSizer(sizer);

    // Build default tabs
    RebuildTabs(2, 2);

    // Tab change event
    m_notebook->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& evt) {
        int page = evt.GetSelection();
        if (page >= 0 && page < (int)m_plotTabs.size() && onTabSelected)
            onTabSelected(page);
        evt.Skip();
    });
}

void ControlPanel::RebuildTabs(int rows, int cols) {
    // Save global state
    float savedSize = m_pointSizeSlider ? m_pointSizeSlider->GetValue() : 6;
    float savedOpacity = m_opacitySlider ? m_opacitySlider->GetValue() : 5;
    int savedBins = m_histBinsSlider ? m_histBinsSlider->GetValue() : 64;
    int savedBrush = m_activeBrush;

    m_savedGridRows = rows;
    m_savedGridCols = cols;

    m_notebook->DeleteAllPages();
    m_plotTabs.clear();

    int numPlots = rows * cols;
    for (int i = 0; i < numPlots; i++) {
        int r = i / cols;
        int c = i % cols;
        auto* tab = new PlotTab(m_notebook, i, r, c);
        m_notebook->AddPage(tab, wxString::Format("%d,%d", r, c));
        m_plotTabs.push_back(tab);

        // Populate columns if we have them
        if (!m_columnNames.empty())
            tab->SetColumns(m_columnNames);

        // Wire per-plot callbacks through
        tab->onAxisChanged = [this](int pi, int x, int y) {
            if (onAxisChanged) onAxisChanged(pi, x, y);
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
    }

    // Create "All" tab
    CreateAllTab();

    // Restore global state
    if (m_pointSizeSlider) m_pointSizeSlider->SetValue(savedSize);
    if (m_opacitySlider) m_opacitySlider->SetValue(savedOpacity);
    if (m_histBinsSlider) m_histBinsSlider->SetValue(savedBins);
    SelectBrush(savedBrush);
}

void ControlPanel::CreateAllTab() {
    m_allTab = new wxPanel(m_notebook);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* header = new wxStaticText(m_allTab, wxID_ANY, "Global Settings");
    auto font = header->GetFont();
    font.SetWeight(wxFONTWEIGHT_BOLD);
    header->SetFont(font);
    sizer->Add(header, 0, wxALL, 8);

    sizer->Add(new wxStaticLine(m_allTab), 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    // Point Size
    m_pointSizeLabel = new wxStaticText(m_allTab, wxID_ANY, "Point Size: 6");
    sizer->Add(m_pointSizeLabel, 0, wxLEFT | wxTOP, 8);
    m_pointSizeSlider = new wxSlider(m_allTab, wxID_ANY, 6, 1, 30);
    sizer->Add(m_pointSizeSlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    // Opacity
    m_opacityLabel = new wxStaticText(m_allTab, wxID_ANY, "Opacity: 5%");
    sizer->Add(m_opacityLabel, 0, wxLEFT | wxTOP, 8);
    m_opacitySlider = new wxSlider(m_allTab, wxID_ANY, 5, 1, 100);
    sizer->Add(m_opacitySlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    // Histogram Bins
    m_histBinsLabel = new wxStaticText(m_allTab, wxID_ANY, "Hist Bins: 64");
    sizer->Add(m_histBinsLabel, 0, wxLEFT | wxTOP, 8);
    m_histBinsSlider = new wxSlider(m_allTab, wxID_ANY, 64, 2, 512);
    sizer->Add(m_histBinsSlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    sizer->Add(new wxStaticLine(m_allTab), 0, wxEXPAND | wxALL, 8);

    // Brush selector
    auto* brushLabel = new wxStaticText(m_allTab, wxID_ANY, "Brush (1-7)");
    auto bFont = brushLabel->GetFont();
    bFont.SetWeight(wxFONTWEIGHT_BOLD);
    brushLabel->SetFont(bFont);
    sizer->Add(brushLabel, 0, wxLEFT, 8);

    auto* brushSizer = new wxGridSizer(1, CP_NUM_BRUSHES, 2, 2);
    for (int i = 0; i < CP_NUM_BRUSHES; i++) {
        auto* btn = new wxButton(m_allTab, wxID_ANY, wxString::Format("%d", i + 1),
                                  wxDefaultPosition, wxSize(26, 26), wxBU_EXACTFIT);
        wxColour col(static_cast<unsigned char>(kDefaultBrushes[i].r * 255),
                     static_cast<unsigned char>(kDefaultBrushes[i].g * 255),
                     static_cast<unsigned char>(kDefaultBrushes[i].b * 255));
        btn->SetBackgroundColour(col);
        btn->SetForegroundColour(*wxWHITE);
        m_brushButtons[i] = btn;
        brushSizer->Add(btn, 0, wxEXPAND);

        btn->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
            SelectBrush(i);
        });
    }
    sizer->Add(brushSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    SelectBrush(0);

    // Selection info
    m_selectionLabel = new wxStaticText(m_allTab, wxID_ANY, "No selection");
    sizer->Add(m_selectionLabel, 0, wxLEFT | wxTOP, 8);

    auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* clearBtn = new wxButton(m_allTab, wxID_ANY, "Clear (C)",
                                   wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* invertBtn = new wxButton(m_allTab, wxID_ANY, "Invert (I)",
                                    wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    btnSizer->Add(clearBtn, 1, wxRIGHT, 4);
    btnSizer->Add(invertBtn, 1);
    sizer->Add(btnSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    sizer->AddStretchSpacer();
    m_allTab->SetSizer(sizer);
    m_notebook->AddPage(m_allTab, "All");

    // Bind events
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
}

void ControlPanel::SelectTab(int plotIndex) {
    if (plotIndex >= 0 && plotIndex < (int)m_plotTabs.size())
        m_notebook->SetSelection(plotIndex);
}

void ControlPanel::SetPlotConfig(int plotIndex, const PlotConfig& cfg) {
    if (plotIndex >= 0 && plotIndex < (int)m_plotTabs.size())
        m_plotTabs[plotIndex]->SyncFromConfig(cfg);
}

void ControlPanel::SetColumns(const std::vector<std::string>& names) {
    m_columnNames = names;
    for (auto* tab : m_plotTabs)
        tab->SetColumns(names);
    m_infoLabel->SetLabel(wxString::Format("%zu columns", names.size()));
}

void ControlPanel::SetSelectionInfo(int selected, int total) {
    if (m_selectionLabel) {
        if (selected > 0)
            m_selectionLabel->SetLabel(wxString::Format("Selected: %d / %d", selected, total));
        else
            m_selectionLabel->SetLabel("No selection");
    }
}

void ControlPanel::SelectBrush(int index) {
    m_activeBrush = index;
    for (int i = 0; i < CP_NUM_BRUSHES; i++) {
        if (m_brushButtons[i]) {
            m_brushButtons[i]->SetLabel(
                i == index ? wxString::Format("[%d]", i + 1) : wxString::Format("%d", i + 1));
        }
    }
    if (onBrushChanged) onBrushChanged(index);
}

float ControlPanel::GetPointSize() const {
    return m_pointSizeSlider ? static_cast<float>(m_pointSizeSlider->GetValue()) : 6.0f;
}

float ControlPanel::GetOpacity() const {
    return m_opacitySlider ? static_cast<float>(m_opacitySlider->GetValue()) / 100.0f : 0.05f;
}
