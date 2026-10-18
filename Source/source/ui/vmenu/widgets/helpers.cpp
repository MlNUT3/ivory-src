#include "../headers/functions.h"
#include "../headers/widgets.h"
#include "../data/IconsFontAwesome6.h"
#include "../data/IconsV2_Defines.h"
#include "../data/fonts.h"

void fw_gui::push_color(style_col idx, ImU32 col)
{
    gui_color_mod backup;
    backup.col = idx;
    backup.backup_value = fvar->style.colors[idx];
    style_col_stack.push_back(backup);
    fvar->style.colors[idx] = ColorConvertU32ToFloat4(col);
}

void fw_gui::pop_color(int count)
{
    while (count > 0)
    {
        gui_color_mod& backup = style_col_stack.back();
        fvar->style.colors[backup.col] = backup.backup_value;
        style_col_stack.pop_back();
        count--;
    }
}

void fw_gui::push_var(style_var idx, float val)
{
    const data_var_info* var_info = get_style_var_info(idx);
    float* pvar = (float*)var_info->get_var_ptr(&fvar->style);
    style_var_stack.push_back(gui_style_mod(idx, *pvar));
    *pvar = val;
}

void fw_gui::push_var(style_var idx, const ImVec2& val)
{
    const data_var_info* var_info = get_style_var_info(idx);
    ImVec2* pvar = (ImVec2*)var_info->get_var_ptr(&fvar->style);
    style_var_stack.push_back(gui_style_mod(idx, *pvar));
    *pvar = val;
}

void fw_gui::pop_var(int count)
{
    ImGuiContext& g = *GImGui;

    while (count > 0)
    {
        gui_style_mod& backup = style_var_stack.back();
        const data_var_info* info = get_style_var_info(backup.var_idx);
        void* data = info->get_var_ptr(&fvar->style);
        if (info->count == 1) { ((float*)data)[0] = backup.backup_float[0]; }
        else if (info->count == 2) { ((float*)data)[0] = backup.backup_float[0]; ((float*)data)[1] = backup.backup_float[1]; }
        style_var_stack.pop_back();
        count--;
    }
}

void fw_gui::push_font(ImFont* font)
{
    ImGuiContext& g = *GImGui;
    if (font == NULL)
        font = GetDefaultFont();
    g.FontStack.push_back(font);
    SetCurrentFont(font);
    g.CurrentWindow->DrawList->_SetTextureID(font->ContainerAtlas->TexID);
}

void fw_gui::pop_font()
{
    ImGuiContext& g = *GImGui;
    IM_ASSERT(g.FontStack.Size > 0);
    g.FontStack.pop_back();
    ImFont* font = g.FontStack.Size == 0 ? GetDefaultFont() : g.FontStack.back();
    SetCurrentFont(font);
    g.CurrentWindow->DrawList->_SetTextureID(font->ContainerAtlas->TexID);
}

void fw_gui::set_pos(const ImVec2& pos, int type)
{
    ImGuiWindow* window = GetCurrentWindow();

    if (type == pos_all)
        window->DC.CursorPos = window->Pos - window->Scroll + pos;
    else if (type == pos_x)
        window->DC.CursorPos.x = window->Pos.x - window->Scroll.x + pos.x;
    else if (type == pos_y)
        window->DC.CursorPos.y = window->Pos.y - window->Scroll.y + pos.y;

    window->DC.IsSetPos = true;
}

void fw_gui::set_pos(float pos, int type)
{
    set_pos(ImVec2(pos, pos), type);
}

ImVec2 fw_gui::get_pos()
{
    ImGuiWindow* window = GetCurrentWindowRead();
    return window->DC.CursorPos - window->Pos + window->Scroll;
}

void fw_gui::set_screen_pos(const ImVec2& pos, int type)
{
    ImGuiWindow* window = GetCurrentWindow();

    if (type == pos_all)
        window->DC.CursorPos = pos;
    else if (type == pos_x)
        window->DC.CursorPos.x = pos.x;
    else if (type == pos_y)
        window->DC.CursorPos.y = pos.y;

    window->DC.IsSetPos = true;
}

void fw_gui::set_screen_pos(float pos, int type)
{
    set_screen_pos(ImVec2(pos, pos), type);
}

ImVec2 fw_gui::get_screen_pos()
{
    ImGuiWindow* window = GetCurrentWindowRead();
    return window->DC.CursorPos;
}

void fw_gui::begin_group()
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;

    g.GroupStack.resize(g.GroupStack.Size + 1);
    ImGuiGroupData& group_data = g.GroupStack.back();
    group_data.WindowID = window->ID;
    group_data.BackupCursorPos = window->DC.CursorPos;
    group_data.BackupCursorPosPrevLine = window->DC.CursorPosPrevLine;
    group_data.BackupCursorMaxPos = window->DC.CursorMaxPos;
    group_data.BackupIndent = window->DC.Indent;
    group_data.BackupGroupOffset = window->DC.GroupOffset;
    group_data.BackupCurrLineSize = window->DC.CurrLineSize;
    group_data.BackupCurrLineTextBaseOffset = window->DC.CurrLineTextBaseOffset;
    group_data.BackupActiveIdIsAlive = g.ActiveIdIsAlive;
    group_data.BackupHoveredIdIsAlive = g.HoveredId != 0;
    group_data.BackupIsSameLine = window->DC.IsSameLine;
    group_data.BackupActiveIdPreviousFrameIsAlive = (g.ActiveIdPreviousFrame != 0);
    group_data.EmitItem = true;

    window->DC.GroupOffset.x = window->DC.CursorPos.x - window->Pos.x - window->DC.ColumnsOffset.x;
    window->DC.Indent = window->DC.GroupOffset;
    window->DC.CursorMaxPos = window->DC.CursorPos;
    window->DC.CurrLineSize = ImVec2(0.0f, 0.0f);
    if (g.LogEnabled)
        g.LogLinePosY = -FLT_MAX;
}

void fw_gui::end_group()
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    IM_ASSERT(g.GroupStack.Size > 0);

    ImGuiGroupData& group_data = g.GroupStack.back();
    IM_ASSERT(group_data.WindowID == window->ID);

    if (window->DC.IsSetPos)
        ErrorCheckUsingSetCursorPosToExtendParentBoundaries();

    ImRect group_bb(group_data.BackupCursorPos, ImMax(ImMax(window->DC.CursorMaxPos, g.LastItemData.Rect.Max), group_data.BackupCursorPos));
    window->DC.CursorPos = group_data.BackupCursorPos;
    window->DC.CursorPosPrevLine = group_data.BackupCursorPosPrevLine;
    window->DC.CursorMaxPos = ImMax(group_data.BackupCursorMaxPos, group_bb.Max);
    window->DC.Indent = group_data.BackupIndent;
    window->DC.GroupOffset = group_data.BackupGroupOffset;
    window->DC.CurrLineSize = group_data.BackupCurrLineSize;
    window->DC.CurrLineTextBaseOffset = group_data.BackupCurrLineTextBaseOffset;
    window->DC.IsSameLine = group_data.BackupIsSameLine;
    if (g.LogEnabled)
        g.LogLinePosY = -FLT_MAX;

    if (!group_data.EmitItem)
    {
        g.GroupStack.pop_back();
        return;
    }

    window->DC.CurrLineTextBaseOffset = ImMax(window->DC.PrevLineTextBaseOffset, group_data.BackupCurrLineTextBaseOffset);
    item_size(group_bb.GetSize());
    item_add(group_bb, 0, NULL, ImGuiItemFlags_NoTabStop);

    const bool group_contains_curr_active_id = (group_data.BackupActiveIdIsAlive != g.ActiveId) && (g.ActiveId != 0) && g.ActiveId;
    const bool group_contains_prev_active_id = (group_data.BackupActiveIdPreviousFrameIsAlive == false) && (g.ActiveIdPreviousFrame != 0);
    if (group_contains_curr_active_id)
        g.LastItemData.ID = g.ActiveId;
    else if (group_contains_prev_active_id)
        g.LastItemData.ID = g.ActiveIdPreviousFrame;
    g.LastItemData.Rect = group_bb;

    const bool group_contains_curr_hovered_id = (group_data.BackupHoveredIdIsAlive == false) && g.HoveredId != 0;
    if (group_contains_curr_hovered_id)
        g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_HoveredWindow;

    if (group_contains_curr_active_id && g.ActiveIdHasBeenEditedThisFrame)
        g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_Edited;

    g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_HasDeactivated;
    if (group_contains_prev_active_id && g.ActiveId != g.ActiveIdPreviousFrame)
        g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_Deactivated;

    g.GroupStack.pop_back();
    if (g.DebugShowGroupRects)
        window->DrawList->AddRect(group_bb.Min, group_bb.Max, IM_COL32(255, 0, 255, 255));   // [Debug]
}

void fw_gui::begin_content(std::string_view id, const ImVec2& size, const ImVec2& padding, const ImVec2& spacing, window_flags window_flags__, child_flags child_flags__)
{
    fgui->push_var(ImGuiStyleVar_WindowPadding, padding);
    fgui->begin_def_child(id, size, child_flags__, window_flags__ | window_flags_no_bring_to_front_on_focus | window_flags_always_use_window_padding | window_flags_no_saved_settings | window_flags_no_focus_on_appearing);
    fgui->push_var(ImGuiStyleVar_ItemSpacing, spacing);
}

void fw_gui::end_content()
{
    fgui->pop_var();
    fgui->end_def_child();
    fgui->pop_var();
}

void fw_gui::sameline(float offset_from_start_x, float spacing_w)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    if (window->SkipItems)
        return;

    if (offset_from_start_x != 0.0f)
    {
        if (spacing_w < 0.0f)
            spacing_w = 0.0f;
        window->DC.CursorPos.x = window->Pos.x - window->Scroll.x + offset_from_start_x + spacing_w + window->DC.GroupOffset.x + window->DC.ColumnsOffset.x;
        window->DC.CursorPos.y = window->DC.CursorPosPrevLine.y;
    }
    else
    {
        if (spacing_w < 0.0f)
            spacing_w = fvar->style.item_spacing.x;
        window->DC.CursorPos.x = window->DC.CursorPosPrevLine.x + spacing_w;
        window->DC.CursorPos.y = window->DC.CursorPosPrevLine.y;
    }
    window->DC.CurrLineSize = window->DC.PrevLineSize;
    window->DC.CurrLineTextBaseOffset = window->DC.PrevLineTextBaseOffset;
    window->DC.IsSameLine = true;
}

