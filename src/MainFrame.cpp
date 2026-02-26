// Viewpoints (MIT License) - See LICENSE file
#include "MainFrame.h"
#include "WebGPUCanvas.h"
#include "ControlPanel.h"
#include <wx/popupwin.h>
#include <wx/progdlg.h>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <random>
#include <chrono>
#include <queue>
#include <unordered_set>

// Lightweight tooltip popup for displaying point values
class PointTooltip : public wxPopupWindow {
public:
    PointTooltip(wxWindow* parent) : wxPopupWindow(parent, wxBORDER_SIMPLE) {
        SetBackgroundColour(wxColour(25, 25, 38));
        m_text = new wxStaticText(this, wxID_ANY, "");
        m_text->SetForegroundColour(wxColour(200, 210, 230));
        m_text->SetBackgroundColour(wxColour(25, 25, 38));
        auto font = m_text->GetFont();
        font.SetPointSize(font.GetPointSize() - 1);
        font.SetFamily(wxFONTFAMILY_TELETYPE);
        m_text->SetFont(font);
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(m_text, 0, wxALL, 4);
        SetSizer(sizer);
    }

    void ShowAt(const wxString& content, const wxPoint& screenPos) {
        m_text->SetLabel(content);
        Layout();
        Fit();
        SetPosition(screenPos);
        if (!IsShown()) Show();
    }

private:
    wxStaticText* m_text;
};

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
    // Brush 0 = unselected points (uses vertex/colormap color by default)
    m_brushColors.push_back({0.15f, 0.4f, 1.0f, 1.0f, SYMBOL_CIRCLE, 0.0f, 0.0f, true});
    // Brushes 1-7 = selection brushes
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
    fileMenu->Append(ID_SaveAll, "Save &All...\tCtrl+S", "Save all data");
    fileMenu->Append(ID_SaveSelected, "Save &Selected...\tCtrl+Shift+S", "Save selected points");
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
    viewMenu->Append(ID_ResetViews, "Reset View\tR", "Reset pan and zoom on active plot");
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
        if (m_activePlot >= 0 && m_activePlot < (int)m_canvases.size()) {
            m_canvases[m_activePlot]->ResetView();
            m_plotConfigs[m_activePlot].rotationY = 0.0f;
            m_controlPanel->StopSpinRock(m_activePlot);
        }
    }, ID_ResetViews);
}

