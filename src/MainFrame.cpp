#include "MainFrame.h"
#include "WebGPUCanvas.h"
#include "ControlPanel.h"
#include <wx/progdlg.h>
#include <algorithm>
#include <cmath>
#include <random>

// Compute "nice" tick values (1, 2, 5 × 10^n series)
static std::vector<float> computeNiceTicks(float rangeMin, float rangeMax, int approxCount) {
    float range = rangeMax - rangeMin;
    if (range <= 0.0f) return {};
    float roughStep = range / approxCount;
    float mag = std::pow(10.0f, std::floor(std::log10(roughStep)));
    float residual = roughStep / mag;
    float niceStep;
    if (residual <= 1.5f) niceStep = mag;
    else if (residual <= 3.5f) niceStep = 2.0f * mag;
    else if (residual <= 7.5f) niceStep = 5.0f * mag;
    else niceStep = 10.0f * mag;

    float start = std::ceil(rangeMin / niceStep) * niceStep;
    std::vector<float> ticks;
    for (float v = start; v <= rangeMax + niceStep * 0.001f; v += niceStep)
        ticks.push_back(v);
    return ticks;
}

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Viewpoints")
{
    // Size window to ~85% of usable screen area
    wxRect clientArea = wxGetClientDisplayRect();  // excludes dock/menubar
    int w = std::min(static_cast<int>(clientArea.width * 0.90), 1800);
    int h = std::min(static_cast<int>(clientArea.height * 0.85), 960);
    SetSize(w, h);
    Centre();

    // Initialize default brush colors
    for (int i = 0; i < NUM_BRUSHES; i++)
        m_brushColors.push_back({kDefaultBrushes[i].r, kDefaultBrushes[i].g, kDefaultBrushes[i].b, 1.0f, i % SYMBOL_COUNT});

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
    fileMenu->Append(ID_SaveAll, "Save &All...\tCtrl+S", "Save all data as CSV");
    fileMenu->Append(ID_SaveSelected, "Save &Selected...\tCtrl+Shift+S", "Save selected points as CSV");
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
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnSave(false); }, ID_SaveAll);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnSave(true); }, ID_SaveSelected);
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

    // Wire control panel callbacks — per-plot (carry plotIndex)
    m_controlPanel->onRandomizeAxes = [this](int plotIndex) {
        const auto& ds = m_dataManager.dataset();
        if (ds.numCols < 2 || plotIndex < 0 || plotIndex >= (int)m_plotConfigs.size())
            return;
        std::random_device rd;
        std::mt19937 rng(rd());
        std::uniform_int_distribution<size_t> dist(0, ds.numCols - 1);
        size_t xCol = dist(rng);
        size_t yCol = dist(rng);
        while (yCol == xCol && ds.numCols > 1)
            yCol = dist(rng);
        m_plotConfigs[plotIndex].xCol = xCol;
        m_plotConfigs[plotIndex].yCol = yCol;
        m_controlPanel->SetPlotConfig(plotIndex, m_plotConfigs[plotIndex]);
        UpdatePlot(plotIndex);
    };

    m_controlPanel->onAxisChanged = [this](int plotIndex, int xCol, int yCol) {
        if (plotIndex >= 0 && plotIndex < (int)m_plotConfigs.size()) {
            m_plotConfigs[plotIndex].xCol = static_cast<size_t>(xCol);
            m_plotConfigs[plotIndex].yCol = static_cast<size_t>(yCol);
            UpdatePlot(plotIndex);
        }
    };

    m_controlPanel->onNormChanged = [this](int plotIndex, int xNorm, int yNorm) {
        if (plotIndex >= 0 && plotIndex < (int)m_plotConfigs.size()) {
            m_plotConfigs[plotIndex].xNorm = static_cast<NormMode>(xNorm);
            m_plotConfigs[plotIndex].yNorm = static_cast<NormMode>(yNorm);
            UpdatePlot(plotIndex);
        }
    };

    m_controlPanel->onShowUnselectedChanged = [this](int plotIndex, bool show) {
        if (plotIndex >= 0 && plotIndex < (int)m_plotConfigs.size()) {
            m_plotConfigs[plotIndex].showUnselected = show;
            m_canvases[plotIndex]->SetShowUnselected(show);
        }
    };

    m_controlPanel->onGridLinesChanged = [this](int plotIndex, bool show) {
        if (plotIndex >= 0 && plotIndex < (int)m_plotConfigs.size()) {
            m_plotConfigs[plotIndex].showGridLines = show;
            m_canvases[plotIndex]->SetShowGridLines(show);
        }
    };

    m_controlPanel->onShowHistogramsChanged = [this](int plotIndex, bool show) {
        if (plotIndex >= 0 && plotIndex < (int)m_plotConfigs.size()) {
            m_plotConfigs[plotIndex].showHistograms = show;
            m_canvases[plotIndex]->SetShowHistograms(show);
        }
    };

    // Per-plot rendering callbacks
    m_controlPanel->onPlotPointSizeChanged = [this](int plotIndex, float size) {
        if (plotIndex >= 0 && plotIndex < (int)m_plotConfigs.size()) {
            m_plotConfigs[plotIndex].pointSize = size;
            m_canvases[plotIndex]->SetPointSize(size);
        }
    };

    m_controlPanel->onPlotOpacityChanged = [this](int plotIndex, float alpha) {
        if (plotIndex >= 0 && plotIndex < (int)m_plotConfigs.size()) {
            m_plotConfigs[plotIndex].opacity = alpha;
            m_canvases[plotIndex]->SetOpacity(alpha);
        }
    };

    m_controlPanel->onPlotHistBinsChanged = [this](int plotIndex, int bins) {
        if (plotIndex >= 0 && plotIndex < (int)m_plotConfigs.size()) {
            m_plotConfigs[plotIndex].histBins = bins;
            m_canvases[plotIndex]->SetHistBins(bins);
        }
    };

    m_controlPanel->onTabSelected = [this](int plotIndex) {
        SetActivePlot(plotIndex);
    };

    m_controlPanel->onAllSelected = [this]() {
        HighlightAllPlots();
    };

    // Global callbacks (from "All" tab) — apply to all plots and update configs
    m_controlPanel->onPointSizeChanged = [this](float size) {
        for (int i = 0; i < (int)m_canvases.size(); i++) {
            m_plotConfigs[i].pointSize = size;
            m_canvases[i]->SetPointSize(size);
        }
    };

    m_controlPanel->onOpacityChanged = [this](float alpha) {
        for (int i = 0; i < (int)m_canvases.size(); i++) {
            m_plotConfigs[i].opacity = alpha;
            m_canvases[i]->SetOpacity(alpha);
        }
    };

    m_controlPanel->onHistBinsChanged = [this](int bins) {
        for (int i = 0; i < (int)m_canvases.size(); i++) {
            m_plotConfigs[i].histBins = bins;
            m_canvases[i]->SetHistBins(bins);
        }
    };

    m_controlPanel->onClearSelection = [this]() {
        ClearAllSelections();
    };

    m_controlPanel->onInvertSelection = [this]() {
        InvertAllSelections();
    };

    m_controlPanel->onKillSelected = [this]() {
        KillSelectedPoints();
    };

    m_controlPanel->onBrushChanged = [this](int brushIndex) {
        m_activeBrush = brushIndex + 1;
    };

    m_controlPanel->onBrushColorEdited = [this](int brushIndex, float r, float g, float b, float a) {
        if (brushIndex >= 0 && brushIndex < (int)m_brushColors.size()) {
            int sym = m_brushColors[brushIndex].symbol;  // preserve symbol
            m_brushColors[brushIndex] = {r, g, b, a, sym};
            for (auto* c : m_canvases)
                c->SetBrushColors(m_brushColors);
        }
    };

    m_controlPanel->onBrushSymbolChanged = [this](int brushIndex, int symbol) {
        if (brushIndex >= 0 && brushIndex < (int)m_brushColors.size()) {
            m_brushColors[brushIndex].symbol = symbol;
            for (auto* c : m_canvases)
                c->SetBrushColors(m_brushColors);
        }
    };

    m_controlPanel->onBrushSizeOffsetChanged = [this](int brushIndex, float offset) {
        if (brushIndex >= 0 && brushIndex < (int)m_brushColors.size()) {
            m_brushColors[brushIndex].sizeOffset = offset;
            for (auto* c : m_canvases)
                c->SetBrushColors(m_brushColors);
        }
    };

    RebuildGrid();
}