void fw_gui::dummy(const ImVec2& size)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return;

    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size);
    fgui->item_size(size);
    fgui->item_add(bb, 0);
}

bool begin_def_child_ex(const char* name, ImGuiID id, const ImVec2& size_arg, child_flags child_flagss, window_flags window_flags)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* parent_window = g.CurrentWindow;
    IM_ASSERT(id != 0);

    const child_flags ImGuiChildFlags_SupportedMask_ = child_flags_borders | child_flags_always_use_window_padding | child_flags_resize_x | child_flags_resize_y | child_flags_auto_resize_x | child_flags_auto_resize_y | child_flags_always_auto_resize | child_flags_frame_style | child_flags_nav_flattened;
    IM_UNUSED(ImGuiChildFlags_SupportedMask_);
    IM_ASSERT((child_flagss & ~ImGuiChildFlags_SupportedMask_) == 0 && "Illegal ImGuiChildFlags value. Did you pass ImGuiWindowFlags values instead of ImGuiChildFlags?");
    IM_ASSERT((window_flags & window_flags_always_auto_resize) == 0 && "Cannot specify ImGuiWindowFlags_AlwaysAutoResize for BeginChild(). Use ImGuiChildFlags_AlwaysAutoResize!");
    if (child_flagss & child_flags_always_auto_resize)
    {
        IM_ASSERT((child_flagss & (child_flags_resize_x | child_flags_resize_y)) == 0 && "Cannot use ImGuiChildFlags_ResizeX or ImGuiChildFlags_ResizeY with ImGuiChildFlags_AlwaysAutoResize!");
        IM_ASSERT((child_flagss & (child_flags_auto_resize_x | child_flags_auto_resize_y)) != 0 && "Must use ImGuiChildFlags_AutoResizeX or ImGuiChildFlags_AutoResizeY with ImGuiChildFlags_AlwaysAutoResize!");
    }
#ifndef IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    if (window_flags & window_flags_always_use_window_padding)
        child_flagss |= child_flags_always_use_window_padding;
    if (window_flags & window_flags_nav_flattened)
        child_flagss |= child_flags_nav_flattened;
#endif
    if (child_flagss & child_flags_auto_resize_x)
        child_flagss &= ~child_flags_resize_x;
    if (child_flagss & child_flags_auto_resize_y)
        child_flagss &= ~child_flags_resize_y;

    window_flags |= window_flags_child_window | window_flags_no_title_bar;
    window_flags |= (parent_window->Flags & window_flags_no_move);
    if (child_flagss & (child_flags_auto_resize_x | child_flags_auto_resize_y | child_flags_always_auto_resize))
        window_flags |= window_flags_always_auto_resize;
    if ((child_flagss & (child_flags_resize_x | child_flags_resize_y)) == 0)
        window_flags |= window_flags_no_resize | window_flags_no_saved_settings;

    if (child_flagss & child_flags_frame_style)
    {
        PushStyleColor(style_col_child_bg, fvar->style.colors[ImGuiCol_FrameBg]);
        PushStyleVar(style_var_child_rounding, fvar->style.frame_rounding);
        PushStyleVar(style_var_child_border_size, fvar->style.frame_border_size);
        PushStyleVar(style_var_window_padding, fvar->style.frame_padding);
        child_flagss |= child_flags_border | child_flags_always_use_window_padding;
        window_flags |= window_flags_no_move;
    }

    // ChildFlags moved into separate window creation in newer ImGui versions
    // Store child flags for use during window creation if API supports it
    if (child_flagss & child_flags_borders)
        window_flags |= window_flags_child_window;

    const ImVec2 size_avail = GetContentRegionAvail();
    const ImVec2 size_default((child_flagss & child_flags_auto_resize_x) ? 0.0f : size_avail.x, (child_flagss & child_flags_auto_resize_y) ? 0.0f : size_avail.y);
    const ImVec2 size = CalcItemSize(size_arg, size_default.x, size_default.y);
    SetNextWindowSize(size);

    const char* temp_window_name;

    if (name)
        ImFormatStringToTempBuffer(&temp_window_name, NULL, "%s/%s_%08X", parent_window->Name, name, id);
    else
        ImFormatStringToTempBuffer(&temp_window_name, NULL, "%s/%08X", parent_window->Name, id);

    const float backup_border_size = fvar->style.child_border_size;
    if ((child_flagss & child_flags_borders) == 0)
        fvar->style.child_border_size = 0.0f;

    const bool ret = fgui->begin(temp_window_name, NULL, window_flags);

    fvar->style.child_border_size = backup_border_size;
    if (child_flagss & child_flags_frame_style)
    {
        PopStyleVar(3);
        PopStyleColor();
    }

    ImGuiWindow* child_window = g.CurrentWindow;
    child_window->ChildId = id;

    if (child_window->BeginCount == 1)
        parent_window->DC.CursorPos = child_window->Pos;

    const ImGuiID temp_id_for_activation = ImHashStr("##Child", 0, id);
    if (g.ActiveId == temp_id_for_activation)
        ClearActiveID();
    if (g.NavActivateId == id && !(child_flagss & child_flags_nav_flattened) && (child_window->DC.NavLayersActiveMask != 0 || child_window->DC.NavWindowHasScrollY))
    {
        FocusWindow(child_window);
        NavInitWindow(child_window, false);
        SetActiveID(temp_id_for_activation, child_window);
        g.ActiveIdSource = g.NavInputSource;
    }
    return ret;
}

bool fw_gui::begin_def_child(std::string_view name, const ImVec2& size_arg, child_flags child_flags, window_flags window_flags)
{
    ImGuiID id = GetCurrentWindow()->GetID(name.data());
    return begin_def_child_ex(name.data(), id, size_arg, child_flags, window_flags);
}

void fw_gui::end_def_child()
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* child_window = g.CurrentWindow;

    // WithinEndChild removed in newer ImGui versions
    IM_ASSERT(child_window->Flags & window_flags_child_window);
    ImVec2 child_size = child_window->Size;
    fgui->end();
    if (child_window->BeginCount == 1)
    {
        ImGuiWindow* parent_window = g.CurrentWindow;
        ImRect bb(parent_window->DC.CursorPos, parent_window->DC.CursorPos + child_size);
        item_size(child_size);
        const bool nav_flattened = (child_window->ChildFlags & child_flags_nav_flattened) != 0;
        if ((child_window->DC.NavLayersActiveMask != 0 || child_window->DC.NavWindowHasScrollY) && !nav_flattened)
        {
            item_add(bb, child_window->ChildId);
            RenderNavHighlight(bb, child_window->ChildId);

            if (child_window->DC.NavLayersActiveMask == 0 && child_window == g.NavWindow)
                RenderNavHighlight(ImRect(bb.Min - ImVec2(2, 2), bb.Max + ImVec2(2, 2)), g.NavId, ImGuiNavHighlightFlags_Compact);
        }
        else
        {
            item_add(bb, child_window->ChildId, NULL, ImGuiItemFlags_NoNav);

            if (nav_flattened)
                parent_window->DC.NavLayersActiveMaskNext |= child_window->DC.NavLayersActiveMaskNext;
        }
        if (g.HoveredWindow == child_window)
            g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_HoveredWindow;
    }
    g.LogLinePosY = -FLT_MAX;
}

void fw_gui::set_next_window_pos(const ImVec2& pos, gui_cond cond, const ImVec2& pivot)
{
    ImGuiContext& g = *GImGui;
    IM_ASSERT(cond == 0 || ImIsPowerOfTwo(cond));
    g.NextWindowData.Flags |= ImGuiNextWindowDataFlags_HasPos;
    g.NextWindowData.PosVal = pos;
    g.NextWindowData.PosPivotVal = pivot;
    g.NextWindowData.PosCond = cond ? cond : gui_cond_always;
}

void fw_gui::set_next_window_size(const ImVec2& size, gui_cond cond)
{
    ImGuiContext& g = *GImGui;
    IM_ASSERT(cond == 0 || ImIsPowerOfTwo(cond));
    g.NextWindowData.Flags |= ImGuiNextWindowDataFlags_HasSize;
    g.NextWindowData.SizeVal = size;
    g.NextWindowData.SizeCond = cond ? cond : gui_cond_always;
}

ImVec2 fw_gui::text_size(ImFont* fontm, const char* text, const char* text_end, bool hide_text_after_double_hash, float wrap_width)
{
    this->push_font(fontm);

    ImGuiContext& g = *GImGui;

    const char* text_display_end;
    if (hide_text_after_double_hash)
        text_display_end = FindRenderedTextEnd(text, text_end);
    else
        text_display_end = text_end;

    ImFont* font = g.Font;
    const float font_size = g.FontSize;
    if (text == text_display_end)
        return ImVec2(0.0f, font_size);
    ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, wrap_width, text, text_display_end, NULL);
    text_size.x = IM_TRUNC(text_size.x + 0.99999f);

    this->pop_font();

    return text_size;
}

ImVec2 fw_gui::window_size()
{
    ImGuiWindow* window = GImGui->CurrentWindow;
    return window->Size;
}

float fw_gui::window_width()
{
    ImGuiWindow* window = GImGui->CurrentWindow;
    return window->Size.x;
}

float fw_gui::window_height()
{
    ImGuiWindow* window = GImGui->CurrentWindow;
    return window->Size.y;
}

ImVec2 fw_gui::window_pos()
{
    ImGuiWindow* window = GImGui->CurrentWindow;
    return window->Pos;
}

ImDrawList* fw_gui::window_drawlist()
{
    ImGuiWindow* window = GetCurrentWindow();
    return window->DrawList;
}

