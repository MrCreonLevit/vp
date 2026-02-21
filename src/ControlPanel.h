#pragma once

#include <wx/wx.h>
#include <vector>
#include <string>
#include <functional>
#include <array>

constexpr int CP_NUM_BRUSHES = 7;

class ControlPanel : public wxPanel {
public:
    explicit ControlPanel(wxWindow* parent);

    void SetColumns(const std::vector<std::string>& names);
    void SetSelectionInfo(int selected, int total);
    void SetActivePlotLabel(int row, int col);
    void SetActiveColumns(int xCol, int yCol);
    void SetActiveNormModes(int xNorm, int yNorm);

    std::function<void(int xCol, int yCol)> onAxisChanged;
    std::function<void(int xNorm, int yNorm)> onNormChanged;
    std::function<void(float size)> onPointSizeChanged;
    std::function<void(float alpha)> onOpacityChanged;
    std::function<void()> onClearSelection;
    std::function<void()> onInvertSelection;
    std::function<void(int brushIndex)> onBrushChanged;

    int GetXColumn() const;
    int GetYColumn() const;
    int GetXNorm() const;
    int GetYNorm() const;
    float GetPointSize() const;
    float GetOpacity() const;

private:
    void CreateControls();
    void OnAxisChoice(wxCommandEvent& event);
    void OnNormChoice(wxCommandEvent& event);
    void OnPointSizeSlider(wxCommandEvent& event);
    void OnOpacitySlider(wxCommandEvent& event);
    void SelectBrush(int index);

    bool m_suppressEvents = false;
    int m_activeBrush = 0;

    wxStaticText* m_activePlotLabel = nullptr;
    wxChoice* m_xAxis = nullptr;
    wxChoice* m_yAxis = nullptr;
    wxChoice* m_xNorm = nullptr;
    wxChoice* m_yNorm = nullptr;
    wxSlider* m_pointSizeSlider = nullptr;
    wxSlider* m_opacitySlider = nullptr;
    wxStaticText* m_pointSizeLabel = nullptr;
    wxStaticText* m_opacityLabel = nullptr;
    wxStaticText* m_infoLabel = nullptr;
    wxStaticText* m_selectionLabel = nullptr;
    std::array<wxButton*, CP_NUM_BRUSHES> m_brushButtons = {};
};
