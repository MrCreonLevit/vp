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

// plotIndex < 0 applies a control-panel action to every plot. The frame logs
// that once as "(all plots)" instead of once per plot. kLeaveField in a
// column, norm, or lock argument means that field is not part of the action.
// Z column uses kLeaveZ because -1 already means "(None)".
constexpr int kAllPlots = -1;
constexpr int kLeaveField = -1;
constexpr int kLeaveZ = -2;

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
    std::function<void(int plotIndex, float angle, bool animated)> onRotationChanged;
    std::function<void(int plotIndex, float angle, bool animated)> onRotationXChanged;
    std::function<void(int plotIndex, float angle, bool animated)> onRotationZChanged;
    std::function<void(int plotIndex, bool zeroY, bool zeroX, bool zeroZ)> onRotationZeroed;

    std::function<void(int plotIndex, int axis, bool spinning, bool rocking)> onSpinRockChanged;

    std::function<void()> onClearSelection;
    std::function<void()> onInvertSelection;
    std::function<void()> onKillSelected;
    std::function<void(bool selectedOnly)> onSaveData;

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
    wxSlider* m_rotationXSlider = nullptr;
    wxStaticText* m_rotationXLabel = nullptr;
    wxSlider* m_rotationZSlider = nullptr;
    wxStaticText* m_rotationZLabel = nullptr;
    wxToggleButton* m_spinButton = nullptr;
    wxToggleButton* m_rockButton = nullptr;
    wxToggleButton* m_spinXButton = nullptr;
    wxToggleButton* m_rockXButton = nullptr;
    wxToggleButton* m_spinZButton = nullptr;
    wxToggleButton* m_rockZButton = nullptr;
    float m_spinAngle = 0.0f;
    bool m_spinning = false;
    bool m_rocking = false;
    float m_rockCenter = 0.0f;
    float m_rockPhase = 0.0f;
    float m_spinXAngle = 0.0f;
    bool m_spinningX = false;
    bool m_rockingX = false;
    float m_rockXCenter = 0.0f;
    float m_rockXPhase = 0.0f;
    float m_spinZAngle = 0.0f;
    bool m_spinningZ = false;
    bool m_rockingZ = false;
    float m_rockZCenter = 0.0f;
    float m_rockZPhase = 0.0f;
    wxCheckBox* m_showUnselected = nullptr;
    wxCheckBox* m_showGridLines = nullptr;
    wxCheckBox* m_showHistograms = nullptr;
    wxSlider* m_pointSizeSlider = nullptr;
    wxSlider* m_opacitySlider = nullptr;
    wxSlider* m_histBinsSlider = nullptr;
    wxStaticText* m_pointSizeLabel = nullptr;
    wxStaticText* m_opacityLabel = nullptr;
    wxStaticText* m_histBinsLabel = nullptr;
    wxStaticText* m_selectionLabel = nullptr;
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
    void StopSpinRock(int plotIndex);

    // Per-plot callbacks. plotIndex == kAllPlots applies the action to every
    // plot; kLeaveField / kLeaveZ mark fields the all-plots control did not set.
    std::function<void(int plotIndex)> onRandomizeAxes;
    std::function<void(int plotIndex, int xCol, int yCol)> onAxisChanged;
    // Locks are 0 or 1. kLeaveField leaves that axis unchanged (all-plots only).
    std::function<void(int plotIndex, int xLock, int yLock)> onAxisLockChanged;
    std::function<void(int plotIndex, int xNorm, int yNorm)> onNormChanged;
    std::function<void(int plotIndex, int zCol, int zNorm)> onZAxisChanged;
    std::function<void(int plotIndex, float angle, bool animated)> onRotationChanged;
    std::function<void(int plotIndex, float angle, bool animated)> onRotationXChanged;
    std::function<void(int plotIndex, float angle, bool animated)> onRotationZChanged;
    std::function<void(int plotIndex, bool zeroY, bool zeroX, bool zeroZ)> onRotationZeroed;
    std::function<void(int plotIndex, int axis, bool spinning, bool rocking)> onSpinRockChanged;
    std::function<void(int plotIndex, bool show)> onShowUnselectedChanged;
    std::function<void(int plotIndex, bool show)> onGridLinesChanged;
    std::function<void(int plotIndex, bool show)> onShowHistogramsChanged;
    std::function<void(int plotIndex, float size)> onPointSizeChanged;
    std::function<void(int plotIndex, float alpha)> onOpacityChanged;
    std::function<void(int plotIndex, int bins)> onHistBinsChanged;
    std::function<void(int plotIndex)> onTabSelected;
    std::function<void()> onAllSelected;

    // Global callbacks
    std::function<void(bool show)> onGlobalTooltipChanged;
    std::function<void(float size)> onGlobalPointSizeChanged;
    std::function<void(int bins)> onGlobalHistBinsChanged;
    std::function<void(int colormap, int colorVar, bool reversed)> onColorMapChanged;
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
    std::function<void(int brushIndex, float offset)> onBrushOpacityOffsetChanged;
    std::function<void(bool selectedOnly)> onSaveData;
    std::function<void(bool)> onAdditiveSelectedChanged;

    float GetPointSize() const;
    void SetGlobalPointSize(float size);
    void SetGlobalHistBins(int bins);
    void SetGlobalTooltip(bool on);
    void ApplyBrushColor(int brushIndex, float r, float g, float b, float a);
    void ShowBrushControls(int brushIndex = -1);
    void SelectBrush(int index);
    void ShowAllPlotsPage();
    void ResetAllAxisDropdowns(bool axes = true, bool norms = true, bool z = true);

    // Push command results into widgets the plot-config sync does not own.
    // SetValue / SetSelection here must not re-fire the control callbacks.
    void SetAllShowUnselected(bool on);
    void SetAllGridLines(bool on);
    void SetAllHistograms(bool on);
    void SetAllLock(bool setX, int xLock, bool setY, int yLock);
    void SetAllAxisColumn(bool setX, int xCol, bool setY, int yCol);
    void SetAllNorm(bool setX, int xNorm, bool setY, int yNorm);
    void SetAllZ(bool setCol, int zCol, bool setNorm, int zNorm);
    void SetAllOpacityPercent(int percent);
    void SetAllRotation(int axis, int degrees);
    void SetAllSpinRock(int axis, bool spinning, bool rocking);
    void SetPlotSpinRock(int plotIndex, int axis, bool spinning, bool rocking);
    void SetDeferRedrawsUi(bool on);
    void SetAdditiveUi(bool on);
    void SetBackgroundUi(int percent);
    void SetColorMapUi(int mapIndex, int varIndex, bool reversed);
    void SetBrushSymbolUi(int brush, int symbol);
    void SetBrushSizeUi(int brush, float offset);
    void SetBrushOpacityUi(int brush, float offset);
    void SetBrushButtonColor(int brush, float r, float g, float b);

