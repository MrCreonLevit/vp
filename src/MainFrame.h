#pragma once

#include <wx/wx.h>
#include "DataManager.h"
#include "WebGPUContext.h"
#include "Normalize.h"
#include "Brush.h"
#include <vector>

class WebGPUCanvas;
class ControlPanel;

struct PlotConfig {
    size_t xCol = 0;
    size_t yCol = 1;
    NormMode xNorm = NormMode::None;
    NormMode yNorm = NormMode::None;
};

class MainFrame : public wxFrame {
public:
    MainFrame();

private:
    void CreateMenuBar();
    void CreateLayout();
    void RebuildGrid();
    void LoadFile(const std::string& path);
    void UpdatePlot(int plotIndex);
    void UpdateAllPlots();
    void SetActivePlot(int plotIndex);
    void PropagateSelection(const std::vector<int>& selection);
    void HandleBrushRect(int plotIndex, float x0, float y0, float x1, float y1, bool extend);
    void ClearAllSelections();
    void InvertAllSelections();

    void OnOpen(wxCommandEvent& event);
    void OnQuit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnAddRow(wxCommandEvent& event);
    void OnAddCol(wxCommandEvent& event);
    void OnRemoveRow(wxCommandEvent& event);
    void OnRemoveCol(wxCommandEvent& event);

    WebGPUContext m_gpuContext;

    std::vector<WebGPUCanvas*> m_canvases;
    std::vector<PlotConfig> m_plotConfigs;
    int m_gridRows = 2;
    int m_gridCols = 2;
    int m_activePlot = 0;
    wxPanel* m_gridPanel = nullptr;

    ControlPanel* m_controlPanel = nullptr;
    DataManager m_dataManager;
    std::vector<int> m_selection;
    int m_activeBrush = 1;  // 1-based brush index (1-7)

    enum {
        ID_AddRow = wxID_HIGHEST + 1,
        ID_AddCol,
        ID_RemoveRow,
        ID_RemoveCol,
        ID_ResetViews,
    };
};
