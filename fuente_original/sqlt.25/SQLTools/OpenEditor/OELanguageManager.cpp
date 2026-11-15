/* 
    Copyright (C) 2002 Aleksey Kochetov

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

#include "stdafx.h"
#include "OpenEditor/OELanguageManager.h"
#include "OpenEditor/OECommonLanguageSupport.h"
#include "OpenEditor/OEPlsSqlSupport.h"
#include "OpenEditor/OESmartIndent.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace OpenEditor
{
HighlighterBasePtr LanguageManager::CreateHighlighter (const string& lang)
{
    return HighlighterFactory::CreateHighlighter(lang);
}

LanguageSupportKind LanguageManager::GetLanguageSupportKind (const string& lang, const Settings& settings)
{
    // Keyword navigation is the gate rather than a detail: with it off there is no
    // consumer for a syntax tree - COEDocument::OnIdle returns FALSE immediately - so
    // building a PlSqlSupport would start a background parse nobody ever reads.
    // Toggling it is therefore a KIND change, which is what makes Storage replace the
    // installed object instead of forwarding to it.
    if (lang == "PL/SQL" && settings.GetPlSqlKeywordNavigation())
        return lskPlSql;

    return lskCommon;
}

LanguageSupportPtr LanguageManager::CreateLanguageSupport (const string& lang, Storage* pstorage)
{
    // pstorage is tested first rather than folded into GetLanguageSupportKind because
    // the settings live ON the storage: with no storage there is nothing to ask. The
    // caller still gets a usable CommonLanguageSupport, which is the pre-existing null
    // tolerance of this function, preserved deliberately.
    if (pstorage && GetLanguageSupportKind(lang, pstorage->GetSettings()) == lskPlSql)
        return LanguageSupportPtr(new PlSqlSupport(pstorage));

    return LanguageSupportPtr(new CommonLanguageSupport(pstorage));
}

const SmartIndent* LanguageManager::GetSmartIndent (const string& lang)
{
    if (lang == "PL/SQL")
    {
        static PlSqlSmartIndent plsql;
        return &plsql;
    }

    return NULL;
}

};//namespace OpenEditor
