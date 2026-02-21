#include "MainFrame.h"
#include "WebGPUCanvas.h"
#include "ControlPanel.h"
#include <algorithm>

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Viewpoints", wxDefaultPosition, wxSize(1200, 800))
{
    // Initialize shared GPU context before creating any canvases
    if (!m_gpuContext.Initialize()) {
        wxMessageBox("Failed to initialize WebGPU", "Fatal Error", wxOK | wxICON_ERROR);
    }

    CreateMenuBar();
    CreateLayout();
    CreateStatusBar();
    SetStatusText("Ready — use File > Open to load data");
}

void MainFrame::CreateMenuBar() {
    auto* menuBar = new wxMenuBar();

    auto* fileMenu = new wxMenu();
    fileMenu->Append(wxID_OPEN, "&Open...\tCtrl+O", "Open a data file");
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, "&Quit\tCtrl+Q", "Quit Viewpoints");
    menuBar->Append(fileMenu, "&File");

    auto* viewMenu = new wxMenu();
    viewMenu->Append(ID_AddRow, "Add Row\tCtrl+Shift+R", "Add a row of plots");
    viewMenu->Append(ID_AddCol, "Add Column\tCtrl+Shift+D", "Add a column of plots");
    viewMenu->AppendSeparator();
    viewMenu->Append(ID_RemoveRow, "Remove Row", "Remove bottom row");
    viewMenu->Append(ID_RemoveCol, "Remove Column", "Remove right column");
    viewMenu->AppendSeparator();
    viewMenu->Append(ID_ResetViews, "Reset All Views", "Reset pan and zoom on all plots");
    menuBar->Append(viewMenu, "&View");

    auto* helpMenu = new wxMenu();
    helpMenu->Append(wxID_ABOUT, "&About...", "About Viewpoints");
    menuBar->Append(helpMenu, "&Help");

    SetMenuBar(menuBar);

    Bind(wxEVT_MENU, &MainFrame::OnOpen, this, wxID_OPEN);
    Bind(wxEVT_MENU, &MainFrame::OnQuit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::OnAbout, this, wxID_ABOUT);
    Bind(wxEVT_MENU, &MainFrame::OnAddRow, this, ID_AddRow);
    Bind(wxEVT_MENU, &MainFrame::OnAddCol, this, ID_AddCol);
    Bind(wxEVT_MENU, &MainFrame::OnRemoveRow, this, ID_RemoveRow);
    Bind(wxEVT_MENU, &MainFrame::OnRemoveCol, this, ID_RemoveCol);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        for (auto* c : m_canvases) c->ResetView();
    }, ID_ResetViews);
}

void MainFrame::CreateLayout() {
    auto* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    m_controlPanel = new ControlPanel(this);
    mainSizer->Add(m_controlPanel, 0, wxEXPAND);

    m_gridPanel = new wxPanel(this);
    mainSizer->Add(m_gridPanel, 1, wxEXPAND);

    SetSizer(mainSizer);

    // Wire control panel callbacks
    m_controlPanel->onAxisChanged = [this](int xCol, int yCol) {
        if (m_activePlot >= 0 && m_activePlot < (int)m_plotConfigs.size()) {
            m_plotConfigs[m_activePlot].xCol = static_cast<size_t>(xCol);
            m_plotConfigs[m_activePlot].yCol = static_cast<size_t>(yCol);
            UpdatePlot(m_activePlot);
        }
    };

    m_controlPanel->onNormChanged = [this](int xNorm, int yNorm) {
        if (m_activePlot >= 0 && m_activePlot < (int)m_plotConfigs.size()) {
            m_plotConfigs[m_activePlot].xNorm = static_cast<NormMode>(xNorm);
            m_plotConfigs[m_activePlot].yNorm = static_cast<NormMode>(yNorm);
            UpdatePlot(m_activePlot);
        }
    };

    m_controlPanel->onPointSizeChanged = [this](float size) {
        for (auto* c : m_canvases) c->SetPointSize(size);
    };

    m_controlPanel->onOpacityChanged = [this](float alpha) {
        for (auto* c : m_canvases) c->SetOpacity(alpha);
    };

    m_controlPanel->onClearSelection = [this]() {
        ClearAllSelections();
    };

    m_controlPanel->onInvertSelection = [this]() {
        InvertAllSelections();
    };

    m_controlPanel->onBrushChanged = [this](int brushIndex) {
        m_activeBrush = brushIndex + 1;  // UI is 0-based, internal is 1-based
    };

    RebuildGrid();
}

