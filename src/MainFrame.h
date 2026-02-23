// Viewpoints (MIT License) - See LICENSE file
#pragma once

#include <wx/wx.h>
#include "DataManager.h"
#include "WebGPUContext.h"
#include "Normalize.h"
#include "Brush.h"
#include "ColorMap.h"
#include "VerticalLabel.h"
#include <vector>
#include <array>
#include <unordered_map>

class WebGPUCanvas;
class ControlPanel;
struct BrushColor;

struct PlotConfig {
    size_t xCol = 0;
    size_t yCol = 1;
    int zCol = -1;  // -1 = "None" (2D mode), 0+ = column index
    bool xLocked = false;
    bool yLocked = false;
    NormMode xNorm = NormMode::None;
    NormMode yNorm = NormMode::None;
    NormMode zNorm = NormMode::None;
    float rotationY = 0.0f;  // degrees, 0-360
    bool showUnselected = true;
    bool showGridLines = false;
    bool showHistograms = true;
    float pointSize = 6.0f;
    float opacity = 0.05f;
    int histBins = 64;
};

class MainFrame : public wxFrame {
public:
    MainFrame();
    void LoadFileFromPath(const std::string& path);
    void SetMaxRows(size_t maxRows) { m_maxRows = maxRows; }

private:
    void CreateMenuBar();
    void CreateLayout();
    void RebuildGrid();
    void LoadFile(const std::string& path);
    void UpdatePlot(int plotIndex);
    void UpdateAllPlots();
    void SetActivePlot(int plotIndex);
    void HighlightAllPlots();
    void PropagateSelection(const std::vector<int>& selection);
    void HandleBrushRect(int plotIndex, float x0, float y0, float x1, float y1, bool extend);
    void ClearAllSelections();
    void KillSelectedPoints();
    void InvertAllSelections();

    void OnOpen(wxCommandEvent& event);
    void OnSave(bool selectedOnly);
    void OnQuit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnAddRow(wxCommandEvent& event);
    void OnAddCol(wxCommandEvent& event);
    void OnRemoveRow(wxCommandEvent& event);
    void OnRemoveCol(wxCommandEvent& event);

    WebGPUContext m_gpuContext;

    static constexpr int NUM_TICKS = 5;

    static constexpr int MAX_NICE_TICKS = 10;

    struct PlotWidgets {
        wxPanel* cellPanel = nullptr;
        wxStaticText* xLabel = nullptr;
        VerticalLabel* yLabel = nullptr;
        wxPanel* xTickPanel = nullptr;
        wxPanel* yTickPanel = nullptr;
        std::array<wxStaticText*, MAX_NICE_TICKS> xTicks = {};
        std::array<wxStaticText*, MAX_NICE_TICKS> yTicks = {};
    };

    std::vector<WebGPUCanvas*> m_canvases;
    std::vector<PlotWidgets> m_plotWidgets;
    std::vector<PlotConfig> m_plotConfigs;
    int m_gridRows = 2;
    int m_gridCols = 2;
    int m_activePlot = 0;
    wxPanel* m_gridPanel = nullptr;

    ControlPanel* m_controlPanel = nullptr;
    DataManager m_dataManager;
    std::vector<int> m_selection;
    int m_activeBrush = 1;
    std::vector<BrushColor> m_brushColors;
    size_t m_maxRows = 0;  // 0 = no limit
    ColorMapType m_colorMap = ColorMapType::Default;
    int m_colorVariable = 0;  // 0 = position, 1+ = column index
    float m_bgBrightness = 0.0f;

    // Normalized column cache: avoids recomputing on every brush drag
    struct NormCacheKey {
        size_t col;
        NormMode mode;
        bool operator==(const NormCacheKey& o) const { return col == o.col && mode == o.mode; }
    };
    struct NormCacheHash {
        size_t operator()(const NormCacheKey& k) const {
            return std::hash<size_t>()(k.col) ^ (std::hash<int>()(static_cast<int>(k.mode)) << 16);
        }
    };
    std::unordered_map<NormCacheKey, std::vector<float>, NormCacheHash> m_normCache;
    const std::vector<float>& GetNormalized(size_t col, NormMode mode);
    void InvalidateNormCache();

    enum {
        ID_AddRow = wxID_HIGHEST + 1,
        ID_AddCol,
        ID_RemoveRow,
        ID_RemoveCol,
        ID_ResetViews,
        ID_SaveAll,
        ID_SaveSelected,
    };
};
