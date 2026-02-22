// Viewpoints (MIT License) - See LICENSE file
#pragma once

#include <wx/wx.h>

class ViewpointsApp : public wxApp {
public:
    bool OnInit() override;
    void OnInitCmdLine(wxCmdLineParser& parser) override;
    bool OnCmdLineParsed(wxCmdLineParser& parser) override;

private:
    wxString m_inputFile;
};