void MainFrame::RebuildGrid() {
    // Destroy existing canvases
    for (auto* c : m_canvases)
        c->Destroy();
    m_canvases.clear();

    int numPlots = m_gridRows * m_gridCols;
    m_plotConfigs.resize(numPlots);
    m_canvases.resize(numPlots);

    auto* gridSizer = new wxGridSizer(m_gridRows, m_gridCols, 2, 2);

    for (int i = 0; i < numPlots; i++) {
        auto* canvas = new WebGPUCanvas(m_gridPanel, &m_gpuContext, i);
        m_canvases[i] = canvas;
        gridSizer->Add(canvas, 1, wxEXPAND);

        // Wire callbacks
        canvas->onBrushRect = [this](int pi, float x0, float y0, float x1, float y1, bool ext) {
            HandleBrushRect(pi, x0, y0, x1, y1, ext);
        };
        canvas->onClearRequested = [this]() { ClearAllSelections(); };
        canvas->onInvertRequested = [this]() { InvertAllSelections(); };

        // Set brush colors on each canvas
        std::vector<BrushColor> colors;
        for (int b = 0; b < NUM_BRUSHES; b++)
            colors.push_back({kDefaultBrushes[b].r, kDefaultBrushes[b].g, kDefaultBrushes[b].b});
        canvas->SetBrushColors(colors);
        canvas->onResetViewRequested = [this]() {
            for (auto* c : m_canvases) c->ResetView();
        };

        // Click to set active plot
        canvas->Bind(wxEVT_LEFT_DOWN, [this, i](wxMouseEvent& evt) {
            SetActivePlot(i);
            evt.Skip();  // let the canvas handle it too
        });
    }

    m_gridPanel->SetSizer(gridSizer);
    m_gridPanel->Layout();

    // Auto-assign columns
    const auto& ds = m_dataManager.dataset();
    if (ds.numCols > 0) {
        for (int i = 0; i < numPlots; i++) {
            size_t col1 = (i * 2) % ds.numCols;
            size_t col2 = (i * 2 + 1) % ds.numCols;
            m_plotConfigs[i] = {col1, col2};
        }
    }

    SetActivePlot(0);

    if (ds.numRows > 0)
        UpdateAllPlots();
}

void MainFrame::SetActivePlot(int plotIndex) {
    if (plotIndex < 0 || plotIndex >= (int)m_canvases.size())
        return;

    m_activePlot = plotIndex;
    for (int i = 0; i < (int)m_canvases.size(); i++)
        m_canvases[i]->SetActive(i == plotIndex);

    const auto& ds = m_dataManager.dataset();
    if (ds.numCols > 0 && plotIndex < (int)m_plotConfigs.size()) {
        m_controlPanel->SetActiveColumns(
            static_cast<int>(m_plotConfigs[plotIndex].xCol),
            static_cast<int>(m_plotConfigs[plotIndex].yCol));
        m_controlPanel->SetActiveNormModes(
            static_cast<int>(m_plotConfigs[plotIndex].xNorm),
            static_cast<int>(m_plotConfigs[plotIndex].yNorm));
    }

    int row = plotIndex / m_gridCols;
    int col = plotIndex % m_gridCols;
    m_controlPanel->SetActivePlotLabel(row, col);
}

void MainFrame::UpdatePlot(int plotIndex) {
    const auto& ds = m_dataManager.dataset();
    if (ds.numRows == 0 || plotIndex < 0 || plotIndex >= (int)m_canvases.size())
        return;

    auto& cfg = m_plotConfigs[plotIndex];
    if (cfg.xCol >= ds.numCols) cfg.xCol = 0;
    if (cfg.yCol >= ds.numCols) cfg.yCol = 0;

    // Normalize each axis according to its per-plot per-axis mode
    auto xVals = NormalizeColumn(&ds.data[cfg.xCol], ds.numRows, ds.numCols, cfg.xNorm);
    auto yVals = NormalizeColumn(&ds.data[cfg.yCol], ds.numRows, ds.numCols, cfg.yNorm);

    float opacity = m_controlPanel->GetOpacity();

    std::vector<PointVertex> points;
    points.reserve(ds.numRows);

    for (size_t r = 0; r < ds.numRows; r++) {
        PointVertex v;
        v.x = xVals[r];
        v.y = yVals[r];
        v.r = 0.15f; v.g = 0.4f; v.b = 1.0f;
        v.a = opacity;
        points.push_back(v);
    }

    m_canvases[plotIndex]->SetPoints(std::move(points));

    // Re-apply current selection
    if (!m_selection.empty())
        m_canvases[plotIndex]->SetSelection(m_selection);
}

void MainFrame::UpdateAllPlots() {
    for (int i = 0; i < (int)m_canvases.size(); i++)
        UpdatePlot(i);
}

