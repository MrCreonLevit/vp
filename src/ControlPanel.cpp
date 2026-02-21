#include "ControlPanel.h"
#include "Normalize.h"
#include <wx/statline.h>

ControlPanel::ControlPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(220, -1))
{
    SetMinSize(wxSize(220, -1));
    CreateControls();
}

void ControlPanel::CreateControls() {
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(this, wxID_ANY, "Plot Controls");
    auto titleFont = title->GetFont();
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    titleFont.SetPointSize(titleFont.GetPointSize() + 2);
    title->SetFont(titleFont);
    sizer->Add(title, 0, wxALL, 10);

    // Active plot indicator
    m_activePlotLabel = new wxStaticText(this, wxID_ANY, "Active: Plot 0,0");
    auto apFont = m_activePlotLabel->GetFont();
    apFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_activePlotLabel->SetFont(apFont);
    m_activePlotLabel->SetForegroundColour(wxColour(100, 150, 255));
    sizer->Add(m_activePlotLabel, 0, wxLEFT | wxBOTTOM, 10);

    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

    // X Axis + normalization
    sizer->Add(new wxStaticText(this, wxID_ANY, "X Axis"), 0, wxLEFT | wxTOP, 10);
    m_xAxis = new wxChoice(this, wxID_ANY);
    sizer->Add(m_xAxis, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

    sizer->Add(new wxStaticText(this, wxID_ANY, "X Normalization"), 0, wxLEFT | wxTOP, 10);
    m_xNorm = new wxChoice(this, wxID_ANY);
    for (const auto& name : AllNormModeNames())
        m_xNorm->Append(name);
    m_xNorm->SetSelection(0);
    sizer->Add(m_xNorm, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    // Y Axis + normalization
    sizer->Add(new wxStaticText(this, wxID_ANY, "Y Axis"), 0, wxLEFT, 10);
    m_yAxis = new wxChoice(this, wxID_ANY);
    sizer->Add(m_yAxis, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

    sizer->Add(new wxStaticText(this, wxID_ANY, "Y Normalization"), 0, wxLEFT | wxTOP, 10);
    m_yNorm = new wxChoice(this, wxID_ANY);
    for (const auto& name : AllNormModeNames())
        m_yNorm->Append(name);
    m_yNorm->SetSelection(0);
    sizer->Add(m_yNorm, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
    sizer->AddSpacer(5);

    m_pointSizeLabel = new wxStaticText(this, wxID_ANY, "Point Size: 6");
    sizer->Add(m_pointSizeLabel, 0, wxLEFT, 10);
    m_pointSizeSlider = new wxSlider(this, wxID_ANY, 6, 1, 30);
    sizer->Add(m_pointSizeSlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

    m_opacityLabel = new wxStaticText(this, wxID_ANY, "Opacity: 15%");
    sizer->Add(m_opacityLabel, 0, wxLEFT | wxTOP, 10);
    m_opacitySlider = new wxSlider(this, wxID_ANY, 15, 1, 100);
    sizer->Add(m_opacitySlider, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxALL, 10);

    // Brush section
    auto* brushTitle = new wxStaticText(this, wxID_ANY, "Brush");
    auto bFont = brushTitle->GetFont();
    bFont.SetWeight(wxFONTWEIGHT_BOLD);
    brushTitle->SetFont(bFont);
    sizer->Add(brushTitle, 0, wxLEFT, 10);

    sizer->Add(new wxStaticText(this, wxID_ANY, "Brush Color"), 0, wxLEFT | wxTOP, 10);
    m_brushColorPicker = new wxColourPickerCtrl(this, wxID_ANY, wxColour(255, 77, 77));
    sizer->Add(m_brushColorPicker, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    m_selectionLabel = new wxStaticText(this, wxID_ANY, "No selection");
    sizer->Add(m_selectionLabel, 0, wxLEFT | wxTOP, 10);

    auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* clearBtn = new wxButton(this, wxID_ANY, "Clear (C)", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    auto* invertBtn = new wxButton(this, wxID_ANY, "Invert (I)", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    btnSizer->Add(clearBtn, 1, wxRIGHT, 5);
    btnSizer->Add(invertBtn, 1);
    sizer->Add(btnSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxALL, 10);

    m_infoLabel = new wxStaticText(this, wxID_ANY, "No data loaded");
    m_infoLabel->Wrap(200);
    sizer->Add(m_infoLabel, 0, wxLEFT | wxRIGHT, 10);

    auto* helpText = new wxStaticText(this, wxID_ANY,
        "Drag: select\n"
        "Shift+drag: pan\n"
        "Scroll: zoom\n"
        "Cmd+drag: extend\n"
        "Click plot: activate");
    helpText->SetForegroundColour(wxColour(120, 120, 120));
    auto hFont = helpText->GetFont();
    hFont.SetPointSize(hFont.GetPointSize() - 1);
    helpText->SetFont(hFont);
    sizer->Add(helpText, 0, wxALL, 10);

    sizer->AddStretchSpacer();
    SetSizer(sizer);

    // Bind events
    m_xAxis->Bind(wxEVT_CHOICE, &ControlPanel::OnAxisChoice, this);
    m_yAxis->Bind(wxEVT_CHOICE, &ControlPanel::OnAxisChoice, this);
    m_xNorm->Bind(wxEVT_CHOICE, &ControlPanel::OnNormChoice, this);
    m_yNorm->Bind(wxEVT_CHOICE, &ControlPanel::OnNormChoice, this);
    m_pointSizeSlider->Bind(wxEVT_SLIDER, &ControlPanel::OnPointSizeSlider, this);
    m_opacitySlider->Bind(wxEVT_SLIDER, &ControlPanel::OnOpacitySlider, this);

    clearBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (onClearSelection) onClearSelection();
    });
    invertBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (onInvertSelection) onInvertSelection();
    });
    m_brushColorPicker->Bind(wxEVT_COLOURPICKER_CHANGED, [this](wxColourPickerEvent& evt) {
        wxColour c = evt.GetColour();
        if (onBrushColorChanged)
            onBrushColorChanged(c.Red() / 255.0f, c.Green() / 255.0f, c.Blue() / 255.0f);
    });
}

void ControlPanel::SetColumns(const std::vector<std::string>& names) {
    m_xAxis->Clear();
    m_yAxis->Clear();
    for (const auto& name : names) {
        m_xAxis->Append(name);
        m_yAxis->Append(name);
    }
    if (names.size() >= 2) {
        m_xAxis->SetSelection(0);
        m_yAxis->SetSelection(1);
    }
    m_infoLabel->SetLabel(wxString::Format("%zu columns available", names.size()));
}

void ControlPanel::SetSelectionInfo(int selected, int total) {
    if (selected > 0)
        m_selectionLabel->SetLabel(wxString::Format("Selected: %d / %d", selected, total));
    else
        m_selectionLabel->SetLabel("No selection");
}

void ControlPanel::SetActivePlotLabel(int row, int col) {
    m_activePlotLabel->SetLabel(wxString::Format("Active: Plot %d,%d", row, col));
}

void ControlPanel::SetActiveColumns(int xCol, int yCol) {
    m_suppressEvents = true;
    if (xCol >= 0 && xCol < (int)m_xAxis->GetCount())
        m_xAxis->SetSelection(xCol);
    if (yCol >= 0 && yCol < (int)m_yAxis->GetCount())
        m_yAxis->SetSelection(yCol);
    m_suppressEvents = false;
}

void ControlPanel::SetActiveNormModes(int xNorm, int yNorm) {
    m_suppressEvents = true;
    if (xNorm >= 0 && xNorm < (int)m_xNorm->GetCount())
        m_xNorm->SetSelection(xNorm);
    if (yNorm >= 0 && yNorm < (int)m_yNorm->GetCount())
        m_yNorm->SetSelection(yNorm);
    m_suppressEvents = false;
}

int ControlPanel::GetXColumn() const { return m_xAxis->GetSelection(); }
int ControlPanel::GetYColumn() const { return m_yAxis->GetSelection(); }
int ControlPanel::GetXNorm() const { return m_xNorm->GetSelection(); }
int ControlPanel::GetYNorm() const { return m_yNorm->GetSelection(); }
float ControlPanel::GetPointSize() const { return static_cast<float>(m_pointSizeSlider->GetValue()); }
float ControlPanel::GetOpacity() const { return static_cast<float>(m_opacitySlider->GetValue()) / 100.0f; }

void ControlPanel::OnAxisChoice(wxCommandEvent& event) {
    if (!m_suppressEvents && onAxisChanged)
        onAxisChanged(GetXColumn(), GetYColumn());
}

void ControlPanel::OnNormChoice(wxCommandEvent& event) {
    if (!m_suppressEvents && onNormChanged)
        onNormChanged(GetXNorm(), GetYNorm());
}

void ControlPanel::OnPointSizeSlider(wxCommandEvent& event) {
    int val = m_pointSizeSlider->GetValue();
    m_pointSizeLabel->SetLabel(wxString::Format("Point Size: %d", val));
    if (onPointSizeChanged) onPointSizeChanged(static_cast<float>(val));
}

void ControlPanel::OnOpacitySlider(wxCommandEvent& event) {
    int val = m_opacitySlider->GetValue();
    m_opacityLabel->SetLabel(wxString::Format("Opacity: %d%%", val));
    if (onOpacityChanged) onOpacityChanged(static_cast<float>(val) / 100.0f);
}
