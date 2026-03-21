#pragma once
#include "ofMain.h"
#include "ParamSystem.h"
#include <vector>
#include <string>

namespace ChaosOsc {

// ============================================================
//  SliderUI
//  Renders and handles mouse interaction for all Params.
//  Each param shows as two stacked bars:
//    top bar    -- base value (draggable, colour = param colour)
//    bottom bar -- chaos amount (draggable, always orange)
//
//  Usage:
//    SliderUI ui;
//    ui.layout(x, y, w, rowH);   // call once in setup
//    ui.draw();                   // call in draw
//    ui.mousePressed(x,y);
//    ui.mouseDragged(x,y);
//    ui.mouseReleased();
// ============================================================

struct SliderDef {
    ChaosOsc::Param* param = nullptr;
    ofColor          color;
    bool             showChaos = true;  // show chaos amount bar below
};

class SliderUI {
public:
    bool darkMode = true;  // set by ofApp to match current theme

    void addSlider(Param* p, ofColor col, bool showChaos = true)
    {
        sliders.push_back({p, col, showChaos});
    }

    // Layout all sliders in a grid.
    // cols: number of columns
    void layout(float startX, float startY, float totalW, float totalH, int cols = 2)
    {
        this->startX = startX;
        this->startY = startY;
        this->totalW = totalW;
        this->totalH = totalH;
        this->cols   = cols;

        int   rows   = ((int)sliders.size() + cols - 1) / cols;
        float colW    = totalW / cols;
        // Fixed absolute heights -- label(12) + base bar(10) + gap(4) + chaos bar(7) + gap(10) = 43px per slot
        float rowH    = totalH / rows;
        baseBarH      = 10.f;
        chaosBarH     = 7.f;
        float labelH  = 12.f;
        float gap     = 4.f;
        float slotPad = (rowH - labelH - baseBarH - gap - chaosBarH) * 0.5f;
        slotH         = rowH;

        for (int i = 0; i < (int)sliders.size(); ++i) {
            int   col = i % cols;
            int   row = i / cols;
            float x   = startX + col * colW;
            float y   = startY + row * rowH + slotPad;

            labelPos.push_back({ x + 6, y + labelH });
            rects.push_back({ x + 6, y + labelH + 2, colW - 12, baseBarH });
            chaosRects.push_back({ x + 6, y + labelH + 2 + baseBarH + gap, colW - 12, chaosBarH });
        }
    }

    void draw()
    {
        ofColor labelCol  = darkMode ? ofColor(160,165,185)   : ofColor(15, 18, 30);
        ofColor barBg     = darkMode ? ofColor(18, 21, 28)    : ofColor(175,180,200);
        ofColor chaosBarBg= darkMode ? ofColor(14, 17, 22)    : ofColor(160,165,185);
        ofColor chaosLbl  = darkMode ? ofColor(100,100,115)   : ofColor(50, 55, 75);
        ofColor smoothInd = darkMode ? ofColor(255,255,255,120): ofColor(0,0,0,100);

        for (int i = 0; i < (int)sliders.size(); ++i) {
            auto& sd = sliders[i];
            auto* p  = sd.param;
            auto& r  = rects[i];
            auto& cr = chaosRects[i];
            auto& lp = labelPos[i];

            ofSetColor(labelCol);
            std::string label = p->name + " " + (p->isInt
                ? ofToString(p->iVal())
                : ofToString(p->base, 2));
            ofDrawBitmapString(label, (int)lp.x, (int)lp.y);

            ofSetColor(barBg);
            ofDrawRectangle(r);

            ofSetColor(sd.color);
            ofDrawRectangle(r.x, r.y, r.width * p->normBase(), r.height);

            float sx = r.x + r.width * ((p->smoothed - p->minVal) / (p->maxVal - p->minVal));
            ofSetColor(smoothInd);
            ofDrawLine(sx, r.y, sx, r.y + r.height);

            if (sd.showChaos) {
                ofSetColor(chaosBarBg);
                ofDrawRectangle(cr);

                ofSetColor(220, 130, 40);
                ofDrawRectangle(cr.x, cr.y, cr.width * p->chaosAmt, cr.height);

                ofSetColor(chaosLbl);
                ofDrawBitmapString("~" + ofToString(p->chaosAmt, 2),
                                   (int)cr.x, (int)(cr.y + cr.height + 8));
            }

            if (dragIdx == i || dragChaosIdx == i) {
                ofSetColor(255, 255, 255, 25);
                ofDrawRectangle(r.x - 2, r.y - 2,
                                r.width + 4,
                                r.height + (sd.showChaos ? cr.height + 12 : 0) + 4);
            }
        }
    }

