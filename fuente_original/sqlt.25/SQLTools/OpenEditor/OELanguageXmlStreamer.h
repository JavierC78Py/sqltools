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

#pragma once
#include "OpenEditor/OELanguage.h"

    class TiXmlElement;                                                 

namespace OpenEditor
{
    ///////////////////////////////////////////////////////////////////////////
    // LanguageXmlStreamer
    ///////////////////////////////////////////////////////////////////////////

    class LanguageXmlStreamer
    {
    public:
        LanguageXmlStreamer (const std::wstring& filename) : m_upgrading(false), m_filename(filename) {}

        void operator >> (LanguagesCollection&);
        void operator << (const LanguagesCollection&);

    private:
        // Takes a non-const element because operator >> upgrades a pre-MINOR_VERSION
        // file in place and re-saves the same DOM - the same way
        // TemplateXmlStreamer::operator >> does it - so the migration must be able to
        // write the attribute it defaulted back into the document.
        void read  (TiXmlElement*, Language&);
        void read  (const TiXmlElement*, LanguageKeywordMapPtr&, vector<string>&, bool caseSensiteve);

        void write (const Language&, TiXmlElement*);
        void write (const LanguageKeywordMapPtr&, const vector<string>&, TiXmlElement*);

        // Called only while upgrading a pre-existing user file (m_upgrading &&
        // fileExisted). Looks up the language of the same Name in the languages.xml
        // shipped in resources and merges in any keyword/group present there but
        // missing from the user's file, so keyword lists gain new additions (e.g. new
        // SQL functions) across upgrades instead of staying frozen at whatever the
        // file contained when it was first written. Existing user keywords/groups,
        // including any the user added themselves, are left untouched. Rewrites the
        // language's KeywordGroups DOM in place so the change is picked up by the
        // same save-back path used for the MinorVersion bump.
        void upgradeKeywords (Language&, TiXmlElement*);

        // Set by operator >> while it walks a file whose MinorVersion predates the
        // AlternativeQuotes attribute, i.e. MinorVersion 1. Per-language migration is
        // gated on this, so a file that already declares the attribute is never
        // second-guessed no matter what the language is called.
        bool m_upgrading;

        const std::wstring m_filename;
    };

};//namespace OpenEditor

