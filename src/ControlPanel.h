#pragma once

#include <wx/wx.h>
#include <wx/notebook.h>
#include <vector>
#include <string>
#include <functional>
#include <array>

// Forward declare PlotConfig to avoid circular include
struct PlotConfig;

constexpr int CP_NUM_BRUSHES = 7;

// Per-plot tab page
class PlotTab : public wxPanel {
public:
    PlotTab(wxNotebook* parent, int plotIndex, int row, int col);

    void SetColumns(const std::vector<std::string>& names);
    void SyncFromConfig(const PlotConfig& cfg);

    // Per-plot callbacks (carry plotIndex)
    std::function<void(int plotIndex, int xCol, int yCol)> onAxisChanged;
    std::function<void(int plotIndex, int xNorm, int yNorm)> onNormChanged;
    std::function<void(int plotIndex, bool show)> onShowUnselectedChanged;
    std::function<void(int plotIndex, bool show)> onGridLinesChanged;

private:
    void CreateControls(int row, int col);

    int m_plotIndex;
    bool m_suppress = false;

    wxChoice* m_xAxis = nullptr;
    wxChoice* m_yAxis = nullptr;
    wxChoice* m_xNorm = nullptr;
    wxChoice* m_yNorm = nullptr;
    wxCheckBox* m_showUnselected = nullptr;
    wxCheckBox* m_showGridLines = nullptr;
};

// Main control panel with tabbed interface
class ControlPanel : public wxPanel {
public:
    explicit ControlPanel(wxWindow* parent);

    void SetColumns(const std::vector<std::string>& names);
    void SetSelectionInfo(int selected, int total);
    void RebuildTabs(int rows, int cols);
    void SelectTab(int plotIndex);
    void SetPlotConfig(int plotIndex, const PlotConfig& cfg);

    // Per-plot callbacks (routed from PlotTab)
    std::function<void(int plotIndex, int xCol, int yCol)> onAxisChanged;
    std::function<void(int plotIndex, int xNorm, int yNorm)> onNormChanged;
    std::function<void(int plotIndex, bool show)> onShowUnselectedChanged;
    std::function<void(int plotIndex, bool show)> onGridLinesChanged;
    std::function<void(int plotIndex)> onTabSelected;

    // Global callbacks (from "All" tab)
    std::function<void(float size)> onPointSizeChanged;
    std::function<void(float alpha)> onOpacityChanged;
    std::function<void(int bins)> onHistBinsChanged;
    std::function<void()> onClearSelection;
    std::function<void()> onInvertSelection;
    std::function<void(int brushIndex)> onBrushChanged;

    float GetPointSize() const;
    float GetOpacity() const;

private:
    void CreateAllTab();
    void SelectBrush(int index);

    wxNotebook* m_notebook = nullptr;
    std::vector<PlotTab*> m_plotTabs;
    wxPanel* m_allTab = nullptr;

    // Column names (cached for rebuilding tabs)
    std::vector<std::string> m_columnNames;

    // "All" tab widgets
    wxSlider* m_pointSizeSlider = nullptr;
    wxSlider* m_opacitySlider = nullptr;
    wxSlider* m_histBinsSlider = nullptr;
    wxStaticText* m_pointSizeLabel = nullptr;
    wxStaticText* m_opacityLabel = nullptr;
    wxStaticText* m_histBinsLabel = nullptr;
    wxStaticText* m_selectionLabel = nullptr;
    wxStaticText* m_infoLabel = nullptr;
    std::array<wxButton*, CP_NUM_BRUSHES> m_brushButtons = {};
    int m_activeBrush = 0;

    // Saved state for rebuild
    int m_savedGridRows = 2;
    int m_savedGridCols = 2;
};
