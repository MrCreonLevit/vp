// Viewpoints (MIT License) - See LICENSE file
#include "ChatPanel.h"
#include <wx/statline.h>
#include <ctime>
#include <cstdio>
#include <chrono>

// ------------------------------------------------------------
//  ChatPanel
// ------------------------------------------------------------

static wxString Timestamp() {
    auto now = std::chrono::system_clock::now();
    auto secs = std::chrono::system_clock::to_time_t(now);
    auto ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count() % 1000);
    std::tm local {};
#ifdef _WIN32
    localtime_s(&local, &secs);
#else
    localtime_r(&secs, &local);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "[%02d:%02d:%02d.%03d]",
                  local.tm_hour, local.tm_min, local.tm_sec, ms);
    return wxString(buf);
}

ChatPanel::ChatPanel(wxWindow* parent, int panelWidth)
          : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(panelWidth, -1))
          , m_panelWidth(panelWidth)
{
    SetMinSize(wxSize(panelWidth, 120));
    BuildUi();
    Log("Viewpoints logging started");
}

void ChatPanel::BuildUi() {
    auto* sizer = new wxBoxSizer(wxVERTICAL);

        // Title bar
    auto* label = new wxStaticText(this, wxID_ANY, "Actions");
    wxFont lf = label->GetFont();
    lf.SetWeight(wxFONTWEIGHT_BOLD);
    label->SetFont(lf);
    sizer->Add(label, 0, wxALL, 6);
    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 2);

         // Scrollable read-only log view.  Uses the system default colours
        // (light grey on macOS light mode) to match the other GUI panels.
    m_text = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                             wxDefaultPosition, wxDefaultSize,
                            wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxHSCROLL);
    wxFont tf = m_text->GetFont();
    tf.SetFamily(wxFONTFAMILY_TELETYPE);
    m_text->SetFont(tf);
    sizer->Add(m_text, 1, wxEXPAND | wxALL, 2);

    SetSizer(sizer);
    Layout();
}

ChatPanel::~ChatPanel() = default;

void ChatPanel::Log(const wxString& line) {
    if (!m_text) return;
    m_text->AppendText(Timestamp() + " " + line + "\n");
    m_text->ShowPosition(m_text->GetLastPosition());
}

void ChatPanel::ClearLog() {
    if (m_text)
        m_text->ChangeValue("");
}

// ------------------------------------------------------------
//  LogThrottle
// ------------------------------------------------------------

void LogThrottle::Flush() {
    std::map<wxString, wxString> emit;
       {
        std::lock_guard<std::mutex> lk(m_mutex);
        emit.swap(m_pending);
         }
    if (emit.empty() || !m_emit) return;
    for (auto& kv : emit)
        m_emit(kv.first, kv.second);
}