void MainFrame::RebuildGrid() {
    m_gridPanel->DestroyChildren();
    m_canvases.clear();
    m_plotWidgets.clear();

    int numPlots = m_gridRows * m_gridCols;
    m_plotConfigs.resize(numPlots);
    m_canvases.resize(numPlots);
    m_plotWidgets.resize(numPlots);

    wxFont tickFont(7, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    wxColour tickTextColor(130, 140, 160);
    wxColour bgColor(30, 30, 40);

    auto* gridSizer = new wxGridSizer(m_gridRows, m_gridCols, 2, 2);

    for (int i = 0; i < numPlots; i++) {
        auto& pw = m_plotWidgets[i];
        auto* cellPanel = new wxPanel(m_gridPanel);
        cellPanel->SetBackgroundColour(bgColor);
        pw.cellPanel = cellPanel;
        auto* cellSizer = new wxBoxSizer(wxHORIZONTAL);

        // Left column: Y label + Y tick panel
        auto* leftSizer = new wxBoxSizer(wxHORIZONTAL);

        pw.yLabel = new VerticalLabel(cellPanel);
        leftSizer->Add(pw.yLabel, 0, wxEXPAND);

        // Y tick panel: manually positioned labels
        pw.yTickPanel = new wxPanel(cellPanel, wxID_ANY, wxDefaultPosition, wxSize(46, -1));
        pw.yTickPanel->SetMinSize(wxSize(46, -1));
        pw.yTickPanel->SetBackgroundColour(bgColor);
        for (int t = 0; t < MAX_NICE_TICKS; t++) {
            auto* tickLabel = new wxStaticText(pw.yTickPanel, wxID_ANY, "",
                wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
            tickLabel->SetFont(tickFont);
            tickLabel->SetForegroundColour(tickTextColor);
            tickLabel->SetBackgroundColour(bgColor);
            tickLabel->Hide();
            pw.yTicks[t] = tickLabel;
        }
        leftSizer->Add(pw.yTickPanel, 0, wxEXPAND);

        cellSizer->Add(leftSizer, 0, wxEXPAND);

        // Right side: canvas + X tick panel + X label
        auto* rightSizer = new wxBoxSizer(wxVERTICAL);

        auto* canvas = new WebGPUCanvas(cellPanel, &m_gpuContext, i);
        m_canvases[i] = canvas;
        rightSizer->Add(canvas, 1, wxEXPAND);

        // X tick panel: manually positioned labels
        pw.xTickPanel = new wxPanel(cellPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 14));
        pw.xTickPanel->SetMinSize(wxSize(-1, 14));
        pw.xTickPanel->SetBackgroundColour(bgColor);
        for (int t = 0; t < MAX_NICE_TICKS; t++) {
            auto* tickLabel = new wxStaticText(pw.xTickPanel, wxID_ANY, "",
                wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
            tickLabel->SetFont(tickFont);
            tickLabel->SetForegroundColour(tickTextColor);
            tickLabel->SetBackgroundColour(bgColor);
            tickLabel->Hide();
            pw.xTicks[t] = tickLabel;
        }
        rightSizer->Add(pw.xTickPanel, 0, wxEXPAND);

        // X axis label
        pw.xLabel = new wxStaticText(cellPanel, wxID_ANY, "",
            wxDefaultPosition, wxSize(-1, 14),
            wxALIGN_CENTRE_HORIZONTAL | wxST_NO_AUTORESIZE);
        pw.xLabel->SetForegroundColour(wxColour(160, 170, 200));
        auto xFont = pw.xLabel->GetFont();
        xFont.SetPointSize(xFont.GetPointSize() - 1);
        pw.xLabel->SetFont(xFont);
        pw.xLabel->SetBackgroundColour(bgColor);
        rightSizer->Add(pw.xLabel, 0, wxEXPAND);

        cellSizer->Add(rightSizer, 1, wxEXPAND);
        cellPanel->SetSizer(cellSizer);
        gridSizer->Add(cellPanel, 1, wxEXPAND);

        // Wire callbacks
        canvas->onBrushRect = [this](int pi, float x0, float y0, float x1, float y1, bool ext) {
            HandleBrushRect(pi, x0, y0, x1, y1, ext);
        };
        canvas->onClearRequested = [this]() { ClearAllSelections(); };
        canvas->onInvertRequested = [this]() { InvertAllSelections(); };
        canvas->onKillRequested = [this]() { KillSelectedPoints(); };

        canvas->SetBrushColors(m_brushColors);
        canvas->onResetViewRequested = [this]() {
            for (auto* c : m_canvases) c->ResetView();
        };

        // Viewport change callback: compute nice ticks, update labels and grid lines
        canvas->onViewportChanged = [this](int pi, float vxMin, float vxMax, float vyMin, float vyMax) {
            if (pi < 0 || pi >= (int)m_plotWidgets.size()) return;
            if (pi >= (int)m_plotConfigs.size() || pi >= (int)m_canvases.size()) return;
            auto& pw2 = m_plotWidgets[pi];
            auto& cfg = m_plotConfigs[pi];
            const auto& ds = m_dataManager.dataset();
            if (ds.numCols == 0) return;

            float xDataMin, xDataMax, yDataMin, yDataMax;
            ds.columnRange(cfg.xCol, xDataMin, xDataMax);
            ds.columnRange(cfg.yCol, yDataMin, yDataMax);
            float xRange = xDataMax - xDataMin;
            float yRange = yDataMax - yDataMin;
            if (xRange == 0.0f) xRange = 1.0f;
            if (yRange == 0.0f) yRange = 1.0f;

            // Map normalized coords to data space
            auto normToDataX = [&](float normX) {
                return xDataMin + ((normX + 0.9f) / 1.8f) * xRange;
            };
            auto normToDataY = [&](float normY) {
                return yDataMin + ((normY + 0.9f) / 1.8f) * yRange;
            };
            // Map data space back to normalized coords
            auto dataToNormX = [&](float dataX) {
                return ((dataX - xDataMin) / xRange) * 1.8f - 0.9f;
            };
            auto dataToNormY = [&](float dataY) {
                return ((dataY - yDataMin) / yRange) * 1.8f - 0.9f;
            };

            float viewW = vxMax - vxMin;
            float viewH = vyMax - vyMin;

            // Compute nice ticks in data space for visible range
            float visDataXMin = normToDataX(vxMin);
            float visDataXMax = normToDataX(vxMax);
            float visDataYMin = normToDataY(vyMin);
            float visDataYMax = normToDataY(vyMax);

            auto xNiceTicks = computeNiceTicks(visDataXMin, visDataXMax, NUM_TICKS + 1);
            auto yNiceTicks = computeNiceTicks(visDataYMin, visDataYMax, NUM_TICKS + 1);

            // Map nice tick data values to clip space for grid lines
            std::vector<float> xClipPositions, yClipPositions;
            for (float dataX : xNiceTicks) {
                float normX = dataToNormX(dataX);
                float clipX = (normX - vxMin) / viewW * 2.0f - 1.0f;
                xClipPositions.push_back(clipX);
            }
            for (float dataY : yNiceTicks) {
                float normY = dataToNormY(dataY);
                float clipY = (normY - vyMin) / viewH * 2.0f - 1.0f;
                yClipPositions.push_back(clipY);
            }

            m_canvases[pi]->SetGridLinePositions(xClipPositions, yClipPositions);

            // Use the canvas size and position as reference
            wxSize canvasSize = m_canvases[pi]->GetClientSize();
            int canvasW = canvasSize.GetWidth();
            int canvasH = canvasSize.GetHeight();
            if (canvasW < 10 || canvasH < 10) return;

            // Get canvas position relative to its cell panel to align tick labels
            // The canvas is inside rightSizer, which is offset from the cell panel
            // by the Y label + Y tick panel width. The Y tick panel needs the
            // canvas's vertical offset (top of canvas relative to cell top).
            wxPoint canvasPos = m_canvases[pi]->GetPosition();
            // Walk up to find position relative to the cell panel
            wxWindow* w = m_canvases[pi]->GetParent();
            while (w && w != pw2.cellPanel && w->GetParent() != pw2.cellPanel) {
                wxPoint parentPos = w->GetPosition();
                canvasPos.x += parentPos.x;
                canvasPos.y += parentPos.y;
                w = w->GetParent();
            }
            // canvasPos.y = vertical offset of canvas top within the cell
            // The Y tick panel starts at the cell top, so Y tick labels need
            // this offset to align with the canvas.
            int canvasTopY = canvasPos.y;

            // Position X tick labels at grid line pixel positions
            for (int t = 0; t < MAX_NICE_TICKS; t++) {
                if (t < (int)xNiceTicks.size() && t < (int)xClipPositions.size()) {
                    float clipX = xClipPositions[t];
                    if (clipX > -0.9f && clipX < 0.9f) {
                        int px = static_cast<int>((clipX + 1.0f) * 0.5f * canvasW);
                        pw2.xTicks[t]->SetLabel(wxString::Format("%.4g", xNiceTicks[t]));
                        pw2.xTicks[t]->SetSize(pw2.xTicks[t]->GetBestSize());
                        wxSize tsz = pw2.xTicks[t]->GetSize();
                        pw2.xTicks[t]->SetPosition(wxPoint(px - tsz.GetWidth() / 2, 0));
                        pw2.xTicks[t]->Show();
                    } else {
                        pw2.xTicks[t]->Hide();
                    }
                } else {
                    pw2.xTicks[t]->Hide();
                }
            }

            // Position Y tick labels at grid line pixel positions
            // Offset by canvasTopY so labels align with the canvas, not the cell
            for (int t = 0; t < MAX_NICE_TICKS; t++) {
                if (t < (int)yNiceTicks.size() && t < (int)yClipPositions.size()) {
                    float clipY = yClipPositions[t];
                    if (clipY > -0.9f && clipY < 0.9f) {
                        int py = canvasTopY + static_cast<int>((1.0f - clipY) * 0.5f * canvasH);
                        pw2.yTicks[t]->SetLabel(wxString::Format("%.4g", yNiceTicks[t]));
                        pw2.yTicks[t]->SetSize(pw2.yTicks[t]->GetBestSize());
                        wxSize tsz = pw2.yTicks[t]->GetSize();
                        pw2.yTicks[t]->SetPosition(wxPoint(46 - tsz.GetWidth() - 2, py - tsz.GetHeight() / 2));
                        pw2.yTicks[t]->Show();
                    } else {
                        pw2.yTicks[t]->Hide();
                    }
                } else {
                    pw2.yTicks[t]->Hide();
                }
            }
        };

        canvas->Bind(wxEVT_LEFT_DOWN, [this, i](wxMouseEvent& evt) {
            SetActivePlot(i);
            evt.Skip();
        });
    }

    m_gridPanel->SetSizer(gridSizer);
    m_gridPanel->Layout();

    // Rebuild control panel tabs to match grid
    m_controlPanel->RebuildTabs(m_gridRows, m_gridCols);

    // Auto-assign columns
    const auto& ds = m_dataManager.dataset();
    if (ds.numCols > 0) {
        for (int i = 0; i < numPlots; i++) {
            size_t col1 = (i * 2) % ds.numCols;
            size_t col2 = (i * 2 + 1) % ds.numCols;
            m_plotConfigs[i] = {col1, col2};
        }
    }

    // Sync each tab's widgets with its config
    for (int i = 0; i < numPlots; i++)
        m_controlPanel->SetPlotConfig(i, m_plotConfigs[i]);

    SetActivePlot(0);

    if (ds.numRows > 0)
        UpdateAllPlots();
}

void MainFrame::SetActivePlot(int plotIndex) {
    if (plotIndex < 0 || plotIndex >= (int)m_canvases.size())
        return;

    m_activePlot = plotIndex;

    // Highlight label/tick area of active plot (not the canvas itself)
    wxColour activeBg(50, 50, 70);
    wxColour normalBg(30, 30, 40);
    for (int i = 0; i < (int)m_plotWidgets.size(); i++) {
        auto& pw = m_plotWidgets[i];
        wxColour bg = (i == plotIndex) ? activeBg : normalBg;
        if (pw.cellPanel) pw.cellPanel->SetBackgroundColour(bg);
        if (pw.xTickPanel) pw.xTickPanel->SetBackgroundColour(bg);
        if (pw.yTickPanel) pw.yTickPanel->SetBackgroundColour(bg);
        if (pw.xLabel) pw.xLabel->SetBackgroundColour(bg);
        if (pw.yLabel) pw.yLabel->SetBackgroundColour(bg);
        // Update tick label backgrounds
        for (auto* t : pw.xTicks) if (t) t->SetBackgroundColour(bg);
        for (auto* t : pw.yTicks) if (t) t->SetBackgroundColour(bg);
        if (pw.cellPanel) pw.cellPanel->Refresh();
    }

    // Canvas background stays black (no SetActive highlight)
    for (auto* c : m_canvases) c->SetActive(false);

    m_controlPanel->SelectTab(plotIndex);
    if (plotIndex < (int)m_plotConfigs.size())
        m_controlPanel->SetPlotConfig(plotIndex, m_plotConfigs[plotIndex]);
}

void MainFrame::HighlightAllPlots() {
    wxColour activeBg(50, 50, 70);
    for (auto& pw : m_plotWidgets) {
        if (pw.cellPanel) pw.cellPanel->SetBackgroundColour(activeBg);
        if (pw.xTickPanel) pw.xTickPanel->SetBackgroundColour(activeBg);
        if (pw.yTickPanel) pw.yTickPanel->SetBackgroundColour(activeBg);
        if (pw.xLabel) pw.xLabel->SetBackgroundColour(activeBg);
        if (pw.yLabel) pw.yLabel->SetBackgroundColour(activeBg);
        for (auto* t : pw.xTicks) if (t) t->SetBackgroundColour(activeBg);
        for (auto* t : pw.yTicks) if (t) t->SetBackgroundColour(activeBg);
        if (pw.cellPanel) pw.cellPanel->Refresh();
    }
    for (auto* c : m_canvases) c->SetActive(false);
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
        v.symbol = 0.0f;
        v.sizeScale = 1.0f;
        points.push_back(v);
    }

    m_canvases[plotIndex]->SetPoints(std::move(points));

    // Set axis labels
    if (plotIndex < (int)m_plotWidgets.size()) {
        m_plotWidgets[plotIndex].xLabel->SetLabel(ds.columnLabels[cfg.xCol]);
        m_plotWidgets[plotIndex].yLabel->SetLabel(ds.columnLabels[cfg.yCol]);
    }

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

    if (m_selection.size() != ds.numRows)
        m_selection.assign(ds.numRows, 0);

    if (!extend) {
        // Clear only the current brush's selections, preserve other brushes
        for (auto& s : m_selection)
            if (s == m_activeBrush) s = 0;
    }

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
        s = (s == 0) ? m_activeBrush : 0;
    PropagateSelection(m_selection);
}

void MainFrame::KillSelectedPoints() {
    int count = 0;
    for (int s : m_selection) if (s > 0) count++;
    if (count == 0) return;

    size_t removed = m_dataManager.removeSelectedRows(m_selection);
    if (removed == 0) return;

    // Reset selection and rebuild all plots with reduced data
    const auto& ds = m_dataManager.dataset();
    m_selection.assign(ds.numRows, 0);
    UpdateAllPlots();
    PropagateSelection(m_selection);

    SetStatusText(wxString::Format("Deleted %zu points, %zu remaining",
                                    removed, ds.numRows));
}

void MainFrame::LoadFile(const std::string& path) {
    wxProgressDialog progressDlg("Loading Data", "Loading " + wxString(path).AfterLast('/'),
                                  100, this,
                                  wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_SMOOTH | wxPD_CAN_ABORT);

    bool cancelled = false;
    auto progressCb = [&](size_t bytesRead, size_t totalBytes) -> bool {
        int pct = (totalBytes > 0) ? static_cast<int>((bytesRead * 100) / totalBytes) : 0;
        pct = std::min(pct, 99);
        cancelled = !progressDlg.Update(pct,
            wxString::Format("Loading... %zu KB / %zu KB",
                             bytesRead / 1024, totalBytes / 1024));
        wxYield();  // keep UI responsive
        return !cancelled;
    };

    if (!m_dataManager.loadAsciiFile(path, progressCb)) {
        if (!cancelled) {
            wxMessageBox("Failed to load file:\n" + m_dataManager.errorMessage(),
                         "Load Error", wxOK | wxICON_ERROR, this);
        }
        return;
    }

    progressDlg.Update(100, "Processing...");

    const auto& ds = m_dataManager.dataset();

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

void MainFrame::OnSave(bool selectedOnly) {
    const auto& ds = m_dataManager.dataset();
    if (ds.numRows == 0) {
        wxMessageBox("No data to save.", "Save", wxOK | wxICON_INFORMATION, this);
        return;
    }

    if (selectedOnly) {
        int count = 0;
        for (int s : m_selection) if (s > 0) count++;
        if (count == 0) {
            wxMessageBox("No points selected.", "Save Selected", wxOK | wxICON_INFORMATION, this);
            return;
        }
    }

    wxString defaultName = selectedOnly ? "selected.csv" : "data.csv";
    wxFileDialog dialog(this,
        selectedOnly ? "Save Selected Points" : "Save All Data",
        "", defaultName,
        "CSV files (*.csv)|*.csv|Text files (*.txt)|*.txt|All files (*.*)|*.*",
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (dialog.ShowModal() == wxID_OK) {
        std::string path = dialog.GetPath().ToStdString();
        bool ok = m_dataManager.saveAsCsv(path, selectedOnly ? m_selection : std::vector<int>{});
        if (ok) {
            SetStatusText("Saved: " + dialog.GetPath());
        } else {
            wxMessageBox("Failed to save file.", "Save Error", wxOK | wxICON_ERROR, this);
        }
    }
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