static ImDrawList* get_viewport_bg_fg_drawlist(ImGuiViewportP* viewport, size_t drawlist_no, const char* drawlist_name)
{
    ImGuiContext& g = *GImGui;
    // Create the draw list on demand, because they are not frequently used for all viewports
    IM_ASSERT(drawlist_no < IM_ARRAYSIZE(viewport->BgFgDrawLists));
    ImDrawList* draw_list = viewport->BgFgDrawLists[drawlist_no];
    if (draw_list == NULL)
    {
        draw_list = IM_NEW(ImDrawList)(&g.DrawListSharedData);
        draw_list->_OwnerName = drawlist_name;
        viewport->BgFgDrawLists[drawlist_no] = draw_list;
    }

    if (viewport->BgFgDrawListsLastFrame[drawlist_no] != g.FrameCount)
    {
        draw_list->_ResetForNewFrame();
        draw_list->PushTextureID(g.IO.Fonts->TexID);
        draw_list->PushClipRect(viewport->Pos, viewport->Pos + viewport->Size, false);
        viewport->BgFgDrawListsLastFrame[drawlist_no] = g.FrameCount;
    }
    return draw_list;
}

ImDrawList* fw_gui::foreground_drawlist()
{
    ImGuiContext& g = *GImGui;
    return get_viewport_bg_fg_drawlist((ImGuiViewportP*)g.Viewports[0], 1, "##Foreground");
}

ImDrawList* fw_gui::background_drawlist()
{
    ImGuiContext& g = *GImGui;
    return get_viewport_bg_fg_drawlist((ImGuiViewportP*)g.Viewports[0], 0, "##Background");
}

ImGuiWindow* fw_gui::get_window()
{
    ImGuiContext& g = *GImGui;
    g.CurrentWindow->WriteAccessed = true;
    return g.CurrentWindow;
}

void fw_gui::push_id(const char* str_id)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImGuiID id = window->GetID(str_id);
    window->IDStack.push_back(id);
}

void fw_gui::push_id(const char* str_id_begin, const char* str_id_end)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImGuiID id = window->GetID(str_id_begin, str_id_end);
    window->IDStack.push_back(id);
}

void fw_gui::push_id(const void* ptr_id)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImGuiID id = window->GetID(ptr_id);
    window->IDStack.push_back(id);
}

void fw_gui::push_id(int int_id)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImGuiID id = window->GetID(int_id);
    window->IDStack.push_back(id);
}

void fw_gui::pop_id()
{
    ImGuiWindow* window = GImGui->CurrentWindow;
    IM_ASSERT(window->IDStack.Size > 1);
    window->IDStack.pop_back();
}

ImGuiID fw_gui::get_id(const char* str, const char* str_end)
{
    ImGuiID seed = get_window()->IDStack.back();
    ImGuiID id = ImHashStr(str, str_end ? (str_end - str) : 0, seed);
#ifndef IMGUI_DISABLE_DEBUG_TOOLS
    ImGuiContext& g = *get_window()->Ctx;
    if (g.DebugHookIdInfo == id)
        ImGui::DebugHookIdInfo(id, ImGuiDataType_String, str, str_end);
#endif
    return id;
}

ImGuiID fw_gui::get_id(const void* ptr)
{
    ImGuiID seed = get_window()->IDStack.back();
    ImGuiID id = ImHashData(&ptr, sizeof(void*), seed);
#ifndef IMGUI_DISABLE_DEBUG_TOOLS
    ImGuiContext& g = *get_window()->Ctx;
    if (g.DebugHookIdInfo == id)
        ImGui::DebugHookIdInfo(id, ImGuiDataType_Pointer, ptr, NULL);
#endif
    return id;
}

ImGuiID fw_gui::get_id(int n)
{
    ImGuiID seed = get_window()->IDStack.back();
    ImGuiID id = ImHashData(&n, sizeof(n), seed);
#ifndef IMGUI_DISABLE_DEBUG_TOOLS
    ImGuiContext& g = *get_window()->Ctx;
    if (g.DebugHookIdInfo == id)
        ImGui::DebugHookIdInfo(id, ImGuiDataType_S32, (void*)(intptr_t)n, NULL);
#endif
    return id;
}

void fw_gui::item_size(const ImVec2& size, float text_baseline_y)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    if (window->SkipItems)
        return;

    const float offset_to_match_baseline_y = (text_baseline_y >= 0) ? ImMax(0.0f, window->DC.CurrLineTextBaseOffset - text_baseline_y) : 0.0f;

    const float line_y1 = window->DC.IsSameLine ? window->DC.CursorPosPrevLine.y : window->DC.CursorPos.y;
    const float line_height = ImMax(window->DC.CurrLineSize.y, window->DC.CursorPos.y - line_y1 + size.y + offset_to_match_baseline_y);

    window->DC.CursorPosPrevLine.x = window->DC.CursorPos.x + size.x;
    window->DC.CursorPosPrevLine.y = line_y1;
    window->DC.CursorPos.x = IM_TRUNC(window->Pos.x + window->DC.Indent.x + window->DC.ColumnsOffset.x);
    window->DC.CursorPos.y = IM_TRUNC(line_y1 + line_height + fvar->style.item_spacing.y);
    window->DC.CursorMaxPos.x = ImMax(window->DC.CursorMaxPos.x, window->DC.CursorPosPrevLine.x);
    window->DC.CursorMaxPos.y = ImMax(window->DC.CursorMaxPos.y, window->DC.CursorPos.y - fvar->style.item_spacing.y);

    window->DC.PrevLineSize.y = line_height;
    window->DC.CurrLineSize.y = 0.0f;
    window->DC.PrevLineTextBaseOffset = ImMax(window->DC.CurrLineTextBaseOffset, text_baseline_y);
    window->DC.CurrLineTextBaseOffset = 0.0f;
    window->DC.IsSameLine = window->DC.IsSetPos = false;

    if (window->DC.LayoutType == ImGuiLayoutType_Horizontal)
        sameline();
}

void fw_gui::item_size(const ImRect& bb, float text_baseline_y)
{
    item_size(bb.GetSize(), text_baseline_y);
}

bool fw_gui::item_add(const ImRect& bb, ImGuiID id, const ImRect* nav_bb_arg, ImGuiItemFlags extra_flags)
{
    return ItemAdd(bb, id, nav_bb_arg, extra_flags);
}

static ImGuiHoveredFlags apply_hover_flags_for_tooltip(ImGuiHoveredFlags user_flags, ImGuiHoveredFlags shared_flags)
{
    if (user_flags & (ImGuiHoveredFlags_DelayNone | ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_DelayNormal))
        shared_flags &= ~(ImGuiHoveredFlags_DelayNone | ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_DelayNormal);
    return user_flags | shared_flags;
}

static ImGuiWindow* get_combined_root_window(ImGuiWindow* window, bool popup_hierarchy)
{
    ImGuiWindow* last_window = NULL;
    while (last_window != window)
    {
        last_window = window;
        window = window->RootWindow;
        if (popup_hierarchy)
            window = window->RootWindowPopupTree;
    }
    return window;
}

