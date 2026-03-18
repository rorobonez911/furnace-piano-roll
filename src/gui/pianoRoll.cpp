/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2026 tildearrow and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "gui.h"
#include "guiConst.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <fmt/printf.h>
#include <string.h>
#include <math.h>
#include <vector>

static const bool PR_BLACK_KEY[12]={false,true,false,true,false,false,true,false,true,false,true,false};

static float prSyncScrollX=0.0f;
static bool  prFollow=true;
static bool  prShowPitchSlide=true;
static bool  prShowVolBars=true;
static int   prEffectLane=0;
static bool  prPainting=false;
static bool  prErasing=false;
static bool  prResizing=false;
static int   prResizeRow=-1;
static bool  prSelecting=false;
static int   prSelR0=-1,prSelR1=-1,prSelN0=-1,prSelN1=-1;
static bool  prFxUndoOpen=false;
static bool  prNoteUndoOpen=false;
static float prPanDX=0.0f, prPanDY=0.0f;
static int   prPianoHeld=-1;
static int   prFxLastDragRow=-1;
static bool  prFxSlopeActive=false;
static int   prFxSlopeR0=-1, prFxSlopeR1=-1;
static int   prFxSlopeV0=0,  prFxSlopeV1=0;
static float prFxSlopeTension=0.0f;
static int   prLastNote=96;
static int   prPaintNote=-1;
static int   prQuantize=1;
static int   prScaleRoot=0;
static int   prScaleType=0;
static int   prDragSelStartR=-1;
static int   prDragSelStartN=-1;
static const char* const PR_NOTE_LBL[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

struct PrClipEntry { int rowOff; short note; short ins; short vol; };
static std::vector<PrClipEntry> prClipboard;
static int prClipRows=0;

static ImU32 prColorMulAlpha(ImVec4 c,float a) {
  return IM_COL32((int)(c.x*255),(int)(c.y*255),(int)(c.z*255),(int)(c.w*a*255));
}

static ImU32 prColorBrighter(ImVec4 c,float f) {
  return IM_COL32((int)(ImMin(c.x*f,1.0f)*255),(int)(ImMin(c.y*f,1.0f)*255),(int)(ImMin(c.z*f,1.0f)*255),(int)(c.w*255));
}

static int prInferDuration(const DivPattern* pat,int row,int patLen) {
  for (int r=row+1;r<patLen;r++) {
    if (pat->newData[r][DIV_PAT_NOTE]!=-1) return r-row;
  }
  return patLen-row;
}

static ImU32 prChanColor(int chan, int alpha) {
  float h=(float)((chan*137)%360)/360.0f;
  float r2,g2,b2;
  ImGui::ColorConvertHSVtoRGB(h,0.62f,0.84f,r2,g2,b2);
  return IM_COL32((int)(r2*255),(int)(g2*255),(int)(b2*255),alpha);
}

static bool prIsSpecial(short v) {
  return v==DIV_NOTE_OFF||v==DIV_NOTE_REL||v==DIV_MACRO_REL;
}

static const int PR_SCALE_IV[2][7]={{0,2,4,5,7,9,11},{0,2,3,5,7,8,10}};

static int prSnapScale(int note) {
  if (prScaleType==0) return note;
  const int* iv=PR_SCALE_IV[prScaleType-1];
  int best=note, bestDist=127;
  for (int delta=-11;delta<=11;delta++) {
    int cand=note+delta;
    if (cand<0||cand>=180) continue;
    int rel=((cand%12)-prScaleRoot+12)%12;
    for (int i=0;i<7;i++) {
      if (iv[i]==rel) {
        int d=abs(delta);
        if (d<bestDist) { bestDist=d; best=cand; }
      }
    }
  }
  return best;
}

static float prFxCurve(float t, float tension) {
  if (fabsf(tension)<0.01f) return t;
  float base=powf(10.0f,fabsf(tension));
  if (tension>0) return (powf(base,t)-1.0f)/(base-1.0f);
  return 1.0f-(powf(base,1.0f-t)-1.0f)/(base-1.0f);
}

void FurnaceGUI::drawPianoRoll() {
  if (!pianoRollOpen) return;

  ImGui::SetNextWindowSizeConstraints(ImVec2(420*dpiScale,300*dpiScale),ImVec2(FLT_MAX,FLT_MAX));
  if (!ImGui::Begin("Piano Roll##pianoRoll",&pianoRollOpen,
      ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse)) {
    ImGui::End(); return;
  }

  if (settings.prFontScale!=1.0f) ImGui::SetWindowFontScale(settings.prFontScale);
  if (!e->curSubSong) { ImGui::Text("No song loaded."); ImGui::End(); return; }
  int totalChans=e->getTotalChannelCount();
  if (totalChans<=0) { ImGui::Text("No channels."); ImGui::End(); return; }
  if (prChan<0||prChan>=totalChans) prChan=0;

  ImGui::Text("Ch:"); ImGui::SameLine();
  ImGui::SetNextItemWidth(150*dpiScale);
  if (ImGui::BeginCombo("##prCh",fmt::sprintf("%d: %s",prChan+1,e->getChannelName(prChan)).c_str())) {
    for (int i=0;i<totalChans;i++) {
      bool s=(i==prChan);
      if (ImGui::Selectable(fmt::sprintf("%d: %s",i+1,e->getChannelName(i)).c_str(),s)) {
        prChan=i; prSelRow0=prSelRow1=-1;
      }
      if (s) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered()) {
    int dw=-(int)ImGui::GetIO().MouseWheel;
    if (dw) { prChan=ImClamp(prChan+dw,0,totalChans-1); prSelRow0=prSelRow1=-1; }
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90*dpiScale);
  ImGui::SliderFloat("Zoom##prZ",&prZoom,0.25f,4.0f,"%.2fx");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70*dpiScale);
  ImGui::SliderFloat("H##prH",&prNoteH,4.0f,20.0f,"%.0f");
  ImGui::SameLine();
  ImGui::Checkbox("Show All##prAC",&prShowAllChans);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show all channels as ghost notes");
  ImGui::SameLine();
  ImGui::Checkbox("Follow##prFollow",&prFollow);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scroll to follow playhead");
  ImGui::SameLine();
  ImGui::Separator();
  ImGui::SameLine();
  {
    static const char* scaleTypeNames[]={"Scale: Off","Major","Minor"};
    static const char* rootNames[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    ImGui::SetNextItemWidth(90*dpiScale);
    if (ImGui::BeginCombo("##scType",scaleTypeNames[prScaleType])) {
      for (int i=0;i<3;i++) {
        if (ImGui::Selectable(scaleTypeNames[i],prScaleType==i)) prScaleType=i;
        if (prScaleType==i) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale snap");
    if (prScaleType>0) {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(52*dpiScale);
      if (ImGui::BeginCombo("##scRoot",rootNames[prScaleRoot])) {
        for (int i=0;i<12;i++) {
          bool sel=(prScaleRoot==i);
          if (ImGui::Selectable(rootNames[i],sel)) prScaleRoot=i;
          if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale root note");
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("View\xef\x83\x83##prView")) ImGui::OpenPopup("prViewMenu");
  if (ImGui::BeginPopup("prViewMenu")) {
    ImGui::Checkbox("Pitch Slides##pvPS",&prShowPitchSlide);
    ImGui::Checkbox("Volume Bars##pvVB",&prShowVolBars);
    ImGui::Separator();
    ImGui::Text("Quantize:");
    if (ImGui::RadioButton("Off##qOff",prQuantize==1)) prQuantize=1; ImGui::SameLine();
    if (ImGui::RadioButton("/2##q2",prQuantize==2))   prQuantize=2; ImGui::SameLine();
    if (ImGui::RadioButton("/4##q4",prQuantize==4))   prQuantize=4; ImGui::SameLine();
    if (ImGui::RadioButton("/8##q8",prQuantize==8))   prQuantize=8; ImGui::SameLine();
    if (ImGui::RadioButton("/16##q16",prQuantize==16)) prQuantize=16;
    ImGui::EndPopup();
  }

  float availH=ImGui::GetContentRegionAvail().y;
  float availW=ImGui::GetContentRegionAvail().x;

  const float noteH=ImMax(prNoteH*(float)dpiScale,2.0f);
  const float rowW=ImMax(12.0f*(float)dpiScale*prZoom,1.0f);
  const int   NOTES=180;
  const float pianoW=56.0f*(float)dpiScale;
  const float timelineH=ImMax(prTimelineH*(float)dpiScale,14.0f*(float)dpiScale);
  const float splitterH=6.0f*(float)dpiScale;
  const float effectLaneH=ImMax(prEffectLaneH*(float)dpiScale,40.0f*(float)dpiScale);
  const float fxRowH=ImGui::GetFrameHeightWithSpacing();
  const float noteAreaH=ImMax(availH-timelineH-splitterH*2-effectLaneH-fxRowH*1.2f,60.0f*(float)dpiScale);
  const float noteAreaW=availW;
  const float totalH=NOTES*noteH;

  int ord=curOrder;
  if (ord<0) ord=0;
  if (ord>=e->curSubSong->ordersLen) ord=e->curSubSong->ordersLen-1;
  if (ord<0) { ImGui::Text("No orders."); ImGui::End(); return; }

  int patIdx=e->curSubSong->orders.ord[prChan][ord];
  DivPattern* pat=e->curPat[prChan].getPattern(patIdx,true);
  if (!pat) { ImGui::Text("Pattern unavailable."); ImGui::End(); return; }

  int patLen=e->curSubSong->patLen;
  if (patLen<=0) { ImGui::Text("Pattern is empty."); ImGui::End(); return; }
  int effectCols=ImMax((int)e->curPat[prChan].effectCols,1);
  int volMax=e->getMaxVolumeChan(prChan);
  if (volMax<=0) volMax=0xff;

  const float totalW=(float)patLen*rowW;

  int selR0=ImMin(prSelRow0,prSelRow1), selR1=ImMax(prSelRow0,prSelRow1);
  int selN0=ImMin(prSelN0,prSelN1),    selN1=ImMax(prSelN0,prSelN1);
  bool hasSel=(prSelRow0>=0&&prSelRow1>=0&&prSelN0>=0&&prSelN1>=0);

  ImU32 cBg      =ImGui::ColorConvertFloat4ToU32(uiColors[GUI_COLOR_PIANO_ROLL_BG]);
  ImU32 cKeyW    =ImGui::ColorConvertFloat4ToU32(uiColors[GUI_COLOR_PIANO_ROLL_KEY_WHITE]);
  ImU32 cKeyB    =ImGui::ColorConvertFloat4ToU32(uiColors[GUI_COLOR_PIANO_ROLL_KEY_BLACK]);
  ImU32 cKeyBrd  =ImGui::ColorConvertFloat4ToU32(uiColors[GUI_COLOR_PIANO_ROLL_KEY_BORDER]);
  ImU32 cGrid    =ImGui::ColorConvertFloat4ToU32(uiColors[GUI_COLOR_PIANO_ROLL_GRID]);
  ImU32 cGridHi1 =ImGui::ColorConvertFloat4ToU32(uiColors[GUI_COLOR_PIANO_ROLL_GRID_HI1]);
  ImU32 cGridHi2 =ImGui::ColorConvertFloat4ToU32(uiColors[GUI_COLOR_PIANO_ROLL_GRID_HI2]);
  ImVec4 cNote4  =uiColors[GUI_COLOR_PIANO_ROLL_NOTE];
  ImU32 cSel     =ImGui::ColorConvertFloat4ToU32(uiColors[GUI_COLOR_PIANO_ROLL_SELECTION]);
  ImU32 cHead    =ImGui::ColorConvertFloat4ToU32(uiColors[GUI_COLOR_PATTERN_PLAY_HEAD]);
  int hiA=ImMax((int)e->curSubSong->hilightA,1);
  int hiB=ImMax((int)e->curSubSong->hilightB,1);

  if (prFollow&&e->isPlaying()&&playOrder==ord) {
    float playX=pianoW+oldRow*rowW;
    float target=playX-noteAreaW*0.5f+pianoW*0.5f;
    prSyncScrollX=ImMax(target,0.0f);
  }

  ImGui::SetNextWindowContentSize(ImVec2(pianoW+totalW,timelineH));
  if (ImGui::BeginChild("##prTL",ImVec2(noteAreaW,timelineH),false,
      ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse)) {
    if (settings.prFontScale!=1.0f) ImGui::SetWindowFontScale(settings.prFontScale);
    ImGui::SetScrollX(prSyncScrollX);
    float tlSX=ImGui::GetScrollX();
    ImDrawList* dl=ImGui::GetWindowDrawList();
    ImVec2 twp=ImGui::GetWindowPos();
    float ox=twp.x-tlSX;
    float vx0=twp.x, vx1=twp.x+noteAreaW;
    dl->AddRectFilled(twp,ImVec2(twp.x+noteAreaW,twp.y+timelineH),
      prColorMulAlpha(uiColors[GUI_COLOR_PIANO_ROLL_BG],1.5f));
    dl->AddRectFilled(twp,ImVec2(twp.x+pianoW,twp.y+timelineH),
      prColorMulAlpha(uiColors[GUI_COLOR_PIANO_ROLL_BG],2.0f));
    {
      char maxLbl[12]; snprintf(maxLbl,sizeof(maxLbl),"%d",patLen-1);
      float maxLblW=ImGui::CalcTextSize(maxLbl).x+3;
      int lblStep=1;
      while (lblStep*rowW<maxLblW&&lblStep<patLen) lblStep++;
      if (lblStep>1&&hiA>1) lblStep=((lblStep+hiA-1)/hiA)*hiA;
      for (int r=0;r<patLen;r++) {
        float rx=ox+pianoW+r*rowW;
        if (rx+rowW<vx0||rx>vx1) continue;
        ImU32 gc=(r%hiB==0)?cGridHi2:(r%hiA==0)?cGridHi1:cGrid;
        dl->AddLine(ImVec2(rx,twp.y),ImVec2(rx,twp.y+timelineH),gc);
        if ((r%lblStep==0)&&rx>=twp.x+pianoW-1)
          dl->AddText(ImVec2(rx+2,twp.y+1),IM_COL32(180,180,180,200),fmt::sprintf("%d",r).c_str());
      }
    }
    if (e->isPlaying()&&playOrder==ord) {
      float phx=ox+pianoW+oldRow*rowW;
      dl->AddLine(ImVec2(phx,twp.y),ImVec2(phx,twp.y+timelineH),cHead,2.0f);
    }
    float cxl=ox+pianoW+cursor.y*rowW;
    dl->AddRectFilled(ImVec2(cxl,twp.y),ImVec2(cxl+2,twp.y+timelineH),IM_COL32(255,255,100,200));
    {
      String ordStrS=fmt::sprintf("ORD %02X",ord);
      const char* ordStr=ordStrS.c_str();
      ImVec2 osz=ImGui::CalcTextSize(ordStr);
      float ox2=twp.x+4, oy2=twp.y+2;
      dl->AddRectFilled(ImVec2(ox2-1,oy2-1),ImVec2(ox2+osz.x+2,oy2+osz.y+1),
        prColorMulAlpha(uiColors[GUI_COLOR_PIANO_ROLL_BG],2.0f));
      dl->AddText(ImVec2(ox2,oy2),IM_COL32(200,200,200,230),ordStr);
    }
    dl->AddLine(ImVec2(twp.x,twp.y+timelineH-1),ImVec2(twp.x+noteAreaW,twp.y+timelineH-1),cKeyBrd);
    ImGui::SetCursorPos(ImVec2(0,0));
    ImGui::InvisibleButton("##prSeek",ImVec2(pianoW+totalW,timelineH));
    if (ImGui::IsItemHovered()) {
      float lx=ImGui::GetMousePos().x-twp.x+tlSX;
      if (lx>=pianoW) {
        int mr=ImClamp((int)((lx-pianoW)/rowW),0,patLen-1);
        ImGui::SetTooltip("Seek to row %d",mr);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
          cursor.y=mr;
          curOrder=ord;
          e->seekTo((unsigned char)ord,mr);
        }
      }
    }
  }
  ImGui::EndChild();

  {
    ImGui::InvisibleButton("##prTLSplit",ImVec2(noteAreaW,splitterH));
    bool th=ImGui::IsItemHovered(), ta=ImGui::IsItemActive();
    if (th||ta) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ta) {
      prTimelineH+=ImGui::GetIO().MouseDelta.y/(float)dpiScale;
      prTimelineH=ImClamp(prTimelineH,14.0f,60.0f);
    }
    ImDrawList* tsdl=ImGui::GetWindowDrawList();
    ImVec2 tsmin=ImGui::GetItemRectMin(), tsmax=ImGui::GetItemRectMax();
    float tsmid=tsmin.y+(tsmax.y-tsmin.y)*0.5f;
    tsdl->AddRectFilled(tsmin,tsmax,prColorMulAlpha(uiColors[GUI_COLOR_PIANO_ROLL_BG],ta?2.0f:1.6f));
    tsdl->AddLine(ImVec2(tsmin.x+4,tsmid),ImVec2(tsmax.x-4,tsmid),cGridHi1,1.0f);
  }

  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize,0.0f);
  ImGui::SetNextWindowContentSize(ImVec2(pianoW+totalW,totalH));
  bool gridOpen=ImGui::BeginChild("##prGrid",ImVec2(noteAreaW,noteAreaH),false,
    ImGuiWindowFlags_HorizontalScrollbar);

  if (gridOpen) {
    if (settings.prFontScale!=1.0f) ImGui::SetWindowFontScale(settings.prFontScale);
    ImGui::SetScrollX(prSyncScrollX);
    if (prPanDY!=0) {
      ImGui::SetScrollY(ImGui::GetScrollY()+prPanDY);
      prPanDY=0;
    }
    float curScrollX=ImGui::GetScrollX();
    float scrollY=ImGui::GetScrollY();
    ImDrawList* dl=ImGui::GetWindowDrawList();
    ImVec2 wp=ImGui::GetWindowPos();
    float ox=wp.x-curScrollX;
    float oy=wp.y-scrollY;
    float vx0=wp.x,vx1=wp.x+ImGui::GetWindowWidth();
    float vy0=wp.y,vy1=wp.y+ImGui::GetWindowHeight();

    dl->AddRectFilled(ImVec2(ox+pianoW,oy),ImVec2(ox+pianoW+totalW,oy+totalH),cBg);

    for (int n=0;n<NOTES;n++) {
      int dn=NOTES-1-n;
      float ry0=oy+n*noteH,ry1=ry0+noteH;
      if (ry1<vy0||ry0>vy1) continue;
      bool blk=PR_BLACK_KEY[dn%12];
      ImVec4 bgV=uiColors[GUI_COLOR_PIANO_ROLL_BG];
      float dim=blk?0.72f:1.0f;
      dl->AddRectFilled(ImVec2(ox+pianoW,ry0),ImVec2(ox+pianoW+totalW,ry1),
        IM_COL32((int)(bgV.x*dim*255),(int)(bgV.y*dim*255),(int)(bgV.z*dim*255),255));
      dl->AddLine(ImVec2(ox+pianoW,ry1),ImVec2(ox+pianoW+totalW,ry1),
        (dn%12==0)?cGridHi1:cGrid);
    }

    ImVec4 gv=uiColors[GUI_COLOR_PIANO_ROLL_GRID];
    ImU32 cGridFaint=IM_COL32((int)(gv.x*255),(int)(gv.y*255),(int)(gv.z*255),22);
    for (int r=0;r<=patLen;r++) {
      float cx=ox+pianoW+r*rowW;
      if (cx<vx0-rowW||cx>vx1+rowW) continue;
      if (r%hiB==0)      dl->AddLine(ImVec2(cx,oy),ImVec2(cx,oy+totalH),cGridHi2);
      else if (r%hiA==0) dl->AddLine(ImVec2(cx,oy),ImVec2(cx,oy+totalH),cGridHi1);
      else               dl->AddLine(ImVec2(cx,oy),ImVec2(cx,oy+totalH),cGridFaint);
    }

    if (prQuantize>1) {
      int qstep=ImMax(patLen/prQuantize,1);
      for (int r=0;r<=patLen;r+=qstep) {
        float cx=ox+pianoW+r*rowW;
        if (cx<vx0-rowW||cx>vx1+rowW) continue;
        dl->AddLine(ImVec2(cx,oy),ImVec2(cx,oy+totalH),IM_COL32(255,220,80,40),2.0f);
      }
    }

    {
      float bx0=ox+pianoW;
      float bx1=ox+pianoW+totalW;
      dl->AddLine(ImVec2(bx0,oy),ImVec2(bx0,oy+totalH),IM_COL32(0,0,0,90),2.0f);
      dl->AddLine(ImVec2(bx1,oy),ImVec2(bx1,oy+totalH),IM_COL32(0,0,0,90),2.0f);
    }

    if (hasSel) {
      float sx0=ox+pianoW+selR0*rowW,   sx1=ox+pianoW+(selR1+1)*rowW;
      float sy0=oy+(NOTES-1-selN1)*noteH,sy1=oy+(NOTES-selN0)*noteH;
      dl->AddRectFilled(ImVec2(sx0,sy0),ImVec2(sx1,sy1),cSel);
      dl->AddRect(ImVec2(sx0,sy0),ImVec2(sx1,sy1),IM_COL32(255,255,255,180));
    }

    if (prShowAllChans) {
      for (int ch=0;ch<totalChans;ch++) {
        if (ch==prChan) continue;
        if (e->isChannelMuted(ch)) continue;
        int cpIdx=e->curSubSong->orders.ord[ch][ord];
        DivPattern* cp=e->curPat[ch].getPattern(cpIdx,false);
        if (!cp) continue;
        ImU32 gTop=prChanColor(ch,95);
        ImU32 gBot=prChanColor(ch,55);
        for (int r=0;r<patLen;r++) {
          short nv=cp->newData[r][DIV_PAT_NOTE];
          if (nv<0||nv>=NOTES||prIsSpecial(nv)) continue;
          int dur=prInferDuration(cp,r,patLen);
          float nx0=ox+pianoW+r*rowW+1;
          float ny0=oy+(NOTES-1-nv)*noteH+1;
          float nx1=ox+pianoW+(r+dur)*rowW-1;
          float ny1=ny0+noteH-2;
          if (nx1<vx0||nx0>vx1||ny1<vy0||ny0>vy1) continue;
          dl->AddRectFilledMultiColor(ImVec2(nx0,ny0),ImVec2(nx1,ny1),gTop,gTop,gBot,gBot);
        }
      }
    }

    ImVec2 fsz=ImGui::CalcTextSize("C-5");
    for (int r=0;r<patLen;r++) {
      short nv=pat->newData[r][DIV_PAT_NOTE];
      if (nv==-1) continue;
      float rx0=ox+pianoW+r*rowW;
      float rx1=ox+pianoW+(r+1)*rowW;

      if (prIsSpecial(nv)) {
        if (rx0<vx0-rowW||rx0>vx1) continue;
        ImVec4 mc4=(nv==DIV_NOTE_OFF)?uiColors[GUI_COLOR_PIANO_ROLL_NOTE_OFF]:uiColors[GUI_COLOR_PIANO_ROLL_NOTE_REL];
        ImU32 mc=ImGui::ColorConvertFloat4ToU32(mc4);
        ImU32 mcFill=IM_COL32((int)(mc4.x*255),(int)(mc4.y*255),(int)(mc4.z*255),55);
        dl->AddRectFilled(ImVec2(rx0+1,oy),ImVec2(ImMin(rx0+rowW*0.35f,rx1),oy+totalH),mcFill);
        dl->AddLine(ImVec2(rx0,oy),ImVec2(rx0,oy+totalH),mc,2.0f);
        const char* lbl=(nv==DIV_NOTE_OFF)?"OFF":(nv==DIV_NOTE_REL)?"===":"REL";
        if (noteH>fsz.y*0.8f) dl->AddText(ImVec2(rx0+2,oy+2),mc,lbl);
        continue;
      }
      if (nv<0||nv>=NOTES) continue;

      int dur=prInferDuration(pat,r,patLen);
      float nx0=ox+pianoW+r*rowW+1;
      float ny0=oy+(NOTES-1-nv)*noteH+1;
      float nx1=ox+pianoW+(r+dur)*rowW-1;
      float ny1=ny0+noteH-2;

      if (nx1<vx0||nx0>vx1||ny1<vy0||ny0>vy1) continue;

      bool inSel=hasSel&&r>=selR0&&r<=selR1&&nv>=selN0&&nv<=selN1;
      float bf=(0.65f+0.35f*((float)(nv/12)/14.0f))*(inSel?1.5f:1.0f);
      ImU32 ncTop=prColorBrighter(cNote4,bf*1.18f);
      ImU32 ncBot=prColorBrighter(cNote4,bf*0.72f);
      dl->AddRectFilledMultiColor(ImVec2(nx0,ny0),ImVec2(nx1,ny1),ncTop,ncTop,ncBot,ncBot);
      dl->AddRect(ImVec2(nx0,ny0),ImVec2(nx1,ny1),IM_COL32(255,255,255,inSel?90:38),1.5f);

      if (nx1-nx0>8) {
        float hx=nx1-4*(float)dpiScale;
        dl->AddRectFilled(ImVec2(hx,ny0),ImVec2(nx1,ny1),IM_COL32(255,255,255,55),1.0f);
      }

      dl->PushClipRect(ImVec2(nx0,ny0),ImVec2(nx1,ny1),true);
      float lw=nx1-nx0-6;
      if (lw>4&&ny1-ny0>fsz.y*0.5f) {
        short ins=pat->newData[r][DIV_PAT_INS];
        char lbl[20];
        if (ins>=0) snprintf(lbl,sizeof(lbl),"%s i%02X",noteNames[nv],(unsigned)ins);
        else        snprintf(lbl,sizeof(lbl),"%s",noteNames[nv]);
        ImVec2 tsz=ImGui::CalcTextSize(lbl);
        if (tsz.x>lw&&ins>=0) {
          snprintf(lbl,sizeof(lbl),"%s",noteNames[nv]);
          tsz=ImGui::CalcTextSize(lbl);
        }
        if (tsz.x<=lw) {
          float ty=ny0+(ny1-ny0-tsz.y)*0.5f;
          dl->AddText(ImVec2(nx0+3,ty),IM_COL32(255,255,255,210),lbl);
        }
      }
      dl->PopClipRect();

      if (prShowVolBars) {
        short vol=pat->newData[r][DIV_PAT_VOL];
        if (vol>=0&&volMax>0) {
          float vf=ImClamp((float)vol/(float)volMax,0.0f,1.0f);
          float bh=ImMax((ny1-ny0)*0.18f,2.0f*(float)dpiScale);
          float bw=(nx1-nx0)*vf;
          dl->AddRectFilled(ImVec2(nx0,ny1-bh),ImVec2(nx0+bw,ny1),
            IM_COL32(255,255,255,120));
        }
      }
    }

    if (prShowPitchSlide) {
      int ec2=ImMax((int)e->curPat[prChan].effectCols,1);
      for (int r=0;r<patLen;r++) {
        short nv=pat->newData[r][DIV_PAT_NOTE];
        if (nv<0||nv>=NOTES||prIsSpecial(nv)) continue;
        bool slideUp=false,slideDown=false;
        int portaTargetNote=-1,portaTargetRow=-1;
        for (int ei=0;ei<ec2;ei++) {
          short fx=pat->newData[r][DIV_PAT_FX(ei)];
          if (fx<0) continue;
          if (fx==0x01||fx==0xE1) slideUp=true;
          else if (fx==0x02||fx==0xE2) slideDown=true;
          else if (fx==0x03) {
            for (int rr=r+1;rr<patLen&&rr<r+256;rr++) {
              short nn=pat->newData[rr][DIV_PAT_NOTE];
              if (nn>=0&&nn<NOTES&&!prIsSpecial(nn)) { portaTargetNote=nn; portaTargetRow=rr; break; }
              if (prIsSpecial(nn)) break;
            }
          }
        }
        bool anySlide=slideUp||slideDown||(portaTargetNote>=0);
        if (!anySlide) continue;

        int dur=prInferDuration(pat,r,patLen);
        float nx0=ox+pianoW+r*rowW+1;
        float nx1=ox+pianoW+(r+dur)*rowW-1;
        float noteTop=oy+(NOTES-1-nv)*noteH+1;
        float noteBot=noteTop+noteH-2;
        float midY=(noteTop+noteBot)*0.5f;
        if (nx1<vx0||nx0>vx1) continue;

        float bf=0.65f+0.35f*((float)(nv/12)/14.0f);
        ImU32 slideCol=prColorBrighter(cNote4,bf*1.7f);

        if (portaTargetNote>=0&&portaTargetRow>=0) {
          float toX=ox+pianoW+portaTargetRow*rowW;
          float toY=oy+(NOTES-1-portaTargetNote)*noteH+noteH*0.5f;
          float y0=ImMin(noteTop,toY)-2, y1=ImMax(noteBot,toY)+2;
          dl->PushClipRect(ImVec2(nx0,y0),ImVec2(toX+rowW,y1),true);
          dl->AddLine(ImVec2(nx1,midY),ImVec2(toX,toY),slideCol,2.0f);
          dl->PopClipRect();
        } else {
          int targetNote=-1;
          for (int rr=r+1;rr<patLen&&rr<r+256;rr++) {
            short nn=pat->newData[rr][DIV_PAT_NOTE];
            if (nn>=0&&nn<NOTES&&!prIsSpecial(nn)) { targetNote=nn; break; }
            if (prIsSpecial(nn)) break;
          }
          float toY;
          if (targetNote>=0)
            toY=oy+(NOTES-1-targetNote)*noteH+noteH*0.5f;
          else
            toY=midY+(slideUp?-noteH*20.0f:noteH*20.0f);
          float cly0=oy, cly1=oy+totalH;
          dl->PushClipRect(ImVec2(nx0,cly0),ImVec2(nx1,cly1),true);
          dl->AddLine(ImVec2(nx0+(nx1-nx0)*0.5f,midY),ImVec2(nx1,toY),slideCol,2.0f);
          dl->PopClipRect();
        }
      }
    }

    if (e->isPlaying()&&playOrder==ord) {
      float phx=ox+pianoW+oldRow*rowW;
      dl->AddLine(ImVec2(phx,oy),ImVec2(phx,oy+totalH),cHead,2.0f);
    }
    {
      float cx=ox+pianoW+cursor.y*rowW;
      dl->AddLine(ImVec2(cx,oy),ImVec2(cx,oy+totalH),IM_COL32(255,255,100,70),1.0f);
    }

    float pkx=wp.x;
    float bkw=pianoW*0.62f;
    float fszH=ImGui::GetFontSize();
    bool showCOnly  =(noteH>=fszH*0.55f);
    bool showAllWhite=(noteH>=fszH*1.0f);
    bool showBlack  =(noteH>=fszH*1.4f);

    dl->PushClipRect(ImVec2(pkx,vy0),ImVec2(pkx+pianoW,vy1),true);
    for (int n=0;n<NOTES;n++) {
      int dn=NOTES-1-n;
      if (PR_BLACK_KEY[dn%12]) continue;
      float ry0=oy+n*noteH,ry1=ry0+noteH;
      if (ry1<vy0||ry0>vy1) continue;
      bool held=(prPianoHeld==dn);
      dl->AddRectFilled(ImVec2(pkx,ry0),ImVec2(pkx+pianoW,ry1),
        held?IM_COL32(180,210,255,255):cKeyW);
      dl->AddLine(ImVec2(pkx,ry1),ImVec2(pkx+pianoW,ry1),cKeyBrd);
      char lb[8]; lb[0]=0;
      bool isC=(dn%12==0);
      if (isC&&showCOnly)          strncpy(lb,noteNames[dn],7);
      else if (!isC&&showAllWhite) strncpy(lb,noteNames[dn],7);
      if (lb[0]) {
        ImVec2 tsz=ImGui::CalcTextSize(lb);
        if (tsz.y<=ry1-ry0) {
          float tx=pkx+bkw-tsz.x-2;
          if (tx<pkx+1) tx=pkx+1;
          float ty=ry0+(ry1-ry0-tsz.y)*0.5f;
          dl->AddText(ImVec2(tx,ty),IM_COL32(40,40,40,230),lb);
        }
      }
    }
    dl->PopClipRect();

    dl->PushClipRect(ImVec2(pkx,vy0),ImVec2(pkx+bkw,vy1),true);
    for (int n=0;n<NOTES;n++) {
      int dn=NOTES-1-n;
      if (!PR_BLACK_KEY[dn%12]) continue;
      float ry0=oy+n*noteH,ry1=ry0+noteH;
      if (ry1<vy0||ry0>vy1) continue;
      bool held=(prPianoHeld==dn);
      dl->AddRectFilled(ImVec2(pkx,ry0),ImVec2(pkx+bkw,ry1),
        held?IM_COL32(60,100,180,255):cKeyB);
      dl->AddRect(ImVec2(pkx,ry0),ImVec2(pkx+bkw,ry1),cKeyBrd,0.0f,0,0.8f);
      if (showBlack) {
        char lb[8];
        strncpy(lb,noteNames[dn],7);
        ImVec2 tsz=ImGui::CalcTextSize(lb);
        if (tsz.y<=ry1-ry0) {
          float ty=ry0+(ry1-ry0-tsz.y)*0.5f;
          dl->AddText(ImVec2(pkx+2,ty),IM_COL32(200,200,200,210),lb);
        }
      }
    }
    dl->PopClipRect();
    dl->AddLine(ImVec2(pkx+pianoW,oy),ImVec2(pkx+pianoW,oy+totalH),cKeyBrd,1.5f);

    ImGui::SetCursorPos(ImVec2(0,0));
    ImGui::InvisibleButton("##prInteract",ImVec2(pianoW+totalW,totalH));
    bool hov=ImGui::IsItemHovered();
    bool midHeld=ImGui::IsMouseDown(ImGuiMouseButton_Middle);

    if (hov) {
      ImVec2 mp=ImGui::GetMousePos();
      float lx=mp.x-wp.x+prSyncScrollX;
      float ly=mp.y-wp.y+scrollY;
      bool inPiano=(mp.x-wp.x<pianoW);
      int mrow=(!inPiano)?(int)((lx-pianoW)/rowW):-1;
      int mnote=ImClamp(NOTES-1-(int)(ly/noteH),0,NOTES-1);

      if (prQuantize>1&&mrow>=0) {
        int qstep=ImMax(patLen/prQuantize,1);
        mrow=(mrow/qstep)*qstep;
        mrow=ImClamp(mrow,0,patLen-1);
      } else if (mrow>=0) {
        mrow=ImClamp(mrow,0,patLen-1);
      }

      if (midHeld&&!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ImVec2 delta=ImGui::GetIO().MouseDelta;
        prPanDX-=delta.x;
        prPanDY-=delta.y;
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      }

      if (inPiano&&mnote>=0&&mnote<NOTES) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
          if (prPianoHeld!=mnote) {
            if (prPianoHeld>=0) e->noteOff(prChan);
            e->noteOn(prChan,curIns>=0?curIns:(prevIns>=0?prevIns:0),mnote-60);
            prPianoHeld=mnote;
          }
        }
        bool blkKey=PR_BLACK_KEY[mnote%12];
        float ry0=oy+(NOTES-1-mnote)*noteH;
        float kw=blkKey?(pianoW*0.62f):pianoW;
        dl->AddRectFilled(ImVec2(pkx,ry0),ImVec2(pkx+kw,ry0+noteH),
          IM_COL32(180,210,255,blkKey?120:80));
      }
      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)&&prPianoHeld>=0&&inPiano) {
        e->noteOff(prChan); prPianoHeld=-1;
      }

      if (!inPiano&&mrow>=0&&mrow<patLen) {
        bool nearEdge=false;
        if (!prSelecting&&!prPainting&&!prErasing&&!prResizing) {
          short ev=pat->newData[mrow][DIV_PAT_NOTE];
          if (ev==mnote&&!prIsSpecial(ev)) {
            float nr=ox+pianoW+(mrow+1)*rowW-1;
            if (mp.x>=nr-5*(float)dpiScale) { nearEdge=true; prResizeRow=mrow; }
          }
          if (!nearEdge) {
            for (int rr=mrow-1;rr>=0&&rr>=mrow-128;rr--) {
              short sv=pat->newData[rr][DIV_PAT_NOTE];
              if (sv==-1) continue;
              if (sv==mnote&&!prIsSpecial(sv)) {
                int dur=prInferDuration(pat,rr,patLen);
                if (mrow==rr+dur-1) {
                  float nr=ox+pianoW+(rr+dur)*rowW-1;
                  if (mp.x>=nr-5*(float)dpiScale) { nearEdge=true; prResizeRow=rr; }
                }
              }
              break;
            }
          }
        }
        if (nearEdge||prResizing) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
          if (nearEdge) {
            prResizing=true;
            prepareUndo(GUI_UNDO_PATTERN_EDIT); prNoteUndoOpen=true;
          } else if (midHeld) {
            prSelecting=true;
            prDragSelStartR=mrow; prDragSelStartN=mnote;
            prSelR0=prSelR1=mrow; prSelN0=prSelN1=mnote;
            prSelRow0=prSelRow1=prSelN0=prSelN1=-1;
          } else {
            prPainting=true; prErasing=false;
            prSelRow0=prSelRow1=-1; prSelN0=prSelN1=-1;
            int defNote=mnote;
            if (pat->newData[mrow][DIV_PAT_NOTE]==-1) {
              if (ord>0) {
                int prevPatIdx=e->curSubSong->orders.ord[prChan][ord-1];
                DivPattern* prevPat=e->curPat[prChan].getPattern(prevPatIdx,false);
                if (prevPat) {
                  for (int rr=mrow;rr>=0;rr--) {
                    short pn=prevPat->newData[rr][DIV_PAT_NOTE];
                    if (pn>=0&&pn<NOTES&&!prIsSpecial(pn)) { defNote=pn; break; }
                  }
                }
              }
              if (defNote==mnote&&prLastNote>=0&&prLastNote<NOTES) defNote=prLastNote;
            }
            prPaintNote=defNote;
            prepareUndo(GUI_UNDO_PATTERN_EDIT); prNoteUndoOpen=true;
          }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)&&!prPainting&&!prResizing)
          ImGui::OpenPopup("##prCtx");

        if (prPainting&&ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
          int pn=prSnapScale((prPaintNote>=0)?prPaintNote:mnote);
          pat->newData[mrow][DIV_PAT_NOTE]=(short)pn;
          prLastNote=pn;
          int insToUse=(curIns>=0)?curIns:(prevIns>=0?prevIns:-1);
          if (pat->newData[mrow][DIV_PAT_INS]==-1&&insToUse>=0)
            pat->newData[mrow][DIV_PAT_INS]=(short)insToUse;
          if (pat->newData[mrow][DIV_PAT_VOL]==-1)
            pat->newData[mrow][DIV_PAT_VOL]=(short)volMax;
          MARK_MODIFIED;
        }

        if (prResizing&&ImGui::IsMouseDown(ImGuiMouseButton_Left)&&prResizeRow>=0) {
          if (mrow>=prResizeRow) {
            for (int rr=prResizeRow+1;rr<patLen;rr++) {
              short sv=pat->newData[rr][DIV_PAT_NOTE];
              if (sv==-1||sv==DIV_NOTE_OFF) pat->newData[rr][DIV_PAT_NOTE]=-1;
              else break;
            }
            if (mrow+1<patLen)
              pat->newData[mrow+1][DIV_PAT_NOTE]=DIV_NOTE_OFF;
          }
          MARK_MODIFIED;
        }

        if (prSelecting&&ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
          prSelR1=mrow; prSelN1=mnote;
          if (prDragSelStartR>=0) {
            int dr0=ImMin(prDragSelStartR,mrow), dr1=ImMax(prDragSelStartR,mrow);
            int dn0=ImMin(prDragSelStartN,mnote), dn1=ImMax(prDragSelStartN,mnote);
            float sx0=ox+pianoW+dr0*rowW;
            float sx1=ox+pianoW+(dr1+1)*rowW;
            float sy0=oy+(NOTES-1-dn1)*noteH;
            float sy1=oy+(NOTES-1-dn0+1)*noteH;
            dl->AddRectFilled(ImVec2(sx0,sy0),ImVec2(sx1,sy1),IM_COL32(100,160,255,30));
            dl->AddRect(ImVec2(sx0,sy0),ImVec2(sx1,sy1),IM_COL32(100,160,255,180),0.0f,0,1.5f);
          }
        }

        float hcx=ox+pianoW+mrow*rowW;
        float hcy=oy+(NOTES-1-mnote)*noteH;
        dl->AddRectFilled(ImVec2(hcx,hcy),ImVec2(hcx+rowW,hcy+noteH),IM_COL32(255,255,255,28));

        if (!ImGui::IsPopupOpen("##prCtx")) {
          if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)&&!prPainting&&!prResizing)
            { prErasing=true; prepareUndo(GUI_UNDO_PATTERN_EDIT); }
          if (prErasing&&ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            for (int col=0;col<DIV_MAX_COLS;col++) pat->newData[mrow][col]=-1;
            MARK_MODIFIED;
          }
        }
      }

      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (prNoteUndoOpen) { makeUndo(GUI_UNDO_PATTERN_EDIT); prNoteUndoOpen=false; }
        if (prSelecting) {
          prSelecting=false;
          prSelRow0=ImMin(prSelR0,prSelR1); prSelRow1=ImMax(prSelR0,prSelR1);
          prSelN0  =ImMin(prSelN0,prSelN1); prSelN1  =ImMax(prSelN0,prSelN1);
          prDragSelStartR=-1; prDragSelStartN=-1;
        }
        prPainting=prResizing=false; prResizeRow=-1; prPaintNote=-1;
        if (prPianoHeld>=0&&!hov) { e->noteOff(prChan); prPianoHeld=-1; }
      }
      if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)&&prErasing)
        { makeUndo(GUI_UNDO_PATTERN_EDIT); prErasing=false; }
    } else {
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (prNoteUndoOpen) { makeUndo(GUI_UNDO_PATTERN_EDIT); prNoteUndoOpen=false; }
        prPainting=prResizing=prSelecting=false; prResizeRow=-1; prPaintNote=-1;
        if (prPianoHeld>=0) { e->noteOff(prChan); prPianoHeld=-1; }
      }
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)&&prErasing)
        { makeUndo(GUI_UNDO_PATTERN_EDIT); prErasing=false; }
    }

    if (ImGui::BeginPopup("##prCtx")) {
      ImVec2 mp2=ImGui::GetMousePos();
      float lx2=mp2.x-wp.x+prSyncScrollX;
      float ly2=mp2.y-wp.y+scrollY;
      int mr=ImClamp((int)((lx2-pianoW)/rowW),0,patLen-1);
      int mn=ImClamp(NOTES-1-(int)(ly2/noteH),0,NOTES-1);
      bool onNote=(pat->newData[mr][DIV_PAT_NOTE]>=0&&!prIsSpecial(pat->newData[mr][DIV_PAT_NOTE]));
      ImGui::TextDisabled("Row %d  |  %s",mr,noteNames[mn]);
      ImGui::Separator();
      if (ImGui::MenuItem("Note On")) {
        prepareUndo(GUI_UNDO_PATTERN_EDIT);
        int pn=prSnapScale(mn);
        pat->newData[mr][DIV_PAT_NOTE]=(short)pn;
        int insToUse2=(curIns>=0)?curIns:(prevIns>=0?prevIns:-1);
        if (pat->newData[mr][DIV_PAT_INS]==-1&&insToUse2>=0) pat->newData[mr][DIV_PAT_INS]=(short)insToUse2;
        if (pat->newData[mr][DIV_PAT_VOL]==-1) pat->newData[mr][DIV_PAT_VOL]=(short)volMax;
        prLastNote=pn;
        makeUndo(GUI_UNDO_PATTERN_EDIT); MARK_MODIFIED;
      }
      if (ImGui::MenuItem("Note Off")) {
        prepareUndo(GUI_UNDO_PATTERN_EDIT);
        pat->newData[mr][DIV_PAT_NOTE]=DIV_NOTE_OFF;
        makeUndo(GUI_UNDO_PATTERN_EDIT); MARK_MODIFIED;
      }
      if (ImGui::MenuItem("Note Release")) {
        prepareUndo(GUI_UNDO_PATTERN_EDIT);
        pat->newData[mr][DIV_PAT_NOTE]=DIV_NOTE_REL;
        makeUndo(GUI_UNDO_PATTERN_EDIT); MARK_MODIFIED;
      }
      if (ImGui::MenuItem("Macro Release")) {
        prepareUndo(GUI_UNDO_PATTERN_EDIT);
        pat->newData[mr][DIV_PAT_NOTE]=DIV_MACRO_REL;
        makeUndo(GUI_UNDO_PATTERN_EDIT); MARK_MODIFIED;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Set Instrument",nullptr,false,onNote||(hasSel&&selR0>=0))) {
        prepareUndo(GUI_UNDO_PATTERN_EDIT);
        int insToSet=(curIns>=0)?curIns:0;
        if (hasSel) {
          for (int r=selR0;r<=selR1;r++) {
            short nv=pat->newData[r][DIV_PAT_NOTE];
            if (nv>=0&&nv<NOTES&&!prIsSpecial(nv)) pat->newData[r][DIV_PAT_INS]=(short)insToSet;
          }
        } else if (onNote) {
          pat->newData[mr][DIV_PAT_INS]=(short)insToSet;
        }
        makeUndo(GUI_UNDO_PATTERN_EDIT); MARK_MODIFIED;
      }
      ImGui::Separator();
      if (hasSel&&ImGui::BeginMenu("Transpose")) {
        if (ImGui::MenuItem("+1 semitone")) {
          prepareUndo(GUI_UNDO_PATTERN_EDIT);
          for (int r=selR0;r<=selR1;r++) {
            short nv=pat->newData[r][DIV_PAT_NOTE];
            if (nv>=selN0&&nv<=selN1&&!prIsSpecial(nv)&&nv+1<NOTES)
              pat->newData[r][DIV_PAT_NOTE]=nv+1;
          }
          prSelN0=ImClamp(prSelN0+1,0,NOTES-1); prSelN1=ImClamp(prSelN1+1,0,NOTES-1);
          makeUndo(GUI_UNDO_PATTERN_EDIT); MARK_MODIFIED;
        }
        if (ImGui::MenuItem("-1 semitone")) {
          prepareUndo(GUI_UNDO_PATTERN_EDIT);
          for (int r=selR0;r<=selR1;r++) {
            short nv=pat->newData[r][DIV_PAT_NOTE];
            if (nv>=selN0&&nv<=selN1&&!prIsSpecial(nv)&&nv-1>=0)
              pat->newData[r][DIV_PAT_NOTE]=nv-1;
          }
          prSelN0=ImClamp(prSelN0-1,0,NOTES-1); prSelN1=ImClamp(prSelN1-1,0,NOTES-1);
          makeUndo(GUI_UNDO_PATTERN_EDIT); MARK_MODIFIED;
        }
        if (ImGui::MenuItem("+1 octave")) {
          prepareUndo(GUI_UNDO_PATTERN_EDIT);
          for (int r=selR0;r<=selR1;r++) {
            short nv=pat->newData[r][DIV_PAT_NOTE];
            if (nv>=selN0&&nv<=selN1&&!prIsSpecial(nv)&&nv+12<NOTES)
              pat->newData[r][DIV_PAT_NOTE]=nv+12;
          }
          prSelN0=ImClamp(prSelN0+12,0,NOTES-1); prSelN1=ImClamp(prSelN1+12,0,NOTES-1);
          makeUndo(GUI_UNDO_PATTERN_EDIT); MARK_MODIFIED;
        }
        if (ImGui::MenuItem("-1 octave")) {
          prepareUndo(GUI_UNDO_PATTERN_EDIT);
          for (int r=selR0;r<=selR1;r++) {
            short nv=pat->newData[r][DIV_PAT_NOTE];
            if (nv>=selN0&&nv<=selN1&&!prIsSpecial(nv)&&nv-12>=0)
              pat->newData[r][DIV_PAT_NOTE]=nv-12;
          }
          prSelN0=ImClamp(prSelN0-12,0,NOTES-1); prSelN1=ImClamp(prSelN1-12,0,NOTES-1);
          makeUndo(GUI_UNDO_PATTERN_EDIT); MARK_MODIFIED;
        }
        ImGui::EndMenu();
      }
      ImGui::Separator();
      if (hasSel&&ImGui::MenuItem("Copy Selection")) {
        prClipboard.clear();
        prClipRows=selR1-selR0+1;
        for (int r=selR0;r<=selR1;r++) {
          short nv=pat->newData[r][DIV_PAT_NOTE];
          if (nv>=0&&nv<NOTES&&nv>=selN0&&nv<=selN1&&!prIsSpecial(nv)) {
            PrClipEntry ce;
            ce.rowOff=r-selR0; ce.note=nv;
            ce.ins=pat->newData[r][DIV_PAT_INS];
            ce.vol=pat->newData[r][DIV_PAT_VOL];
            prClipboard.push_back(ce);
          }
        }
      }
      if (!prClipboard.empty()&&ImGui::MenuItem("Paste")) {
        prepareUndo(GUI_UNDO_PATTERN_EDIT);
        int baseRow=cursor.y;
        for (auto& ce:prClipboard) {
          int r=baseRow+ce.rowOff;
          if (r>=0&&r<patLen) {
            pat->newData[r][DIV_PAT_NOTE]=ce.note;
            if (ce.ins>=0) pat->newData[r][DIV_PAT_INS]=ce.ins;
            if (ce.vol>=0) pat->newData[r][DIV_PAT_VOL]=ce.vol;
          }
        }
        makeUndo(GUI_UNDO_PATTERN_EDIT); MARK_MODIFIED;
      }
      if (ImGui::MenuItem("Select All")) {
        prSelRow0=0; prSelRow1=patLen-1; prSelN0=0; prSelN1=NOTES-1;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Erase Note")) {
        prepareUndo(GUI_UNDO_PATTERN_EDIT);
        int noteStart=mr;
        short sv=pat->newData[mr][DIV_PAT_NOTE];
        if (sv==-1||sv==DIV_NOTE_OFF) {
          for (int rr=mr-1;rr>=0;rr--) {
            short bv=pat->newData[rr][DIV_PAT_NOTE];
            if (bv==-1||bv==DIV_NOTE_OFF) continue;
            if (!prIsSpecial(bv)) { noteStart=rr; break; }
            break;
          }
        }
        for (int col=0;col<DIV_MAX_COLS;col++) pat->newData[noteStart][col]=-1;
        for (int rr=noteStart+1;rr<patLen;rr++) {
          short nv2=pat->newData[rr][DIV_PAT_NOTE];
          if (nv2==DIV_NOTE_OFF) { pat->newData[rr][DIV_PAT_NOTE]=-1; break; }
          if (nv2!=-1) break;
        }
        makeUndo(GUI_UNDO_PATTERN_EDIT); MARK_MODIFIED;
      }
      if (hasSel&&ImGui::MenuItem("Erase Selection")) {
        prepareUndo(GUI_UNDO_PATTERN_EDIT);
        for (int r=selR0;r<=selR1;r++)
          for (int n=selN0;n<=selN1;n++)
            if (pat->newData[r][DIV_PAT_NOTE]==n) pat->newData[r][DIV_PAT_NOTE]=-1;
        makeUndo(GUI_UNDO_PATTERN_EDIT); MARK_MODIFIED;
        prSelRow0=prSelRow1=prSelN0=prSelN1=-1;
      }
      if (hasSel&&ImGui::MenuItem("Erase Row Data")) {
        prepareUndo(GUI_UNDO_PATTERN_EDIT);
        for (int r=selR0;r<=selR1;r++) {
          short nv=pat->newData[r][DIV_PAT_NOTE];
          if (nv>=selN0&&nv<=selN1&&!prIsSpecial(nv))
            for (int col=0;col<DIV_MAX_COLS;col++) pat->newData[r][col]=-1;
        }
        makeUndo(GUI_UNDO_PATTERN_EDIT); MARK_MODIFIED;
        prSelRow0=prSelRow1=prSelN0=prSelN1=-1;
      }
      ImGui::EndPopup();
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
      if (ImGui::IsKeyPressed(ImGuiKey_A,false)&&ImGui::GetIO().KeyCtrl)
        { prSelRow0=0; prSelRow1=patLen-1; prSelN0=0; prSelN1=NOTES-1; }

      if (ImGui::IsKeyPressed(ImGuiKey_C,false)&&ImGui::GetIO().KeyCtrl&&hasSel) {
        prClipboard.clear();
        prClipRows=selR1-selR0+1;
        for (int r=selR0;r<=selR1;r++) {
          short nv=pat->newData[r][DIV_PAT_NOTE];
          if (nv>=0&&nv<NOTES&&nv>=selN0&&nv<=selN1&&!prIsSpecial(nv)) {
            PrClipEntry ce;
            ce.rowOff=r-selR0; ce.note=nv;
            ce.ins=pat->newData[r][DIV_PAT_INS];
            ce.vol=pat->newData[r][DIV_PAT_VOL];
            prClipboard.push_back(ce);
          }
        }
      }

      if (ImGui::IsKeyPressed(ImGuiKey_V,false)&&ImGui::GetIO().KeyCtrl&&!prClipboard.empty()) {
        prepareUndo(GUI_UNDO_PATTERN_EDIT);
        int baseRow=cursor.y;
        for (auto& ce:prClipboard) {
          int r=baseRow+ce.rowOff;
          if (r>=0&&r<patLen) {
            pat->newData[r][DIV_PAT_NOTE]=ce.note;
            if (ce.ins>=0) pat->newData[r][DIV_PAT_INS]=ce.ins;
            else if (prevIns>=0) pat->newData[r][DIV_PAT_INS]=(short)prevIns;
            if (ce.vol>=0) pat->newData[r][DIV_PAT_VOL]=ce.vol;
            else pat->newData[r][DIV_PAT_VOL]=(short)volMax;
          }
        }
        makeUndo(GUI_UNDO_PATTERN_EDIT); MARK_MODIFIED;
      }

      if (hasSel) {
        int tDir=0;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow,false)&&ImGui::GetIO().KeyShift)
          tDir=ImGui::GetIO().KeyCtrl?12:1;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow,false)&&ImGui::GetIO().KeyShift)
          tDir=ImGui::GetIO().KeyCtrl?-12:-1;
        if (tDir!=0) {
          int ns0=selN0+tDir, ns1=selN1+tDir;
          if (ns0>=0&&ns1<NOTES) {
            prepareUndo(GUI_UNDO_PATTERN_EDIT);
            for (int r=selR0;r<=selR1;r++) {
              short nv=pat->newData[r][DIV_PAT_NOTE];
              if (nv>=selN0&&nv<=selN1&&!prIsSpecial(nv)) {
                int nn=nv+tDir;
                if (nn>=0&&nn<NOTES) pat->newData[r][DIV_PAT_NOTE]=(short)nn;
              }
            }
            prSelN0=ns0; prSelN1=ns1;
            makeUndo(GUI_UNDO_PATTERN_EDIT); MARK_MODIFIED;
          }
        }
      }

      if (ImGui::IsKeyPressed(ImGuiKey_Delete,false)&&hasSel) {
        prepareUndo(GUI_UNDO_PATTERN_EDIT);
        for (int r=selR0;r<=selR1;r++)
          for (int n=selN0;n<=selN1;n++)
            if (pat->newData[r][DIV_PAT_NOTE]==n) pat->newData[r][DIV_PAT_NOTE]=-1;
        makeUndo(GUI_UNDO_PATTERN_EDIT); MARK_MODIFIED;
        prSelRow0=prSelRow1=prSelN0=prSelN1=-1;
      }
      if (ImGui::IsKeyPressed(ImGuiKey_Escape,false)) prSelRow0=prSelRow1=prSelN0=prSelN1=-1;
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();

  {
    ImGui::InvisibleButton("##prSplit",ImVec2(noteAreaW,splitterH));
    bool splHov=ImGui::IsItemHovered();
    bool splAct=ImGui::IsItemActive();
    if (splHov||splAct) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (splAct) {
      prEffectLaneH-=ImGui::GetIO().MouseDelta.y/(float)dpiScale;
      prEffectLaneH=ImClamp(prEffectLaneH,40.0f,500.0f);
    }
    ImDrawList* sdl=ImGui::GetWindowDrawList();
    ImVec2 smin=ImGui::GetItemRectMin(), smax=ImGui::GetItemRectMax();
    float smid=smin.y+(smax.y-smin.y)*0.5f;
    sdl->AddRectFilled(smin,smax,prColorMulAlpha(uiColors[GUI_COLOR_PIANO_ROLL_BG],splAct?2.0f:1.6f));
    sdl->AddLine(ImVec2(smin.x+4,smid),ImVec2(smax.x-4,smid),cGridHi1,1.0f);
    for (int i=0;i<3;i++) {
      float dx=smax.x*0.5f-8+(float)i*8;
      sdl->AddLine(ImVec2(dx,smid-2),ImVec2(dx,smid+2),cGridHi2,1.5f);
    }
  }

  char dynNames[9][64];
  snprintf(dynNames[0],64,"Volume (0-%d)",volMax);
  for (int ei=0;ei<ImMin(effectCols,4);ei++) {
    int counts[256]={};
    for (int r=0;r<patLen;r++) {
      short fx=pat->newData[r][DIV_PAT_FX(ei)];
      if (fx>=0&&fx<=0xff) counts[(unsigned char)fx]++;
    }
    int maxFx=0,maxCnt=0;
    for (int i=1;i<256;i++) if (counts[i]>maxCnt) { maxCnt=counts[i]; maxFx=i; }
    if (maxCnt>0) {
      const char* d=e->getEffectDesc((unsigned char)maxFx,prChan,false);
      if (d&&strlen(d)>6) {
        char code[5]; strncpy(code,d,4); code[4]=0;
        const char* nm=d+6;
        char sn[20]=""; int ni=0;
        while(nm[ni]&&nm[ni]!='('&&ni<18) { sn[ni]=nm[ni]; ni++; }
        while(ni>0&&sn[ni-1]==' ') ni--;
        sn[ni]=0;
        snprintf(dynNames[1+ei*2],64,"FX%d: %s %s",ei+1,code,sn);
      } else {
        snprintf(dynNames[1+ei*2],64,"FX%d",ei+1);
      }
    } else {
      snprintf(dynNames[1+ei*2],64,"FX%d",ei+1);
    }
    snprintf(dynNames[1+ei*2+1],64,"FX%d Val",ei+1);
  }

  char shortNames[9][16];
  snprintf(shortNames[0],16,"Vol");
  for (int ei=0;ei<ImMin(effectCols,4);ei++) {
    snprintf(shortNames[1+ei*2],16,"FX%d",ei+1);
    snprintf(shortNames[1+ei*2+1],16,"FX%d Val",ei+1);
  }

  int maxLane=ImMin(1+effectCols*2,9);
  if (prEffectLane>=maxLane) prEffectLane=0;
  ImGui::Text("FX:"); ImGui::SameLine();
  ImGui::SetNextItemWidth(ImMax(300.0f*(float)dpiScale, ImGui::GetContentRegionAvail().x*0.35f));
  if (ImGui::BeginCombo("##prLane",shortNames[prEffectLane])) {
    for (int i=0;i<maxLane;i++) {
      bool s=(i==prEffectLane);
      if (ImGui::Selectable(dynNames[i],s)) prEffectLane=i;
      if (s) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered()) {
    int dw=-(int)ImGui::GetIO().MouseWheel;
    if (dw) prEffectLane=ImClamp(prEffectLane+dw,0,maxLane-1);
  }

  bool isEffNum=(prEffectLane>0&&(prEffectLane&1)==1);
  int laneEffIdx=0, laneCol=DIV_PAT_VOL, laneMax=volMax;
  if (prEffectLane==0) {
    laneCol=DIV_PAT_VOL; laneMax=volMax;
  } else if (isEffNum) {
    laneEffIdx=(prEffectLane-1)/2;
    laneCol=DIV_PAT_FX(laneEffIdx); laneMax=0xff;
  } else {
    laneEffIdx=(prEffectLane-2)/2;
    laneCol=DIV_PAT_FXVAL(laneEffIdx); laneMax=0xff;
  }
  ImU32 laneBarColor=(prEffectLane==0)
    ?ImGui::ColorConvertFloat4ToU32(uiColors[GUI_COLOR_PIANO_ROLL_FX_VOL])
    :(isEffNum
      ?ImGui::ColorConvertFloat4ToU32(uiColors[GUI_COLOR_PIANO_ROLL_FX_NUM])
      :ImGui::ColorConvertFloat4ToU32(uiColors[GUI_COLOR_PIANO_ROLL_FX_VAL]));

  ImGui::SetNextWindowContentSize(ImVec2(pianoW+totalW,effectLaneH));
  if (ImGui::BeginChild("##prFX",ImVec2(noteAreaW,effectLaneH),false,
      ImGuiWindowFlags_HorizontalScrollbar|ImGuiWindowFlags_NoScrollWithMouse)) {
    if (settings.prFontScale!=1.0f) ImGui::SetWindowFontScale(settings.prFontScale);
    if (prPanDX!=0) {
      ImGui::SetScrollX(ImGui::GetScrollX()+prPanDX);
      prPanDX=0;
    }
    prSyncScrollX=ImGui::GetScrollX();
    float fxSX=prSyncScrollX;
    ImDrawList* dl=ImGui::GetWindowDrawList();
    ImVec2 wp2=ImGui::GetWindowPos();
    float ox2=wp2.x-fxSX;
    float vx0=wp2.x,vx1=wp2.x+ImGui::GetWindowWidth();
    float sbSz=ImGui::GetStyle().ScrollbarSize;
    float lBot=wp2.y+ImGui::GetWindowHeight()-sbSz-2;
    float lTop=wp2.y+2;
    float lH=lBot-lTop;

    dl->AddRectFilled(wp2,ImVec2(wp2.x+pianoW+totalW,wp2.y+effectLaneH),
      prColorMulAlpha(uiColors[GUI_COLOR_PIANO_ROLL_BG],0.85f));

    for (int r=0;r<=patLen;r++) {
      float cx=ox2+pianoW+r*rowW;
      if (cx<vx0-rowW||cx>vx1+rowW) continue;
      dl->AddLine(ImVec2(cx,wp2.y),ImVec2(cx,wp2.y+effectLaneH),
        (r%hiB==0)?cGridHi2:(r%hiA==0)?cGridHi1:cGrid);
    }
    for (int q=1;q<4;q++) {
      float qy=lBot-lH*(q/4.0f);
      dl->AddLine(ImVec2(ox2+pianoW,qy),ImVec2(ox2+pianoW+totalW,qy),cGrid);
    }

    ImVec2 fSz=ImGui::CalcTextSize("FF");
    for (int r=0;r<patLen;r++) {
      short val=pat->newData[r][laneCol];
      if (val<0) continue;
      short cv=(short)ImClamp((int)val,0,laneMax);
      float bx0=ox2+pianoW+r*rowW+1;
      float bx1=ox2+pianoW+(r+1)*rowW-1;
      if (bx1<vx0||bx0>vx1) continue;
      float norm=(laneMax>0)?(float)cv/(float)laneMax:0.0f;
      float bh=ImMax(norm*lH,2.0f);
      float by0=lBot-bh;
      dl->AddRectFilled(ImVec2(bx0,by0),ImVec2(bx1,lBot),laneBarColor,1.5f);
      dl->AddRect(ImVec2(bx0,by0),ImVec2(bx1,lBot),IM_COL32(255,255,255,25),1.5f);
      float bw=bx1-bx0;
      if (bw>=fSz.x+2&&bh>=fSz.y) {
        char vs[8];
        if (prEffectLane==0) snprintf(vs,8,"%d",(int)cv);
        else snprintf(vs,8,"%02X",(unsigned char)cv);
        dl->AddText(ImVec2(bx0+1,by0+1),IM_COL32(255,255,255,200),vs);
      }
      if (isEffNum&&bw>=fSz.x+2) {
        const char* desc=e->getEffectDesc((unsigned char)cv,prChan,false);
        if (desc) {
          char sd[10]; snprintf(sd,sizeof(sd),"%.8s",desc);
          dl->AddText(ImVec2(bx0+1,lBot-fSz.y-1),IM_COL32(220,210,100,180),sd);
        }
      }
    }

    if (e->isPlaying()&&playOrder==ord) {
      float phx=ox2+pianoW+oldRow*rowW;
      dl->AddLine(ImVec2(phx,wp2.y),ImVec2(phx,wp2.y+effectLaneH),cHead,2.0f);
    }

    dl->AddRectFilled(wp2,ImVec2(wp2.x+pianoW,wp2.y+effectLaneH),
      prColorMulAlpha(uiColors[GUI_COLOR_PIANO_ROLL_BG],1.25f));
    char topLbl[16],btmLbl[8];
    if (prEffectLane==0) { snprintf(topLbl,16,"%d",laneMax); snprintf(btmLbl,8,"0"); }
    else { snprintf(topLbl,16,"FF"); snprintf(btmLbl,8,"00"); }
    dl->PushClipRect(wp2,ImVec2(wp2.x+pianoW,wp2.y+effectLaneH),true);
    dl->AddText(ImVec2(wp2.x+3,lTop),IM_COL32(160,160,160,220),topLbl);
    dl->AddText(ImVec2(wp2.x+3,lBot-fSz.y),IM_COL32(160,160,160,220),btmLbl);
    {
      const char* nm=shortNames[prEffectLane];
      ImVec2 nsz=ImGui::CalcTextSize(nm);
      float midY=(lTop+fSz.y+4+lBot-fSz.y-4)*0.5f-nsz.y*0.5f;
      dl->AddText(ImVec2(wp2.x+3,midY),IM_COL32(230,230,230,255),nm);
    }
    dl->PopClipRect();
    dl->AddLine(ImVec2(wp2.x+pianoW,wp2.y),ImVec2(wp2.x+pianoW,wp2.y+effectLaneH),cKeyBrd,1.5f);

    if (prFxSlopeActive&&prFxSlopeR0>=0&&prFxSlopeR1>=0) {
      int sr0=ImMin(prFxSlopeR0,prFxSlopeR1), sr1=ImMax(prFxSlopeR0,prFxSlopeR1);
      int sv0=(prFxSlopeR0<=prFxSlopeR1)?prFxSlopeV0:prFxSlopeV1;
      int sv1=(prFxSlopeR0<=prFxSlopeR1)?prFxSlopeV1:prFxSlopeV0;
      int span=ImMax(sr1-sr0,1);
      for (int rr=sr0;rr<sr1;rr++) {
        float t0=(float)(rr-sr0)/span, t1=(float)(rr+1-sr0)/span;
        float c0=prFxCurve(t0,prFxSlopeTension), c1=prFxCurve(t1,prFxSlopeTension);
        float pv0=sv0+(sv1-sv0)*c0, pv1=sv0+(sv1-sv0)*c1;
        float x0=ox2+pianoW+rr*rowW+rowW*0.5f;
        float x1=ox2+pianoW+(rr+1)*rowW+rowW*0.5f;
        float y0=lBot-(pv0/ImMax((float)laneMax,1.0f))*lH;
        float y1=lBot-(pv1/ImMax((float)laneMax,1.0f))*lH;
        if (x1>=vx0&&x0<=vx1)
          dl->AddLine(ImVec2(x0,y0),ImVec2(x1,y1),IM_COL32(255,180,60,230),2.0f);
      }
      char tlbl[20]; snprintf(tlbl,sizeof(tlbl),"T:%.1f",prFxSlopeTension);
      dl->AddText(ImVec2(wp2.x+pianoW+4,lTop+2),IM_COL32(255,220,100,230),tlbl);
    }

    ImGui::SetCursorPos(ImVec2(0,0));
    ImGui::InvisibleButton("##prFXClick",ImVec2(pianoW+totalW,effectLaneH));
    bool fxHov=ImGui::IsItemHovered();

    ImVec2 fxMp=ImGui::GetMousePos();
    float fxLx=fxMp.x-wp2.x+fxSX;
    float fxLy=fxMp.y-wp2.y;
    int fxRow=ImClamp((int)((fxLx-pianoW)/rowW),0,patLen-1);
    float fxNorm=ImClamp(1.0f-(fxLy-lTop+wp2.y)/lH,0.0f,1.0f);
    int fxPv=(int)(fxNorm*(float)laneMax+0.5f);

    if (fxHov||prFxSlopeActive) {
      if (!prFxSlopeActive) {
        float pvy=lBot-((float)fxPv/ImMax(laneMax,1))*lH;
        dl->AddLine(ImVec2(ox2+pianoW,pvy),ImVec2(ox2+pianoW+totalW,pvy),
          IM_COL32(255,255,100,110),1.0f);
        char tip[160];
        if (prEffectLane==0) snprintf(tip,sizeof(tip),"Vol: %d / %d",fxPv,laneMax);
        else if (isEffNum) {
          const char* d=e->getEffectDesc((unsigned char)fxPv,prChan,false);
          snprintf(tip,sizeof(tip),"FX: %02X  %s",(unsigned char)fxPv,d?d:"");
        } else {
          short pf=pat->newData[fxRow][DIV_PAT_FX(laneEffIdx)];
          const char* d=(pf>=0)?e->getEffectDesc((unsigned char)pf,prChan,false):nullptr;
          snprintf(tip,sizeof(tip),"Val: %02X  (FX %02X: %s)",
            (unsigned char)fxPv,(pf>=0?(unsigned char)pf:0),d?d:"");
        }
        ImGui::SetTooltip("%s (RMB drag=slope, wheel=tension)",tip);
      }

      if (!prFxSlopeActive) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
          prepareUndo(GUI_UNDO_PATTERN_EDIT); prFxUndoOpen=true;
          prFxLastDragRow=fxRow;
        }
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)&&prFxUndoOpen) {
          int r0=(prFxLastDragRow>=0)?ImMin(prFxLastDragRow,fxRow):fxRow;
          int r1=(prFxLastDragRow>=0)?ImMax(prFxLastDragRow,fxRow):fxRow;
          int vStart=(prFxLastDragRow>=0&&prFxLastDragRow!=fxRow)
            ?((int)(pat->newData[r0][laneCol]))
            :fxPv;
          for (int rr=r0;rr<=r1;rr++) {
            float t=(r1>r0)?(float)(rr-r0)/(r1-r0):0.0f;
            short vv=(short)ImClamp((int)(vStart+(fxPv-vStart)*t+0.5f),0,laneMax);
            pat->newData[rr][laneCol]=vv;
            if (!isEffNum&&prEffectLane>0&&pat->newData[rr][DIV_PAT_FX(laneEffIdx)]==-1)
              pat->newData[rr][DIV_PAT_FX(laneEffIdx)]=0;
          }
          prFxLastDragRow=fxRow;
          MARK_MODIFIED;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)&&prFxUndoOpen) {
          makeUndo(GUI_UNDO_PATTERN_EDIT); prFxUndoOpen=false; prFxLastDragRow=-1;
        }
      }

      if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)&&!prFxSlopeActive) {
        prepareUndo(GUI_UNDO_PATTERN_EDIT); prFxUndoOpen=true;
        prFxSlopeActive=true;
        prFxSlopeR0=prFxSlopeR1=fxRow;
        prFxSlopeV0=prFxSlopeV1=fxPv;
        prFxSlopeTension=0.0f;
      }
      if (prFxSlopeActive) {
        prFxSlopeR1=fxRow; prFxSlopeV1=fxPv;
        float wheel=ImGui::GetIO().MouseWheel;
        if (wheel!=0) prFxSlopeTension=ImClamp(prFxSlopeTension+wheel*0.2f,-3.0f,3.0f);
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
          int sr0=ImMin(prFxSlopeR0,prFxSlopeR1), sr1=ImMax(prFxSlopeR0,prFxSlopeR1);
          int sv0=(prFxSlopeR0<=prFxSlopeR1)?prFxSlopeV0:prFxSlopeV1;
          int sv1=(prFxSlopeR0<=prFxSlopeR1)?prFxSlopeV1:prFxSlopeV0;
          int span=ImMax(sr1-sr0,1);
          for (int rr=sr0;rr<=sr1;rr++) {
            float t=(sr0==sr1)?0.5f:(float)(rr-sr0)/span;
            float cv=prFxCurve(t,prFxSlopeTension);
            short vv=(short)ImClamp((int)(sv0+(sv1-sv0)*cv+0.5f),0,laneMax);
            pat->newData[rr][laneCol]=vv;
            if (!isEffNum&&prEffectLane>0&&pat->newData[rr][DIV_PAT_FX(laneEffIdx)]==-1)
              pat->newData[rr][DIV_PAT_FX(laneEffIdx)]=0;
          }
          MARK_MODIFIED;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
          if (prFxUndoOpen) { makeUndo(GUI_UNDO_PATTERN_EDIT); prFxUndoOpen=false; }
          prFxSlopeActive=false; prFxLastDragRow=-1;
        }
      }
    } else {
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)&&prFxUndoOpen) {
        makeUndo(GUI_UNDO_PATTERN_EDIT); prFxUndoOpen=false; prFxLastDragRow=-1;
      }
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)&&prFxSlopeActive) {
        if (prFxUndoOpen) { makeUndo(GUI_UNDO_PATTERN_EDIT); prFxUndoOpen=false; }
        prFxSlopeActive=false;
      }
    }
  }
  ImGui::EndChild();

  ImGui::End();
}