void MainFrame::HandleBrushRect(int plotIndex, float x0, float y0, float x1, float y1, bool extend) {
    const auto& ds = m_dataManager.dataset();
    if (ds.numRows == 0)
        return;

    // The brush rect is in the normalized coordinate space of the brushing plot.
    // Re-normalize the data using the same normalization to test against the rect.
    auto& cfg = m_plotConfigs[plotIndex];

    auto xVals = NormalizeColumn(&ds.data[cfg.xCol], ds.numRows, ds.numCols, cfg.xNorm);
    auto yVals = NormalizeColumn(&ds.data[cfg.yCol], ds.numRows, ds.numCols, cfg.yNorm);

    float rectMinX = std::min(x0, x1);
    float rectMaxX = std::max(x0, x1);
    float rectMinY = std::min(y0, y1);
    float rectMaxY = std::max(y0, y1);

    if (!extend)
        m_selection.assign(ds.numRows, 0);
    else if (m_selection.size() != ds.numRows)
        m_selection.assign(ds.numRows, 0);

    for (size_t r = 0; r < ds.numRows; r++) {
        if (xVals[r] >= rectMinX && xVals[r] <= rectMaxX &&
            yVals[r] >= rectMinY && yVals[r] <= rectMaxY) {
            m_selection[r] = m_activeBrush;
        }
    }

    PropagateSelection(m_selection);
}

void MainFrame::PropagateSelection(const std::vector<int>& selection) {
    m_selection = selection;
    for (auto* c : m_canvases)
        c->SetSelection(m_selection);

    int count = 0;
    for (int s : m_selection) if (s > 0) count++;
    m_controlPanel->SetSelectionInfo(count, static_cast<int>(m_selection.size()));
    SetStatusText(wxString::Format("Selected: %d / %zu points", count, m_selection.size()));
}

void MainFrame::ClearAllSelections() {
    m_selection.assign(m_selection.size(), 0);
    PropagateSelection(m_selection);
}

void MainFrame::InvertAllSelections() {
    for (auto& s : m_selection)
        s = (s == 0) ? 1 : 0;
    PropagateSelection(m_selection);
}

void MainFrame::LoadFile(const std::string& path) {
    fprintf(stderr, "Loading: %s\n", path.c_str());
    fflush(stderr);
    wxBusyCursor wait;

    if (!m_dataManager.loadAsciiFile(path)) {
        wxMessageBox("Failed to load file:\n" + m_dataManager.errorMessage(),
                     "Load Error", wxOK | wxICON_ERROR, this);
        return;
    }

    const auto& ds = m_dataManager.dataset();
    fprintf(stderr, "Loaded: %zu rows x %zu cols\n", ds.numRows, ds.numCols);

    m_controlPanel->SetColumns(ds.columnLabels);
    m_selection.assign(ds.numRows, 0);

    // Auto-assign column pairs to plots
    int numPlots = m_gridRows * m_gridCols;
    for (int i = 0; i < numPlots; i++) {
        size_t col1 = (i * 2) % ds.numCols;
        size_t col2 = (i * 2 + 1) % ds.numCols;
        m_plotConfigs[i] = {col1, col2};
    }

    SetTitle("Viewpoints — " + wxString(path).AfterLast('/'));
    UpdateAllPlots();
    SetActivePlot(0);
}

void MainFrame::OnOpen(wxCommandEvent& event) {
    // Hide canvases so the file dialog isn't covered by Metal layers
    for (auto* c : m_canvases) c->Hide();

    wxFileDialog dialog(this, "Open Data File", "", "",
        "All supported files|*.txt;*.csv;*.dat;*.tsv|"
        "Text files (*.txt)|*.txt|"
        "CSV files (*.csv)|*.csv|"
        "All files (*.*)|*.*",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    int result = dialog.ShowModal();
    for (auto* c : m_canvases) c->Show();
    Layout();

    if (result == wxID_OK)
        LoadFile(dialog.GetPath().ToStdString());
}

void MainFrame::OnQuit(wxCommandEvent& event) { Close(true); }

void MainFrame::OnAbout(wxCommandEvent& event) {
    wxMessageBox(
        "Viewpoints\n\n"
        "Fast interactive linked plotting\n"
        "of large multivariate datasets\n\n"
        "Original authors: Creon Levit & Paul Gazis\n"
        "Modernized with wxWidgets + WebGPU",
        "About Viewpoints", wxOK | wxICON_INFORMATION, this);
}

void MainFrame::OnAddRow(wxCommandEvent& event) {
    m_gridRows++;
    RebuildGrid();
}

void MainFrame::OnAddCol(wxCommandEvent& event) {
    m_gridCols++;
    RebuildGrid();
}

void MainFrame::OnRemoveRow(wxCommandEvent& event) {
    if (m_gridRows > 1) {
        m_gridRows--;
        RebuildGrid();
    }
}

void MainFrame::OnRemoveCol(wxCommandEvent& event) {
    if (m_gridCols > 1) {
        m_gridCols--;
        RebuildGrid();
    }
}
