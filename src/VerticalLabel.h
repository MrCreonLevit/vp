#pragma once

#include <wx/wx.h>

// A small panel that draws text rotated 90 degrees (bottom to top).
class VerticalLabel : public wxPanel {
public:
    VerticalLabel(wxWindow* parent, const wxString& label = "")
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(18, -1))
    {
        m_label = label;
        SetMinSize(wxSize(18, -1));
        SetBackgroundColour(wxColour(30, 30, 40));
        Bind(wxEVT_PAINT, &VerticalLabel::OnPaint, this);
    }

    void SetLabel(const wxString& label) override {
        m_label = label;
        Refresh();
    }

private:
    void OnPaint(wxPaintEvent&) {
        wxPaintDC dc(this);
        if (m_label.empty()) return;

        wxFont font(9, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
        dc.SetFont(font);
        dc.SetTextForeground(wxColour(160, 170, 200));
        dc.SetBackground(wxBrush(wxColour(30, 30, 40)));
        dc.Clear();

        wxSize sz = GetClientSize();
        wxSize textSz = dc.GetTextExtent(m_label);

        // Draw rotated 90 degrees, centered vertically
        int x = (sz.GetWidth() - textSz.GetHeight()) / 2;
        int y = sz.GetHeight() / 2 + textSz.GetWidth() / 2;
        dc.DrawRotatedText(m_label, x, y, 90.0);
    }

    wxString m_label;
};
