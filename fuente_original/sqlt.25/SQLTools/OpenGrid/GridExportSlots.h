#ifndef __GridExportSlots_h__
#define __GridExportSlots_h__

// Per-destination export settings, shared by GridView and GridWnd so the slot rules exist once
// instead of once per host window.

// GridTextFormat.h rather than GridSourceBase.h: the helpers need only ETextFormat and
// IsBinaryExportFormat, and GridSourceBase.h would drag in AGridSource.h and its MFC parameters.
#include "GridTextFormat.h"

class SQLToolsSettings;

namespace OG2
{
    // Clipboard and Editor hold text only; File is the one destination a worksheet can go to, which is
    // why it needs a slot of its own rather than sharing the clipboard's format.
    enum EExportSlot { eslotClipboard, eslotEditor, eslotFile };

    // Constexpr and free of both settings and MFC, so callers can assert at build time. Both go
    // through IsBinaryExportFormat - never a second list of formats.
    constexpr int clampFormatForSlot (EExportSlot slot, int format)
    {
        return (eslotFile != slot && IsBinaryExportFormat(format)) ? int(etfQuoteDelimited) : format;
    }

    constexpr bool isFormatOffered (EExportSlot slot, int format)
    {
        return eslotFile == slot || !IsBinaryExportFormat(format);
    }

    // The global set with one slot's three values written over GridExpFormat / GridExpWithHeader /
    // GridExpJsonPretty, so every existing consumer reads the slot without knowing slots exist.
    SQLToolsSettings exportSettingsFor (EExportSlot slot);

    // The inverse: persist those three fields out of an edited object into the slot.
    void storeExportSlot (EExportSlot slot, const SQLToolsSettings& edited);

    // The slot to property mapping on its own, so the settings page can seed and save its three rows
    // per slot without a fourth copy of the switch the two functions above are built on.
    int  slotFormat     (const SQLToolsSettings& settings, EExportSlot slot);
    bool slotWithHeader (const SQLToolsSettings& settings, EExportSlot slot);
    bool slotJsonPretty (const SQLToolsSettings& settings, EExportSlot slot);

    void setSlotFormat     (SQLToolsSettings& settings, EExportSlot slot, int format, bool notify);
    void setSlotWithHeader (SQLToolsSettings& settings, EExportSlot slot, bool value, bool notify);
    void setSlotJsonPretty (SQLToolsSettings& settings, EExportSlot slot, bool value, bool notify);
}

#endif // __GridExportSlots_h__
