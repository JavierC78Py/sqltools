/* 
    Copyright (C) 2005 Aleksey Kochetov

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
*/

// TODO: error handling

#include "stdafx.h"
#include <sstream>
#include <algorithm>
#include "TinyXml/TinyXml.h"
#include "COMMON/StrHelpers.h"
#include "COMMON/ExceptionHelper.h"
#include "COMMON/AppUtilities.h"
#include "OpenEditor/OELanguageXmlStreamer.h"
#include "Common/MyUtf.h"
#include "Common/TiXmlUtils.h"

#ifdef _AFX
#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif
#endif//_AFX

namespace OpenEditor
{
    using namespace Common;

    static const int MAJOR_VERSION = 1; // indicates incompatibility 
    static const int MINOR_VERSION = 2; // any minor changes

    // write helper procedures
    inline void writeXml (TiXmlElement* elem, const char* name, int val) { 
        elem->SetAttribute(name, val); 
    }
    inline void writeXml (TiXmlElement* elem, const char* name, bool val) { 
        elem->SetAttribute(name, val); 
    }
    inline void writeXml (TiXmlElement* elem, const char* name, const string& val) { 
        elem->SetAttribute(name, val.c_str()); 
    }
    template <class TCollection> 
    void writeXml (TiXmlElement* parentElem, const char* elemName, TCollection collection) {
        TCollection::const_iterator it = collection.begin();
        for (; it != collection.end(); ++it) {
            std::unique_ptr<TiXmlElement> elem(new TiXmlElement(elemName));
            writeXml(elem.get(), "value", *it);
            parentElem->LinkEndChild(elem.release());
        }
    }
    // read helper procedures
    inline void readXml (const TiXmlElement* elem, const char* name, int& val) {
        if (elem->QueryIntAttribute(name, &val) != TIXML_SUCCESS)
            val = 0;
    }
    inline void readXml (const TiXmlElement* elem, const char* name, bool& val) {
        int _val;
        val = (elem->QueryIntAttribute(name, &_val) == TIXML_SUCCESS) ? _val : false;
    }
    inline void readXml (const TiXmlElement* elem, const char* name, string& val) {
        if (const char* _val = elem->Attribute(name))
            val = _val;
    }
    inline void readXml (const TiXmlElement* parentElem, const char* elemName, set<string>& collection) {
        const TiXmlElement* elem = parentElem->FirstChildElement(elemName);
        for (; elem; elem = elem->NextSiblingElement(elemName))
            if (const char* _val = elem->Attribute("value"))
                collection.insert(_val);
    }
    inline void readXml (const TiXmlElement* parentElem, const char* elemName, vector<string>& collection) {
        const TiXmlElement* elem = parentElem->FirstChildElement(elemName);
        for (; elem; elem = elem->NextSiblingElement(elemName))
            if (const char* _val = elem->Attribute("value"))
                collection.push_back(_val);
    }


/////////////////////////////////////////////////////////////////////////////
// LanguageXmlStreamer

void LanguageXmlStreamer::operator << (const LanguagesCollection&)
{
    TiXmlDocument doc;
    TiXmlDeclaration decl("1.0", ""/*"Windows-1252"*/, "yes");
    doc.InsertEndChild(decl);

    std::unique_ptr<TiXmlElement> rootElem(new TiXmlElement("OpenEditor"));
    std::unique_ptr<TiXmlElement> langsElem(new TiXmlElement("Languages"));
    writeXml(langsElem.get(), "MajorVersion", MAJOR_VERSION);
    writeXml(langsElem.get(), "MinorVersion", MINOR_VERSION);
    
    std::vector<LanguagePtr>::const_iterator it = LanguagesCollection::m_Languages.begin();
    for (; it != LanguagesCollection::m_Languages.end(); ++it)
    {
        std::unique_ptr<TiXmlElement> langElem(new TiXmlElement("Language"));
        write(**it, langElem.get());
        langsElem->LinkEndChild(langElem.release());
    }

    rootElem->LinkEndChild(langsElem.release());
    doc.LinkEndChild(rootElem.release());

    if (!TiXmlUtils::SaveFile(doc, m_filename.c_str()))
        throw Common::AppException("Cannot save \"" + Common::str(m_filename) + "\"\nERROR: " + doc.ErrorDesc());
}

void LanguageXmlStreamer::write (const Language& lang, TiXmlElement* langElem)
{
    writeXml(langElem, "Name",              lang.m_name);
    writeXml(langElem, "Delimiters",        lang.m_delimiters);
    writeXml(langElem, "CaseSensiteve",     lang.m_caseSensiteve);
    writeXml(langElem, "CommentPairFirst",  lang.m_commentPair.first );
    writeXml(langElem, "CommentPairLast",   lang.m_commentPair.second);
    writeXml(langElem, "EndLineComment",    lang.m_endLineComment);
    writeXml(langElem, "EndLineCommentCol", lang.m_endLineCommentCol);
    writeXml(langElem, "EscapeChar",        lang.m_escapeChar);
    writeXml(langElem, "StringPairFirst",   lang.m_stringPair.first );
    writeXml(langElem, "StringPairLast",    lang.m_stringPair.second);
    writeXml(langElem, "CharPairFirst",     lang.m_charPair.first );
    writeXml(langElem, "CharPairLast",      lang.m_charPair.second);
    writeXml(langElem, "AlternativeQuotes", lang.m_alternativeQuotes);

    std::unique_ptr<TiXmlElement> comElem(new TiXmlElement("StartLineComments"));
    writeXml(comElem.get(), "StartLineComment", lang.m_startLineComment);
    langElem->LinkEndChild(comElem.release());

    std::unique_ptr<TiXmlElement> setsElem(new TiXmlElement("MatchBraceSets"));
    vector<vector<string> >::const_iterator it = lang.m_matchBraces.begin();
    for (; it != lang.m_matchBraces.end(); ++it)
    {
        std::unique_ptr<TiXmlElement> setElem(new TiXmlElement("MatchBraceSet"));
        writeXml(setElem.get(), "Brace", *it);
        setsElem->LinkEndChild(setElem.release());
    }
    langElem->LinkEndChild(setsElem.release());

    write(lang.m_LanguageKeywordMap, lang.m_keywordGroups, langElem);
}


void LanguageXmlStreamer::write (const LanguageKeywordMapPtr& keywordMap, const vector<string>& groups, TiXmlElement* langElem)
{
    std::unique_ptr<TiXmlElement> keywordGroupsElem(new TiXmlElement("KeywordGroups"));

    std::vector<string>::const_iterator groupIt = groups.begin();
    for (int groupIndex(0); groupIt != groups.end(); ++groupIt, groupIndex++)
    {
        std::unique_ptr<TiXmlElement> keywordGroupElem(new TiXmlElement("KeywordGroup"));
        writeXml(keywordGroupElem.get(), "Name", *groupIt);

        std::ostringstream out;
        LanguageKeywordMapConstIterator it = keywordMap->begin();
        for (int keywordIndex = 0; it != keywordMap->end(); ++it)
            if (groupIndex == it->second.groupIndex)
            {
                if (keywordIndex) out << ' ';
                out << Common::str(it->second.keyword);
                keywordIndex++;
            }

        std::unique_ptr<TiXmlText> text(new TiXmlText(out.str().c_str()));
        keywordGroupElem->LinkEndChild(text.release());
        
        keywordGroupsElem->LinkEndChild(keywordGroupElem.release());
    }

    langElem->LinkEndChild(keywordGroupsElem.release());
}

void LanguageXmlStreamer::operator >> (LanguagesCollection&)
{
    LanguagesCollection::m_Languages.clear();
    m_upgrading = false;

    TiXmlDocument doc;
    bool fileExisted = ::PathFileExists(m_filename.c_str()) != FALSE;

    if (fileExisted)
    {
        if (!TiXmlUtils::LoadFile(doc, m_filename.c_str()))
            THROW_APP_EXCEPTION("Cannot parse \"" + Common::str(m_filename) + "\"\nERROR: " + doc.ErrorDesc());
    }
    else // no problem - will load settings from resources
    {
        string xmlText;
        if (!AppLoadTextFromResources(::PathFindFileName(m_filename.c_str()), RT_HTML, xmlText) || xmlText.empty())
            THROW_APP_EXCEPTION("Cannot load language settings from resources");

        if (!doc.Parse(xmlText.c_str()))
            THROW_APP_EXCEPTION(string("Cannot parse language settings\nERROR: ") + doc.ErrorDesc());
    }

    if (TiXmlNode* rootElem = doc.FirstChildElement("OpenEditor"))
    {
        if (TiXmlElement* langsElem = rootElem->FirstChildElement("Languages"))
        {
            int majorVer = -1;
            readXml(langsElem, "MajorVersion", majorVer);
            if (majorVer != MAJOR_VERSION)
                throw Common::AppException("Cannot load \""  + Common::str(m_filename) + "\".\nERROR: Unsupported languages version.");

            // readXml yields 0 when the attribute is missing, so a file old enough not
            // to carry MinorVersion at all is treated as the oldest version too.
            int minorVer = 0;
            readXml(langsElem, "MinorVersion", minorVer);

            // MinorVersion 1 files were written before the AlternativeQuotes attribute
            // existed, so read() has to supply the default itself. Gate on the declared
            // version rather than on the attribute's absence, which cannot tell "this
            // is an old file" apart from "this is a new file for a language that has no
            // opinion on the matter".
            m_upgrading = minorVer < MINOR_VERSION;

            TiXmlElement* langElem = langsElem->FirstChildElement("Language");
            for (; langElem; langElem = langElem->NextSiblingElement("Language"))
            {
                LanguagePtr lang(new Language);
                read(langElem, *lang);
                LanguagesCollection::m_Languages.push_back(lang);
            }

            // Upgrade in place and rewrite the very same DOM, following
            // TemplateXmlStreamer::operator >>. That keeps everything in the user's file
            // that the Language model does not round-trip, instead of regenerating the
            // file from the model. It is also idempotent: once stamped with
            // MINOR_VERSION, m_upgrading is false on every later load.
            //
            // When the file did not exist the caller writes it from the model via
            // operator <<, which stamps MINOR_VERSION itself, so there is nothing to
            // upgrade here and saving the resource DOM would only be overwritten.
            if (m_upgrading && fileExisted)
            {
                writeXml(langsElem, "MajorVersion", MAJOR_VERSION);
                writeXml(langsElem, "MinorVersion", MINOR_VERSION);

                if (!TiXmlUtils::SaveFile(doc, m_filename.c_str()))
                    THROW_APP_EXCEPTION("Cannot save \"" + Common::str(m_filename) + "\"\nERROR: " + doc.ErrorDesc());
            }
        }
    }
}

void LanguageXmlStreamer::read (TiXmlElement* langElem, Language& lang)
{
    readXml(langElem, "Name",              lang.m_name);
    readXml(langElem, "Delimiters",        lang.m_delimiters);
    readXml(langElem, "CaseSensiteve",     lang.m_caseSensiteve);
    readXml(langElem, "CommentPairFirst",  lang.m_commentPair.first );
    readXml(langElem, "CommentPairLast",   lang.m_commentPair.second);
    readXml(langElem, "EndLineComment",    lang.m_endLineComment);
    readXml(langElem, "EndLineCommentCol", lang.m_endLineCommentCol);
    readXml(langElem, "EscapeChar",        lang.m_escapeChar);
    readXml(langElem, "StringPairFirst",   lang.m_stringPair.first );
    readXml(langElem, "StringPairLast",    lang.m_stringPair.second);
    readXml(langElem, "CharPairFirst",     lang.m_charPair.first );
    readXml(langElem, "CharPairLast",      lang.m_charPair.second);
    readXml(langElem, "AlternativeQuotes", lang.m_alternativeQuotes);

    lang.m_startLineComment.clear();
    if (const TiXmlElement* comElem = langElem->FirstChildElement("StartLineComments"))
        readXml(comElem, "StartLineComment", lang.m_startLineComment);

    lang.m_matchBraces.clear();
    if (const TiXmlElement* setsElem = langElem->FirstChildElement("MatchBraceSets"))
    {
        const TiXmlElement* setElem = setsElem->FirstChildElement("MatchBraceSet");
        for (; setElem; setElem = setElem->NextSiblingElement("MatchBraceSet"))
        {
            lang.m_matchBraces.push_back(vector<string>());
            readXml(setElem, "Brace", *lang.m_matchBraces.rbegin());
        }
    }

    read(langElem, lang.m_LanguageKeywordMap, lang.m_keywordGroups, lang.m_caseSensiteve);

    if (m_upgrading)
        upgradeKeywords(lang, langElem);

    if (lang.GetName() == "PL/SQL")
    {
        vector<string>::const_iterator it = lang.m_keywordGroups.begin();
        for (; it != lang.m_keywordGroups.end(); ++it)
            if (*it == "User Objects")
                break;

        if (it == lang.m_keywordGroups.end())
            lang.m_keywordGroups.push_back("User Objects");

        {
        string::size_type pos = lang.m_delimiters.find('@');
        if (pos != string::npos)
            lang.m_delimiters.erase(pos, 1);
        }

        // Alternative quoting is intrinsic to PL/SQL, but a languages.xml written by an
        // earlier version does not carry the attribute at all and readXml defaults it to
        // false. Turn it on so that an existing installation gets q'[...]' support.
        //
        // m_upgrading is what makes this a one-time migration rather than a permanent
        // default: operator >> sets it only while walking a file whose MinorVersion
        // predates the attribute, so a MinorVersion 2 file is taken at face value and a
        // deliberate AlternativeQuotes="0" is honoured. Within an upgrade the attribute
        // test still matters, because it separates "never written" from "written as 0".
        // The enclosing "PL/SQL" name test is the secondary flag that keeps the other
        // languages, which already had the old escaping behaviour, unchanged.
        //
        // Write the resolved value back into the DOM so the re-saved file records it
        // explicitly and stays put if the language is ever renamed. The other languages
        // are left unwritten, which keeps the diff to the user's file down to the entries
        // that actually changed; readXml defaults an absent attribute to false, so they
        // load identically either way.
        if (m_upgrading && langElem->Attribute("AlternativeQuotes") == nullptr)
        {
            lang.m_alternativeQuotes = true;
            writeXml(langElem, "AlternativeQuotes", lang.m_alternativeQuotes);
        }
    }
    else if (lang.GetName() == "XML")
    {
        // 2018-01-04 bug fix, broken XML highlighting after "<!-- anything-->"
        if (lang.m_delimiters.find('-') == string::npos)
            lang.m_delimiters += '-';
    }
}

void LanguageXmlStreamer::upgradeKeywords (Language& lang, TiXmlElement* langElem)
{
    // Loaded once per process and cached, since this runs once per language on a
    // single upgrade pass and the resource content never changes at runtime.
    static TiXmlDocument resourceDoc;
    static bool resourceLoaded = false;
    if (!resourceLoaded)
    {
        resourceLoaded = true;
        string xmlText;
        if (AppLoadTextFromResources(::PathFindFileName(m_filename.c_str()), RT_HTML, xmlText) && !xmlText.empty())
            resourceDoc.Parse(xmlText.c_str());
    }

    TiXmlNode* rootElem = resourceDoc.FirstChildElement("OpenEditor");
    TiXmlElement* langsElem = rootElem ? rootElem->FirstChildElement("Languages") : 0;
    if (!langsElem)
        return;

    const TiXmlElement* resLangElem = langsElem->FirstChildElement("Language");
    for (; resLangElem; resLangElem = resLangElem->NextSiblingElement("Language"))
    {
        const char* name = resLangElem->Attribute("Name");
        if (name && lang.GetName() == name)
            break;
    }
    if (!resLangElem)
        return; // no matching language shipped in resources, nothing to merge

    LanguageKeywordMapPtr resKeywordMap(new LanguageKeywordMap);
    vector<string> resKeywordGroups;
    read(resLangElem, resKeywordMap, resKeywordGroups, lang.m_caseSensiteve);

    bool changed = false;

    // INSERTING/UPDATING/DELETING were mistakenly shipped under "SYS Packages" in
    // older resource builds - they are PL/SQL trigger predicates, not packages.
    // Remove them from the user's existing "SYS Packages" group wherever they were
    // already carried over from that bug; they belong to (and, for an already
    // up-to-date resource, already are present in) "PL/SQL Keywords".
    {
        int sysPackagesIndex = -1;
        for (size_t i = 0; i < lang.m_keywordGroups.size(); ++i)
            if (lang.m_keywordGroups[i] == "SYS Packages")
            {
                sysPackagesIndex = static_cast<int>(i);
                break;
            }

        if (sysPackagesIndex != -1)
        {
            static const wchar_t* misplaced[] = { L"INSERTING", L"UPDATING", L"DELETING" };
            for (auto kit = lang.m_LanguageKeywordMap->begin(); kit != lang.m_LanguageKeywordMap->end(); )
            {
                bool remove = kit->second.groupIndex == sysPackagesIndex &&
                    std::find(std::begin(misplaced), std::end(misplaced), kit->second.keyword) != std::end(misplaced);
                if (remove)
                {
                    kit = lang.m_LanguageKeywordMap->erase(kit);
                    changed = true;
                }
                else
                    ++kit;
            }
        }
    }

    // Add groups the user's file does not have yet, preserving the resource order
    // after the user's existing groups.
    for (vector<string>::const_iterator git = resKeywordGroups.begin(); git != resKeywordGroups.end(); ++git)
    {
        if (std::find(lang.m_keywordGroups.begin(), lang.m_keywordGroups.end(), *git) == lang.m_keywordGroups.end())
        {
            lang.m_keywordGroups.push_back(*git);
            changed = true;
        }
    }

    // Add keywords missing from the user's map, attaching each to the (possibly
    // just-added) group of the same name so newly introduced keywords in an
    // existing group land in the right place.
    for (LanguageKeywordMapConstIterator kit = resKeywordMap->begin(); kit != resKeywordMap->end(); ++kit)
    {
        if (lang.m_LanguageKeywordMap->find(kit->first) != lang.m_LanguageKeywordMap->end())
            continue;

        const string& groupName = resKeywordGroups[kit->second.groupIndex];

        vector<string>::iterator git = std::find(lang.m_keywordGroups.begin(), lang.m_keywordGroups.end(), groupName);
        if (git == lang.m_keywordGroups.end())
            continue; // should not happen - groups were merged above

        int groupIndex = static_cast<int>(git - lang.m_keywordGroups.begin());
        lang.m_LanguageKeywordMap->insert(
            pair<std::wstring, LanguageKeyword>(kit->first, LanguageKeyword(kit->second.keyword, groupIndex)));
        changed = true;
    }

    if (changed)
    {
        // Rewrite this language's KeywordGroups DOM in place so the merged keywords
        // are part of what gets saved by the MinorVersion-bump save-back in
        // operator >>, rather than requiring a separate save path.
        if (TiXmlElement* oldKeywordGroupsElem = langElem->FirstChildElement("KeywordGroups"))
            langElem->RemoveChild(oldKeywordGroupsElem);

        write(lang.m_LanguageKeywordMap, lang.m_keywordGroups, langElem);
    }
}

void LanguageXmlStreamer::read (const TiXmlElement* langElem, LanguageKeywordMapPtr& keywordMap, vector<string>& keywordGroups, bool caseSensiteve)
{
    keywordGroups.clear();
    keywordMap->clear();

    if (const TiXmlElement* keywordGroupsElem = langElem->FirstChildElement("KeywordGroups"))
    {
        const TiXmlElement* keywordGroupElem = keywordGroupsElem->FirstChildElement("KeywordGroup");
        for (; keywordGroupElem; keywordGroupElem = keywordGroupElem->NextSiblingElement("KeywordGroup"))
        {
            // Register the group name unconditionally, even when the group has no
            // text child (an empty group, or one that only ever had keywords that
            // were all later removed). Without this an empty <KeywordGroup Name="X">
            // is silently dropped on the next load because it has no TiXmlText child
            // to detect, which would also shift every later group's index and
            // corrupt the group<->keyword association.
            int groupIndex = keywordGroups.size();
            keywordGroups.push_back(keywordGroupElem->Attribute("Name"));

            if (const TiXmlNode* textNode = keywordGroupElem->FirstChild())
            {
                if (textNode->Type() == TiXmlNode::TINYXML_TEXT)
                {
                    std::string buffer = textNode->Value();
                    std::string::iterator it = buffer.begin();
                    for (; it != buffer.end(); ++it)
                        switch (*it) {
                        case '\n': 
                        case '\r': 
                            *it = ' ';
                        }

                    std::istringstream in(buffer);

                    string keywordStr;
                    while (getline(in, keywordStr, ' '))
                    {
                        trim_symmetric(keywordStr);
                        if (!keywordStr.empty())
                        {
                            std::wstring keyword = Common::wstr(keywordStr);
                            std::wstring key = keyword;

                            if (!caseSensiteve)
                                for (auto it2 = key.begin(); it2 != key.end(); ++it2)
                                    *it2 = towupper(*it2);

                            if (keywordMap->find(key) == keywordMap->end())
                                keywordMap->insert(pair<std::wstring, LanguageKeyword>(key, LanguageKeyword(keyword, groupIndex)));
                            else
                                TRACE1("\tWARNING: skip duplicated keyword \"%s\"\n", key.c_str());
                        }
                    }
                }
            }
        }
    }
}

};//namespace OpenEditor