bool fw_gui::is_window_hovered(ImGuiHoveredFlags flags)
{
    IM_ASSERT((flags & ~ImGuiHoveredFlags_AllowedMaskForIsWindowHovered) == 0 && "Invalid flags for IsWindowHovered()!");

    ImGuiContext& g = *GImGui;
    ImGuiWindow* ref_window = g.HoveredWindow;
    ImGuiWindow* cur_window = g.CurrentWindow;
    if (ref_window == NULL)
        return false;

    if ((flags & ImGuiHoveredFlags_AnyWindow) == 0)
    {
        IM_ASSERT(cur_window);
        const bool popup_hierarchy = (flags & ImGuiHoveredFlags_NoPopupHierarchy) == 0;
        if (flags & ImGuiHoveredFlags_RootWindow)
            cur_window = get_combined_root_window(cur_window, popup_hierarchy);

        bool result;
        if (flags & ImGuiHoveredFlags_ChildWindows)
            result = IsWindowChildOf(ref_window, cur_window, popup_hierarchy);
        else
            result = (ref_window == cur_window);
        if (!result)
            return false;
    }

    if (!IsWindowContentHoverable(ref_window, flags))
        return false;
    if (!(flags & ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        if (g.ActiveId != 0 && !g.ActiveIdAllowOverlap && g.ActiveId != ref_window->MoveId)
            return false;

    if (flags & ImGuiHoveredFlags_ForTooltip)
        flags = apply_hover_flags_for_tooltip(flags, fvar->style.hover_flags_for_tooltip_mouse);
    if ((flags & ImGuiHoveredFlags_Stationary) != 0 && g.HoverWindowUnlockedStationaryId != ref_window->ID)
        return false;

    return true;
}

bool fw_gui::is_window_focused(ImGuiFocusedFlags flags)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* ref_window = g.NavWindow;
    ImGuiWindow* cur_window = g.CurrentWindow;

    if (ref_window == NULL)
        return false;
    if (flags & ImGuiFocusedFlags_AnyWindow)
        return true;

    IM_ASSERT(cur_window);
    const bool popup_hierarchy = (flags & ImGuiFocusedFlags_NoPopupHierarchy) == 0;
    if (flags & ImGuiHoveredFlags_RootWindow)
        cur_window = get_combined_root_window(cur_window, popup_hierarchy);

    if (flags & ImGuiHoveredFlags_ChildWindows)
        return IsWindowChildOf(ref_window, cur_window, popup_hierarchy);
    else
        return (ref_window == cur_window);
}

void fw_gui::set_window_focus()
{
    FocusWindow(GImGui->CurrentWindow);
}

void fw_gui::set_window_focus(const char* name)
{
    if (name)
    {
        if (ImGuiWindow* window = FindWindowByName(name))
            FocusWindow(window);
    }
    else
    {
        FocusWindow(NULL);
    }
}

bool fw_gui::is_rect_visible(const ImVec2& size)
{
    ImGuiWindow* window = GImGui->CurrentWindow;
    return window->ClipRect.Overlaps(ImRect(window->DC.CursorPos, window->DC.CursorPos + size));
}

bool fw_gui::is_rect_visible(const ImRect& rect)
{
    ImGuiWindow* window = GImGui->CurrentWindow;
    return window->ClipRect.Overlaps(rect);
}

ImVec2 fw_gui::adjust_window_pos(const ImVec2& rect, const ImVec2& window_size)
{
    ImVec2 wpos = rect;
    ImGuiIO& io = ImGui::GetIO();
    wpos = ImClamp(wpos, ImVec2(0, 0), io.DisplaySize - window_size);

    return wpos;
}

bool fw_gui::button_behavior(const ImRect& bb, ImGuiID id, bool* out_hovered, bool* out_held, ImGuiButtonFlags flags)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = GetCurrentWindow();

    if ((flags & ImGuiButtonFlags_MouseButtonMask_) == 0)
        flags |= ImGuiButtonFlags_MouseButtonLeft;

    if ((flags & ImGuiButtonFlags_PressedOnMask_) == 0)
        flags |= ImGuiButtonFlags_PressedOnDefault_;

    ImGuiItemFlags item_flags = (g.LastItemData.ID == id ? g.LastItemData.InFlags : g.CurrentItemFlags);
    if (flags & ImGuiButtonFlags_AllowOverlap)
        item_flags |= ImGuiItemFlags_AllowOverlap;
    // ImGuiButtonFlags_Repeat might be named differently or use ImGuiItemFlags_ButtonRepeat
    if (flags & ImGuiButtonFlags_Repeat)
        item_flags |= ImGuiItemFlags_ButtonRepeat;

    ImGuiWindow* backup_hovered_window = g.HoveredWindow;
    const bool flatten_hovered_children = (flags & ImGuiButtonFlags_FlattenChildren) && g.HoveredWindow && g.HoveredWindow->RootWindow == window;
    if (flatten_hovered_children)
        g.HoveredWindow = window;

#ifdef IMGUI_ENABLE_TEST_ENGINE
    if (g.LastItemData.ID != id)
        IMGUI_TEST_ENGINE_ITEM_ADD(id, bb, NULL);
#endif

    bool pressed = false;
    bool hovered = ItemHoverable(bb, id, item_flags);

    if (g.DragDropActive && (flags & ImGuiButtonFlags_PressedOnDragDropHold) && !(g.DragDropSourceFlags & ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
        if (IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        {
            hovered = true;
            SetHoveredID(id);
            if (g.HoveredIdTimer - g.IO.DeltaTime <= 0.7f && g.HoveredIdTimer >= 0.7f)
            {
                pressed = true;
                g.DragDropHoldJustPressedId = id;
                FocusWindow(window);
            }
        }

    if (flatten_hovered_children)
        g.HoveredWindow = backup_hovered_window;

    const ImGuiID test_owner_id = (flags & ImGuiButtonFlags_NoTestKeyOwner) ? ImGuiKeyOwner_Any : id;
    if (hovered)
    {
        IM_ASSERT(id != 0);

        int mouse_button_clicked = -1;
        int mouse_button_released = -1;
        for (int button = 0; button < 3; button++)
            if (flags & (ImGuiButtonFlags_MouseButtonLeft << button))
            {
                if (IsMouseClicked(button, ImGuiInputFlags_None, test_owner_id) && mouse_button_clicked == -1) { mouse_button_clicked = button; }
                if (IsMouseReleased(button, test_owner_id) && mouse_button_released == -1) { mouse_button_released = button; }
            }

        // Check for key modifiers
        if (!(flags & ImGuiButtonFlags_NoKeyModifiers) || (!g.IO.KeyCtrl && !g.IO.KeyShift && !g.IO.KeyAlt))
        {
            if (mouse_button_clicked != -1 && g.ActiveId != id)
            {
                if (!(flags & ImGuiButtonFlags_NoSetKeyOwner))
                    SetKeyOwner(MouseButtonToKey(mouse_button_clicked), id);
                if (flags & (ImGuiButtonFlags_PressedOnClickRelease | ImGuiButtonFlags_PressedOnClickReleaseAnywhere))
                {
                    SetActiveID(id, window);
                    g.ActiveIdMouseButton = mouse_button_clicked;
                    if (!(flags & ImGuiButtonFlags_NoNavFocus))
                    {
                        SetFocusID(id, window);
                        FocusWindow(window);
                    }
                    else
                    {
                        FocusWindow(window, ImGuiFocusRequestFlags_RestoreFocusedChild);
                    }
                }
                if ((flags & ImGuiButtonFlags_PressedOnClick) || ((flags & ImGuiButtonFlags_PressedOnDoubleClick) && g.IO.MouseClickedCount[mouse_button_clicked] == 2))
                {
                    pressed = true;
                    if (flags & ImGuiButtonFlags_NoHoldingActiveId)
                        ClearActiveID();
                    else
                        SetActiveID(id, window);
                    g.ActiveIdMouseButton = mouse_button_clicked;
                    if (!(flags & ImGuiButtonFlags_NoNavFocus))
                    {
                        SetFocusID(id, window);
                        FocusWindow(window);
                    }
                    else
                    {
                        FocusWindow(window, ImGuiFocusRequestFlags_RestoreFocusedChild);
                    }
                }
            }
            if (flags & ImGuiButtonFlags_PressedOnRelease)
            {
                if (mouse_button_released != -1)
                {
                    const bool has_repeated_at_least_once = (item_flags & ImGuiItemFlags_ButtonRepeat) && g.IO.MouseDownDurationPrev[mouse_button_released] >= g.IO.KeyRepeatDelay; // Repeat mode trumps on release behavior
                    if (!has_repeated_at_least_once)
                        pressed = true;
                    if (!(flags & ImGuiButtonFlags_NoNavFocus))
                        SetFocusID(id, window);
                    ClearActiveID();
                }
            }

            if (g.ActiveId == id && (item_flags & ImGuiItemFlags_ButtonRepeat))
                if (g.IO.MouseDownDuration[g.ActiveIdMouseButton] > 0.0f && IsMouseClicked(g.ActiveIdMouseButton, ImGuiInputFlags_Repeat, test_owner_id))
                    pressed = true;
        }

        if (pressed)
            g.NavDisableHighlight = true;
    }

    if (g.NavId == id && !g.NavDisableHighlight && g.NavDisableMouseHover)
        if (!(flags & ImGuiButtonFlags_NoHoveredOnFocus))
            hovered = true;
    if (g.NavActivateDownId == id)
    {
        bool nav_activated_by_code = (g.NavActivateId == id);
        bool nav_activated_by_inputs = (g.NavActivatePressedId == id);
        if (!nav_activated_by_inputs && (item_flags & ImGuiItemFlags_ButtonRepeat))
        {
            const ImGuiKeyData* key1 = GetKeyData(ImGuiKey_Space);
            const ImGuiKeyData* key2 = GetKeyData(ImGuiKey_Enter);
            const ImGuiKeyData* key3 = GetKeyData(ImGuiKey_NavGamepadActivate);
            const float t1 = ImMax(ImMax(key1->DownDuration, key2->DownDuration), key3->DownDuration);
            nav_activated_by_inputs = CalcTypematicRepeatAmount(t1 - g.IO.DeltaTime, t1, g.IO.KeyRepeatDelay, g.IO.KeyRepeatRate) > 0;
        }
        if (nav_activated_by_code || nav_activated_by_inputs)
        {
            pressed = true;
            SetActiveID(id, window);
            g.ActiveIdSource = g.NavInputSource;
            if (!(flags & ImGuiButtonFlags_NoNavFocus) && !(g.NavActivateFlags & ImGuiActivateFlags_FromShortcut))
                SetFocusID(id, window);
            if (g.NavActivateFlags & ImGuiActivateFlags_FromShortcut)
                g.ActiveIdFromShortcut = true;
        }
    }

    bool held = false;
    if (g.ActiveId == id)
    {
        if (g.ActiveIdSource == ImGuiInputSource_Mouse)
        {
            if (g.ActiveIdIsJustActivated)
                g.ActiveIdClickOffset = g.IO.MousePos - bb.Min;

            const int mouse_button = g.ActiveIdMouseButton;
            if (mouse_button == -1)
            {
                ClearActiveID();
            }
            else if (IsMouseDown(mouse_button, test_owner_id))
            {
                held = true;
            }
            else
            {
                bool release_in = hovered && (flags & ImGuiButtonFlags_PressedOnClickRelease) != 0;
                bool release_anywhere = (flags & ImGuiButtonFlags_PressedOnClickReleaseAnywhere) != 0;
                if ((release_in || release_anywhere) && !g.DragDropActive)
                {
                    bool is_double_click_release = (flags & ImGuiButtonFlags_PressedOnDoubleClick) && g.IO.MouseReleased[mouse_button] && g.IO.MouseClickedLastCount[mouse_button] == 2;
                    bool is_repeating_already = (item_flags & ImGuiItemFlags_ButtonRepeat) && g.IO.MouseDownDurationPrev[mouse_button] >= g.IO.KeyRepeatDelay;
                    bool is_button_avail_or_owned = TestKeyOwner(MouseButtonToKey(mouse_button), test_owner_id);
                    if (!is_double_click_release && !is_repeating_already && is_button_avail_or_owned)
                        pressed = true;
                }
                ClearActiveID();
            }
            if (!(flags & ImGuiButtonFlags_NoNavFocus))
                g.NavDisableHighlight = true;
        }
        else if (g.ActiveIdSource == ImGuiInputSource_Keyboard || g.ActiveIdSource == ImGuiInputSource_Gamepad)
        {
            if (g.NavActivateDownId == id)
                held = true;
            else
                ClearActiveID();
        }
        if (pressed)
            g.ActiveIdHasBeenPressedBefore = true;
    }

    if (g.NavHighlightActivatedId == id)
        hovered = true;

    if (out_hovered) *out_hovered = hovered;
    if (out_held) *out_held = held;

    return pressed;
}

bool fw_gui::invisible_button(const char* str_id, const ImVec2& size_arg, ImGuiButtonFlags flags)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    IM_ASSERT(size_arg.x != 0.0f && size_arg.y != 0.0f);

    const ImGuiID id = window->GetID(str_id);
    ImVec2 size = CalcItemSize(size_arg, 0.0f, 0.0f);
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size);
    item_size(size);
    if (!item_add(bb, id))
        return false;

    bool hovered, held;
    bool pressed = button_behavior(bb, id, &hovered, &held, flags);

    IMGUI_TEST_ENGINE_ITEM_INFO(id, str_id, g.LastItemData.StatusFlags);
    return pressed;
}

ImVec4 fw_gui::u32_to_float4(ImU32 in)
{
    float s = 1.0f / 255.0f;
    return ImVec4(
        ((in >> IM_COL32_R_SHIFT) & 0xFF) * s,
        ((in >> IM_COL32_G_SHIFT) & 0xFF) * s,
        ((in >> IM_COL32_B_SHIFT) & 0xFF) * s,
        ((in >> IM_COL32_A_SHIFT) & 0xFF) * s);
}

ImU32 fw_gui::float4_to_u32(const ImVec4& in)
{
    ImU32 out;
    out = ((ImU32)IM_F32_TO_INT8_SAT(in.x)) << IM_COL32_R_SHIFT;
    out |= ((ImU32)IM_F32_TO_INT8_SAT(in.y)) << IM_COL32_G_SHIFT;
    out |= ((ImU32)IM_F32_TO_INT8_SAT(in.z)) << IM_COL32_B_SHIFT;
    out |= ((ImU32)IM_F32_TO_INT8_SAT(in.w)) << IM_COL32_A_SHIFT;
    return out;
}

void fw_gui::rgb_to_hsv(float r, float g, float b, float& out_h, float& out_s, float& out_v)
{
    float K = 0.f;
    if (g < b)
    {
        ImSwap(g, b);
        K = -1.f;
    }
    if (r < g)
    {
        ImSwap(r, g);
        K = -2.f / 6.f - K;
    }

    const float chroma = r - (g < b ? g : b);
    out_h = ImFabs(K + (g - b) / (6.f * chroma + 1e-20f));
    out_s = chroma / (r + 1e-20f);
    out_v = r;
}

void fw_gui::hsv_to_rgb(float h, float s, float v, float& out_r, float& out_g, float& out_b)
{
    if (s == 0.0f)
    {
        out_r = out_g = out_b = v;
        return;
    }

    h = ImFmod(h, 1.0f) / (60.0f / 360.0f);
    int   i = (int)h;
    float f = h - (float)i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));

    switch (i)
    {
    case 0: out_r = v; out_g = t; out_b = p; break;
    case 1: out_r = q; out_g = v; out_b = p; break;
    case 2: out_r = p; out_g = v; out_b = t; break;
    case 3: out_r = p; out_g = q; out_b = v; break;
    case 4: out_r = t; out_g = p; out_b = v; break;
    case 5: default: out_r = v; out_g = p; out_b = q; break;
    }
}