private:
    void CreateAllPage();
    void CreateAllPlotsSubPage();
    void CreateBrushesSubPage();
    void SelectPage(int pageIndex);  // 0..N-1 = plot, N = "All"
    void SelectAllSubPage(int idx);  // 0 = All Plots, 1 = Brushes & Colormaps
    void RebuildSelectorGrid();

    wxSimplebook* m_book = nullptr;
    std::vector<PlotTab*> m_plotTabs;
    wxPanel* m_allPage = nullptr;

    // Plot selector grid
    wxPanel* m_selectorPanel = nullptr;
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
    wxLongLong m_lastSpinTime;
    void OnSpinTimer(wxTimerEvent& event);

    // "All" page sub-panel switching
    wxSimplebook* m_allSubBook = nullptr;
    wxButton* m_allPlotsBtn = nullptr;
    wxButton* m_brushesBtn = nullptr;

    // "All Plots" page axis controls (need member access for SetColumns + reset)
    wxChoice* m_allXAxis = nullptr;
    wxChoice* m_allYAxis = nullptr;
    wxChoice* m_allZAxis = nullptr;
    wxChoice* m_allXNorm = nullptr;
    wxChoice* m_allYNorm = nullptr;
    wxChoice* m_allZNorm = nullptr;

    // "All" page widgets
    wxChoice* m_colorVarChoice = nullptr;
    wxSlider* m_pointSizeSlider = nullptr;
    wxSlider* m_histBinsSlider = nullptr;
    wxStaticText* m_pointSizeLabel = nullptr;
    wxStaticText* m_histBinsLabel = nullptr;
    wxStaticText* m_selectionLabel = nullptr;
    std::array<wxButton*, CP_NUM_BRUSHES> m_brushButtons = {};
    wxButton* m_allBrushButton = nullptr;
    int m_activeBrush = 0;        // -1 = "all" mode
    int m_lastIndividualBrush = 0; // last individually selected brush (for display when "all")
    wxChoice* m_brushSymbolChoice = nullptr;
    wxSlider* m_brushSizeSlider = nullptr;
    wxStaticText* m_brushSizeLabel = nullptr;
    wxSlider* m_brushOpacitySlider = nullptr;
    std::array<int, CP_NUM_BRUSHES> m_brushSymbols = {};
    std::array<float, CP_NUM_BRUSHES> m_brushSizeOffsets = {};
    std::array<float, CP_NUM_BRUSHES> m_brushOpacityOffsets = {};
    wxCheckBox* m_globalTooltipCheck = nullptr;
    wxCheckBox* m_additiveSelectedCheck = nullptr;
    wxCheckBox* m_deferRedrawsCheck = nullptr;
    wxCheckBox* m_allShowUnselected = nullptr;
    wxCheckBox* m_allGridLines = nullptr;
    wxCheckBox* m_allHistograms = nullptr;
    wxCheckBox* m_allXLock = nullptr;
    wxCheckBox* m_allYLock = nullptr;
    wxSlider* m_allOpacitySlider = nullptr;
    wxStaticText* m_allOpacityLabel = nullptr;
    std::array<wxSlider*, 3> m_allRotSlider = {};
    std::array<wxStaticText*, 3> m_allRotLabel = {};
    std::array<wxToggleButton*, 3> m_allSpinBtn = {};
    std::array<wxToggleButton*, 3> m_allRockBtn = {};
    wxChoice* m_colorMapChoice = nullptr;
    wxToggleButton* m_colorMapReversed = nullptr;
    wxSlider* m_bgSlider = nullptr;
    bool m_uiSuppress = false;
};