void MainFrame::CreateLayout() {
    auto* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    m_controlPanel = new ControlPanel(this);
    mainSizer->Add(m_controlPanel, 0, wxEXPAND);

    m_gridPanel = new wxPanel(this);
    m_gridPanel->SetBackgroundColour(wxColour(120, 120, 130));
    m_gridPanel->Bind(wxEVT_SIZE, &MainFrame::OnGridSize, this);
    m_gridPanel->Bind(wxEVT_LEFT_DOWN, &MainFrame::OnGridMouse, this);
    m_gridPanel->Bind(wxEVT_LEFT_UP, &MainFrame::OnGridMouse, this);
    m_gridPanel->Bind(wxEVT_MOTION, &MainFrame::OnGridMouse, this);
    mainSizer->Add(m_gridPanel, 1, wxEXPAND);

    SetSizer(mainSizer);

    // Wire control panel callbacks — per-plot (carry plotIndex)
    m_controlPanel->onRandomizeAxes = [this](int plotIndex) {
        const auto& ds = m_dataManager.dataset();
        if (ds.numCols < 2 || plotIndex < 0 || plotIndex >= (int)m_plotConfigs.size())
            return;
        auto& cfg = m_plotConfigs[plotIndex];
        std::random_device rd;
        std::mt19937 rng(rd());
        std::uniform_int_distribution<size_t> dist(0, ds.numCols - 1);
        // Only randomize unlocked axes
        if (!cfg.xLocked) {
            cfg.xCol = dist(rng);
            cfg.xNorm = DefaultNormForColumn(cfg.xCol);
        }
        if (!cfg.yLocked) {
            cfg.yCol = dist(rng);
            while (cfg.yCol == cfg.xCol && ds.numCols > 1)
                cfg.yCol = dist(rng);
            cfg.yNorm = DefaultNormForColumn(cfg.yCol);
        }
        m_controlPanel->SetPlotConfig(plotIndex, cfg);
        UpdatePlot(plotIndex);
    };

    m_controlPanel->onAxisChanged = [this](int plotIndex, int xCol, int yCol) {
        if (plotIndex >= 0 && plotIndex < (int)m_plotConfigs.size()) {
            auto& cfg = m_plotConfigs[plotIndex];
            cfg.xCol = static_cast<size_t>(xCol);
            cfg.yCol = static_cast<size_t>(yCol);
            cfg.xNorm = DefaultNormForColumn(cfg.xCol);
            cfg.yNorm = DefaultNormForColumn(cfg.yCol);
            m_controlPanel->SetPlotConfig(plotIndex, cfg);
            UpdatePlot(plotIndex);
        }
    };

    m_controlPanel->onAxisLockChanged = [this](int plotIndex, bool xLock, bool yLock) {
        if (plotIndex >= 0 && plotIndex < (int)m_plotConfigs.size()) {
            m_plotConfigs[plotIndex].xLocked = xLock;
            m_plotConfigs[plotIndex].yLocked = yLock;
        }
    };

    // Axis lock callback is also wired per-canvas below in RebuildGrid

    m_controlPanel->onNormChanged = [this](int plotIndex, int xNorm, int yNorm) {
        if (plotIndex >= 0 && plotIndex < (int)m_plotConfigs.size()) {
            m_plotConfigs[plotIndex].xNorm = static_cast<NormMode>(xNorm);
            m_plotConfigs[plotIndex].yNorm = static_cast<NormMode>(yNorm);
            UpdatePlot(plotIndex);
        }
    };

    m_controlPanel->onZAxisChanged = [this](int plotIndex, int zCol, int zNorm) {
        if (plotIndex >= 0 && plotIndex < (int)m_plotConfigs.size()) {
            m_plotConfigs[plotIndex].zCol = zCol;
            m_plotConfigs[plotIndex].zNorm = static_cast<NormMode>(zNorm);
            UpdatePlot(plotIndex);
        }
    };

    m_controlPanel->onRotationChanged = [this](int plotIndex, float angle) {
        if (plotIndex >= 0 && plotIndex < (int)m_plotConfigs.size()) {
            m_plotConfigs[plotIndex].rotationY = angle;
            m_canvases[plotIndex]->SetRotation(angle);
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

    m_controlPanel->onGlobalTooltipChanged = [this](bool show) {
        m_globalTooltip = show;
        for (auto* c : m_canvases)
            c->SetShowTooltip(show);
        if (!show) HideAllTooltips();
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


    m_controlPanel->onHistBinsChanged = [this](int bins) {
        for (int i = 0; i < (int)m_canvases.size(); i++) {
            m_plotConfigs[i].histBins = bins;
            m_canvases[i]->SetHistBins(bins);
        }
    };

    m_controlPanel->onColorMapChanged = [this](int colormap, int colorVar) {
        m_colorMap = static_cast<ColorMapType>(colormap);
        m_colorVariable = colorVar;
        bool additive = (m_colorMap == ColorMapType::Default);
        // Reset brush 0 to vertex/colormap mode so the colormap takes effect
        m_brushColors[0].useVertexColor = true;
        // Upload brush params FIRST (so useVertexColor flag is current)
        for (auto* c : m_canvases) {
            c->SetBrushColors(m_brushColors);
            c->SetUseAdditiveBlending(additive);
            c->SetColorMap(colormap, colorVar);
        }
        // Then rebuild plots with colormap-colored vertex data
        UpdateAllPlots();
    };

    m_controlPanel->onBackgroundChanged = [this](float brightness) {
        m_bgBrightness = brightness;
        for (auto* c : m_canvases) c->SetBackground(brightness);
    };

    m_controlPanel->onDeferRedrawsChanged = [this](bool defer) {
        for (auto* c : m_canvases) c->SetDeferRedraws(defer);
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

    m_controlPanel->onSaveData = [this](bool selectedOnly) {
        OnSave(selectedOnly);
    };

    m_controlPanel->onBrushChanged = [this](int brushIndex) {
        // Brush 0 controls unselected appearance; brushes 1-7 are selection brushes
        // Active brush for selection: brush 0 maps to 1 (can't select with brush 0)
        m_activeBrush = (brushIndex == 0) ? 1 : brushIndex;
    };

    m_controlPanel->onBrushReset = [this](int brushIndex) {
        if (brushIndex >= 0 && brushIndex < (int)m_brushColors.size()) {
            if (brushIndex == 0) {
                // Reset brush 0 to vertex/colormap mode
                m_brushColors[0] = {0.15f, 0.4f, 1.0f, 1.0f, SYMBOL_CIRCLE, 0.0f, 0.0f, true};
            } else {
                // Reset selection brush to default
                int di = brushIndex - 1;
                m_brushColors[brushIndex] = {
                    kDefaultBrushes[di].r, kDefaultBrushes[di].g, kDefaultBrushes[di].b,
                    1.0f, di % SYMBOL_COUNT, 0.0f, 0.0f, false};
            }
            for (auto* c : m_canvases)
                c->SetBrushColors(m_brushColors);
        }
    };

    m_controlPanel->onBrushColorEdited = [this](int brushIndex, float r, float g, float b, float a) {
        if (brushIndex >= 0 && brushIndex < (int)m_brushColors.size()) {
            int sym = m_brushColors[brushIndex].symbol;
            float sizeOff = m_brushColors[brushIndex].sizeOffset;
            float opOff = m_brushColors[brushIndex].opacityOffset;
            // When user explicitly picks a color, disable vertex color mode
            m_brushColors[brushIndex] = {r, g, b, a, sym, sizeOff, opOff, false};
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

    m_controlPanel->onBrushOpacityOffsetChanged = [this](int brushIndex, float offset) {
        if (brushIndex >= 0 && brushIndex < (int)m_brushColors.size()) {
            m_brushColors[brushIndex].opacityOffset = offset;
            for (auto* c : m_canvases)
                c->SetBrushColors(m_brushColors);
        }
    };

    RebuildGrid();
}

void MainFrame::RebuildGrid() {
    // Clean up old tooltips
    for (auto* tt : m_tooltips) tt->Destroy();
    m_tooltips.clear();
    m_hoveredDataRow = -1;

    m_gridPanel->DestroyChildren();
    m_canvases.clear();
    m_plotWidgets.clear();

    // Initialize proportions (reset to uniform if dimensions changed)
    if ((int)m_colWidths.size() != m_gridCols)
        m_colWidths.assign(m_gridCols, 1.0 / m_gridCols);
    if ((int)m_rowHeights.size() != m_gridRows)
        m_rowHeights.assign(m_gridRows, 1.0 / m_gridRows);

    int numPlots = m_gridRows * m_gridCols;
    m_plotConfigs.resize(numPlots);
    m_canvases.resize(numPlots);
    m_plotWidgets.resize(numPlots);

    wxFont tickFont(7, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    wxColour tickTextColor(130, 140, 160);
    wxColour bgColor(30, 30, 40);

    for (int i = 0; i < numPlots; i++) {
        auto& pw = m_plotWidgets[i];
        auto* cellPanel = new wxPanel(m_gridPanel);
        cellPanel->SetBackgroundColour(bgColor);
        pw.cellPanel = cellPanel;
        auto* cellSizer = new wxBoxSizer(wxHORIZONTAL);

        // Left column: margin + Y label + Y tick panel
        auto* leftSizer = new wxBoxSizer(wxHORIZONTAL);

        pw.yLabel = new VerticalLabel(cellPanel);
        leftSizer->Add(pw.yLabel, 0, wxEXPAND | wxLEFT, 4);

        // Y tick panel: manually positioned labels
        pw.yTickPanel = new wxPanel(cellPanel, wxID_ANY, wxDefaultPosition, wxSize(16, -1));
        pw.yTickPanel->SetMinSize(wxSize(16, -1));
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
        canvas->SetShowTooltip(m_globalTooltip);
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
        rightSizer->Add(pw.xLabel, 0, wxEXPAND | wxBOTTOM, 4);

        // Click on axis labels to change variable
        pw.xLabel->SetCursor(wxCursor(wxCURSOR_HAND));
        pw.xLabel->Bind(wxEVT_LEFT_DOWN, [this, i](wxMouseEvent&) {
            const auto& ds = m_dataManager.dataset();
            if (ds.numCols == 0) return;
            wxMenu menu;
            for (size_t c = 0; c < ds.numCols; c++)
                menu.Append(static_cast<int>(c), wxString::FromUTF8(ds.columnLabels[c]));
            menu.Bind(wxEVT_MENU, [this, i](wxCommandEvent& evt) {
                int col = evt.GetId();
                m_plotConfigs[i].xCol = static_cast<size_t>(col);
                m_plotConfigs[i].xNorm = DefaultNormForColumn(col);
                UpdatePlot(i);
                m_controlPanel->SetPlotConfig(i, m_plotConfigs[i]);
            });
            m_plotWidgets[i].xLabel->PopupMenu(&menu);
        });
        pw.yLabel->SetCursor(wxCursor(wxCURSOR_HAND));
        pw.yLabel->Bind(wxEVT_LEFT_DOWN, [this, i](wxMouseEvent&) {
            const auto& ds = m_dataManager.dataset();
            if (ds.numCols == 0) return;
            wxMenu menu;
            for (size_t c = 0; c < ds.numCols; c++)
                menu.Append(static_cast<int>(c), wxString::FromUTF8(ds.columnLabels[c]));
            menu.Bind(wxEVT_MENU, [this, i](wxCommandEvent& evt) {
                int col = evt.GetId();
                m_plotConfigs[i].yCol = static_cast<size_t>(col);
                m_plotConfigs[i].yNorm = DefaultNormForColumn(col);
                UpdatePlot(i);
                m_controlPanel->SetPlotConfig(i, m_plotConfigs[i]);
            });
            m_plotWidgets[i].yLabel->PopupMenu(&menu);
        });

        cellSizer->Add(rightSizer, 1, wxEXPAND);
        cellPanel->SetSizer(cellSizer);

        // Wire callbacks
        canvas->onBrushRect = [this](int pi, float x0, float y0, float x1, float y1, bool ext) {
            HandleBrushRect(pi, x0, y0, x1, y1, ext);
        };
        canvas->onClearRequested = [this]() { ClearAllSelections(); };
        canvas->onInvertRequested = [this]() { InvertAllSelections(); };
        canvas->onKillRequested = [this]() { KillSelectedPoints(); };
        canvas->onToggleUnselected = [this]() {
            // Toggle globally across all plots
            bool newState = !m_plotConfigs[0].showUnselected;
            for (int j = 0; j < (int)m_canvases.size(); j++) {
                m_plotConfigs[j].showUnselected = newState;
                m_canvases[j]->SetShowUnselected(newState);
            }
            if (m_activePlot >= 0 && m_activePlot < (int)m_plotConfigs.size())
                m_controlPanel->SetPlotConfig(m_activePlot, m_plotConfigs[m_activePlot]);
        };

        // Linked axis view: propagate pan/zoom to plots sharing locked variables
        canvas->onViewChanged = [this](int pi, float panX, float panY, float zoomX, float zoomY) {
            if (pi < 0 || pi >= (int)m_plotConfigs.size()) return;
            auto& srcCfg = m_plotConfigs[pi];

            for (int j = 0; j < (int)m_canvases.size(); j++) {
                if (j == pi) continue;
                auto& dstCfg = m_plotConfigs[j];
                bool needsUpdate = false;
                float dstPanX = m_canvases[j]->GetPanX();
                float dstPanY = m_canvases[j]->GetPanY();
                float dstZoomX = m_canvases[j]->GetZoomX();
                float dstZoomY = m_canvases[j]->GetZoomY();

                // If source X is locked, sync only the matching axis
                if (srcCfg.xLocked) {
                    if (dstCfg.xCol == srcCfg.xCol) {
                        dstPanX = panX; dstZoomX = zoomX; needsUpdate = true;
                    } else if (dstCfg.yCol == srcCfg.xCol) {
                        dstPanY = panX; dstZoomY = zoomX; needsUpdate = true;
                    }
                }
                // If source Y is locked, sync only the matching axis
                if (srcCfg.yLocked) {
                    if (dstCfg.yCol == srcCfg.yCol) {
                        dstPanY = panY; dstZoomY = zoomY; needsUpdate = true;
                    } else if (dstCfg.xCol == srcCfg.yCol) {
                        dstPanX = panY; dstZoomX = zoomY; needsUpdate = true;
                    }
                }

                if (needsUpdate)
                    m_canvases[j]->SetPanZoom(dstPanX, dstPanY, dstZoomX, dstZoomY);
            }
        };

        // Show selection box coordinates in status bar during drag
        canvas->onSelectionDrag = [this](int pi, float x0, float y0, float x1, float y1) {
            if (pi < 0 || pi >= (int)m_plotConfigs.size()) return;
            auto& cfg = m_plotConfigs[pi];
            const auto& ds = m_dataManager.dataset();
            if (ds.numCols == 0) return;

            float xDataMin, xDataMax, yDataMin, yDataMax;
            ds.columnRange(cfg.xCol, xDataMin, xDataMax);
            ds.columnRange(cfg.yCol, yDataMin, yDataMax);
            float xRange = xDataMax - xDataMin; if (xRange == 0) xRange = 1;
            float yRange = yDataMax - yDataMin; if (yRange == 0) yRange = 1;

            float dLeft   = xDataMin + ((x0 + 0.9f) / 1.8f) * xRange;
            float dRight  = xDataMin + ((x1 + 0.9f) / 1.8f) * xRange;
            float dBottom = yDataMin + ((y0 + 0.9f) / 1.8f) * yRange;
            float dTop    = yDataMin + ((y1 + 0.9f) / 1.8f) * yRange;

            // Format boundary values, showing category strings for categorical axes
            auto fmtVal = [&](float val, size_t col) -> wxString {
                if (col < ds.columnMeta.size() && ds.columnMeta[col].isCategorical) {
                    const auto& cats = ds.columnMeta[col].categories;
                    if (!cats.empty()) {
                        int idx = std::max(0, std::min(static_cast<int>(std::round(val)),
                                                       static_cast<int>(cats.size()) - 1));
                        return wxString::FromUTF8(cats[idx]);
                    }
                }
                return wxString::Format("%.4g", val);
            };

            int selCount = 0;
            for (int s : m_selection) if (s > 0) selCount++;
            float pct = ds.numRows > 0 ? (100.0f * selCount / ds.numRows) : 0;
            SetStatusText(m_dataStatusText + wxString::Format("  |  Selection: X [%s, %s]  Y [%s, %s]  |  %d / %zu (%.1f%%)",
                                            fmtVal(dLeft, cfg.xCol), fmtVal(dRight, cfg.xCol),
                                            fmtVal(dBottom, cfg.yCol), fmtVal(dTop, cfg.yCol),
                                            selCount, ds.numRows, pct));
        };

        canvas->SetBrushColors(m_brushColors);
        canvas->onResetViewRequested = [this, i]() {
            m_canvases[i]->ResetView();
            m_plotConfigs[i].rotationY = 0.0f;
            m_controlPanel->StopSpinRock(i);
        };
        canvas->onResetAllViewsRequested = [this]() {
            for (int j = 0; j < (int)m_canvases.size(); j++) {
                m_canvases[j]->ResetView();
                m_plotConfigs[j].rotationY = 0.0f;
                m_controlPanel->StopSpinRock(j);
            }
        };

        // Create tooltip popup for this canvas
        m_tooltips.push_back(new PointTooltip(this));

        canvas->onTooltipToggled = [this](int pi, bool show) {
            m_globalTooltip = show;
            for (auto* c : m_canvases)
                c->SetShowTooltip(show);
            m_controlPanel->SetGlobalTooltip(show);
            if (!show) HideAllTooltips();
        };

        // Tooltip hover callback
        canvas->onPointHover = [this](int pi, int dataRow, int sx, int sy) {
            if (dataRow < 0) {
                HideAllTooltips();
                m_hoveredDataRow = -1;
                return;
            }
            const auto& ds = m_dataManager.dataset();
            if (dataRow >= (int)ds.numRows) return;
            m_hoveredDataRow = dataRow;
            wxString text = BuildTooltipText(dataRow);

            if (pi >= 0 && pi < (int)m_tooltips.size()) {
                wxPoint screenPos = m_canvases[pi]->ClientToScreen(wxPoint(sx, sy));
                m_tooltips[pi]->ShowAt(text, screenPos + wxPoint(12, 12));
            }
            for (int j = 0; j < (int)m_tooltips.size(); j++) {
                if (j != pi) m_tooltips[j]->Hide();
            }
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

            bool xCat = cfg.xCol < ds.columnMeta.size() && ds.columnMeta[cfg.xCol].isCategorical;
            bool yCat = cfg.yCol < ds.columnMeta.size() && ds.columnMeta[cfg.yCol].isCategorical;

            // Helper: truncate long category strings for tick labels
            auto truncate = [](const std::string& s, size_t maxLen) -> std::string {
                if (s.size() <= maxLen) return s;
                return s.substr(0, maxLen - 1) + "\xe2\x80\xa6"; // UTF-8 ellipsis
            };

            // Helper: get tick label for a data value (categorical or numeric)
            auto tickLabel = [&](float dataVal, bool isCat, const std::vector<std::string>& cats, size_t maxLen) -> wxString {
                if (isCat && !cats.empty()) {
                    int idx = std::max(0, std::min(static_cast<int>(std::round(dataVal)), static_cast<int>(cats.size()) - 1));
                    return wxString::FromUTF8(truncate(cats[idx], maxLen));
                }
                return wxString::Format("%.4g", dataVal);
            };

            // Compute ticks: categorical low-cardinality uses explicit integer positions
            auto xNiceTicks = computeNiceTicks(visDataXMin, visDataXMax, NUM_TICKS + 1);
            auto yNiceTicks = computeNiceTicks(visDataYMin, visDataYMax, NUM_TICKS + 1);
            std::vector<wxString> xTickLabels, yTickLabels;

            if (xCat) {
                const auto& xCats = ds.columnMeta[cfg.xCol].categories;
                if ((int)xCats.size() <= MAX_NICE_TICKS) {
                    // Low cardinality: explicit integer ticks 0..N-1
                    xNiceTicks.clear();
                    for (int i = 0; i < (int)xCats.size(); i++)
                        xNiceTicks.push_back(static_cast<float>(i));
                }
                for (float v : xNiceTicks)
                    xTickLabels.push_back(tickLabel(v, true, xCats, 8));
            } else {
                for (float v : xNiceTicks)
                    xTickLabels.push_back(wxString::Format("%.4g", v));
            }

            if (yCat) {
                const auto& yCats = ds.columnMeta[cfg.yCol].categories;
                if ((int)yCats.size() <= MAX_NICE_TICKS) {
                    yNiceTicks.clear();
                    for (int i = 0; i < (int)yCats.size(); i++)
                        yNiceTicks.push_back(static_cast<float>(i));
                }
                for (float v : yNiceTicks)
                    yTickLabels.push_back(tickLabel(v, true, yCats, 8));
            } else {
                for (float v : yNiceTicks)
                    yTickLabels.push_back(wxString::Format("%.4g", v));
            }

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
            wxPoint canvasPos = m_canvases[pi]->GetPosition();
            wxWindow* w = m_canvases[pi]->GetParent();
            while (w && w != pw2.cellPanel && w->GetParent() != pw2.cellPanel) {
                wxPoint parentPos = w->GetPosition();
                canvasPos.x += parentPos.x;
                canvasPos.y += parentPos.y;
                w = w->GetParent();
            }
            int canvasTopY = canvasPos.y;

            // Position X tick labels at grid line pixel positions
            for (int t = 0; t < MAX_NICE_TICKS; t++) {
                if (t < (int)xNiceTicks.size() && t < (int)xClipPositions.size()) {
                    float clipX = xClipPositions[t];
                    if (clipX > -0.9f && clipX < 0.9f) {
                        int px = static_cast<int>((clipX + 1.0f) * 0.5f * canvasW);
                        pw2.xTicks[t]->SetLabel(xTickLabels[t]);
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
            for (int t = 0; t < MAX_NICE_TICKS; t++) {
                if (t < (int)yNiceTicks.size() && t < (int)yClipPositions.size()) {
                    float clipY = yClipPositions[t];
                    if (clipY > -0.9f && clipY < 0.9f) {
                        int py = canvasTopY + static_cast<int>((1.0f - clipY) * 0.5f * canvasH);
                        pw2.yTicks[t]->SetLabel(yTickLabels[t]);
                        pw2.yTicks[t]->SetSize(pw2.yTicks[t]->GetBestSize());
                        wxSize tsz = pw2.yTicks[t]->GetSize();
                        pw2.yTicks[t]->SetPosition(wxPoint(16 - tsz.GetWidth() - 2, py - tsz.GetHeight() / 2));
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

    LayoutGrid();

    // Rebuild control panel tabs to match grid
    m_controlPanel->RebuildTabs(m_gridRows, m_gridCols);

    // Auto-assign columns and choose normalization per axis
    const auto& ds = m_dataManager.dataset();
    if (ds.numCols > 0) {
        for (int i = 0; i < numPlots; i++) {
            size_t col1 = (i * 2) % ds.numCols;
            size_t col2 = (i * 2 + 1) % ds.numCols;
            m_plotConfigs[i] = {col1, col2};
            m_plotConfigs[i].xNorm = DefaultNormForColumn(col1);
            m_plotConfigs[i].yNorm = DefaultNormForColumn(col2);
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

const std::vector<float>& MainFrame::GetNormalized(size_t col, NormMode mode) {
    NormCacheKey key{col, mode};
    auto it = m_normCache.find(key);
    if (it != m_normCache.end())
        return it->second;
    const auto& ds = m_dataManager.dataset();
    auto& cached = m_normCache[key];
    cached = NormalizeColumn(&ds.data[col], ds.numRows, ds.numCols, mode);
    return cached;
}

void MainFrame::InvalidateNormCache() {
    m_normCache.clear();
}

void MainFrame::UpdatePlot(int plotIndex) {
    using Clock = std::chrono::steady_clock;
    auto tPlotStart = Clock::now();
    auto elapsed = [](Clock::time_point since) {
        return std::chrono::duration<double>(Clock::now() - since).count();
    };

    const auto& ds = m_dataManager.dataset();
    if (ds.numRows == 0 || plotIndex < 0 || plotIndex >= (int)m_canvases.size())
        return;
    fprintf(stderr, "  UpdatePlot(%d): %zu rows, cols %zu,%zu\n",
            plotIndex, ds.numRows, m_plotConfigs[plotIndex].xCol, m_plotConfigs[plotIndex].yCol);
    fflush(stderr);

    auto& cfg = m_plotConfigs[plotIndex];
    if (cfg.xCol >= ds.numCols) cfg.xCol = 0;
    if (cfg.yCol >= ds.numCols) cfg.yCol = 0;

    // Normalize each axis using cache
    auto tNorm = Clock::now();
    const auto& xVals = GetNormalized(cfg.xCol, cfg.xNorm);
    const auto& yVals = GetNormalized(cfg.yCol, cfg.yNorm);

    // Z-axis normalization (optional)
    bool hasZ = (cfg.zCol >= 0 && static_cast<size_t>(cfg.zCol) < ds.numCols);
    const std::vector<float>* zValsPtr = nullptr;
    if (hasZ) {
        zValsPtr = &GetNormalized(static_cast<size_t>(cfg.zCol), cfg.zNorm);
    }
    fprintf(stderr, "    normalize: %.3f s\n", elapsed(tNorm));

    float opacity = m_plotConfigs[plotIndex].opacity;

    // Subsample if dataset is very large to avoid GPU memory issues
    constexpr size_t MAX_DISPLAY_POINTS = 4000000;  // 4M points max per plot
    std::vector<size_t> displayIndices;
    bool subsampled = false;
    if (ds.numRows > MAX_DISPLAY_POINTS) {
        subsampled = true;

        // Guarantee the top/bottom EXTREME_K points in each plotted dimension
        // are included so outliers aren't lost to random subsampling.
        constexpr size_t EXTREME_K = 10;
        std::vector<size_t> mustInclude;
        auto findExtremes = [&](const std::vector<float>& vals) {
            using P = std::pair<float, size_t>;
            std::priority_queue<P> bottomK;  // max-heap: tracks smallest values
            std::priority_queue<P, std::vector<P>, std::greater<P>> topK;  // min-heap: tracks largest
            for (size_t i = 0; i < vals.size(); i++) {
                if (bottomK.size() < EXTREME_K || vals[i] < bottomK.top().first) {
                    bottomK.push({vals[i], i});
                    if (bottomK.size() > EXTREME_K) bottomK.pop();
                }
                if (topK.size() < EXTREME_K || vals[i] > topK.top().first) {
                    topK.push({vals[i], i});
                    if (topK.size() > EXTREME_K) topK.pop();
                }
            }
            while (!bottomK.empty()) { mustInclude.push_back(bottomK.top().second); bottomK.pop(); }
            while (!topK.empty()) { mustInclude.push_back(topK.top().second); topK.pop(); }
        };
        findExtremes(xVals);
        findExtremes(yVals);
        if (hasZ) findExtremes(*zValsPtr);

        // Deduplicate
        std::sort(mustInclude.begin(), mustInclude.end());
        mustInclude.erase(std::unique(mustInclude.begin(), mustInclude.end()), mustInclude.end());
        std::unordered_set<size_t> mustSet(mustInclude.begin(), mustInclude.end());

        // Reservoir sampling for remaining budget (avoids allocating a full N-element array)
        size_t budget = MAX_DISPLAY_POINTS - mustInclude.size();
        std::mt19937 rng(plotIndex * 42 + 7);  // deterministic per-plot seed
        std::vector<size_t> reservoir;
        reservoir.reserve(budget);
        size_t seen = 0;
        for (size_t i = 0; i < ds.numRows; i++) {
            if (mustSet.count(i)) continue;
            if (reservoir.size() < budget) {
                reservoir.push_back(i);
            } else {
                std::uniform_int_distribution<size_t> dist(0, seen);
                size_t j = dist(rng);
                if (j < budget) reservoir[j] = i;
            }
            seen++;
        }

        displayIndices = std::move(mustInclude);
        displayIndices.insert(displayIndices.end(), reservoir.begin(), reservoir.end());
        std::sort(displayIndices.begin(), displayIndices.end());
        fprintf(stderr, "  Subsampled %zu -> %zu points (%zu extremes preserved)\n",
                ds.numRows, displayIndices.size(), mustSet.size());
    }

    size_t numDisplay = subsampled ? displayIndices.size() : ds.numRows;
    std::vector<PointVertex> points;
    points.reserve(numDisplay);

    // Compute colormap values if a non-default colormap is active
    std::vector<float> colormapValues;
    if (m_colorMap != ColorMapType::Default) {
        colormapValues.resize(ds.numRows);

        if (m_colorVariable == 0) {
            // Color by density: build 2D density grid
            constexpr int GRID_SIZE = 128;
            std::vector<int> grid(GRID_SIZE * GRID_SIZE, 0);
            float dataMinX = -0.9f, dataMaxX = 0.9f;
            float dataMinY = -0.9f, dataMaxY = 0.9f;
            float gridW = (dataMaxX - dataMinX) / GRID_SIZE;
            float gridH = (dataMaxY - dataMinY) / GRID_SIZE;

            for (size_t r = 0; r < ds.numRows; r++) {
                int gx = static_cast<int>((xVals[r] - dataMinX) / gridW);
                int gy = static_cast<int>((yVals[r] - dataMinY) / gridH);
                gx = std::max(0, std::min(gx, GRID_SIZE - 1));
                gy = std::max(0, std::min(gy, GRID_SIZE - 1));
                grid[gy * GRID_SIZE + gx]++;
            }

            int maxDensity = *std::max_element(grid.begin(), grid.end());
            if (maxDensity == 0) maxDensity = 1;

            for (size_t r = 0; r < ds.numRows; r++) {
                int gx = static_cast<int>((xVals[r] - dataMinX) / gridW);
                int gy = static_cast<int>((yVals[r] - dataMinY) / gridH);
                gx = std::max(0, std::min(gx, GRID_SIZE - 1));
                gy = std::max(0, std::min(gy, GRID_SIZE - 1));
                float d = static_cast<float>(grid[gy * GRID_SIZE + gx]);
                colormapValues[r] = std::log(1.0f + d) / std::log(1.0f + maxDensity);
            }
        } else {
            // Color by a specific column variable
            size_t colorCol = static_cast<size_t>(m_colorVariable - 1);
            if (colorCol < ds.numCols) {
                float cMin, cMax;
                ds.columnRange(colorCol, cMin, cMax);
                float cRange = cMax - cMin;
                if (cRange == 0.0f) cRange = 1.0f;
                for (size_t r = 0; r < ds.numRows; r++)
                    colormapValues[r] = (ds.value(r, colorCol) - cMin) / cRange;
            }
        }
    }

    auto tVerts = Clock::now();
    for (size_t di = 0; di < numDisplay; di++) {
        size_t r = subsampled ? displayIndices[di] : di;
        PointVertex v{};
        v.x = xVals[r];
        v.y = yVals[r];
        v.z = hasZ ? (*zValsPtr)[r] : 0.0f;
        if (m_colorMap != ColorMapType::Default && r < colormapValues.size()) {
            ColorMapLookup(m_colorMap, colormapValues[r], v.r, v.g, v.b);
        } else {
            v.r = 0.15f; v.g = 0.4f; v.b = 1.0f;
        }
        v.a = opacity;
        v.symbol = 0.0f;
        v.sizeScale = 1.0f;
        points.push_back(v);
    }

    fprintf(stderr, "    build verts: %.3f s\n", elapsed(tVerts));

    // Pass display indices for subsampled datasets
    auto tGpu = Clock::now();
    if (subsampled) {
        m_canvases[plotIndex]->SetDisplayIndices(displayIndices);
    } else {
        m_canvases[plotIndex]->SetDisplayIndices({});  // clear
    }
    m_canvases[plotIndex]->SetPoints(std::move(points));

    // Set axis labels
    if (plotIndex < (int)m_plotWidgets.size()) {
        m_plotWidgets[plotIndex].xLabel->SetLabel(ds.columnLabels[cfg.xCol]);
        m_plotWidgets[plotIndex].yLabel->SetLabel(ds.columnLabels[cfg.yCol]);
    }

    fprintf(stderr, "    GPU upload: %.3f s\n", elapsed(tGpu));

    // Re-apply current selection
    if (!m_selection.empty())
        m_canvases[plotIndex]->SetSelection(m_selection);

    fprintf(stderr, "  UpdatePlot(%d) total       %.3f s\n", plotIndex, elapsed(tPlotStart));
}

void MainFrame::UpdateAllPlots() {
    using Clock = std::chrono::steady_clock;
    auto tAll = Clock::now();

    // Pre-normalize all needed columns before building plots.
    // This populates the cache so UpdatePlot doesn't do strided reads
    // interleaved with vertex construction.
    auto tPreNorm = Clock::now();
    for (int i = 0; i < (int)m_canvases.size(); i++) {
        auto& cfg = m_plotConfigs[i];
        const auto& ds = m_dataManager.dataset();
        if (ds.numRows == 0) continue;
        size_t xc = std::min(cfg.xCol, ds.numCols - 1);
        size_t yc = std::min(cfg.yCol, ds.numCols - 1);
        GetNormalized(xc, cfg.xNorm);
        GetNormalized(yc, cfg.yNorm);
        if (cfg.zCol >= 0 && static_cast<size_t>(cfg.zCol) < ds.numCols)
            GetNormalized(static_cast<size_t>(cfg.zCol), cfg.zNorm);
    }
    fprintf(stderr, "  pre-normalize all: %.3f s\n",
            std::chrono::duration<double>(Clock::now() - tPreNorm).count());

    for (int i = 0; i < (int)m_canvases.size(); i++)
        UpdatePlot(i);
    fprintf(stderr, "TIMING: UpdateAllPlots total %.3f s\n",
            std::chrono::duration<double>(Clock::now() - tAll).count());
}

void MainFrame::HandleBrushRect(int plotIndex, float x0, float y0, float x1, float y1, bool extend) {
    const auto& ds = m_dataManager.dataset();
    if (ds.numRows == 0)
        return;

    // The brush rect is in the normalized coordinate space of the brushing plot.
    // Use cached normalization to avoid recomputing on every drag event.
    auto& cfg = m_plotConfigs[plotIndex];

    const auto& xVals = GetNormalized(cfg.xCol, cfg.xNorm);
    const auto& yVals = GetNormalized(cfg.yCol, cfg.yNorm);

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

    int matchCount = 0;
    for (size_t r = 0; r < ds.numRows; r++) {
        if (xVals[r] >= rectMinX && xVals[r] <= rectMaxX &&
            yVals[r] >= rectMinY && yVals[r] <= rectMaxY) {
            m_selection[r] = m_activeBrush;
            matchCount++;
        }
    }
    PropagateSelection(m_selection);
}

void MainFrame::PropagateSelection(const std::vector<int>& selection) {
    m_selection = selection;
    // Update the active plot immediately and defer the rest to the
    // next event loop iteration so it gets a full frame to itself.
    if (m_activePlot >= 0 && m_activePlot < (int)m_canvases.size()) {
        m_canvases[m_activePlot]->SetSelection(m_selection);
        m_canvases[m_activePlot]->Update();
    }
    CallAfter([this, selection]() {
        for (int i = 0; i < (int)m_canvases.size(); i++)
            if (i != m_activePlot)
                m_canvases[i]->SetSelection(selection);
    });

    int count = 0;
    for (int s : m_selection) if (s > 0) count++;
    m_controlPanel->SetSelectionInfo(count, static_cast<int>(m_selection.size()));
    float pct = m_selection.size() > 0 ? (100.0f * count / m_selection.size()) : 0;
    SetStatusText(m_dataStatusText + wxString::Format("  |  Selected: %d / %zu (%.1f%%)", count, m_selection.size(), pct));
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

NormMode MainFrame::DefaultNormForColumn(size_t col) const {
    const auto& ds = m_dataManager.dataset();
    if (col >= ds.numCols) return NormMode::MinMax;
    bool hasPos = false, hasNeg = false;
    for (size_t r = 0; r < ds.numRows; r++) {
        float v = ds.value(r, col);
        if (v > 0) hasPos = true;
        if (v < 0) hasNeg = true;
        if (hasPos && hasNeg) return NormMode::MaxAbs;
    }
    return NormMode::MinMax;
}

void MainFrame::KillSelectedPoints() {
    int count = 0;
    for (int s : m_selection) if (s > 0) count++;
    if (count == 0) return;

    size_t removed = m_dataManager.removeSelectedRows(m_selection);
    if (removed == 0) return;

    InvalidateNormCache();
    // Reset selection and rebuild all plots with reduced data
    const auto& ds = m_dataManager.dataset();
    m_selection.assign(ds.numRows, 0);
    UpdateAllPlots();
    PropagateSelection(m_selection);

    m_dataStatusText = wxString::Format("%zu rows x %zu columns", ds.numRows, ds.numCols);
    SetStatusText(m_dataStatusText + wxString::Format("  |  Deleted %zu points, %zu remaining",
                                    removed, ds.numRows));
}

void MainFrame::LoadFile(const std::string& path) {
    using Clock = std::chrono::steady_clock;
    auto t0 = Clock::now();
    auto elapsed = [&](Clock::time_point since) {
        return std::chrono::duration<double>(Clock::now() - since).count();
    };

    wxProgressDialog progressDlg("Loading Data", "Loading " + wxString(path).AfterLast('/'),
                                  100, this,
                                  wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_SMOOTH | wxPD_CAN_ABORT);

    bool cancelled = false;
    auto progressCb = [&](size_t current, size_t total) -> bool {
        int pct = (total > 0) ? static_cast<int>((current * 100) / total) : 0;
        pct = std::min(pct, 99);
        wxString msg;
        if (total > 10000) {
            msg = wxString::Format("Loading... %zu KB / %zu KB", current / 1024, total / 1024);
        } else {
            msg = wxString::Format("Loading... %zu / %zu", current, total);
        }
        cancelled = !progressDlg.Update(pct, msg);
        wxYield();
        return !cancelled;
    };

    auto tLoad = Clock::now();
    if (!m_dataManager.loadFile(path, progressCb, m_maxRows)) {
        if (!cancelled) {
            wxMessageBox("Failed to load file:\n" + m_dataManager.errorMessage(),
                         "Load Error", wxOK | wxICON_ERROR, this);
        }
        return;
    }
    fprintf(stderr, "TIMING: loadFile            %.3f s\n", elapsed(tLoad));

    auto tProcess = Clock::now();

    const auto& ds = m_dataManager.dataset();
    fprintf(stderr, "Processing %zu rows x %zu cols\n", ds.numRows, ds.numCols);

    InvalidateNormCache();
    m_controlPanel->SetColumns(ds.columnLabels);
    m_dataStatusText = wxString::Format("%zu rows x %zu columns", ds.numRows, ds.numCols);
    m_selection.assign(ds.numRows, 0);
    SetStatusText(m_dataStatusText + wxString::Format("  |  Selected: 0 / %zu (0.0%%)", ds.numRows));

    // Auto-assign column pairs to plots
    int numPlots = m_gridRows * m_gridCols;
    for (int i = 0; i < numPlots; i++) {
        size_t col1 = (i * 2) % ds.numCols;
        size_t col2 = (i * 2 + 1) % ds.numCols;
        m_plotConfigs[i] = {col1, col2};
    }

    // Set default point size and opacity based on dataset size
    float defaultSize = std::max(0.5f, std::min(30.0f,
        14.0f - 2.0f * std::log10(static_cast<float>(ds.numRows))));
    defaultSize = std::round(defaultSize * 10.0f) / 10.0f;
    float defaultOpacity = std::max(0.03f, std::min(1.0f,
        1.2f - 0.2f * std::log10(static_cast<float>(ds.numRows))));
    defaultOpacity = std::round(defaultOpacity * 100.0f) / 100.0f;
    for (auto& cfg : m_plotConfigs) {
        cfg.pointSize = defaultSize;
        cfg.opacity = defaultOpacity;
    }
    for (auto* c : m_canvases) {
        c->SetPointSize(defaultSize);
        c->SetOpacity(defaultOpacity);
    }
    m_controlPanel->SetGlobalPointSize(defaultSize);
    fprintf(stderr, "Default point size: %.1f, opacity: %.0f%% (for %zu rows)\n",
            defaultSize, defaultOpacity * 100.0f, ds.numRows);
    fprintf(stderr, "TIMING: processing          %.3f s\n", elapsed(tProcess));

    SetTitle("Viewpoints — " + wxString(path).AfterLast('/'));

    auto tPlots = Clock::now();
    UpdateAllPlots();
    fprintf(stderr, "TIMING: UpdateAllPlots      %.3f s\n", elapsed(tPlots));

    SetActivePlot(0);
    fprintf(stderr, "TIMING: total load-to-ready %.3f s\n", elapsed(t0));
    fflush(stderr);
}

void MainFrame::OnOpen(wxCommandEvent& event) {
    // Hide canvases so the file dialog isn't covered by Metal layers
    for (auto* c : m_canvases) c->Hide();

    wxFileDialog dialog(this, "Open Data File", "", "",
        "All supported files|*.txt;*.csv;*.dat;*.tsv;*.parquet;*.pq|"
        "Parquet files (*.parquet)|*.parquet;*.pq|"
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

    wxString defaultName = selectedOnly ? "selected.parquet" : "data.parquet";
    wxFileDialog dialog(this,
        selectedOnly ? "Save Selected Points" : "Save All Data",
        "", defaultName,
        "Parquet files (*.parquet)|*.parquet|CSV files (*.csv)|*.csv|Text files (*.txt)|*.txt|All files (*.*)|*.*",
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (dialog.ShowModal() == wxID_OK) {
        std::string path = dialog.GetPath().ToStdString();
        const auto& sel = selectedOnly ? m_selection : std::vector<int>{};

        bool ok;
        // Choose format based on file extension
        if (path.size() >= 8 && path.substr(path.size() - 8) == ".parquet") {
            ok = m_dataManager.saveAsParquet(path, sel);
        } else {
            ok = m_dataManager.saveAsCsv(path, sel);
        }

        if (ok) {
            SetStatusText(m_dataStatusText + "  |  Saved: " + dialog.GetPath());
        } else {
            wxMessageBox("Failed to save file.", "Save Error", wxOK | wxICON_ERROR, this);
        }
    }
}

void MainFrame::LoadFileFromPath(const std::string& path) {
    CallAfter([this, path]() { LoadFile(path); });
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

wxString MainFrame::BuildTooltipText(int dataRow) {
    const auto& ds = m_dataManager.dataset();
    if (dataRow < 0 || dataRow >= (int)ds.numRows) return "";

    wxString text;
    for (size_t c = 0; c < ds.numCols; c++) {
        float val = ds.value(dataRow, c);
        wxString valStr;
        if (c < ds.columnMeta.size() && ds.columnMeta[c].isCategorical) {
            const auto& cats = ds.columnMeta[c].categories;
            if (!cats.empty()) {
                int idx = std::max(0, std::min(static_cast<int>(std::round(val)),
                                               static_cast<int>(cats.size()) - 1));
                valStr = wxString::FromUTF8(cats[idx]);
            } else {
                valStr = wxString::Format("%.6g", val);
            }
        } else {
            valStr = wxString::Format("%.6g", val);
        }
        if (c > 0) text += "\n";
        text += wxString::Format("%s: %s", ds.columnLabels[c], valStr);
    }
    return text;
}

void MainFrame::HideAllTooltips() {
    for (auto* tt : m_tooltips) {
        if (tt->IsShown()) tt->Hide();
    }
}

void MainFrame::LayoutGrid() {
    wxSize sz = m_gridPanel->GetClientSize();
    int totalW = sz.GetWidth();
    int totalH = sz.GetHeight();
    if (totalW < 1 || totalH < 1) return;

    int numPlots = m_gridRows * m_gridCols;
    if (numPlots == 0 || (int)m_plotWidgets.size() != numPlots) return;

    int gapW = GRID_GAP * (m_gridCols - 1);
    int gapH = GRID_GAP * (m_gridRows - 1);
    int availW = totalW - gapW;
    int availH = totalH - gapH;
    if (availW < m_gridCols || availH < m_gridRows) return;

    // Convert proportions to pixel widths
    std::vector<int> colPx(m_gridCols);
    int usedW = 0;
    for (int c = 0; c < m_gridCols; c++) {
        colPx[c] = static_cast<int>(m_colWidths[c] * availW);
        usedW += colPx[c];
    }
    colPx[m_gridCols - 1] += (availW - usedW); // distribute rounding remainder

    // Convert proportions to pixel heights
    std::vector<int> rowPx(m_gridRows);
    int usedH = 0;
    for (int r = 0; r < m_gridRows; r++) {
        rowPx[r] = static_cast<int>(m_rowHeights[r] * availH);
        usedH += rowPx[r];
    }
    rowPx[m_gridRows - 1] += (availH - usedH);

    // Position each cell panel
    int y = 0;
    for (int r = 0; r < m_gridRows; r++) {
        int x = 0;
        for (int c = 0; c < m_gridCols; c++) {
            int idx = r * m_gridCols + c;
            auto* cell = m_plotWidgets[idx].cellPanel;
            if (cell) {
                cell->SetSize(x, y, colPx[c], rowPx[r]);
                cell->Layout();
            }
            x += colPx[c] + GRID_GAP;
        }
        y += rowPx[r] + GRID_GAP;
    }
}

MainFrame::DividerHit MainFrame::HitTestDivider(int mx, int my, int& hitCol, int& hitRow) {
    wxSize sz = m_gridPanel->GetClientSize();
    int availW = sz.GetWidth() - GRID_GAP * (m_gridCols - 1);
    int availH = sz.GetHeight() - GRID_GAP * (m_gridRows - 1);

    hitCol = -1;
    hitRow = -1;

    // Compute the center of each column boundary gap
    std::vector<int> colCenters(m_gridCols - 1);
    int x = 0;
    for (int c = 0; c < m_gridCols - 1; c++) {
        x += static_cast<int>(m_colWidths[c] * availW);
        colCenters[c] = x + GRID_GAP / 2;
        x += GRID_GAP;
    }

    // Compute the center of each row boundary gap
    std::vector<int> rowCenters(m_gridRows - 1);
    int y = 0;
    for (int r = 0; r < m_gridRows - 1; r++) {
        y += static_cast<int>(m_rowHeights[r] * availH);
        rowCenters[r] = y + GRID_GAP / 2;
        y += GRID_GAP;
    }

    // First pass: check for intersection hits with large grab radius
    int half = CORNER_GRAB / 2;
    for (int c = 0; c < (int)colCenters.size(); c++) {
        if (std::abs(mx - colCenters[c]) <= half) {
            for (int r = 0; r < (int)rowCenters.size(); r++) {
                if (std::abs(my - rowCenters[r]) <= half) {
                    hitCol = c;
                    hitRow = r;
                    return DividerHit::Intersection;
                }
            }
        }
    }

    // Second pass: narrow edge detection using GRID_GAP
    for (int c = 0; c < (int)colCenters.size(); c++) {
        int gapLeft = colCenters[c] - GRID_GAP / 2;
        if (mx >= gapLeft && mx < gapLeft + GRID_GAP) {
            hitCol = c;
            return DividerHit::Vertical;
        }
    }

    for (int r = 0; r < (int)rowCenters.size(); r++) {
        int gapTop = rowCenters[r] - GRID_GAP / 2;
        if (my >= gapTop && my < gapTop + GRID_GAP) {
            hitRow = r;
            return DividerHit::Horizontal;
        }
    }

    return DividerHit::None;
}

void MainFrame::OnGridSize(wxSizeEvent& event) {
    LayoutGrid();
    event.Skip();
}

void MainFrame::OnGridMouse(wxMouseEvent& event) {
    int mx = event.GetX();
    int my = event.GetY();

    if (event.LeftDown()) {
        int col, row;
        DividerHit hit = HitTestDivider(mx, my, col, row);
        if (hit != DividerHit::None) {
            m_dividerHit = hit;
            m_dragCol = col;
            m_dragRow = row;
            m_draggingDivider = true;
            m_dragStart = wxPoint(mx, my);
            if (col >= 0) {
                m_dragStartColWidth0 = m_colWidths[col];
                m_dragStartColWidth1 = m_colWidths[col + 1];
            }
            if (row >= 0) {
                m_dragStartRowHeight0 = m_rowHeights[row];
                m_dragStartRowHeight1 = m_rowHeights[row + 1];
            }
            m_gridPanel->CaptureMouse();
        }
    } else if (event.Dragging() && m_draggingDivider) {
        wxSize sz = m_gridPanel->GetClientSize();
        int availW = sz.GetWidth() - GRID_GAP * (m_gridCols - 1);
        int availH = sz.GetHeight() - GRID_GAP * (m_gridRows - 1);

        if (m_dragCol >= 0 && availW > 0) {
            double dx = static_cast<double>(mx - m_dragStart.x) / availW;
            double newW0 = m_dragStartColWidth0 + dx;
            double newW1 = m_dragStartColWidth1 - dx;
            double minFrac = static_cast<double>(MIN_CELL_W) / availW;
            if (newW0 < minFrac) { newW0 = minFrac; newW1 = m_dragStartColWidth0 + m_dragStartColWidth1 - minFrac; }
            if (newW1 < minFrac) { newW1 = minFrac; newW0 = m_dragStartColWidth0 + m_dragStartColWidth1 - minFrac; }
            m_colWidths[m_dragCol] = newW0;
            m_colWidths[m_dragCol + 1] = newW1;
        }

        if (m_dragRow >= 0 && availH > 0) {
            double dy = static_cast<double>(my - m_dragStart.y) / availH;
            double newH0 = m_dragStartRowHeight0 + dy;
            double newH1 = m_dragStartRowHeight1 - dy;
            double minFrac = static_cast<double>(MIN_CELL_H) / availH;
            if (newH0 < minFrac) { newH0 = minFrac; newH1 = m_dragStartRowHeight0 + m_dragStartRowHeight1 - minFrac; }
            if (newH1 < minFrac) { newH1 = minFrac; newH0 = m_dragStartRowHeight0 + m_dragStartRowHeight1 - minFrac; }
            m_rowHeights[m_dragRow] = newH0;
            m_rowHeights[m_dragRow + 1] = newH1;
        }

        LayoutGrid();
    } else if (event.LeftUp()) {
        if (m_draggingDivider) {
            m_gridPanel->ReleaseMouse();
            m_draggingDivider = false;
            m_dividerHit = DividerHit::None;
            m_dragCol = -1;
            m_dragRow = -1;
        }
    } else if (event.Moving()) {
        int col, row;
        DividerHit hit = HitTestDivider(mx, my, col, row);
        switch (hit) {
            case DividerHit::Vertical:
                m_gridPanel->SetCursor(wxCursor(wxCURSOR_SIZEWE));
                break;
            case DividerHit::Horizontal:
                m_gridPanel->SetCursor(wxCursor(wxCURSOR_SIZENS));
                break;
            case DividerHit::Intersection:
                m_gridPanel->SetCursor(wxCursor(wxCURSOR_SIZING));
                break;
            case DividerHit::None:
                m_gridPanel->SetCursor(wxNullCursor);
                break;
        }
    }
}
