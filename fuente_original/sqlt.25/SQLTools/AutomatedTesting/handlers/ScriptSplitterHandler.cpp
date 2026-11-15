#include "stdafx.h"
#include "ScriptSplitterHandler.h"
#include "SQLWorksheet/CommandParser.h"
#include "Common/MyUtf.h"
#include <exception>
#include <sstream>
#include <string>
#include <vector>

// ----------------------------------------------------------------------------
// A CommandPerformer that records instead of executing.
//
// CommandParser *is* the production splitter, and the only way to observe where
// it puts a statement boundary is to be the thing it calls back on, so this
// stands in for the worksheet. Every method is inert except DoExecuteSql and
// DoMessage - the two observable outputs of a split. Nothing here reaches OCI,
// which is why the handler needs no session.
class SplitterRecorder : public CommandPerformer
{
public:
    struct Statement
    {
        std::string text;
        int firstLine;  // 1-based, inclusive
        int lastLine;   // 1-based, inclusive
    };

    std::vector<Statement>   m_statements;
    std::vector<std::string> m_messages;

    void DoExecuteSql (CommandParser& parser, const string& sql, const vector<string>&) override
    {
        Statement s;
        s.text = sql;

        // CommandParser counts lines from 0 (Start(0, 0)); the transcript is
        // 1-based because that is how an editor shows them.
        int first(0), last(0);
        if (parser.GetLastExecutedLines(first, last))
        {
            s.firstLine = first + 1;
            s.lastLine  = last + 1;
        }
        else
            s.firstLine = s.lastLine = -1;

        m_statements.push_back(s);
    }

    void DoMessage    (CommandParser&, const string& msg) override { m_messages.push_back(msg); }
    void DoNothing    (CommandParser&) override {}
    bool InputValue   (CommandParser&, const string&, string& value) override { value.clear(); return false; }
    void GoTo         (CommandParser&, int) override {}
    void DoConnect    (CommandParser&, const string&) override {}
    void DoDisconnect (CommandParser&) override {}
    void AddBind      (CommandParser&, const string&, EBindVariableTypes, ub2) override {}
    void PrintBind    (CommandParser&, const string&) override {}
};

bool ScriptSplitterHandler::Execute(const std::wstring& payload, std::wstring& out)
{
    out.clear();

    // Narrow, exactly as the worksheet narrows editor text before feeding the
    // splitter (SQLWorksheetDoc.cpp does Common::str(wbuff) for the same call).
    const std::string text = Common::str(payload);

    // CommandParser::PutLine takes one line without its terminator and counts
    // lines itself, so split here the way the worksheet does. Normalising CRLF
    // and bare CR to a single boundary keeps the transcript identical whichever
    // line ending the fixture was checked out with.
    std::vector<std::string> lines;
    std::string cur;
    for (size_t i = 0; i < text.size(); i++)
    {
        const char c = text[i];

        if (c == '\n' || c == '\r')
        {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                i++;
            lines.push_back(cur);
            cur.clear();
        }
        else
            cur += c;
    }
    // A trailing newline ends the last line rather than starting an empty extra
    // one; a final line with no newline at all is real content and is fed.
    if (!cur.empty())
        lines.push_back(cur);

    SplitterRecorder recorder;
    std::string error;

    try
    {
        // substitution off, so '&' stays literal and the handler can never block
        // on InputValue; ignoreCommentsAfterSemicolon off, the default worksheet
        // behaviour the splitter is judged on.
        CommandParser parser(recorder, false /*substitution*/,
                                    false /*ignoreCommmentsAfterSemicolon*/);
        parser.Start(0, 0);

        for (size_t i = 0; i < lines.size(); i++)
            parser.PutLine(lines[i].c_str(), static_cast<int>(lines[i].size()));

        parser.PutEOF();
    }
    catch (const std::exception& e)
    {
        // Recorded rather than propagated: a splitter that throws on a fixture is
        // itself the finding, and it must show up as a baseline diff instead of
        // killing the COM call and leaving the suite with an empty result.
        error = e.what();
    }

    std::ostringstream os;
    os << "statements: " << recorder.m_statements.size() << "\n";
    os << "messages: "   << recorder.m_messages.size()   << "\n";

    for (size_t i = 0; i < recorder.m_statements.size(); i++)
    {
        const SplitterRecorder::Statement& s = recorder.m_statements[i];
        os << "--- statement " << (i + 1)
           << " (lines " << s.firstLine << ".." << s.lastLine << ") ---\n"
           << s.text << "\n";
    }

    for (size_t i = 0; i < recorder.m_messages.size(); i++)
        os << "--- message " << (i + 1) << " ---\n" << recorder.m_messages[i] << "\n";

    if (!error.empty())
        os << "--- exception ---\n" << error << "\n";

    out = Common::wstr(os.str());
    return true;
}