bool fw_gui::item_hoverable(const ImRect& bb, ImGuiID id, ImGuiItemFlags item_flags)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;

#ifndef IMGUI_DISABLE_DEBUG_TOOLS
    if (id != 0 && g.HoveredIdPreviousFrame == id && (item_flags & ImGuiItemFlags_AllowDuplicateId) == 0)
    {
        g.HoveredIdPreviousFrameItemCount++;
        if (g.DebugDrawIdConflicts == id)
            window->DrawList->AddRect(bb.Min - ImVec2(1, 1), bb.Max + ImVec2(1, 1), IM_COL32(255, 0, 0, 255), 0.0f, ImDrawFlags_None, 2.0f);
    }
#endif

    if (g.HoveredWindow != window)
        return false;
    if (!IsMouseHoveringRect(bb.Min, bb.Max))
        return false;

    if (g.HoveredId != 0 && g.HoveredId != id && !g.HoveredIdAllowOverlap)
        return false;
    if (g.ActiveId != 0 && g.ActiveId != id && !g.ActiveIdAllowOverlap)
        if (!g.ActiveIdFromShortcut)
            return false;

    if (!(item_flags & ImGuiItemFlags_NoWindowHoverableCheck) && !IsWindowContentHoverable(window, ImGuiHoveredFlags_None))
    {
        g.HoveredIdIsDisabled = true;
        return false;
    }

    if (id != 0)
    {
        if (g.DragDropActive && g.DragDropPayload.SourceId == id && !(g.DragDropSourceFlags & ImGuiDragDropFlags_SourceNoDisableHover))
            return false;

        SetHoveredID(id);

        if (item_flags & ImGuiItemFlags_AllowOverlap)
        {
            g.HoveredIdAllowOverlap = true;
            if (g.HoveredIdPreviousFrame != id)
                return false;
        }

        if (id == g.LastItemData.ID && (g.LastItemData.StatusFlags & ImGuiItemStatusFlags_HasShortcut) && g.ActiveId != id)
            if (IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_DelayNormal))
                SetTooltip("%s", GetKeyChordName(g.LastItemData.Shortcut));
    }

    if (item_flags & ImGuiItemFlags_Disabled)
    {
        if (g.ActiveId == id && id != 0)
            ClearActiveID();
        g.HoveredIdIsDisabled = true;
        return false;
    }

#ifndef IMGUI_DISABLE_DEBUG_TOOLS
    if (id != 0)
    {
        if (g.DebugItemPickerActive && g.HoveredIdPreviousFrame == id)
            GetForegroundDrawList()->AddRect(bb.Min, bb.Max, IM_COL32(255, 255, 0, 255));
        if (g.DebugItemPickerBreakId == id)
            IM_DEBUG_BREAK();
    }
#endif

    if (g.NavDisableMouseHover)
        return false;

    return true;
}

bool fw_gui::is_item_hovered(ImGuiHoveredFlags flags)
{
    return IsItemHovered(flags);
}

bool fw_gui::is_item_active()
{
    ImGuiContext& g = *GImGui;
    if (g.ActiveId)
        return g.ActiveId == g.LastItemData.ID;
    return false;
}

bool fw_gui::is_item_clicked(mouse_button mouse_button)
{
    return mouse_clicked(mouse_button) && is_item_hovered(ImGuiHoveredFlags_None);
}

bool fw_gui::mouse_down(mouse_button button)
{
    ImGuiContext& g = *GImGui;
    IM_ASSERT(button >= 0 && button < IM_ARRAYSIZE(g.IO.MouseDown));
    return g.IO.MouseDown[button] && TestKeyOwner(MouseButtonToKey(button), ImGuiKeyOwner_Any);
}

bool fw_gui::mouse_clicked(mouse_button button, bool repeat)
{
    return IsMouseClicked(button, repeat ? ImGuiInputFlags_Repeat : ImGuiInputFlags_None, ImGuiKeyOwner_Any);
}

bool fw_gui::mouse_released(mouse_button button)
{
    ImGuiContext& g = *GImGui;
    IM_ASSERT(button >= 0 && button < IM_ARRAYSIZE(g.IO.MouseDown));
    return g.IO.MouseReleased[button] && TestKeyOwner(MouseButtonToKey(button), ImGuiKeyOwner_Any);
}

bool fw_gui::mouse_double_clicked(mouse_button button)
{
    ImGuiContext& g = *GImGui;
    IM_ASSERT(button >= 0 && button < IM_ARRAYSIZE(g.IO.MouseDown));
    return g.IO.MouseClickedCount[button] == 2 && TestKeyOwner(MouseButtonToKey(button), ImGuiKeyOwner_Any);
}

ImVec2 fw_gui::content_avail()
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImVec2 mx = (window->DC.CurrentColumns || g.CurrentTable) ? window->WorkRect.Max : window->ContentRegionRect.Max;
    return mx - window->DC.CursorPos;
}

ImVec2 fw_gui::content_max()
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImVec2 mx = (window->DC.CurrentColumns || g.CurrentTable) ? window->WorkRect.Max : window->ContentRegionRect.Max;
    return mx - window->Pos;
}

const char* fw_gui::text_end(const char* text)
{
    const char* text_display_end = text;
    const char* text_end = text + strlen(text);

    while (text_display_end < text_end && *text_display_end != '\0' && (text_display_end[0] != '#' || text_display_end[1] != '#'))
        text_display_end++;
    return text_display_end;
}

bool fw_gui::is_window_cond(ImGuiWindow* window, const std::vector<std::string>& names)
{
    if (window)
    {
        for (const auto& name : names)
        {
            if (strstr(window->Name, name.c_str()) != nullptr)
            {
                return true;
            }
        }
    }

    return false;
}

ImVec2 fw_gui::mouse_pos()
{
    ImGuiContext& g = *GImGui;
    return g.IO.MousePos;
}

void fw_gui::set_style()
{
    auto* style = &fvar->style;

    style->window_padding = SCALE(0, 0);
    style->item_spacing = SCALE(0, 0);
    style->window_border_size = 0.f;
    style->window_shadow_offset = SCALE(0, 0);
    style->window_shadow_size = 0.f;

}

