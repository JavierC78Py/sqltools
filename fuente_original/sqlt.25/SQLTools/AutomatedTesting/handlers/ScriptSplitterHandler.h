#pragma once
#include <string>
#include "AutomatedTesting/TestController.h"

// Test command 4 (STP_CMD_TEST_SCRIPT_SPLITTER) - a script-splitting harness.
//
// The payload is the script text under test, exactly as for SqlFormatterHandler,
// and the result is a deterministic transcript of how the worksheet splitter cut
// it up: one numbered block per statement the splitter would hand to the
// database, with the 1-based line span it came from, followed by any messages the
// splitter emitted. The PowerShell side (Tests\lib\SqlFormatterTestLib.ps1, via
// Invoke-FormatterTest -ProxyMethod TestScriptSplitter) baselines that transcript
// just as the format_*.ps1 suites baseline formatted SQL, so a splitting change is
// a baseline diff and never a silent pass.
//
// This drives the real production path rather than a reimplementation of it:
// CommandParser -> SimplePlsSqlParser -> ScriptAnalyser / PlSqlAnalyser, with a
// CommandPerformer that records instead of executing. So the transcript is the
// same statement list a worksheet execution would produce, minus the database.
// That is what makes it a useful oracle for anything that changes where a
// statement boundary falls - Oracle alternative quoting (q'X...X'), the
// SQL*Plus command set, block-terminator handling.
//
// No database connection is required: the recorder never touches OCI, and no
// SQL*Plus command in the fixtures needs a session, so the handler deliberately
// has no session precondition (unlike ColumnAutocompleteHandler).
class ScriptSplitterHandler : public ITestCommandHandler
{
public:
    bool Execute(const std::wstring& payload, std::wstring& out) override;
};