    void mousePressed(int x, int y, int button)
    {
        dragIdx = dragChaosIdx = -1;
        double now = ofGetElapsedTimeMillis();
        for (int i = 0; i < (int)sliders.size(); ++i) {
            if (rects[i].inside(x, y)) {
                bool dbl = (i == lastClickIdx && (now - lastClickTime) < DBL_MS);
                lastClickIdx = i; lastClickTime = now;
                if (dbl) { sliders[i].param->resetToDefault(); }
                else { dragIdx = i; dragStartX = (float)x; dragStartBase = sliders[i].param->normBase(); setFromMouse(i, x, false); }
                return;
            }
            if (sliders[i].showChaos && chaosRects[i].inside(x, y)) {
                bool dbl = (i == lastChaosIdx && (now - lastChaosTime) < DBL_MS);
                lastChaosIdx = i; lastChaosTime = now;
                if (dbl) { sliders[i].param->chaosAmt = 0.f; }
                else { dragChaosIdx = i; dragStartX = (float)x; dragStartBase = sliders[i].param->chaosAmt; setFromMouseChaos(i, x); }
                return;
            }
        }
    }

    void mouseDragged(int x, int y, int button)
    {
        if (dragIdx >= 0)
            setFromMouse(dragIdx, x, false);
        if (dragChaosIdx >= 0)
            setFromMouseChaos(dragChaosIdx, x);
    }

    void mouseReleased(int x, int y, int button)
    {
        dragIdx      = -1;
        dragChaosIdx = -1;
    }

    bool isActive() const { return dragIdx >= 0 || dragChaosIdx >= 0; }

    // Return param under (x,y), or nullptr
    Param* getParamAt(int x, int y) {
        for (int i = 0; i < (int)sliders.size(); ++i)
            if (rects[i].inside(x,y) || (sliders[i].showChaos && chaosRects[i].inside(x,y)))
                return sliders[i].param;
        return nullptr;
    }

private:
    void setFromMouse(int i, int mx, bool /*fine*/)
    {
        auto& r = rects[i];
        float t = (mx - r.x) / r.width;
        sliders[i].param->setNormBase(t);
    }

    void setFromMouseChaos(int i, int mx)
    {
        auto& r = chaosRects[i];
        float t = (mx - r.x) / r.width;
        sliders[i].param->chaosAmt = std::max(0.f, std::min(1.f, t));
    }

    std::vector<SliderDef> sliders;
    std::vector<ofRectangle> rects;
    std::vector<ofRectangle> chaosRects;
    std::vector<glm::vec2>   labelPos;

    float startX = 0, startY = 0, totalW = 800, totalH = 200;
    float baseBarH = 12, chaosBarH = 7, slotH = 30;
    int   cols = 2;

    int   dragIdx = -1, dragChaosIdx = -1;
    float dragStartX = 0, dragStartBase = 0;
    // Double-click tracking
    int    lastClickIdx = -1;  double lastClickTime = 0;
    int    lastChaosIdx = -1;  double lastChaosTime = 0;
    static constexpr double DBL_MS = 350.0;
};

} // namespace ChaosOsc