void fw_gui::draw_decorations()
{
    c_window* window = fgui->get_window();

    c_vec2 pos = window->Pos;
    c_vec2 size = window->Size;
    c_rect rect = c_rect(pos, pos + size);

    c_draw_list* draw_list = fgui->background_drawlist();

    // Main background - very dark
    draw->rect_filled(draw_list, rect.Min, rect.Max, draw->get_clr(ImVec4(0.035f, 0.035f, 0.035f, 1.0f)), SCALE(felements->window.rounding)); // RGB(9,9,9)

    // Content area where childs are (below topbar)
    float topbar_height = SCALE(60.0f);
    ImVec2 content_start = ImVec2(pos.x, pos.y + topbar_height);
    ImVec2 content_end = ImVec2(pos.x + size.x, pos.y + size.y);

    // Draw animated background pattern
    widgets->draw_background_pattern(draw_list, content_start, content_end);

    // Topbar - slightly lighter
    c_rect topbar_rect = c_rect(pos, c_vec2(pos.x + size.x, pos.y + topbar_height));
    draw->rect_filled(draw_list, topbar_rect.Min, topbar_rect.Max, draw->get_clr(ImVec4(0.055f, 0.055f, 0.055f, 1.0f)), SCALE(felements->window.rounding), draw_flags_round_corners_top); // RGB(14,14,14)

    // Tab icons - Aimbot, Visuals, Misc, Settings, Lua
    const char* tab_icons[] = { ICON_V2_AIMBOT, ICON_V2_VISUALS, ICON_V2_MISC, ICON_V2_SETTINGS, ICON_FA_CODE };

    int num_tabs = 5;

    // Get tab font with merged icons
    ImFont* tab_font = font->get(main_font_data, 16.0f);

    // Background color - dark
    ImVec4 bg_color = ImVec4(0.055f, 0.055f, 0.055f, 1.0f); // RGB(14,14,14)
    ImVec4 accent_color = ImVec4(0.561f, 0.784f, 1.0f, 1.0f);  // Baby blue RGB(143,200,255)

    // Tab animation struct
    struct tab_anim_t
    {
        float alpha{ 0 };
        ImVec4 text{ ImVec4(0.6f, 0.6f, 0.6f, 1.0f) };
    };

    // Draw main tabs on the LEFT SIDE (vertical with background bar)
    static tab_anim_t tab_anims[5] = {};

    float tab_button_size = SCALE(50.0f);
    float tab_spacing = SCALE(12.0f);
    float tab_padding = SCALE(15.0f);
    float tab_bar_width = tab_button_size + (tab_padding * 2);
    float tab_bar_height = size.y - topbar_height;

    // Draw tab bar background on the left
    ImVec2 tab_bar_start = ImVec2(pos.x, pos.y + topbar_height);
    c_rect tab_bar_rect = c_rect(tab_bar_start, tab_bar_start + ImVec2(tab_bar_width, tab_bar_height));
    draw->rect_filled(draw_list, tab_bar_rect.Min, tab_bar_rect.Max, draw->get_clr(ImVec4(0.055f, 0.055f, 0.055f, 1.0f)), SCALE(8.0f), draw_flags_round_corners_bottom_left);

    // Calculate vertical centering for tabs (shifted up a bit)
    float total_tabs_height = (tab_button_size * num_tabs) + (tab_spacing * (num_tabs - 1));
    float vertical_offset = SCALE(-50.0f); // Shift up by 50px
    float start_y = pos.y + topbar_height + (tab_bar_height - total_tabs_height) * 0.5f + vertical_offset; // Center vertically but higher

    for (int i = 0; i < num_tabs; i++)
    {
        c_vec2 button_min = c_vec2(pos.x + tab_padding, start_y + (tab_button_size + tab_spacing) * i);
        c_vec2 button_max = c_vec2(button_min.x + tab_button_size, button_min.y + tab_button_size);
        c_rect button_rect = c_rect(button_min, button_max);

        tab_anim_t* anim = &tab_anims[i];

        bool is_active = (fvar->fgui.active_tab == i);

        // Hover detection
        ImVec2 mouse_pos = ImGui::GetMousePos();
        bool is_hovered = (mouse_pos.x >= button_min.x && mouse_pos.x <= button_max.x &&
                          mouse_pos.y >= button_min.y && mouse_pos.y <= button_max.y);

        if (is_hovered && fgui->mouse_clicked(mouse_button_left) && fgui->is_window_hovered(ImGuiHoveredFlags_None))
        {
            fvar->fgui.active_tab = i;
        }

        fgui->easing(anim->alpha, is_active ? 1.f : 0.f, 8.f, dynamic_easing);
        fgui->easing(anim->text, is_active ? ImVec4(0.0f, 0.0f, 0.0f, 1.0f) : ImVec4(0.616f, 0.616f, 0.624f, 1.0f), 16.f, dynamic_easing);

        // Only draw background when ACTIVE (transparent when inactive)
        if (anim->alpha > 0.01f)
        {
            // Draw accent circle when active
            ImVec2 circle_center = ImVec2(button_min.x + tab_button_size * 0.5f, button_min.y + tab_button_size * 0.5f);
            float circle_radius = tab_button_size * 0.5f;
            draw_list->AddCircleFilled(circle_center, circle_radius, draw->get_clr(accent_color, anim->alpha), 32);

            // Draw gradient shadow
            draw->rect_filled_multi_color(draw_list,
                button_rect.Min, button_rect.Max,
                draw->get_clr({ 0.f, 0.f, 0.f, 0.f }),
                draw->get_clr({ 0.f, 0.f, 0.f, 0.f }),
                draw->get_clr({ 0.f, 0.f, 0.f, 0.5f * anim->alpha }),
                draw->get_clr({ 0.f, 0.f, 0.f, 0.5f * anim->alpha }),
                SCALE(6.0f));
        }

        // Draw icon centered (always visible)
        ImVec2 icon_size = tab_font->CalcTextSizeA(tab_font->FontSize, FLT_MAX, 0.0f, tab_icons[i]);
        ImVec2 icon_pos = ImVec2(
            button_rect.Min.x + (tab_button_size - icon_size.x) * 0.5f,
            button_rect.Min.y + (tab_button_size - icon_size.y) * 0.5f
        );

        draw->text(draw_list, tab_font, tab_font->FontSize, icon_pos,
            draw->get_clr(anim->text), tab_icons[i], nullptr);
    }

    // ESP Preview Settings Icon (only visible in Visuals tab - tab 1)
    static bool esp_preview_open = false;

    struct preview_icon_anim_t
    {
        float alpha{ 0 };
        float pulse{ 0 };
        float rotation_time{ 0 };
    };

    static preview_icon_anim_t preview_icon_anim = {};

    if (fvar->fgui.active_tab == 1) // Visuals tab
    {
        float icon_size = SCALE(40.0f);
        float icon_padding = SCALE(15.0f);
        float topbar_center_y = pos.y + topbar_height * 0.5f;
        c_vec2 icon_min = c_vec2(pos.x + size.x - icon_padding - icon_size, topbar_center_y - icon_size * 0.5f);
        c_vec2 icon_max = c_vec2(icon_min.x + icon_size, icon_min.y + icon_size);
        c_rect icon_rect = c_rect(icon_min, icon_max);

        // Hover detection
        ImVec2 mouse_pos = ImGui::GetMousePos();
        bool is_hovered = (mouse_pos.x >= icon_min.x && mouse_pos.x <= icon_max.x &&
                          mouse_pos.y >= icon_min.y && mouse_pos.y <= icon_max.y);

        // Click handler
        if (is_hovered && fgui->mouse_clicked(mouse_button_left) && fgui->is_window_hovered(ImGuiHoveredFlags_None))
        {
            esp_preview_open = !esp_preview_open;
        }

        // Animate alpha when open
        fgui->easing(preview_icon_anim.alpha, esp_preview_open ? 1.f : 0.f, 10.f, static_easing);

        // Pulsing animation when open
        if (esp_preview_open)
        {
            preview_icon_anim.rotation_time += ImGui::GetIO().DeltaTime * 3.0f; // Speed
            preview_icon_anim.pulse = (sinf(preview_icon_anim.rotation_time) + 1.0f) * 0.5f; // 0.0 to 1.0
        }
        else
        {
            preview_icon_anim.rotation_time = 0.0f;
            preview_icon_anim.pulse = 0.0f;
        }

        // Draw icon background with pulsing size
        float pulse_scale = 1.0f + (preview_icon_anim.pulse * 0.1f); // Scale from 1.0 to 1.1
        float pulsed_size = icon_size * pulse_scale;
        float size_diff = (pulsed_size - icon_size) / 2.0f;

        c_vec2 pulsed_min = c_vec2(icon_min.x - size_diff, icon_min.y - size_diff);
        c_vec2 pulsed_max = c_vec2(icon_max.x + size_diff, icon_max.y + size_diff);
    }

    // ESP Preview Window
    if (esp_preview_open && fvar->fgui.active_tab == 1)
    {
        float preview_width = SCALE(350.0f);
        float preview_height = SCALE(500.0f);
        float window_spacing = SCALE(15.0f);

        ImVec2 preview_pos = ImVec2(pos.x + size.x + window_spacing, pos.y);
        ImVec2 preview_size = ImVec2(preview_width, preview_height);

        // Draw preview window background
        draw->rect_filled(draw_list, preview_pos, ImVec2(preview_pos.x + preview_size.x, preview_pos.y + preview_size.y),
            draw->get_clr(ImVec4(0.035f, 0.035f, 0.035f, 1.0f)), SCALE(8.0f)); // RGB(9,9,9)

        // Draw preview window border
        draw->rect(draw_list, preview_pos, ImVec2(preview_pos.x + preview_size.x, preview_pos.y + preview_size.y),
            draw->get_clr(ImVec4(0.176f, 0.176f, 0.176f, 0.4f)), SCALE(8.0f), 0, SCALE(1.5f)); // RGB(45,45,45)

        // Draw title bar
        float title_height = SCALE(40.0f);
        draw->rect_filled(draw_list, preview_pos, ImVec2(preview_pos.x + preview_size.x, preview_pos.y + title_height),
            draw->get_clr(ImVec4(0.055f, 0.055f, 0.055f, 1.0f)), SCALE(8.0f), draw_flags_round_corners_top); // RGB(14,14,14)

        // Draw title text
        draw->text_clipped(draw_list, tab_font,
            ImVec2(preview_pos.x + SCALE(15.0f), preview_pos.y),
            ImVec2(preview_pos.x + preview_size.x, preview_pos.y + title_height),
            draw->get_clr(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), "ESP Preview", nullptr, NULL, ImVec2(0.f, 0.5f));

        // Accent line under title
        draw->rect_filled(draw_list,
            ImVec2(preview_pos.x, preview_pos.y + title_height - SCALE(2.0f)),
            ImVec2(preview_pos.x + preview_size.x, preview_pos.y + title_height),
            draw->get_clr(ImVec4(0.561f, 0.784f, 1.0f, 0.3f)), 0); // Baby blue
    }

    // Content area below tabs
    fgui->set_screen_pos(ImVec2(pos.x + SCALE(12), pos.y + topbar_height + SCALE(12)), pos_all);

    // Tab transition animation
    struct tab_transition_anim_t
    {
        float fade_alpha{ 1.0f };
        float slide_offset{ 0.0f };
        int previous_tab{ 0 };
        bool transitioning{ false };
    };

    static tab_transition_anim_t tab_transition = {};

    // Detect tab change
    if (tab_transition.previous_tab != fvar->fgui.active_tab)
    {
        tab_transition.transitioning = true;
        tab_transition.fade_alpha = 0.0f;
        tab_transition.slide_offset = SCALE(30.0f);
        tab_transition.previous_tab = fvar->fgui.active_tab;
    }

    // Animate transition
    if (tab_transition.transitioning)
    {
        fgui->easing(tab_transition.fade_alpha, 1.0f, 10.f, dynamic_easing);
        fgui->easing(tab_transition.slide_offset, 0.0f, 12.f, dynamic_easing);

        // End transition when fully faded in
        if (tab_transition.fade_alpha > 0.98f && tab_transition.slide_offset < 1.0f)
        {
            tab_transition.transitioning = false;
        }
    }

    // Apply transition effects to content
    ImVec4 content_tint = ImVec4(1.0f, 1.0f, 1.0f, tab_transition.fade_alpha);
    ImVec4 slider_tint = ImVec4(0.561f, 0.784f, 1.0f, tab_transition.fade_alpha); // Baby blue

    // Push style for fade effect
    fgui->push_color(style_col_text, draw->get_clr(content_tint));
    fgui->push_color(style_col_slider_grab, draw->get_clr(slider_tint));

    // Store original cursor position for slide effect
    ImVec2 original_cursor = window->DC.CursorPos;

    // Render content based on active tab
    const char* tab_names[] = { "Aimbot", "Visuals", "Misc", "Settings", "Lua" };

    // Subtabs for each main tab
    const char* aimbot_subtabs[] = { "Silent Aim", "Triggerbot", "Backtrack" };
    const char* visuals_subtabs[] = { "ESP", "Chams", "Glow" };
    const char* misc_subtabs[] = { "Movement", "Exploits", "Other" };
    const char* settings_subtabs[] = { "Config", "Colors", "Keybinds" };
    const char* lua_subtabs[] = { "Scripts", "Editor", "Console" };

    struct subtab_state
    {
        int active_subtab{ 0 };
    };

    for (int tab = 0; tab < num_tabs; tab++)
    {
        if (fvar->fgui.active_tab == tab)
        {
            // Get subtab list for current tab
            const char** subtab_list = nullptr;
            int subtab_count = 0;

            if (tab == 0) { subtab_list = aimbot_subtabs; subtab_count = 3; }
            else if (tab == 1) { subtab_list = visuals_subtabs; subtab_count = 3; }
            else if (tab == 2) { subtab_list = misc_subtabs; subtab_count = 3; }
            else if (tab == 3) { subtab_list = settings_subtabs; subtab_count = 3; }
            else if (tab == 4) { subtab_list = lua_subtabs; subtab_count = 3; }

            subtab_state* subtab_st = fgui->anim_container<subtab_state>(fgui->get_id((std::string(tab_names[tab]) + "_subtab_state").c_str(), nullptr));

            // Subtab dimensions - NOW IN TOPBAR (horizontal)
            float subtab_button_width = SCALE(120.0f);
            float subtab_button_height = SCALE(35.0f);
            float subtab_spacing = SCALE(10.0f);
            float subtab_rounding = SCALE(6.0f);

            // Position subtabs in topbar (start after tab bar ends)
            float subtab_start_x = pos.x + tab_bar_width + SCALE(20.0f); // Start after tab bar with padding
            float subtab_start_y = pos.y + (topbar_height - subtab_button_height) * 0.5f;

            // Subtab animation struct
            struct subtab_anim_t
            {
                float alpha{ 0 };
                ImVec4 text{ ImVec4(0.6f, 0.6f, 0.6f, 1.0f) };
            };

            static subtab_anim_t subtab_anims[5][3] = {};  // Max 5 tabs, 3 subtabs each

            // Draw subtabs horizontally in topbar
            for (int i = 0; i < subtab_count; i++)
            {
                ImVec2 subtab_min = ImVec2(subtab_start_x + (subtab_button_width + subtab_spacing) * i, subtab_start_y);
                ImVec2 subtab_max = ImVec2(subtab_min.x + subtab_button_width, subtab_min.y + subtab_button_height);
                c_rect subtab_rect = c_rect(subtab_min, subtab_max);

                subtab_anim_t* anim = &subtab_anims[tab][i];

                bool is_active = (subtab_st->active_subtab == i);

                // Hover detection
                ImVec2 mouse_pos = ImGui::GetMousePos();
                bool is_hovered = (mouse_pos.x >= subtab_min.x && mouse_pos.x <= subtab_max.x &&
                                  mouse_pos.y >= subtab_min.y && mouse_pos.y <= subtab_max.y);

                // Click handler
                if (is_hovered && fgui->mouse_clicked(mouse_button_left) && fgui->is_window_hovered(ImGuiHoveredFlags_None))
                {
                    subtab_st->active_subtab = i;
                }

                // Animation
                fgui->easing(anim->alpha, is_active ? 1.f : 0.f, 8.f, dynamic_easing);
                fgui->easing(anim->text, is_active ? ImVec4(0.0f, 0.0f, 0.0f, 1.0f) : ImVec4(0.616f, 0.616f, 0.624f, 1.0f), 16.f, dynamic_easing);

                // Draw button background
                draw->rect_filled(draw_list, subtab_rect.Min, subtab_rect.Max, draw->get_clr(ImVec4(0.082f, 0.082f, 0.082f, 1.0f)), subtab_rounding);

                // Draw accent overlay
                draw->rect_filled(draw_list, subtab_rect.Min, subtab_rect.Max, draw->get_clr(ImVec4(0.561f, 0.784f, 1.0f, 1.0f), anim->alpha), subtab_rounding);

                // Draw gradient shadow
                draw->rect_filled_multi_color(draw_list,
                    subtab_rect.Min, subtab_rect.Max,
                    draw->get_clr({ 0.f, 0.f, 0.f, 0.f }),
                    draw->get_clr({ 0.f, 0.f, 0.f, 0.f }),
                    draw->get_clr({ 0.f, 0.f, 0.f, 0.5f * anim->alpha }),
                    draw->get_clr({ 0.f, 0.f, 0.f, 0.5f * anim->alpha }),
                    subtab_rounding);

                // Draw text centered
                ImVec2 text_size = tab_font->CalcTextSizeA(tab_font->FontSize, FLT_MAX, 0.0f, subtab_list[i]);
                ImVec2 text_pos = ImVec2(
                    subtab_rect.Min.x + (subtab_button_width - text_size.x) * 0.5f,
                    subtab_rect.Min.y + (subtab_button_height - text_size.y) * 0.5f
                );

                draw->text(draw_list, tab_font, tab_font->FontSize, text_pos,
                    draw->get_clr(anim->text), subtab_list[i], nullptr);
            }

            // Child state for dynamic sizing
            struct child_state
            {
                float height{ 100.0f };
                float width{ 0 };
            };

            // Calculate child width (2 childs side by side, with main tab bar on left)
            float content_start_x = pos.x + SCALE(12) + tab_bar_width + SCALE(12);
            float content_width = size.x - SCALE(24) - tab_bar_width - SCALE(12); // Remaining width minus tab bar
            float child_width = (content_width - SCALE(12)) / 2.0f; // Split in half with spacing
            float child_title_height = SCALE(30.0f);
            float child_rounding = SCALE(8.0f);
            float child_padding_x = SCALE(20.0f);
            float child_padding_y = SCALE(15.0f);

            // Left child
            ImVec2 left_child_start = ImVec2(content_start_x, pos.y + topbar_height + SCALE(12));
            child_state* left_anim = fgui->anim_container<child_state>(fgui->get_id((std::string(tab_names[tab]) + "_left").c_str(), nullptr));
            left_anim->width = child_width;

            // Draw left child header
            draw->rect_filled(window->DrawList, left_child_start,
                left_child_start + ImVec2(left_anim->width, child_title_height),
                draw->get_clr(ImVec4(0.055f, 0.055f, 0.055f, 1.0f)), child_rounding, draw_flags_round_corners_top); // RGB(14,14,14)

            // Subtle accent line under header
            draw->rect_filled(window->DrawList,
                ImVec2(left_child_start.x, left_child_start.y + child_title_height - SCALE(2.0f)),
                ImVec2(left_child_start.x + left_anim->width, left_child_start.y + child_title_height),
                draw->get_clr(ImVec4(0.561f, 0.784f, 1.0f, 0.3f)), 0); // Baby blue

            // Draw left child content background
            draw->rect_filled(window->DrawList,
                left_child_start + ImVec2(0, child_title_height),
                left_child_start + ImVec2(left_anim->width, child_title_height + left_anim->height),
                draw->get_clr(ImVec4(0.082f, 0.082f, 0.082f, 1.0f)), child_rounding, draw_flags_round_corners_bottom); // RGB(21,21,21)

            // Draw left child border
            draw->rect(window->DrawList, left_child_start,
                left_child_start + ImVec2(left_anim->width, child_title_height + left_anim->height),
                draw->get_clr(ImVec4(0.176f, 0.176f, 0.176f, 0.4f)), child_rounding, 0, SCALE(1.5f)); // RGB(45,45,45)

            // Draw left child title
            draw->text_clipped(window->DrawList, tab_font,
                left_child_start + SCALE(15, 0),
                left_child_start + ImVec2(left_anim->width, child_title_height),
                draw->get_clr(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), "Settings", nullptr, NULL, ImVec2(0.f, 0.5f));

            // Begin left child content
            fgui->set_screen_pos(left_child_start + ImVec2(0, child_title_height), pos_all);
            fgui->push_var(style_var_window_padding, ImVec2(child_padding_x, child_padding_y));
            fgui->begin_def_child((std::string(tab_names[tab]) + "_left_content").c_str(),
                ImVec2(left_anim->width, left_anim->height), 0,
                window_flags_always_use_window_padding | window_flags_no_move |
                window_flags_nav_flattened | window_flags_no_saved_settings |
                window_flags_no_scrollbar | window_flags_no_scroll_with_mouse);

            fgui->push_var(style_var_item_spacing, ImVec2(SCALE(12), SCALE(10)));

            // Test checkboxes
            static bool test_checkbox_1 = false;
            static bool test_checkbox_2 = true;
            static bool test_checkbox_3 = false;
            static float test_color[4] = { 0.35f, 0.2f, 0.5f, 1.0f };
            static int test_slider = 50;

            widgets->checkbox("Enable Feature 1", &test_checkbox_1);
            widgets->checkbox("Enable Feature 2", &test_checkbox_2);
            widgets->checkbox("Enable Feature 3", &test_checkbox_3);
            widgets->color_picker("Accent Color", test_color);
            widgets->slider_int("Value", &test_slider, 0, 100);

            left_anim->height = fgui->get_window()->ContentSize.y + (child_padding_y * 2.0f);

            fgui->pop_var();
            fgui->end_def_child();
            fgui->pop_var();

            // Right child
            ImVec2 right_child_start = ImVec2(content_start_x + child_width + SCALE(12), pos.y + topbar_height + SCALE(12));
            child_state* right_anim = fgui->anim_container<child_state>(fgui->get_id((std::string(tab_names[tab]) + "_right").c_str(), nullptr));
            right_anim->width = child_width;

            // Draw right child header
            draw->rect_filled(window->DrawList, right_child_start,
                right_child_start + ImVec2(right_anim->width, child_title_height),
                draw->get_clr(ImVec4(0.055f, 0.055f, 0.055f, 1.0f)), child_rounding, draw_flags_round_corners_top); // RGB(14,14,14)

            // Subtle accent line under header
            draw->rect_filled(window->DrawList,
                ImVec2(right_child_start.x, right_child_start.y + child_title_height - SCALE(2.0f)),
                ImVec2(right_child_start.x + right_anim->width, right_child_start.y + child_title_height),
                draw->get_clr(ImVec4(0.561f, 0.784f, 1.0f, 0.3f)), 0); // Baby blue

            // Draw right child content background
            draw->rect_filled(window->DrawList,
                right_child_start + ImVec2(0, child_title_height),
                right_child_start + ImVec2(right_anim->width, child_title_height + right_anim->height),
                draw->get_clr(ImVec4(0.082f, 0.082f, 0.082f, 1.0f)), child_rounding, draw_flags_round_corners_bottom); // RGB(21,21,21)

            // Draw right child border
            draw->rect(window->DrawList, right_child_start,
                right_child_start + ImVec2(right_anim->width, child_title_height + right_anim->height),
                draw->get_clr(ImVec4(0.176f, 0.176f, 0.176f, 0.4f)), child_rounding, 0, SCALE(1.5f)); // RGB(45,45,45)

            // Draw right child title
            draw->text_clipped(window->DrawList, tab_font,
                right_child_start + SCALE(15, 0),
                right_child_start + ImVec2(right_anim->width, child_title_height),
                draw->get_clr(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), "Advanced", nullptr, NULL, ImVec2(0.f, 0.5f));

            // Begin right child content
            fgui->set_screen_pos(right_child_start + ImVec2(0, child_title_height), pos_all);
            fgui->push_var(style_var_window_padding, ImVec2(child_padding_x, child_padding_y));
            fgui->begin_def_child((std::string(tab_names[tab]) + "_right_content").c_str(),
                ImVec2(right_anim->width, right_anim->height), 0,
                window_flags_always_use_window_padding | window_flags_no_move |
                window_flags_nav_flattened | window_flags_no_saved_settings |
                window_flags_no_scrollbar | window_flags_no_scroll_with_mouse);

            fgui->push_var(style_var_item_spacing, ImVec2(SCALE(12), SCALE(10)));

            // Test checkboxes
            static bool test_checkbox_4 = false;
            static bool test_checkbox_5 = false;
            static bool test_checkbox_6 = true;
            static float test_color2[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
            static int test_slider2 = 75;

            widgets->checkbox("Advanced Option 1", &test_checkbox_4);
            widgets->checkbox("Advanced Option 2", &test_checkbox_5);
            widgets->checkbox("Advanced Option 3", &test_checkbox_6);
            widgets->color_picker("Theme Color", test_color2);
            widgets->slider_int("Intensity", &test_slider2, 0, 100);

            right_anim->height = fgui->get_window()->ContentSize.y + (child_padding_y * 2.0f);

            fgui->pop_var();
            fgui->end_def_child();
            fgui->pop_var();

            break; // Only render active tab
        }
    }

    // Restore cursor position after content
    window->DC.CursorPos = original_cursor;

    // Pop color styles to restore original colors
    fgui->pop_color(2);
}

void fw_gui::initialize()
{
    boot_config();
    cfg->init_config();
    notify->setup_notify();
}

// Tooltip implementation
void c_widgets::render_tooltip(std::string_view text, bool hovered)
{
    if (text.empty())
        return;

    struct tooltip_anim_t
    {
        float alpha{ 0.f };
    };

    ImGuiWindow* window = fgui->get_window();
    if (!window)
        return;

    // Create unique ID for tooltip animation
    std::string tooltip_id = std::string(text) + "_tooltip";
    ImGuiID id = window->GetID(tooltip_id.c_str());

    tooltip_anim_t* anim = fgui->anim_container<tooltip_anim_t>(id);
    if (!anim)
        return;

    // Animate tooltip fade in/out
    fgui->easing(anim->alpha, hovered ? 1.0f : 0.0f, 15.f, static_easing);

    // Only render if visible
    if (anim->alpha < 0.01f)
        return;

    // Get mouse position
    ImVec2 mouse_pos = ImGui::GetMousePos();

    // Calculate text size
    ImFont* text_font = font->get(main_font_data, 13.0f);
    if (!text_font)
        return;

    ImVec2 text_size = fgui->text_size(text_font, text.data(), fgui->text_end(text.data()));

    // Tooltip dimensions
    ImVec2 tooltip_padding = SCALE(ImVec2(8.0f, 6.0f));
    ImVec2 tooltip_size = ImVec2(text_size.x + tooltip_padding.x * 2.0f, text_size.y + tooltip_padding.y * 2.0f);
    float tooltip_offset = SCALE(10.0f);

    // Position tooltip above and slightly to the right of cursor
    ImVec2 tooltip_pos = ImVec2(mouse_pos.x + tooltip_offset, mouse_pos.y - tooltip_size.y - tooltip_offset);

    // Make sure tooltip stays on screen
    ImVec2 screen_size = ImGui::GetIO().DisplaySize;
    if (tooltip_pos.x + tooltip_size.x > screen_size.x)
        tooltip_pos.x = screen_size.x - tooltip_size.x - SCALE(10.0f);
    if (tooltip_pos.x < 0)
        tooltip_pos.x = SCALE(10.0f);
    if (tooltip_pos.y < 0)
        tooltip_pos.y = mouse_pos.y + tooltip_offset;

    // Draw on foreground (always on top)
    ImDrawList* fg_draw_list = ImGui::GetForegroundDrawList();

    float shadow_blur = SCALE(12.0f);
    for (int s = 0; s < 6; s++)
    {
        float progress = (float)s / 6.0f;
        float shadow_alpha = (1.0f - progress) * 0.25f * anim->alpha;
        float blur_amount = shadow_blur * progress;

        fg_draw_list->AddRectFilled(
            ImVec2(tooltip_pos.x - blur_amount, tooltip_pos.y - blur_amount),
            ImVec2(tooltip_pos.x + tooltip_size.x + blur_amount, tooltip_pos.y + tooltip_size.y + blur_amount),
            IM_COL32(0, 0, 0, (int)(shadow_alpha * 255)),
            SCALE(4.0f) + blur_amount
        );
    }

    fg_draw_list->PushClipRect(tooltip_pos, ImVec2(tooltip_pos.x + tooltip_size.x, tooltip_pos.y + tooltip_size.y), true);

    ImVec4 bg_top = ImVec4(66.0f / 255.0f, 68.0f / 255.0f, 72.0f / 255.0f, 0.62f * anim->alpha);
    ImVec4 bg_bottom = ImVec4(46.0f / 255.0f, 48.0f / 255.0f, 52.0f / 255.0f, 0.62f * anim->alpha);

    fg_draw_list->AddRectFilledMultiColor(
        tooltip_pos,
        ImVec2(tooltip_pos.x + tooltip_size.x, tooltip_pos.y + tooltip_size.y),
        draw->get_clr(bg_top),
        draw->get_clr(bg_top),
        draw->get_clr(bg_bottom),
        draw->get_clr(bg_bottom)
    );

    for (int b = 0; b < 10; b++)
    {
        float blur_inset = SCALE(b * 0.6f);
        float blur_alpha = 0.06f * anim->alpha;

        float brightness = (b % 2 == 0) ? 35.0f : 10.0f;
        ImVec4 blur_color = ImVec4(brightness / 255.0f, brightness / 255.0f, (brightness + 4.0f) / 255.0f, blur_alpha);

        fg_draw_list->AddRectFilled(
            ImVec2(tooltip_pos.x + blur_inset, tooltip_pos.y + blur_inset),
            ImVec2(tooltip_pos.x + tooltip_size.x - blur_inset, tooltip_pos.y + tooltip_size.y - blur_inset),
            draw->get_clr(blur_color),
            ImMax(SCALE(4.0f) - blur_inset, 0.0f)
        );
    }

    fg_draw_list->PopClipRect();

    // Subtle border
    ImVec4 border_color = ImVec4(143.0f / 255.0f, 200.0f / 255.0f, 255.0f / 255.0f, 0.15f * anim->alpha);
    fg_draw_list->AddRect(
        tooltip_pos,
        ImVec2(tooltip_pos.x + tooltip_size.x, tooltip_pos.y + tooltip_size.y),
        draw->get_clr(border_color),
        SCALE(4.0f),
        0,
        SCALE(1.0f)
    );

    // White text
    ImVec2 text_pos = ImVec2(
        tooltip_pos.x + tooltip_padding.x,
        tooltip_pos.y + tooltip_padding.y
    );

    draw->text(fg_draw_list, text_font, 13.0f, text_pos,
        draw->get_clr(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), anim->alpha), text.data());
}
