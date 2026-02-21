// Viewpoints (MIT License) - See LICENSE file
#include "ViewpointsApp.h"
#include "MainFrame.h"

bool ViewpointsApp::OnInit() {
    if (!wxApp::OnInit())
        return false;

    auto* frame = new MainFrame();
    frame->Show();
    return true;
}
