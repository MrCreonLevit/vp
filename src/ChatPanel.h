// Viewpoints (MIT License) - See LICENSE file
#pragma once

#include <wx/wx.h>
#include <string>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

// ============================================================
//  ChatPanel — scrollable action log with a command line.
//
//  A sizer element on the right of the main frame, separated from the
//  plot grid by a draggable sash (same 4px grey gap as plot dividers).
//  The frame toggles visibility with sizer->Show/Hide; when hidden the
//  grid reclaims the space.  The view is a wxTextCtrl that retains all
//  text across hide/show, so re-showing reveals the full scrollback.
//
//  Enter in the bottom field submits onCommand. Up/down recalls history.
//  A line starting with '#' is a comment. Log text that is not a command
//  is written with that prefix so a copied log can be run as a command file.
// ============================================================
class ChatPanel : public wxPanel {
public:
    explicit ChatPanel(wxWindow* parent, int panelWidth = 360);
    ~ChatPanel() override;

     // Append a timestamped log line on the UI thread.
    void Log(const wxString& line);
    void ClearLog();

    std::function<void(const wxString& line)> onCommand;

private:
    void BuildUi();
    void SubmitInput();
    void RecallHistory(int direction);

    wxTextCtrl* m_text = nullptr;       // read-only scrollable view
    wxTextCtrl* m_input = nullptr;
    std::vector<wxString> m_history;
    int m_historyPos = 0;               // size() means "draft / newest"
    wxString m_historyDraft;
    int  m_panelWidth;
};

// ============================================================
//  LogThrottle — coalesce high-frequency events into at most one
//  emitted line per flush interval, carrying the latest value per key.
//
//  Record(key, text) sets the pending payload for a key.  A caller
//  (e.g. a wxTimer on MainFrame) periodically calls Flush() which
//  emits all pending keys once and clears the map.  A held spin /
//  drag therefore produces one line every ~intervalms (bounded),
//  each carrying the current value — not hundreds.  Keys live in a
//  map so simultaneous, differently-keyed actions don't clobber each
//  other.  The map is mutex-guarded so Record() is safe from any thread.
// ============================================================
class LogThrottle {
public:
    using EmitFn = std::function<void(const wxString& key, const wxString& text)>;

    LogThrottle() = default;
    ~LogThrottle() = default;

    void SetEmit(EmitFn fn) { m_emit = std::move(fn); }

     // Set the pending payload for `key`.  Thread-safe.
    void Record(const wxString& key, const wxString& text) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_pending[key] = text;
     }

     // Emit all pending keys then clear.  Call from the UI thread.
    void Flush();

private:
    EmitFn  m_emit;
    std::map<wxString, wxString> m_pending;
    std::mutex m_mutex;
};
