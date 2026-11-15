/* 
	SQLTools is a tool for Oracle database developers and DBAs.
    Copyright (C) 1997-2015 Aleksey Kochetov

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

#ifndef _EXTRACTSCHEMADLG_H_
#define _EXTRACTSCHEMADLG_H_
#pragma once

#include "MetaDictionary/MetaSettings.h"
#include "MetaDictionary/MetaDictionary.h"
#include "Utilities/ExtractSchema/ExtractDDLSettings.h"
#include "Utilities/ExtractSchema/ExtractSchemaMainPage.h"
#include "Utilities/ExtractSchema/ExtractSchemaFilterPage.h"
#include "Utilities/ExtractSchema/ExtractSchemaOptionPage.h"

class CExtractSchemaDlg : public CPropertySheet
{
public:
    ExtractDDLSettings       m_DDLSettings;
    CExtractSchemaMainPage   m_MainPage;
    CExtractSchemaFilterPage m_FilterPage;
    CExtractSchemaOptionPage m_OptionPage;

    CExtractSchemaDlg (CWnd* pParentWnd = NULL);

public:
	virtual BOOL OnInitDialog();
	virtual void OnOK();

protected:
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
#endif//_EXTRACTSCHEMADLG_H_
