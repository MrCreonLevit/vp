// Viewpoints (MIT License) - See LICENSE file
#include "ViewpointsApp.h"
#include "MainFrame.h"
#include <wx/cmdline.h>

void ViewpointsApp::OnInitCmdLine(wxCmdLineParser& parser) {
    wxApp::OnInitCmdLine(parser);
    parser.AddOption("i", "input-file", "Data file to load on startup",
                     wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
    parser.AddOption("n", "number-of-rows", "Maximum number of rows to read",
                     wxCMD_LINE_VAL_NUMBER, wxCMD_LINE_PARAM_OPTIONAL);
    parser.AddParam("input file", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
}

bool ViewpointsApp::OnCmdLineParsed(wxCmdLineParser& parser) {
    wxString val;
    if (parser.Found("i", &val)) {
        m_inputFile = val;
    } else if (parser.GetParamCount() > 0) {
        m_inputFile = parser.GetParam(0);
    }
    parser.Found("n", &m_maxRows);
    return wxApp::OnCmdLineParsed(parser);
}

bool ViewpointsApp::OnInit() {
    if (!wxApp::OnInit())
        return false;

    auto* frame = new MainFrame();
    frame->Show();

    if (m_maxRows > 0)
        frame->SetMaxRows(static_cast<size_t>(m_maxRows));

    if (!m_inputFile.empty()) {
        frame->LoadFileFromPath(m_inputFile.ToStdString());
    }

    return true;
}
