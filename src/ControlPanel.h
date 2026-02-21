#pragma once

#include <wx/wx.h>
#include <wx/clrpicker.h>
#include <vector>
#include <string>
#include <functional>

class ControlPanel : public wxPanel {
public:
    explicit ControlPanel(wxWindow* parent);

    void SetColumns(const std::vector<std::string>& names);
    void SetSelectionInfo(int selected, int total);
    void SetActivePlotLabel(int row, int col);
    void SetActiveColumns(int xCol, int yCol);

    std::function<void(int xCol, int yCol)> onAxisChanged;
    std::function<void(float size)> onPointSizeChanged;
    std::function<void(float alpha)> onOpacityChanged;
    std::function<void()> onClearSelection;
    std::function<void()> onInvertSelection;
    std::function<void(float r, float g, float b)> onBrushColorChanged;

    int GetXColumn() const;
    int GetYColumn() const;
    float GetPointSize() const;
    float GetOpacity() const;

private:
    void CreateControls();
    void OnAxisChoice(wxCommandEvent& event);
    void OnPointSizeSlider(wxCommandEvent& event);
    void OnOpacitySlider(wxCommandEvent& event);

    bool m_suppressAxisEvents = false;

    wxStaticText* m_activePlotLabel = nullptr;
    wxChoice* m_xAxis = nullptr;
    wxChoice* m_yAxis = nullptr;
    wxSlider* m_pointSizeSlider = nullptr;
    wxSlider* m_opacitySlider = nullptr;
    wxStaticText* m_pointSizeLabel = nullptr;
    wxStaticText* m_opacityLabel = nullptr;
    wxStaticText* m_infoLabel = nullptr;
    wxStaticText* m_selectionLabel = nullptr;
    wxColourPickerCtrl* m_brushColorPicker = nullptr;
};
