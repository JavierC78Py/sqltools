#include "stdafx.h"
#include "GridExportSlots.h"

#include "App/SQLTools.h" // GetSQLToolsSettings / GetSQLToolsSettingsForUpdate

namespace OG2
{
    int slotFormat (const SQLToolsSettings& settings, EExportSlot slot)
    {
        switch (slot)
        {
        case eslotClipboard: return settings.GetGridExpClipboardFormat();
        case eslotEditor:    return settings.GetGridExpEditorFormat();
        case eslotFile:
        default:             return settings.GetGridExpFileFormat();
        }
    }

    bool slotWithHeader (const SQLToolsSettings& settings, EExportSlot slot)
    {
        switch (slot)
        {
        case eslotClipboard: return settings.GetGridExpClipboardWithHeader();
        case eslotEditor:    return settings.GetGridExpEditorWithHeader();
        case eslotFile:
        default:             return settings.GetGridExpFileWithHeader();
        }
    }

    bool slotJsonPretty (const SQLToolsSettings& settings, EExportSlot slot)
    {
        switch (slot)
        {
        case eslotClipboard: return settings.GetGridExpClipboardJsonPretty();
        case eslotEditor:    return settings.GetGridExpEditorJsonPretty();
        case eslotFile:
        default:             return settings.GetGridExpFileJsonPretty();
        }
    }

    void setSlotFormat (SQLToolsSettings& settings, EExportSlot slot, int format, bool notify)
    {
        switch (slot)
        {
        case eslotClipboard: settings.SetGridExpClipboardFormat(format, notify); break;
        case eslotEditor:    settings.SetGridExpEditorFormat   (format, notify); break;
        case eslotFile:
        default:             settings.SetGridExpFileFormat     (format, notify); break;
        }
    }

    void setSlotWithHeader (SQLToolsSettings& settings, EExportSlot slot, bool value, bool notify)
    {
        switch (slot)
        {
        case eslotClipboard: settings.SetGridExpClipboardWithHeader(value, notify); break;
        case eslotEditor:    settings.SetGridExpEditorWithHeader   (value, notify); break;
        case eslotFile:
        default:             settings.SetGridExpFileWithHeader     (value, notify); break;
        }
    }

    void setSlotJsonPretty (SQLToolsSettings& settings, EExportSlot slot, bool value, bool notify)
    {
        switch (slot)
        {
        case eslotClipboard: settings.SetGridExpClipboardJsonPretty(value, notify); break;
        case eslotEditor:    settings.SetGridExpEditorJsonPretty   (value, notify); break;
        case eslotFile:
        default:             settings.SetGridExpFileJsonPretty     (value, notify); break;
        }
    }

    SQLToolsSettings exportSettingsFor (EExportSlot slot)
    {
        SQLToolsSettings settings = GetSQLToolsSettings();

        settings.SetGridExpFormat    (clampFormatForSlot(slot, slotFormat(settings, slot)), false);
        settings.SetGridExpWithHeader(slotWithHeader(settings, slot), false);
        settings.SetGridExpJsonPretty(slotJsonPretty(settings, slot), false);

        return settings;
    }

    void storeExportSlot (EExportSlot slot, const SQLToolsSettings& edited)
    {
        SQLToolsSettings& settings = GetSQLToolsSettingsForUpdate();

        setSlotFormat    (settings, slot, clampFormatForSlot(slot, edited.GetGridExpFormat()), false);
        setSlotWithHeader(settings, slot, edited.GetGridExpWithHeader(), false);
        setSlotJsonPretty(settings, slot, edited.GetGridExpJsonPretty(), false);
    }

}//namespace OG2
