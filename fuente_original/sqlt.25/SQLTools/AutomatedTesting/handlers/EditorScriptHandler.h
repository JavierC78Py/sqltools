#pragma once
#include <string>
#include "AutomatedTesting/TestController.h"

// Test command 3 (STP_CMD_TEST_EDITOR_SCRIPT) - a scripted editor-behaviour
// harness. The payload is a small line-oriented *script*, not text under test as
// it is for SqlFormatterHandler: the handler opens one isolated worksheet,
// replays the script against the real editor input path (WM_CHAR / WM_KEYDOWN /
// WM_COMMAND on the live COEditorView) and returns a deterministic transcript in
// which every assertion carries its own PASS/FAIL verdict. The PowerShell side
// (Tests\lib\SqlEditorScriptTestLib.ps1) simply baselines that transcript,
// exactly as the format_*.ps1 suites baseline formatted SQL, so a behaviour
// change is a baseline diff and never a silent pass.
//
// Nothing in here knows which feature is under test. Every directive and every
// assertion is an editor primitive - text, caret, selection, block mode,
// overtype, a keystroke, a command, undo/redo, a pinned global setting - so a
// new suite is a new .stp file plus a .good baseline, not a new handler. The one
// feature-specific thing is the *content* of the g_settings table in the .cpp,
// which lists the global settings a script may pin; adding a switch there is one
// row. Auto-close/auto-surround
// (md/Editor Auto-closing and Auto-surround.md, section 7.1 / implementation
// step 7) is the first consumer, and AUTO_CLOSE.stp is its script.
//
// The script grammar is documented at the top of EditorScriptHandler.cpp.
//
// No database connection is required: unlike ColumnAutocompleteHandler an editor
// script is purely an editor concern, so the handler deliberately has no session
// precondition.
class EditorScriptHandler : public ITestCommandHandler
{
public:
    bool Execute(const std::wstring& payload, std::wstring& out) override;
};
