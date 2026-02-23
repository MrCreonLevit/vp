// Viewpoints (MIT License) - See LICENSE file
#pragma once

#include <wx/wx.h>
#include <wx/simplebook.h>
#include <wx/timer.h>
#include <wx/tglbtn.h>
#include <vector>
#include <string>
#include <functional>
#include <array>

struct PlotConfig;

constexpr int CP_NUM_BRUSHES = 8;  // brush 0 (unselected) + brushes 1-7

// Per-plot settings page
class PlotTab : public wxScrolledWindow {
public:
    PlotTab(wxWindow* parent, int plotIndex, int row, int col);

    void SetColumns(const std::vector<std::string>& names);
    void SyncFromConfig(const PlotConfig& cfg);

    std::function<void(int plotIndex)> onRandomizeAxes;
    std::function<void(int plotIndex, int xCol, int yCol)> onAxisChanged;
    std::function<void(int plotIndex, int xNorm, int yNorm)> onNormChanged;
    std::function<void(int plotIndex, bool show)> onShowUnselectedChanged;
    std::function<void(int plotIndex, bool show)> onGridLinesChanged;
    std::function<void(int plotIndex, bool show)> onShowHistogramsChanged;
    std::function<void(int plotIndex, float size)> onPointSizeChanged;
    std::function<void(int plotIndex, float alpha)> onOpacityChanged;
    std::function<void(int plotIndex, int bins)> onHistBinsChanged;
    std::function<void(int plotIndex, bool xLock, bool yLock)> onAxisLockChanged;
    std::function<void(int plotIndex, int zCol, int zNorm)> onZAxisChanged;
    std::function<void(int plotIndex, float angle)> onRotationChanged;

private:
    friend class ControlPanel;
    void CreateControls(int row, int col);

    int m_plotIndex;
    bool m_suppress = false;

    wxChoice* m_xAxis = nullptr;
    wxChoice* m_yAxis = nullptr;
    wxChoice* m_zAxis = nullptr;
    wxCheckBox* m_xLock = nullptr;
    wxCheckBox* m_yLock = nullptr;
    wxChoice* m_xNorm = nullptr;
    wxChoice* m_yNorm = nullptr;
    wxChoice* m_zNorm = nullptr;
    wxSlider* m_rotationSlider = nullptr;
    wxStaticText* m_rotationLabel = nullptr;
    wxToggleButton* m_spinButton = nullptr;
    wxToggleButton* m_rockButton = nullptr;
    float m_spinAngle = 0.0f;
    bool m_spinning = false;
    bool m_rocking = false;
    float m_rockCenter = 0.0f;
    float m_rockPhase = 0.0f;
    wxCheckBox* m_showUnselected = nullptr;
    wxCheckBox* m_showGridLines = nullptr;
    wxCheckBox* m_showHistograms = nullptr;
    wxSlider* m_pointSizeSlider = nullptr;
    wxSlider* m_opacitySlider = nullptr;
    wxSlider* m_histBinsSlider = nullptr;
    wxStaticText* m_pointSizeLabel = nullptr;
    wxStaticText* m_opacityLabel = nullptr;
    wxStaticText* m_histBinsLabel = nullptr;
};

// Main control panel with grid-based plot selector
class ControlPanel : public wxPanel {
public:
    explicit ControlPanel(wxWindow* parent);

    void SetColumns(const std::vector<std::string>& names);
    void SetSelectionInfo(int selected, int total);
    void RebuildTabs(int rows, int cols);
    void SelectTab(int plotIndex);
    void SetPlotConfig(int plotIndex, const PlotConfig& cfg);

    // Per-plot callbacks
    std::function<void(int plotIndex)> onRandomizeAxes;
    std::function<void(int plotIndex, int xCol, int yCol)> onAxisChanged;
    std::function<void(int plotIndex, bool xLock, bool yLock)> onAxisLockChanged;
    std::function<void(int plotIndex, int xNorm, int yNorm)> onNormChanged;
    std::function<void(int plotIndex, int zCol, int zNorm)> onZAxisChanged;
    std::function<void(int plotIndex, float angle)> onRotationChanged;
    std::function<void(int plotIndex, bool show)> onShowUnselectedChanged;
    std::function<void(int plotIndex, bool show)> onGridLinesChanged;
    std::function<void(int plotIndex, bool show)> onShowHistogramsChanged;
    std::function<void(int plotIndex, float size)> onPlotPointSizeChanged;
    std::function<void(int plotIndex, float alpha)> onPlotOpacityChanged;
    std::function<void(int plotIndex, int bins)> onPlotHistBinsChanged;
    std::function<void(int plotIndex)> onTabSelected;
    std::function<void()> onAllSelected;

    // Global callbacks
    std::function<void(float size)> onPointSizeChanged;
    std::function<void(float alpha)> onOpacityChanged;
    std::function<void(int bins)> onHistBinsChanged;
    std::function<void(int colormap, int colorVar)> onColorMapChanged;
    std::function<void(float brightness)> onBackgroundChanged;
    std::function<void(bool defer)> onDeferRedrawsChanged;
    std::function<void()> onClearSelection;
    std::function<void()> onInvertSelection;
    std::function<void()> onKillSelected;
    std::function<void(int brushIndex)> onBrushChanged;
    std::function<void(int brushIndex, float r, float g, float b, float a)> onBrushColorEdited;
    std::function<void(int brushIndex)> onBrushReset;  // reset brush to default
    std::function<void(int brushIndex, int symbol)> onBrushSymbolChanged;
    std::function<void(int brushIndex, float offset)> onBrushSizeOffsetChanged;
    std::function<void(bool selectedOnly)> onSaveData;

    float GetPointSize() const;
    float GetOpacity() const;

private:
    void CreateAllPage();
    void SelectBrush(int index);
    void SelectPage(int pageIndex);  // 0..N-1 = plot, N = "All"
    void RebuildSelectorGrid();

    wxSimplebook* m_book = nullptr;
    std::vector<PlotTab*> m_plotTabs;
    wxPanel* m_allPage = nullptr;

    // Plot selector grid
    wxPanel* m_selectorPanel = nullptr;
    wxButton* m_allButton = nullptr;
    std::vector<wxButton*> m_plotButtons;
    int m_selectedPage = -1;
    int m_gridRows = 2;
    int m_gridCols = 2;
    bool m_ready = false;  // prevents dialogs during construction

    std::vector<std::string> m_columnNames;

    wxTimer m_spinTimer;
    static constexpr float SPIN_SPEED = 10.0f;  // degrees per second
    static constexpr float ROCK_AMPLITUDE = 3.0f; // degrees
    static constexpr int SPIN_INTERVAL_MS = 33; // ~30 fps
    void OnSpinTimer(wxTimerEvent& event);

    // "All" page widgets
    wxChoice* m_colorVarChoice = nullptr;
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
};
