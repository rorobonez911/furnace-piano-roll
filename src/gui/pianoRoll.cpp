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
static int   prChanEnd=-1;
static bool  prPolyEnabled=false;
static int   prPaintHeld=-1;
static int   prPaintChan=-1;
static int   prPaintLen=1;
static int   prPaintStartRow=-1;
static int   prMode=0; // 0=Draw, 1=Select, 2=Paint
static bool  prDragMaybe=false;
static bool  prDragging=false;
static int   prDragStartR=-1, prDragStartN=-1;
static int   prDragClickN=-1;
static int   prDragDeltaR=0,  prDragDeltaN=0;
static ImVec2 prDragMouseStart={0,0};
struct PrDragNote { int row, endRow, chan; short note, ins, vol; };
static std::vector<PrDragNote> prDragBuf;
static int prPreviewTimer=0;
static int prPreviewChan=-1;
static int prFollowPrevPlayOrd=-1;
static bool  prKbdShowConfig=false;
static const char* const PR_NOTE_LBL[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

struct PrClipEntry { int rowOff; short note; short ins; short vol; };
static std::vector<PrClipEntry> prClipboard;
static int prClipRows=0;

struct PrPolyGroup { int from, to; };
static std::vector<PrPolyGroup> prPolyGroups;
static int prNewGrpFrom=0;
static int prNewGrpTo=0;

static void prPolySerialize(DivSong* s) {
  String& n=s->notes;
  size_t p=n.find("\n<!-- jfpoly:");
  if (p!=String::npos) n=n.substr(0,p);
  if (prPolyGroups.empty()) return;
  n+="\n<!-- jfpoly:";
  for (int i=0;i<(int)prPolyGroups.size();i++) {
    if (i) n+=",";
    n+=fmt::sprintf("%d-%d",prPolyGroups[i].from,prPolyGroups[i].to);
  }
  n+=" -->";
}

static String prPolyMarkerCache;

static String prPolyExtractMarker(const String& notes) {
  size_t p=notes.find("\n<!-- jfpoly:");
  if (p==String::npos) return "";
  size_t e2=notes.find("-->",p);
  if (e2==String::npos) return "";
  return notes.substr(p,e2+3-p);
}

static void prPolyDeserialize(DivSong* s) {
  prPolyGroups.clear();
  const String& n=s->notes;
  size_t p=n.find("\n<!-- jfpoly:");
  if (p==String::npos) return;
  size_t e2=n.find("-->",p);
  if (e2==String::npos) return;
  String data=n.substr(p+13,e2-(p+13));
  data.erase(data.find_last_not_of(" \t")+1);
  const char* c=data.c_str();
  while (*c) {
    int a=-1,b=-1;
    if (sscanf(c,"%d-%d",&a,&b)==2&&a>=0&&b>=a) {
      prPolyGroups.push_back({a,b});
    }
    while (*c&&*c!=',') c++;
    if (*c==',') c++;
  }
}

static void prPolyCommit(DivSong* s) {
  prPolySerialize(s);
  prPolyMarkerCache=prPolyExtractMarker(s->notes);
}

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
  if (prChanEnd<prChan||prChanEnd>=totalChans) prChanEnd=prChan;
  if (prPolyEnabled&&prChanEnd<=prChan) prPolyEnabled=false;

  {
    String curMarker=prPolyExtractMarker(e->song.notes);
    if (curMarker!=prPolyMarkerCache) {
      prPolyMarkerCache=curMarker;
      prPolyDeserialize(&e->song);
      prPolyEnabled=false;
    }
  }

  {
    String chBtnLbl=fmt::sprintf("Ch %d: %s###prChBtn",prChan+1,e->getChannelName(prChan));
    if (ImGui::Button(chBtnLbl.c_str())) ImGui::OpenPopup("##prChPop");
    if (ImGui::IsItemHovered()) {
      int dw=-(int)ImGui::GetIO().MouseWheel;
      if (dw) { prChan=ImClamp(prChan+dw,0,totalChans-1); prChanEnd=prChan; prPolyEnabled=false; prSelRow0=prSelRow1=-1; }
      ImGui::SetTooltip("Active channel\nClick to pick  |  Scroll to change");
    }
    ImGui::SameLine();
  }

  {
    bool polyHasGrps=!prPolyGroups.empty();
    if (prPolyEnabled) ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0.12f,0.38f,0.68f,0.9f));
    else if (polyHasGrps) ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0.18f,0.25f,0.38f,0.8f));
    String polyLbl=prPolyEnabled
      ?fmt::sprintf("Poly: Ch%d-%d###prPolyBtn",prChan+1,prChanEnd+1)
      :"Poly###prPolyBtn";
    if (ImGui::Button(polyLbl.c_str())) ImGui::OpenPopup("##prPolyPop");
    if (prPolyEnabled||polyHasGrps) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
      if (prPolyEnabled) ImGui::SetTooltip("Polyphony ON: Ch%d-Ch%d\nClick to manage groups",prChan+1,prChanEnd+1);
      else if (polyHasGrps) ImGui::SetTooltip("%d poly group(s) defined\nClick to manage",(int)prPolyGroups.size());
      else ImGui::SetTooltip("Polyphonic multi-channel editing\nClick to set up groups");
    }
    ImGui::SameLine();
  }

  if (ImGui::BeginPopup("##prChPop")) {
    static const ImVec4 chTypeCol[6]={
      ImVec4(0.95f,0.75f,0.15f,1.0f),
      ImVec4(0.25f,0.65f,1.0f, 1.0f),
      ImVec4(0.90f,0.35f,0.25f,1.0f),
      ImVec4(0.25f,0.85f,0.45f,1.0f),
      ImVec4(0.70f,0.35f,0.90f,1.0f),
      ImVec4(0.75f,0.95f,0.25f,1.0f),
    };
    static const char* chTypeName[6]={"FM","Pulse","Noise","Wave","PCM","OP"};

    auto isBound=[&](int ch)->int {
      for (int gi=0;gi<(int)prPolyGroups.size();gi++)
        if (ch>=prPolyGroups[gi].from&&ch<=prPolyGroups[gi].to) return gi;
      return -1;
    };

    float btnSz=ImMax(26.0f*(float)dpiScale,ImGui::GetFrameHeight()*1.2f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(3.0f*(float)dpiScale,3.0f*(float)dpiScale));
    int prevChip=-1;
    int inRow=0;
    const int chPerRow=12;

    for (int ch=0;ch<totalChans;ch++) {
      int boundGi=isBound(ch);
      int chip=e->song.dispatchOfChan[ch];
      if (chip!=prevChip) {
        if (prevChip>=0) ImGui::NewLine();
        inRow=0;
        DivSystem sys=e->song.sysOfChan[ch];
        ImGui::TextColored(ImVec4(0.55f,0.78f,1.0f,0.9f),"%s",e->getSystemName(sys));
        prevChip=chip;
      } else if (inRow>0&&inRow%chPerRow==0) {
        ImGui::NewLine();
      } else {
        ImGui::SameLine(0,3.0f*(float)dpiScale);
      }

      int ct=ImClamp(e->getChannelType(ch),0,5);
      ImVec4 col=chTypeCol[ct];
      bool isSel=(ch==prChan&&!prPolyEnabled);
      bool inGrp=(boundGi>=0);

      float br=isSel?col.x*0.65f:(inGrp?col.x*0.2f:col.x*0.22f);
      float bg=isSel?col.y*0.65f:(inGrp?col.y*0.2f:col.y*0.22f);
      float bb=isSel?col.z*0.65f:(inGrp?col.z*0.2f:col.z*0.22f);
      float ba=inGrp?0.4f:0.85f;
      ImGui::PushStyleColor(ImGuiCol_Button,    ImVec4(br,bg,bb,ba));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(ImMin(br+0.2f,1.0f),ImMin(bg+0.2f,1.0f),ImMin(bb+0.2f,1.0f),1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(ImMin(br+0.35f,1.0f),ImMin(bg+0.35f,1.0f),ImMin(bb+0.35f,1.0f),1.0f));

      const char* sn=e->getChannelShortName(ch);
      bool clicked=ImGui::Button(fmt::sprintf("%s##cb%d",sn?sn:"?",ch).c_str(),ImVec2(btnSz,btnSz));
      ImGui::PopStyleColor(3);

      if (isSel) {
        ImVec2 rm=ImGui::GetItemRectMin(), rx=ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRect(rm,rx,IM_COL32(100,185,255,255),2.0f*(float)dpiScale,0,2.0f);
      }
      if (ImGui::IsItemHovered()) {
        if (inGrp) ImGui::SetTooltip("Ch %d: %s  [%s]\nIn poly group %d",ch+1,e->getChannelName(ch),chTypeName[ct],boundGi+1);
        else       ImGui::SetTooltip("Ch %d: %s  [%s]",ch+1,e->getChannelName(ch),chTypeName[ct]);
      }
      if (clicked) {
        prChan=ch; prChanEnd=ch; prPolyEnabled=false;
        prSelRow0=prSelRow1=-1;
        ImGui::CloseCurrentPopup();
      }
      inRow++;
    }
    ImGui::PopStyleVar();
    ImGui::NewLine();
    ImGui::Separator();
    ImGui::TextDisabled("  ");
    for (int i=0;i<6;i++) {
      ImGui::SameLine(0,5.0f*(float)dpiScale);
      ImGui::TextColored(chTypeCol[i],"\xe2\x97\x8f %s",chTypeName[i]);
    }
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopup("##prPolyPop")) {
    static const ImVec4 chTypeCol2[6]={
      ImVec4(0.95f,0.75f,0.15f,1.0f),
      ImVec4(0.25f,0.65f,1.0f, 1.0f),
      ImVec4(0.90f,0.35f,0.25f,1.0f),
      ImVec4(0.25f,0.85f,0.45f,1.0f),
      ImVec4(0.70f,0.35f,0.90f,1.0f),
      ImVec4(0.75f,0.95f,0.25f,1.0f),
    };
    static const char* chTypeName2[6]={"FM","Pulse","Noise","Wave","PCM","OP"};

    auto isBound2=[&](int ch)->int {
      for (int gi=0;gi<(int)prPolyGroups.size();gi++)
        if (ch>=prPolyGroups[gi].from&&ch<=prPolyGroups[gi].to) return gi;
      return -1;
    };

    float btnSz=ImMax(26.0f*(float)dpiScale,ImGui::GetFrameHeight()*1.2f);
    float popW=ImMax(320.0f*(float)dpiScale,ImGui::GetContentRegionAvail().x);
    ImGui::Dummy(ImVec2(popW,0));

    ImGui::TextColored(ImVec4(0.55f,0.78f,1.0f,1.0f),"Polyphonic Groups");
    ImGui::SameLine();
    ImGui::TextDisabled(" — notes spread across voices in a group");
    ImGui::Separator();
    ImGui::Spacing();

    if (prPolyGroups.empty()) {
      ImGui::TextDisabled("No groups. Add one below.");
      ImGui::Spacing();
    } else {
      int toRemove=-1;
      for (int gi=0;gi<(int)prPolyGroups.size();gi++) {
        PrPolyGroup& g=prPolyGroups[gi];
        bool isActive=(prChan==g.from&&prChanEnd==g.to&&prPolyEnabled);
        ImGui::PushID(gi);

        if (isActive) {
          ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0.1f,0.42f,0.75f,0.9f));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(0.15f,0.52f,0.88f,1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(0.2f,0.6f,1.0f,1.0f));
        } else {
          ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0.15f,0.2f,0.28f,0.8f));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(0.22f,0.32f,0.45f,1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(0.28f,0.42f,0.6f,1.0f));
        }
        bool activate=ImGui::Button(isActive?"\xe2\x96\xba ON##act":"\xe2\x96\xba##act",ImVec2(isActive?50.0f*(float)dpiScale:32.0f*(float)dpiScale,btnSz));
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(isActive?"Poly active on this group\nClick to deactivate":"Click to activate this group for polyphony");
        if (activate) {
          if (isActive) { prPolyEnabled=false; }
          else { prChan=g.from; prChanEnd=g.to; prPolyEnabled=true; prSelRow0=prSelRow1=-1; }
        }

        ImGui::SameLine(0,4.0f*(float)dpiScale);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(2.0f*(float)dpiScale,2.0f*(float)dpiScale));
        for (int ch=g.from;ch<=g.to;ch++) {
          int ct=ImClamp(e->getChannelType(ch),0,5);
          ImVec4 col=chTypeCol2[ct];
          ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(col.x*0.4f,col.y*0.4f,col.z*0.4f,0.9f));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(col.x*0.6f,col.y*0.6f,col.z*0.6f,1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(col.x*0.75f,col.y*0.75f,col.z*0.75f,1.0f));
          const char* sn=e->getChannelShortName(ch);
          ImGui::Button(fmt::sprintf("%s##pgch%d",sn?sn:"?",ch).c_str(),ImVec2(btnSz,btnSz));
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ch %d: %s  [%s]",ch+1,e->getChannelName(ch),chTypeName2[ct]);
          ImGui::PopStyleColor(3);
          ImGui::SameLine(0,2.0f*(float)dpiScale);
        }
        ImGui::PopStyleVar();
        ImGui::TextDisabled("(%d voices)",g.to-g.from+1);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0.4f,0.1f,0.1f,0.7f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(0.7f,0.15f,0.15f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(0.85f,0.25f,0.25f,1.0f));
        if (ImGui::SmallButton("Remove##rm")) toRemove=gi;
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this poly group");
        ImGui::PopID();
        ImGui::Spacing();
      }
      if (toRemove>=0) {
        if (prChan==prPolyGroups[toRemove].from&&prChanEnd==prPolyGroups[toRemove].to)
          { prChanEnd=prChan; prPolyEnabled=false; }
        prPolyGroups.erase(prPolyGroups.begin()+toRemove);
        prPolyCommit(&e->song);
      }
    }

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.55f,0.78f,1.0f,0.85f),"New Group");
    ImGui::Spacing();

    if (prNewGrpFrom<0||prNewGrpFrom>=totalChans) prNewGrpFrom=0;
    if (prNewGrpTo<prNewGrpFrom||prNewGrpTo>=totalChans) prNewGrpTo=prNewGrpFrom;

    ImGui::Text("From:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f*(float)dpiScale);
    if (ImGui::BeginCombo("##ngFrom",fmt::sprintf("Ch %d: %s",prNewGrpFrom+1,e->getChannelName(prNewGrpFrom)).c_str())) {
      for (int ch=0;ch<totalChans;ch++) {
        if (ImGui::Selectable(fmt::sprintf("Ch %d: %s",ch+1,e->getChannelName(ch)).c_str(),ch==prNewGrpFrom)) {
          prNewGrpFrom=ch; if (prNewGrpTo<prNewGrpFrom) prNewGrpTo=prNewGrpFrom;
        }
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Text("To:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f*(float)dpiScale);
    if (ImGui::BeginCombo("##ngTo",fmt::sprintf("Ch %d: %s",prNewGrpTo+1,e->getChannelName(prNewGrpTo)).c_str())) {
      for (int ch=prNewGrpFrom;ch<totalChans;ch++) {
        if (ImGui::Selectable(fmt::sprintf("Ch %d: %s",ch+1,e->getChannelName(ch)).c_str(),ch==prNewGrpTo))
          prNewGrpTo=ch;
      }
      ImGui::EndCombo();
    }

    bool canAdd=(prNewGrpTo>prNewGrpFrom);
    String addErr;
    if (canAdd) {
      int refChip=e->song.dispatchOfChan[prNewGrpFrom];
      int refType=e->getChannelType(prNewGrpFrom);
      for (int ch=prNewGrpFrom+1;ch<=prNewGrpTo&&canAdd;ch++) {
        if (e->song.dispatchOfChan[ch]!=refChip) { canAdd=false; addErr="Channels span multiple chips"; }
        else if (e->getChannelType(ch)!=refType) { canAdd=false; addErr="Mixed channel types (e.g. FM + noise)"; }
        else if (isBound2(ch)>=0)                { canAdd=false; addErr=fmt::sprintf("Ch %d already in a group",ch+1); }
      }
      if (isBound2(prNewGrpFrom)>=0) { canAdd=false; addErr=fmt::sprintf("Ch %d already in a group",prNewGrpFrom+1); }
    } else if (prNewGrpTo==prNewGrpFrom) {
      addErr="Need at least 2 channels";
    }

    ImGui::Spacing();
    if (!canAdd) {
      ImGui::TextColored(ImVec4(0.9f,0.5f,0.3f,0.9f),"%s",addErr.empty()?"Select a range above":addErr.c_str());
    } else {
      int voices=prNewGrpTo-prNewGrpFrom+1;
      int ct=ImClamp(e->getChannelType(prNewGrpFrom),0,5);
      ImGui::TextColored(ImVec4(0.5f,0.9f,0.5f,0.9f),"%d %s voices: Ch%d - Ch%d",voices,chTypeName2[ct],prNewGrpFrom+1,prNewGrpTo+1);
    }
    ImGui::Spacing();

    if (!canAdd) ImGui::BeginDisabled();
    if (ImGui::Button("Add Group##prAddGrp")) {
      PrPolyGroup ng={prNewGrpFrom,prNewGrpTo};
      prPolyGroups.push_back(ng);
      prChan=ng.from; prChanEnd=ng.to; prPolyEnabled=true;
      prSelRow0=prSelRow1=-1;
      prPolyCommit(&e->song);
    }
    if (ImGui::IsItemHovered()&&canAdd)
      ImGui::SetTooltip("Add Ch%d-Ch%d as a poly group and activate it",prNewGrpFrom+1,prNewGrpTo+1);
    if (!canAdd) ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextDisabled("Groups are saved automatically with the module.");

    ImGui::EndPopup();
  }
  ImGui::SetNextItemWidth(90*dpiScale);
  ImGui::SliderFloat("Zoom##prZ",&prZoom,0.25f,4.0f,"%.2fx");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Horizontal zoom (scroll in grid to zoom)\nRight-click to reset");
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) prZoom=1.0f;
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70*dpiScale);
  ImGui::SliderFloat("H##prH",&prNoteH,4.0f,20.0f,"%.0f");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Note row height in pixels\nRight-click to reset");
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) prNoteH=8.0f;
  ImGui::SameLine();
  ImGui::Checkbox("All##prAC",&prShowAllChans);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show all other channels as faint ghost notes");
  ImGui::SameLine();
  ImGui::Checkbox("Follow##prFollow",&prFollow);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Auto-scroll to follow the playhead during playback");
  ImGui::SameLine();
  ImGui::Separator(); ImGui::SameLine();
  {
    static const char* modeLabels[]={"D##prMod","S##prMod","P##prMod"};
    static const char* modeTips[]={"Draw (D): click to place/pick notes","Select (S): click/drag to select; drag selection to move","Paint (P): drag to paint continuously"};
    for (int m=0;m<3;m++) {
      if (m>0) ImGui::SameLine(0,2.0f*(float)dpiScale);
      bool active=(prMode==m);
      if (active) ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0.18f,0.45f,0.82f,1.0f));
      if (ImGui::SmallButton(modeLabels[m])) prMode=m;
      if (active) ImGui::PopStyleColor();
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",modeTips[m]);
    }
  }
  ImGui::SameLine(0,6.0f*(float)dpiScale);
  ImGui::TextDisabled("N:%d",prPaintLen);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Current note paint length: %d row(s)\nClick an existing note to pick up its length",prPaintLen);
  ImGui::SameLine(0,6.0f*(float)dpiScale);
  if (ImGui::SmallButton("\xe2\x9a\x99##prKbdCfg")) prKbdShowConfig=!prKbdShowConfig;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Piano roll key bindings");
  if (prKbdShowConfig) {
    ImGui::SetNextWindowSize(ImVec2(260.0f*(float)dpiScale,0),ImGuiCond_Appearing);
    if (ImGui::Begin("Piano Roll Keys##prKbdWin",&prKbdShowConfig,ImGuiWindowFlags_NoScrollbar)) {
      ImGui::TextDisabled("Mode keys");
      ImGui::BulletText("D — Draw mode");
      ImGui::BulletText("S — Select mode");
      ImGui::BulletText("P — Paint mode");
      ImGui::Separator();
      ImGui::TextDisabled("Selection");
      ImGui::BulletText("Shift+drag — box select (any mode)");
      ImGui::BulletText("Shift+\xe2\x86\x91\xe2\x86\x93 — transpose \xc2\xb11 semitone");
      ImGui::BulletText("Ctrl+Shift+\xe2\x86\x91\xe2\x86\x93 — transpose \xc2\xb1 octave");
      ImGui::BulletText("Shift+\xe2\x86\x90\xe2\x86\x92 — move selection \xc2\xb11 row");
      ImGui::BulletText("Ctrl+A — select all");
      ImGui::BulletText("Ctrl+C / Ctrl+V — copy / paste");
      ImGui::BulletText("Delete — erase selection");
      ImGui::Separator();
      ImGui::TextDisabled("Editing");
      ImGui::BulletText("Right-click note — erase (Draw/Paint)");
      ImGui::BulletText("Right-click drag — erase strip");
      ImGui::BulletText("Drag note edge — resize");
    }
    ImGui::End();
  }
  ImGui::SameLine();
  ImGui::Separator();
  ImGui::SameLine();
  {
    static const char* scaleTypeNames2[]={"Scale: Off","Major","Minor"};
    static const char* rootNames2[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    ImGui::SetNextItemWidth(90*dpiScale);
    if (ImGui::BeginCombo("##scType",scaleTypeNames2[prScaleType])) {
      for (int i=0;i<3;i++) {
        if (ImGui::Selectable(scaleTypeNames2[i],prScaleType==i)) prScaleType=i;
        if (prScaleType==i) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap placed notes to a musical scale\nOff = free placement");
    if (prScaleType>0) {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(52*dpiScale);
      if (ImGui::BeginCombo("##scRoot",rootNames2[prScaleRoot])) {
        for (int i=0;i<12;i++) {
          bool sel=(prScaleRoot==i);
          if (ImGui::Selectable(rootNames2[i],sel)) prScaleRoot=i;
          if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Root note of the selected scale");
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("View\xef\x83\x83##prView")) ImGui::OpenPopup("prViewMenu");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Display and quantize options");
  if (ImGui::BeginPopup("prViewMenu")) {
    ImGui::Checkbox("Pitch Slides##pvPS",&prShowPitchSlide);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Draw lines showing pitch slide effects (01/02/03)");
    ImGui::Checkbox("Volume Bars##pvVB",&prShowVolBars);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show volume as a bar at the bottom of each note");
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

  if (e->isPlaying()&&playOrder>=0&&playOrder<e->curSubSong->ordersLen)
    curOrder=playOrder;

  int ord=curOrder;
  if (ord<0) ord=0;
  if (ord>=e->curSubSong->ordersLen) ord=e->curSubSong->ordersLen-1;
  if (ord<0) { ImGui::Text("No orders."); ImGui::End(); return; }

  if (prPreviewTimer>0) {
    prPreviewTimer--;
    if (prPreviewTimer==0&&prPreviewChan>=0) { e->noteOff(prPreviewChan); prPreviewChan=-1; }
  }

  int patIdx=e->curSubSong->orders.ord[prChan][ord];
  DivPattern* pat=e->curPat[prChan].getPattern(patIdx,true);
  if (!pat) { ImGui::Text("Pattern unavailable."); ImGui::End(); return; }

  int patLen=e->curSubSong->patLen;
  if (patLen<=0) { ImGui::Text("Pattern is empty."); ImGui::End(); return; }
  int effectCols=ImMax((int)e->curPat[prChan].effectCols,1);
  int volMax=e->getMaxVolumeChan(prChan);
  if (volMax<=0) volMax=0xff;

  const float totalW=(float)patLen*rowW;

  int ordersLen=e->curSubSong->ordersLen;
  const float allW=totalW*(float)ordersLen;

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

  if (prFollow&&e->isPlaying()) {
    float absX=pianoW+(float)ord*totalW+oldRow*rowW;
    prSyncScrollX=ImMax(absX-noteAreaW*0.35f,0.0f);
  }

  ImGui::SetNextWindowContentSize(ImVec2(pianoW+allW,timelineH));
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
      for (int vi=0;vi<ordersLen;vi++) {
        float oBase=ox+pianoW+vi*totalW;
        if (oBase+totalW<vx0||oBase>vx1) continue;
        bool isCur=(vi==ord);
        for (int r=0;r<patLen;r++) {
          float rx=oBase+r*rowW;
          if (rx+rowW<vx0||rx>vx1) continue;
          ImU32 gc=(r%hiB==0)?cGridHi2:(r%hiA==0)?cGridHi1:cGrid;
          if (!isCur) { ImU32 a=(gc>>24)&0xFF; gc=(gc&0x00FFFFFF)|((ImU32)(a*0.5f)<<24); }
          dl->AddLine(ImVec2(rx,twp.y),ImVec2(rx,twp.y+timelineH),gc);
          if (isCur&&(r%lblStep==0)&&rx>=twp.x+pianoW-1)
            dl->AddText(ImVec2(rx+2,twp.y+1),IM_COL32(180,180,180,200),fmt::sprintf("%d",r).c_str());
        }
        dl->AddLine(ImVec2(oBase,twp.y),ImVec2(oBase,twp.y+timelineH),IM_COL32(255,255,255,isCur?80:40),2.0f);
        String olbl=fmt::sprintf("ORD %02X",vi);
        ImVec2 osz=ImGui::CalcTextSize(olbl.c_str());
        float olx=oBase+4, oly=twp.y+2;
        dl->AddRectFilled(ImVec2(olx-1,oly-1),ImVec2(olx+osz.x+2,oly+osz.y+1),
          prColorMulAlpha(uiColors[GUI_COLOR_PIANO_ROLL_BG],2.0f));
        dl->AddText(ImVec2(olx,oly),isCur?IM_COL32(200,200,200,230):IM_COL32(140,140,140,160),olbl.c_str());
      }
    }
    if (e->isPlaying()) {
      float phx=ox+pianoW+(float)playOrder*totalW+oldRow*rowW;
      dl->AddLine(ImVec2(phx,twp.y),ImVec2(phx,twp.y+timelineH),cHead,2.0f);
    }
    {
      float cxl=ox+pianoW+(float)ord*totalW+cursor.y*rowW;
      dl->AddRectFilled(ImVec2(cxl,twp.y),ImVec2(cxl+2,twp.y+timelineH),IM_COL32(255,255,100,200));
    }
    dl->AddLine(ImVec2(twp.x,twp.y+timelineH-1),ImVec2(twp.x+noteAreaW,twp.y+timelineH-1),cKeyBrd);
    ImGui::SetCursorPos(ImVec2(0,0));
    ImGui::InvisibleButton("##prSeek",ImVec2(pianoW+allW,timelineH));
    if (ImGui::IsItemHovered()) {
      float lx=ImGui::GetMousePos().x-twp.x+tlSX;
      if (lx>=pianoW) {
        int absRow=(int)((lx-pianoW)/rowW);
        int seekOrd=ImClamp(absRow/patLen,0,ordersLen-1);
        int seekRow=ImClamp(absRow%patLen,0,patLen-1);
        ImGui::SetTooltip("Ord %02X  Row %d",seekOrd,seekRow);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
          cursor.y=seekRow;
          curOrder=seekOrd;
          e->seekTo((unsigned char)seekOrd,seekRow);
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
  ImGui::SetNextWindowContentSize(ImVec2(pianoW+allW,totalH));
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

    ImVec2 fsz=ImGui::CalcTextSize("C-5");
    ImVec4 gv=uiColors[GUI_COLOR_PIANO_ROLL_GRID];
    ImU32 cGridFaint=IM_COL32((int)(gv.x*255),(int)(gv.y*255),(int)(gv.z*255),55);
    int qstep=(prQuantize>1)?ImMax(patLen/prQuantize,1):0;

    for (int vi=0;vi<ordersLen;vi++) {
      float oBase=ox+pianoW+vi*totalW;
      if (oBase+totalW<vx0||oBase>vx1) continue;
      bool isCur=(vi==ord);
      float dimFactor=isCur?1.0f:0.7f;
      int alphaScale=isCur?255:140;

      int viPatIdx=e->curSubSong->orders.ord[prChan][vi];
      DivPattern* viPat=e->curPat[prChan].getPattern(viPatIdx,isCur);
      if (!viPat) continue;

      for (int n=0;n<NOTES;n++) {
        int dn=NOTES-1-n;
        float ry0=oy+n*noteH,ry1=ry0+noteH;
        if (ry1<vy0||ry0>vy1) continue;
        bool blk=PR_BLACK_KEY[dn%12];
        ImVec4 bgV=uiColors[GUI_COLOR_PIANO_ROLL_BG];
        float dim=blk?(0.72f*dimFactor):(1.0f*dimFactor);
        dl->AddRectFilled(ImVec2(oBase,ry0),ImVec2(oBase+totalW,ry1),
          IM_COL32((int)(bgV.x*dim*255),(int)(bgV.y*dim*255),(int)(bgV.z*dim*255),255));
        dl->AddLine(ImVec2(oBase,ry1),ImVec2(oBase+totalW,ry1),
          (dn%12==0)?cGridHi1:cGrid);
      }

      for (int r=0;r<=patLen;r++) {
        float cx=oBase+r*rowW;
        if (cx<vx0-rowW||cx>vx1+rowW) continue;
        if (isCur) {
          bool isQLine=(qstep>0&&r%qstep==0);
          if (isQLine)       dl->AddLine(ImVec2(cx,oy),ImVec2(cx,oy+totalH),cGridHi2,1.5f);
          else if (r%hiB==0) dl->AddLine(ImVec2(cx,oy),ImVec2(cx,oy+totalH),cGridHi2);
          else if (r%hiA==0) dl->AddLine(ImVec2(cx,oy),ImVec2(cx,oy+totalH),cGridHi1);
          else               dl->AddLine(ImVec2(cx,oy),ImVec2(cx,oy+totalH),cGridFaint);
        } else {
          ImU32 gc=(r%hiB==0)?cGridHi2:(r%hiA==0)?cGridHi1:cGrid;
          ImU32 a=(gc>>24)&0xFF;
          dl->AddLine(ImVec2(cx,oy),ImVec2(cx,oy+totalH),(gc&0x00FFFFFF)|((ImU32)(a*0.4f)<<24));
        }
      }

      dl->AddLine(ImVec2(oBase,oy),ImVec2(oBase,oy+totalH),IM_COL32(255,255,255,isCur?80:40),2.0f);

      if (prShowAllChans) {
        for (int ch=0;ch<totalChans;ch++) {
          if (ch==prChan) continue;
          if (prPolyEnabled&&ch>=prChan&&ch<=prChanEnd) continue;
          if (e->isChannelMuted(ch)) continue;
          int cpIdx=e->curSubSong->orders.ord[ch][vi];
          DivPattern* cp=e->curPat[ch].getPattern(cpIdx,false);
          if (!cp) continue;
          int gA=isCur?120:60;
          int gFA=isCur?25:12;
          ImU32 gOutline=prChanColor(ch,gA);
          ImU32 gFill=prChanColor(ch,gFA);
          for (int r=0;r<patLen;r++) {
            short nv=cp->newData[r][DIV_PAT_NOTE];
            if (nv<0||nv>=NOTES||prIsSpecial(nv)) continue;
            int dur=prInferDuration(cp,r,patLen);
            float nx0=oBase+r*rowW+1;
            float ny0=oy+(NOTES-1-nv)*noteH+1;
            float nx1=oBase+(r+dur)*rowW-1;
            float ny1=ny0+noteH-2;
            if (nx1<vx0||nx0>vx1||ny1<vy0||ny0>vy1) continue;
            dl->AddRectFilled(ImVec2(nx0,ny0),ImVec2(nx1,ny1),gFill);
            dl->AddRect(ImVec2(nx0,ny0),ImVec2(nx1,ny1),gOutline,0.0f,0,1.2f);
          }
        }
      }

      if (isCur&&prPolyEnabled&&prChanEnd>prChan) {
        for (int ch=prChan+1;ch<=prChanEnd;ch++) {
          if (e->isChannelMuted(ch)) continue;
          int cpIdx=e->curSubSong->orders.ord[ch][vi];
          DivPattern* cp=e->curPat[ch].getPattern(cpIdx,false);
          if (!cp) continue;
          ImU32 cTop=prChanColor(ch,200);
          ImU32 cBot=prChanColor(ch,130);
          ImU32 cBdr=prChanColor(ch,80);
          for (int r=0;r<patLen;r++) {
            short nv=cp->newData[r][DIV_PAT_NOTE];
            if (nv<0||nv>=NOTES||prIsSpecial(nv)) continue;
            int dur=prInferDuration(cp,r,patLen);
            float nx0=oBase+r*rowW+1;
            float ny0=oy+(NOTES-1-nv)*noteH+1;
            float nx1=oBase+(r+dur)*rowW-1;
            float ny1=ny0+noteH-2;
            if (nx1<vx0||nx0>vx1||ny1<vy0||ny0>vy1) continue;
            dl->AddRectFilledMultiColor(ImVec2(nx0,ny0),ImVec2(nx1,ny1),cTop,cTop,cBot,cBot);
            dl->AddRect(ImVec2(nx0,ny0),ImVec2(nx1,ny1),cBdr,1.5f);
          }
        }
      }

      for (int r=0;r<patLen;r++) {
        short nv=viPat->newData[r][DIV_PAT_NOTE];
        if (nv==-1) continue;
        float rx0=oBase+r*rowW;
        float rx1=oBase+(r+1)*rowW;

        if (prIsSpecial(nv)) {
          if (rx0<vx0-rowW||rx0>vx1) continue;
          if (isCur) {
            ImVec4 mc4=(nv==DIV_NOTE_OFF)?uiColors[GUI_COLOR_PIANO_ROLL_NOTE_OFF]:uiColors[GUI_COLOR_PIANO_ROLL_NOTE_REL];
            ImU32 mc=ImGui::ColorConvertFloat4ToU32(mc4);
            ImU32 mcFill=IM_COL32((int)(mc4.x*255),(int)(mc4.y*255),(int)(mc4.z*255),55);
            dl->AddRectFilled(ImVec2(rx0+1,oy),ImVec2(ImMin(rx0+rowW*0.35f,rx1),oy+totalH),mcFill);
            dl->AddLine(ImVec2(rx0,oy),ImVec2(rx0,oy+totalH),mc,2.0f);
            const char* lbl=(nv==DIV_NOTE_OFF)?"OFF":(nv==DIV_NOTE_REL)?"===":"REL";
            if (noteH>fsz.y*0.8f) dl->AddText(ImVec2(rx0+2,oy+2),mc,lbl);
          }
          continue;
        }
        if (nv<0||nv>=NOTES) continue;

        int dur=prInferDuration(viPat,r,patLen);
        float nx0=oBase+r*rowW+1;
        float ny0=oy+(NOTES-1-nv)*noteH+1;
        float nx1=oBase+(r+dur)*rowW-1;
        float ny1=ny0+noteH-2;
        if (nx1<vx0||nx0>vx1||ny1<vy0||ny0>vy1) continue;

        bool inSel=isCur&&hasSel&&r>=selR0&&r<=selR1&&nv>=selN0&&nv<=selN1;
        float bf=(0.65f+0.35f*((float)(nv/12)/14.0f))*(inSel?1.5f:1.0f);
        int noteAlpha=isCur?255:alphaScale;
        ImU32 ncTop=isCur?prColorBrighter(cNote4,bf*1.18f):prColorMulAlpha(cNote4,bf*1.18f*(float)noteAlpha/255.0f);
        ImU32 ncBot=isCur?prColorBrighter(cNote4,bf*0.72f):prColorMulAlpha(cNote4,bf*0.72f*(float)noteAlpha/255.0f);
        dl->AddRectFilledMultiColor(ImVec2(nx0,ny0),ImVec2(nx1,ny1),ncTop,ncTop,ncBot,ncBot);
        dl->AddRect(ImVec2(nx0,ny0),ImVec2(nx1,ny1),IM_COL32(255,255,255,inSel?90:(isCur?38:20)),1.5f);

        if (isCur&&nx1-nx0>8) {
          float hx=nx1-4*(float)dpiScale;
          dl->AddRectFilled(ImVec2(hx,ny0),ImVec2(nx1,ny1),IM_COL32(255,255,255,55),1.0f);
        }

        if (ny1-ny0>fsz.y*0.5f) {
          dl->PushClipRect(ImVec2(nx0,ny0),ImVec2(nx1,ny1),true);
          float lw=nx1-nx0-6;
          if (lw>4) {
            char lbl[20];
            if (isCur) {
              short ins=viPat->newData[r][DIV_PAT_INS];
              if (ins>=0) snprintf(lbl,sizeof(lbl),"%s i%02X",noteNames[nv],(unsigned)ins);
              else        snprintf(lbl,sizeof(lbl),"%s",noteNames[nv]);
            } else {
              snprintf(lbl,sizeof(lbl),"%s",noteNames[nv]);
            }
            ImVec2 tsz=ImGui::CalcTextSize(lbl);
            if (tsz.x>lw) { snprintf(lbl,sizeof(lbl),"%s",noteNames[nv]); tsz=ImGui::CalcTextSize(lbl); }
            if (tsz.x<=lw) {
              float ty=ny0+(ny1-ny0-tsz.y)*0.5f;
              dl->AddText(ImVec2(nx0+3,ty),IM_COL32(255,255,255,isCur?210:100),lbl);
            }
          }
          dl->PopClipRect();
        }

        if (isCur&&prShowVolBars) {
          short vol=viPat->newData[r][DIV_PAT_VOL];
          if (vol>=0&&volMax>0) {
            float vf=ImClamp((float)vol/(float)volMax,0.0f,1.0f);
            float bh=ImMax((ny1-ny0)*0.18f,2.0f*(float)dpiScale);
            float bw=(nx1-nx0)*vf;
            dl->AddRectFilled(ImVec2(nx0,ny1-bh),ImVec2(nx0+bw,ny1),IM_COL32(255,255,255,120));
          }
        }
      }
    }

    if (prShowPitchSlide) {
      float curBase=ox+pianoW+(float)ord*totalW;
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
        if (!slideUp&&!slideDown&&portaTargetNote<0) continue;
        int dur=prInferDuration(pat,r,patLen);
        float nx0=curBase+r*rowW+1;
        float nx1=curBase+(r+dur)*rowW-1;
        float noteTop=oy+(NOTES-1-nv)*noteH+1;
        float noteBot=noteTop+noteH-2;
        float midY=(noteTop+noteBot)*0.5f;
        if (nx1<vx0||nx0>vx1) continue;
        float bf=0.65f+0.35f*((float)(nv/12)/14.0f);
        ImU32 slideCol=prColorBrighter(cNote4,bf*1.7f);
        if (portaTargetNote>=0&&portaTargetRow>=0) {
          float toX=curBase+portaTargetRow*rowW;
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
          float toY=(targetNote>=0)?(oy+(NOTES-1-targetNote)*noteH+noteH*0.5f):(midY+(slideUp?-noteH*20.0f:noteH*20.0f));
          dl->PushClipRect(ImVec2(nx0,oy),ImVec2(nx1,oy+totalH),true);
          dl->AddLine(ImVec2(nx0+(nx1-nx0)*0.5f,midY),ImVec2(nx1,toY),slideCol,2.0f);
          dl->PopClipRect();
        }
      }
    }

    if (prDragging&&!prDragBuf.empty()) {
      float curBase=ox+pianoW+(float)ord*totalW;
      for (auto& dn:prDragBuf) {
        int nr=ImClamp(dn.row+prDragDeltaR,0,patLen-1);
        int nn=ImClamp((int)dn.note+prDragDeltaN,0,NOTES-1);
        int nEnd=ImClamp(dn.endRow+prDragDeltaR,0,patLen);
        float gx0=curBase+nr*rowW+1;
        float gx1=curBase+ImMin(nEnd,nr+1)*rowW-1;
        if (dn.endRow>dn.row) gx1=curBase+ImClamp(dn.endRow+prDragDeltaR,nr+1,patLen)*rowW-1;
        float gy0=oy+(NOTES-1-nn)*noteH+1;
        float gy1=gy0+noteH-2;
        if (gx1<vx0||gx0>vx1||gy1<vy0||gy0>vy1) continue;
        dl->AddRect(ImVec2(gx0,gy0),ImVec2(gx1,gy1),IM_COL32(255,220,60,200),1.5f);
        dl->AddRectFilled(ImVec2(gx0,gy0),ImVec2(gx1,gy1),IM_COL32(255,220,60,55));
      }
    }

    if (e->isPlaying()) {
      float phx=ox+pianoW+(float)playOrder*totalW+oldRow*rowW;
      dl->AddLine(ImVec2(phx,oy),ImVec2(phx,oy+totalH),cHead,2.0f);
    }
    {
      float cx=ox+pianoW+(float)ord*totalW+cursor.y*rowW;
      dl->AddLine(ImVec2(cx,oy),ImVec2(cx,oy+totalH),IM_COL32(255,255,100,70),1.0f);
    }

    if (hasSel) {
      float curBase=ox+pianoW+(float)ord*totalW;
      float sx0=curBase+selR0*rowW,   sx1=curBase+(selR1+1)*rowW;
      float sy0=oy+(NOTES-1-selN1)*noteH,sy1=oy+(NOTES-selN0)*noteH;
      dl->AddRectFilled(ImVec2(sx0,sy0),ImVec2(sx1,sy1),cSel);
      dl->AddRect(ImVec2(sx0,sy0),ImVec2(sx1,sy1),IM_COL32(255,255,255,180));
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
    ImGui::InvisibleButton("##prInteract",ImVec2(pianoW+allW,totalH));
    bool hov=ImGui::IsItemHovered();
    bool midHeld=ImGui::IsMouseDown(ImGuiMouseButton_Middle);

    if (hov) {
      ImVec2 mp=ImGui::GetMousePos();
      float lx=mp.x-wp.x+curScrollX;
      float ly=mp.y-wp.y+scrollY;
      bool inPiano=(mp.x-wp.x<pianoW);
      int absRow=(!inPiano)?(int)((lx-pianoW)/rowW):-1;
      int clickOrd=(absRow>=0)?ImClamp(absRow/patLen,0,ordersLen-1):ord;
      int mrow=(absRow>=0)?(absRow-clickOrd*patLen):-1;
      int mnote=ImClamp(NOTES-1-(int)(ly/noteH),0,NOTES-1);

      if (clickOrd!=ord&&!prPainting&&!prErasing&&!prResizing&&!prDragging&&!prDragMaybe&&!prSelecting) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)||ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
          curOrder=clickOrd;
          ord=clickOrd;
          patIdx=e->curSubSong->orders.ord[prChan][ord];
          pat=e->curPat[prChan].getPattern(patIdx,true);
        }
      }

      if (prQuantize>1&&mrow>=0) {
        int qstep2=ImMax(patLen/prQuantize,1);
        mrow=(mrow/qstep2)*qstep2;
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
          } else if (ImGui::GetIO().KeyShift) {
            prSelecting=true;
            prDragSelStartR=mrow; prDragSelStartN=mnote;
            prSelR0=prSelR1=mrow; prSelN0=prSelN1=mnote;
            prSelRow0=mrow; prSelRow1=mrow;
          } else if (prMode==1) {
            short existNote=pat->newData[mrow][DIV_PAT_NOTE];
            bool onNote=(existNote>=0&&existNote<NOTES&&!prIsSpecial(existNote));
            bool onSel=hasSel&&mrow>=selR0&&mrow<=selR1&&onNote&&existNote>=selN0&&existNote<=selN1;
            if (onSel) {

              prDragMaybe=true;
              prDragStartR=mrow; prDragStartN=existNote;
              prDragDeltaR=0; prDragDeltaN=0;
              prDragMouseStart=mp;
              prepareUndo(GUI_UNDO_PATTERN_EDIT); prNoteUndoOpen=true;
            } else if (onNote) {

              int dur=prInferDuration(pat,mrow,patLen);
              prSelRow0=mrow; prSelRow1=ImMin(mrow+dur-1,patLen-1);
              prSelN0=existNote; prSelN1=existNote;
              prLastNote=existNote;
            } else {
              prSelecting=true;
              prDragSelStartR=mrow; prDragSelStartN=mnote;
              prSelR0=prSelR1=mrow; prSelN0=prSelN1=mnote;
              prSelRow0=mrow; prSelRow1=mrow;
            }
          } else {
            short existNote=pat->newData[mrow][DIV_PAT_NOTE];
            DivPattern* existPat=pat;
            if (prPolyEnabled&&prChanEnd>prChan&&!(existNote>=0&&existNote<NOTES&&!prIsSpecial(existNote))) {
              for (int tc=prChan+1;tc<=prChanEnd;tc++) {
                int tpIdx=e->curSubSong->orders.ord[tc][ord];
                DivPattern* tp=e->curPat[tc].getPattern(tpIdx,false);
                if (tp) {
                  short tn=tp->newData[mrow][DIV_PAT_NOTE];
                  if (tn>=0&&tn<NOTES&&!prIsSpecial(tn)) { existNote=tn; existPat=tp; break; }
                }
              }
            }
            bool onNote=(existNote>=0&&existNote<NOTES&&!prIsSpecial(existNote));
            if (onNote) {
              prPaintLen=prInferDuration(existPat,mrow,patLen);
              prLastNote=existNote;
              if (prMode==0) {
                prDragMaybe=true;
                prDragStartR=mrow; prDragStartN=existNote; prDragClickN=mnote;
                prDragDeltaR=0; prDragDeltaN=0;
                prDragMouseStart=mp;
                prepareUndo(GUI_UNDO_PATTERN_EDIT); prNoteUndoOpen=true;
              } else {

                prPainting=true; prErasing=false;
                prSelRow0=prSelRow1=-1; prSelN0=prSelN1=-1;
                prPaintNote=existNote; prPaintStartRow=mrow;
                prepareUndo(GUI_UNDO_PATTERN_EDIT); prNoteUndoOpen=true;
              }
            } else {

              if (hasSel) { prSelRow0=prSelRow1=-1; prSelN0=prSelN1=-1; }
              prPainting=true; prErasing=false;
              prPaintStartRow=mrow;
              if (prMode==0) {
                prPaintNote=mnote;
                {
                  int insToUse=(curIns>=0)?curIns:(prevIns>=0?prevIns:-1);
                  if (prPreviewTimer>0&&prPreviewChan>=0) e->noteOff(prPreviewChan);
                  e->noteOn(prChan,insToUse>=0?insToUse:0,mnote-60);
                  prPreviewTimer=15; prPreviewChan=prChan;
                }
              } else {
                int defNote=mnote;
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
                prPaintNote=defNote;
              }
              prepareUndo(GUI_UNDO_PATTERN_EDIT); prNoteUndoOpen=true;
            }
          }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)&&!prPainting&&!prResizing&&!prDragging&&!prDragMaybe) {
          if (prMode==1) {
            bool onSelNote=hasSel&&mrow>=selR0&&mrow<=selR1;
            short noteHere=pat->newData[mrow][DIV_PAT_NOTE];
            bool onNote=(noteHere>=0&&noteHere<NOTES&&!prIsSpecial(noteHere));
            if (onSelNote||onNote) ImGui::OpenPopup("##prCtx");
            else { prErasing=true; prepareUndo(GUI_UNDO_PATTERN_EDIT); }
          } else {
            short noteHere=pat->newData[mrow][DIV_PAT_NOTE];
            bool onNote=(noteHere>=0&&noteHere<NOTES&&!prIsSpecial(noteHere));
            if (onNote) {
              prepareUndo(GUI_UNDO_PATTERN_EDIT);
              pat->newData[mrow][DIV_PAT_NOTE]=-1;
              for (int ei=1;ei<DIV_MAX_COLS;ei++) pat->newData[mrow][ei]=-1;
              for (int rr=mrow+1;rr<patLen;rr++) {
                if (pat->newData[rr][DIV_PAT_NOTE]==DIV_NOTE_OFF) { pat->newData[rr][DIV_PAT_NOTE]=-1; break; }
                if (pat->newData[rr][DIV_PAT_NOTE]!=-1) break;
              }
              e->noteOff(prChan);
              makeUndo(GUI_UNDO_PATTERN_EDIT); MARK_MODIFIED;
            } else {
              prErasing=true; prepareUndo(GUI_UNDO_PATTERN_EDIT);
            }
          }
        }

        if (prDragMaybe&&ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
          float ddx=mp.x-prDragMouseStart.x, ddy=mp.y-prDragMouseStart.y;
          if (ddx*ddx+ddy*ddy>9.0f*(float)dpiScale*(float)dpiScale) {
            prDragMaybe=false; prDragging=true;
            prDragBuf.clear();
            if (hasSel) {
              int chFirst=prChan, chLast=(prPolyEnabled?prChanEnd:prChan);
              for (int ch=chFirst;ch<=chLast;ch++) {
                int cpIdx=e->curSubSong->orders.ord[ch][ord];
                DivPattern* cp=e->curPat[ch].getPattern(cpIdx,true);
                if (!cp) continue;
                for (int r=selR0;r<=selR1;r++) {
                  short nv=cp->newData[r][DIV_PAT_NOTE];
                  if (nv<0||nv>=NOTES||prIsSpecial(nv)) continue;
                  if (nv<selN0||nv>selN1) continue;
                  int dur=prInferDuration(cp,r,patLen);
                  prDragBuf.push_back({r,r+dur,ch,nv,cp->newData[r][DIV_PAT_INS],cp->newData[r][DIV_PAT_VOL]});
                }
              }
            } else {
              int srcCh=prChan;
              DivPattern* srcPat=pat;
              if (prPolyEnabled&&prChanEnd>prChan) {
                for (int tc=prChan;tc<=prChanEnd;tc++) {
                  int tpIdx=e->curSubSong->orders.ord[tc][ord];
                  DivPattern* tp=e->curPat[tc].getPattern(tpIdx,false);
                  if (tp&&tp->newData[prDragStartR][DIV_PAT_NOTE]==prDragStartN) { srcCh=tc; srcPat=tp; break; }
                }
              }
              int dur=prInferDuration(srcPat,prDragStartR,patLen);
              prDragBuf.push_back({prDragStartR,prDragStartR+dur,srcCh,(short)prDragStartN,
                srcPat->newData[prDragStartR][DIV_PAT_INS],srcPat->newData[prDragStartR][DIV_PAT_VOL]});
            }
            for (auto& dn:prDragBuf) {
              int cpIdx=e->curSubSong->orders.ord[dn.chan][ord];
              DivPattern* cp=e->curPat[dn.chan].getPattern(cpIdx,true);
              if (!cp) continue;
              cp->newData[dn.row][DIV_PAT_NOTE]=-1;
              if (dn.endRow<patLen&&cp->newData[dn.endRow][DIV_PAT_NOTE]==DIV_NOTE_OFF)
                cp->newData[dn.endRow][DIV_PAT_NOTE]=-1;
            }
          }
        }


        if (prDragging&&ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
          prDragDeltaR=mrow-prDragStartR;
          prDragDeltaN=mnote-prDragStartN;
          if (!prDragBuf.empty()) {
            int previewNote=ImClamp((int)prDragBuf[0].note+prDragDeltaN,0,NOTES-1);
            if (previewNote!=prPaintHeld) {
              int insToUse=(curIns>=0)?curIns:(prevIns>=0?prevIns:-1);
              if (prPaintHeld>=0) e->noteOff(prChan);
              e->noteOn(prChan,insToUse>=0?insToUse:0,previewNote-60);
              prPaintHeld=previewNote;
            }
          }
          MARK_MODIFIED;
        }

        if (prPainting&&ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
          int pn=prSnapScale(mnote);
          int insToUse=(curIns>=0)?curIns:(prevIns>=0?prevIns:-1);
          int paintCh=prChan;
          if (prPolyEnabled&&prChanEnd>prChan) {

            for (int tc=prChan;tc<=prChanEnd;tc++) {
              int tpIdx=e->curSubSong->orders.ord[tc][ord];
              DivPattern* tp=e->curPat[tc].getPattern(tpIdx,true);
              if (tp&&tp->newData[mrow][DIV_PAT_NOTE]==-1) { paintCh=tc; break; }
            }
            prPaintChan=paintCh;
          }
          int ppIdx=e->curSubSong->orders.ord[paintCh][ord];
          DivPattern* pp=e->curPat[paintCh].getPattern(ppIdx,true);
          if (pp) {
            pp->newData[mrow][DIV_PAT_NOTE]=(short)pn;
            prLastNote=pn;
            if (pp->newData[mrow][DIV_PAT_INS]==-1&&insToUse>=0)
              pp->newData[mrow][DIV_PAT_INS]=(short)insToUse;
            if (pp->newData[mrow][DIV_PAT_VOL]==-1)
              pp->newData[mrow][DIV_PAT_VOL]=(short)volMax;
            {
              int noteEnd=mrow+prPaintLen;
              for (int rr=mrow+1;rr<noteEnd&&rr<patLen;rr++)
                if (pp->newData[rr][DIV_PAT_NOTE]==DIV_NOTE_OFF)
                  pp->newData[rr][DIV_PAT_NOTE]=-1;
              if (noteEnd<patLen&&(pp->newData[noteEnd][DIV_PAT_NOTE]==-1||pp->newData[noteEnd][DIV_PAT_NOTE]==DIV_NOTE_OFF))
                pp->newData[noteEnd][DIV_PAT_NOTE]=DIV_NOTE_OFF;
            }
            if (prPolyEnabled&&prChanEnd>prChan) {
              for (int tc=prChan;tc<=prChanEnd;tc++) {
                if (tc==paintCh) continue;
                int tpIdx2=e->curSubSong->orders.ord[tc][ord];
                DivPattern* tp2=e->curPat[tc].getPattern(tpIdx2,true);
                if (!tp2) continue;
                if (pp->newData[mrow][DIV_PAT_VOL]>=0)
                  tp2->newData[mrow][DIV_PAT_VOL]=pp->newData[mrow][DIV_PAT_VOL];
                for (int ei=0;ei<effectCols;ei++) {
                  tp2->newData[mrow][DIV_PAT_FX(ei)]=pp->newData[mrow][DIV_PAT_FX(ei)];
                  tp2->newData[mrow][DIV_PAT_FXVAL(ei)]=pp->newData[mrow][DIV_PAT_FXVAL(ei)];
                }
              }
            }
          }
          if (pn!=prPaintHeld) {
            if (prPaintHeld>=0) e->noteOff(paintCh);
            e->noteOn(paintCh,insToUse>=0?insToUse:0,pn-60);
            prPaintHeld=pn;
          }
          MARK_MODIFIED;
        }

        if (prResizing&&ImGui::IsMouseDown(ImGuiMouseButton_Left)&&prResizeRow>=0) {
          if (mrow>=prResizeRow) {
            int oldEnd=prResizeRow+1;
            for (int rr=prResizeRow+1;rr<patLen;rr++) {
              short sv=pat->newData[rr][DIV_PAT_NOTE];
              if (sv==DIV_NOTE_OFF) { oldEnd=rr; break; }
              if (sv!=-1) { oldEnd=rr; break; }
              oldEnd=rr+1;
            }
            int newEnd=mrow+1;
            int clearTo=ImMax(oldEnd,newEnd);
            for (int rr=prResizeRow+1;rr<=clearTo&&rr<patLen;rr++) {
              short sv=pat->newData[rr][DIV_PAT_NOTE];
              if (sv==-1||sv==DIV_NOTE_OFF) pat->newData[rr][DIV_PAT_NOTE]=-1;
              else if (rr>newEnd) break;
            }
            if (newEnd<patLen&&newEnd>prResizeRow) {
              short sv=pat->newData[newEnd][DIV_PAT_NOTE];
              if (sv==-1||sv==DIV_NOTE_OFF) pat->newData[newEnd][DIV_PAT_NOTE]=DIV_NOTE_OFF;
            }
            prPaintLen=newEnd-prResizeRow;
          }
          MARK_MODIFIED;
        }

        if (prSelecting&&ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
          prSelR1=mrow; prSelN1=mnote;
          if (prDragSelStartR>=0) {
            prSelRow0=ImMin(prDragSelStartR,mrow);
            prSelRow1=ImMax(prDragSelStartR,mrow);
            prSelN0=ImMin(prDragSelStartN,mnote);
            prSelN1=ImMax(prDragSelStartN,mnote);
          }
        }

        float hcx=ox+pianoW+mrow*rowW;
        float hcy=oy+(NOTES-1-mnote)*noteH;
        dl->AddRectFilled(ImVec2(hcx,hcy),ImVec2(hcx+rowW,hcy+noteH),IM_COL32(255,255,255,28));

        if (prErasing&&ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
          short noteHere=pat->newData[mrow][DIV_PAT_NOTE];
          if (noteHere>=0&&noteHere<NOTES&&!prIsSpecial(noteHere)) {
            pat->newData[mrow][DIV_PAT_NOTE]=-1;
            for (int ei=1;ei<DIV_MAX_COLS;ei++) pat->newData[mrow][ei]=-1;
            for (int rr=mrow+1;rr<patLen;rr++) {
              if (pat->newData[rr][DIV_PAT_NOTE]==DIV_NOTE_OFF) { pat->newData[rr][DIV_PAT_NOTE]=-1; break; }
              if (pat->newData[rr][DIV_PAT_NOTE]!=-1) break;
            }
            e->noteOff(prChan);
            MARK_MODIFIED;
          }
        }
      }

      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (prDragMaybe) {
          prDragMaybe=false;
          if (prMode==0&&prDragClickN>=0&&prDragClickN!=prDragStartN) {
            DivPattern* rpPat=pat;
            int rpCh=prChan;
            if (prPolyEnabled&&prChanEnd>prChan) {
              for (int tc=prChan;tc<=prChanEnd;tc++) {
                int tpIdx=e->curSubSong->orders.ord[tc][ord];
                DivPattern* tp=e->curPat[tc].getPattern(tpIdx,false);
                if (tp&&tp->newData[prDragStartR][DIV_PAT_NOTE]==prDragStartN) { rpPat=tp; rpCh=tc; break; }
              }
            }
            rpPat->newData[prDragStartR][DIV_PAT_NOTE]=(short)prDragClickN;
            prLastNote=prDragClickN;
            int insToUse=(curIns>=0)?curIns:(prevIns>=0?prevIns:-1);
            if (prPreviewTimer>0&&prPreviewChan>=0) e->noteOff(prPreviewChan);
            e->noteOn(rpCh,insToUse>=0?insToUse:0,prDragClickN-60);
            prPreviewTimer=15; prPreviewChan=rpCh;
            MARK_MODIFIED;
          }
          int dur=prInferDuration(pat,prDragStartR,patLen);
          prSelRow0=prDragStartR; prSelRow1=ImMin(prDragStartR+dur-1,patLen-1);
          prSelN0=(prDragClickN>=0&&prDragClickN!=prDragStartN)?prDragClickN:prDragStartN;
          prSelN1=prSelN0;
          if (prNoteUndoOpen) { makeUndo(GUI_UNDO_PATTERN_EDIT); prNoteUndoOpen=false; }
          prDragClickN=-1;
        }
        if (prDragging) {
          prDragging=false;
          for (auto& dn:prDragBuf) {
            int nr=ImClamp(dn.row+prDragDeltaR,0,patLen-1);
            int nn=ImClamp((int)dn.note+prDragDeltaN,0,NOTES-1);
            int cpIdx=e->curSubSong->orders.ord[dn.chan][ord];
            DivPattern* cp=e->curPat[dn.chan].getPattern(cpIdx,true);
            if (!cp) continue;
            cp->newData[nr][DIV_PAT_NOTE]=(short)nn;
            cp->newData[nr][DIV_PAT_INS]=dn.ins;
            cp->newData[nr][DIV_PAT_VOL]=dn.vol;
            int nEnd=ImClamp(dn.endRow+prDragDeltaR,0,patLen);
            if (nEnd<patLen&&nEnd>nr) cp->newData[nEnd][DIV_PAT_NOTE]=DIV_NOTE_OFF;
          }
          if (hasSel) {
            prSelRow0=ImClamp(selR0+prDragDeltaR,0,patLen-1);
            prSelRow1=ImClamp(selR1+prDragDeltaR,0,patLen-1);
            prSelN0=ImClamp(selN0+prDragDeltaN,0,NOTES-1);
            prSelN1=ImClamp(selN1+prDragDeltaN,0,NOTES-1);
          }
          prDragBuf.clear(); prDragDeltaR=0; prDragDeltaN=0; prDragClickN=-1;
          MARK_MODIFIED;
          if (prNoteUndoOpen) { makeUndo(GUI_UNDO_PATTERN_EDIT); prNoteUndoOpen=false; }
        }
        if (prNoteUndoOpen) { makeUndo(GUI_UNDO_PATTERN_EDIT); prNoteUndoOpen=false; }
        if (prSelecting) {
          prSelecting=false;
          prSelRow0=ImMin(prSelR0,prSelR1); prSelRow1=ImMax(prSelR0,prSelR1);
          prSelN0  =ImMin(prSelN0,prSelN1); prSelN1  =ImMax(prSelN0,prSelN1);
          prDragSelStartR=-1; prDragSelStartN=-1;
        }
        if (prPaintHeld>=0) { e->noteOff(prPaintChan>=0?prPaintChan:prChan); prPaintHeld=-1; }
        prPainting=prResizing=false; prResizeRow=-1; prPaintNote=-1; prPaintChan=-1; prPaintStartRow=-1;
        if (prPianoHeld>=0&&!hov) { e->noteOff(prChan); prPianoHeld=-1; }
      }
      if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)&&prErasing)
        { makeUndo(GUI_UNDO_PATTERN_EDIT); prErasing=false; }
    } else {
      if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (prDragMaybe) { prDragMaybe=false; prDragClickN=-1; }
        if (prDragging) {
          prDragging=false; prDragBuf.clear(); prDragDeltaR=0; prDragDeltaN=0; prDragClickN=-1;
        }
        if (prNoteUndoOpen) { makeUndo(GUI_UNDO_PATTERN_EDIT); prNoteUndoOpen=false; }
        if (prPaintHeld>=0) { e->noteOff(prPaintChan>=0?prPaintChan:prChan); prPaintHeld=-1; }
        prPainting=prResizing=prSelecting=false; prResizeRow=-1; prPaintNote=-1; prPaintChan=-1;
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

      if (!ImGui::GetIO().KeyCtrl&&!ImGui::GetIO().KeyShift) {
        if (ImGui::IsKeyPressed(ImGuiKey_D,false)) prMode=0;
        if (ImGui::IsKeyPressed(ImGuiKey_S,false)) prMode=1;
        if (ImGui::IsKeyPressed(ImGuiKey_P,false)) prMode=2;
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
        int rDir=0;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow,true)&&ImGui::GetIO().KeyShift&&!ImGui::GetIO().KeyCtrl)
          rDir=-1;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow,true)&&ImGui::GetIO().KeyShift&&!ImGui::GetIO().KeyCtrl)
          rDir=1;
        if (rDir!=0) {
          int nr0=selR0+rDir, nr1=selR1+rDir;
          if (nr0>=0&&nr1<patLen) {
            prepareUndo(GUI_UNDO_PATTERN_EDIT);
            if (rDir<0) {
              for (int r=selR0;r<=selR1;r++) {
                short nv=pat->newData[r][DIV_PAT_NOTE];
                if (nv>=selN0&&nv<=selN1&&nv>=0&&nv<NOTES&&!prIsSpecial(nv)) {
                  pat->newData[r+rDir][DIV_PAT_NOTE]=nv;
                  pat->newData[r+rDir][DIV_PAT_INS]=pat->newData[r][DIV_PAT_INS];
                  pat->newData[r+rDir][DIV_PAT_VOL]=pat->newData[r][DIV_PAT_VOL];
                  pat->newData[r][DIV_PAT_NOTE]=-1;
                }
              }
            } else {
              for (int r=selR1;r>=selR0;r--) {
                short nv=pat->newData[r][DIV_PAT_NOTE];
                if (nv>=selN0&&nv<=selN1&&nv>=0&&nv<NOTES&&!prIsSpecial(nv)) {
                  pat->newData[r+rDir][DIV_PAT_NOTE]=nv;
                  pat->newData[r+rDir][DIV_PAT_INS]=pat->newData[r][DIV_PAT_INS];
                  pat->newData[r+rDir][DIV_PAT_VOL]=pat->newData[r][DIV_PAT_VOL];
                  pat->newData[r][DIV_PAT_NOTE]=-1;
                }
              }
            }
            prSelRow0=nr0; prSelRow1=nr1;
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

    {
      float sz=16.0f*(float)dpiScale;
      float gap=3.0f*(float)dpiScale;
      float pad=4.0f*(float)dpiScale;
      float rx=wp.x+noteAreaW-sz-pad;
      float winH=ImGui::GetWindowHeight();
      float wheel=ImGui::GetIO().MouseWheel;
      ImDrawList* odl=ImGui::GetWindowDrawList();
      ImU32 bgIdle=IM_COL32(20,20,20,180);
      ImU32 bgHot =IM_COL32(55,95,55,230);
      ImU32 arCol =IM_COL32(170,215,170,255);
      float r=3.0f*(float)dpiScale;

      auto drawArrow=[&](ImVec2 p, int dir, bool hov)->void{
        odl->AddRectFilled(p,ImVec2(p.x+sz,p.y+sz),hov?bgHot:bgIdle,r);
        float cx=p.x+sz*0.5f, cy=p.y+sz*0.5f;
        float hw=sz*0.28f, hh=sz*0.22f;
        switch(dir) {
          case 0: odl->AddTriangleFilled(ImVec2(cx,cy-hh),ImVec2(cx-hw,cy+hh),ImVec2(cx+hw,cy+hh),arCol); break;
          case 1: odl->AddTriangleFilled(ImVec2(cx,cy+hh),ImVec2(cx-hw,cy-hh),ImVec2(cx+hw,cy-hh),arCol); break;
          case 2: odl->AddTriangleFilled(ImVec2(cx-hw,cy),ImVec2(cx+hh,cy-hh),ImVec2(cx+hh,cy+hh),arCol); break;
          case 3: odl->AddTriangleFilled(ImVec2(cx+hw,cy),ImVec2(cx-hh,cy-hh),ImVec2(cx-hh,cy+hh),arCol); break;
        }
      };

      ImVec2 hUpP(rx, wp.y+pad);
      ImGui::SetCursorScreenPos(hUpP);
      ImGui::InvisibleButton("##prHUp",ImVec2(sz,sz));
      bool hUpHov=ImGui::IsItemHovered();
      if (hUpHov) {
        if (wheel!=0.0f) prNoteH=ImClamp(prNoteH+wheel,4.0f,20.0f);
        if (ImGui::IsMouseClicked(0)) prNoteH=ImMin(prNoteH+1.0f,20.0f);
        ImGui::SetTooltip("Row height: %.0f  (scroll or click)",prNoteH);
      }
      drawArrow(hUpP,0,hUpHov);

      ImVec2 hDnP(rx, wp.y+pad+sz+gap);
      ImGui::SetCursorScreenPos(hDnP);
      ImGui::InvisibleButton("##prHDn",ImVec2(sz,sz));
      bool hDnHov=ImGui::IsItemHovered();
      if (hDnHov) {
        if (wheel!=0.0f) prNoteH=ImClamp(prNoteH+wheel,4.0f,20.0f);
        if (ImGui::IsMouseClicked(0)) prNoteH=ImMax(prNoteH-1.0f,4.0f);
        ImGui::SetTooltip("Row height: %.0f  (scroll or click)",prNoteH);
      }
      drawArrow(hDnP,1,hDnHov);

      float zBot=wp.y+winH-pad;
      ImVec2 zInP(rx, zBot-sz);
      ImGui::SetCursorScreenPos(zInP);
      ImGui::InvisibleButton("##prZIn",ImVec2(sz,sz));
      bool zInHov=ImGui::IsItemHovered();
      if (zInHov) {
        if (wheel!=0.0f) prZoom=ImClamp(prZoom+wheel*0.25f,0.25f,4.0f);
        if (ImGui::IsMouseClicked(0)) prZoom=ImMin(prZoom+0.25f,4.0f);
        ImGui::SetTooltip("Zoom: %.2fx  (scroll or click)",prZoom);
      }
      drawArrow(zInP,3,zInHov);

      ImVec2 zOutP(rx, zBot-sz*2-gap);
      ImGui::SetCursorScreenPos(zOutP);
      ImGui::InvisibleButton("##prZOut",ImVec2(sz,sz));
      bool zOutHov=ImGui::IsItemHovered();
      if (zOutHov) {
        if (wheel!=0.0f) prZoom=ImClamp(prZoom+wheel*0.25f,0.25f,4.0f);
        if (ImGui::IsMouseClicked(0)) prZoom=ImMax(prZoom-0.25f,0.25f);
        ImGui::SetTooltip("Zoom: %.2fx  (scroll or click)",prZoom);
      }
      drawArrow(zOutP,2,zOutHov);
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
  char shortNames[9][16];
  snprintf(dynNames[0],64,"Volume (0-%d)",volMax);
  snprintf(shortNames[0],16,"Vol");
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
      if (d&&d[0]) {
        char code[5]; strncpy(code,d,4); code[4]=0;
        const char* nameStart=d;
        const char* colon=strchr(d,':');
        if (colon&&colon[1]==' ') nameStart=colon+2;
        char sn[40]=""; int ni=0;
        while(nameStart[ni]&&nameStart[ni]!='('&&ni<38) { sn[ni]=nameStart[ni]; ni++; }
        while(ni>0&&sn[ni-1]==' ') ni--;
        sn[ni]=0;
        snprintf(dynNames[1+ei*2],64,"FX%d  %s — %s",ei+1,code,sn);
        snprintf(shortNames[1+ei*2],16,"FX%d:%s",ei+1,code);
      } else {
        snprintf(dynNames[1+ei*2],64,"FX%d (effect %d)",ei+1,ei+1);
        snprintf(shortNames[1+ei*2],16,"FX%d",ei+1);
      }
    } else {
      snprintf(dynNames[1+ei*2],64,"FX%d (empty)",ei+1);
      snprintf(shortNames[1+ei*2],16,"FX%d",ei+1);
    }
    int valCounts[256]={};
    for (int r=0;r<patLen;r++) {
      short fv=pat->newData[r][DIV_PAT_FXVAL(ei)];
      if (fv>=0) valCounts[(unsigned char)fv]++;
    }
    int maxVCnt=0;
    for (int i=0;i<256;i++) if (valCounts[i]>maxVCnt) { maxVCnt=valCounts[i]; }
    if (maxCnt>0) {
      const char* d=e->getEffectDesc((unsigned char)maxFx,prChan,false);
      if (d&&d[0]) {
        char code[5]; strncpy(code,d,4); code[4]=0;
        snprintf(dynNames[1+ei*2+1],64,"FX%d Val  %s (value)",ei+1,code);
      } else {
        snprintf(dynNames[1+ei*2+1],64,"FX%d Val",ei+1);
      }
    } else {
      snprintf(dynNames[1+ei*2+1],64,"FX%d Val (empty)",ei+1);
    }
    snprintf(shortNames[1+ei*2+1],16,"FX%dV",ei+1);
  }

  int maxLane=ImMin(1+effectCols*2,9);
  if (prEffectLane>=maxLane) prEffectLane=0;
  ImGui::Text("FX:"); ImGui::SameLine();
  ImGui::SetNextItemWidth(ImMax(300.0f*(float)dpiScale, ImGui::GetContentRegionAvail().x*0.35f));
  if (ImGui::BeginCombo("##prLane",dynNames[prEffectLane])) {
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
    ImVec2 fSz=ImGui::CalcTextSize("FF");
    for (int q=1;q<4;q++) {
      float qy=lBot-lH*(q/4.0f);
      dl->AddLine(ImVec2(ox2+pianoW,qy),ImVec2(ox2+pianoW+totalW,qy),
        (q==2)?cGridHi1:cGrid);
      char qlbl[8];
      int qv=(int)(laneMax*q/4.0f+0.5f);
      if (prEffectLane==0) snprintf(qlbl,8,"%d",qv);
      else snprintf(qlbl,8,"%02X",(unsigned char)qv);
      dl->AddText(ImVec2(ox2+pianoW+2,qy-fSz.y*0.5f),IM_COL32(140,140,140,100),qlbl);
    }
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
        if (desc&&desc[0]) {
          const char* nameStart=desc;
          const char* colon=strchr(desc,':');
          if (colon&&colon[1]==' ') nameStart=colon+2;
          char sd[32]=""; int ni=0;
          while(nameStart[ni]&&nameStart[ni]!='('&&ni<30) { sd[ni]=nameStart[ni]; ni++; }
          while(ni>0&&sd[ni-1]==' ') ni--;
          sd[ni]=0;
          if (ni>0) dl->AddText(ImVec2(bx0+1,lBot-fSz.y-1),IM_COL32(220,210,100,180),sd);
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
      const char* nm=dynNames[prEffectLane];
      ImVec2 nsz=ImGui::CalcTextSize(nm);
      float midY=(lTop+fSz.y+4+lBot-fSz.y-4)*0.5f-nsz.y*0.5f;
      dl->PushClipRect(ImVec2(wp2.x,lTop+fSz.y+4),ImVec2(wp2.x+pianoW,lBot-fSz.y-4),true);
      dl->AddText(ImVec2(wp2.x+3,midY),IM_COL32(230,230,230,200),nm);
      dl->PopClipRect();
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
