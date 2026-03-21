#include "ofMain.h"
#include "ofApp.h"

// ============================================================
//  main.cpp
//  Standard OF entry point.  Nothing project-specific here.
// ============================================================

int main()
{
    // Window settings — reasonable size for oscilloscope view
    ofGLFWWindowSettings settings;
    settings.setSize(1024, 512);
    settings.resizable = true;
    settings.title = "ChaosOsc";

    auto window = ofCreateWindow(settings);
    ofRunApp(window, std::make_shared<ofApp>());
    return ofRunMainLoop();
}
