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

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __OEPropTreePageHelpers_h__
#define __OEPropTreePageHelpers_h__

#include "Common/PropTreePageHelpers.h"

// The row builders of the CPropTree based ("new style") settings pages moved to
// Common\PropTreePageHelpers.h: they only use CPropTree types and are used by
// pages outside OpenEditor too (Dialogs\CPropServerScriptPageTree).
//
// This header is kept as a shim so the existing pages - and the code snippets
// of md/proptree_settings_page_conversion.md - keep compiling with their
// historical OEPropTreePage:: names. New pages should use
// Common/PropTreePageHelpers.h directly.

namespace OEPropTreePage
{
	using Common::PropTreeRows::InsertGroup;
	using Common::PropTreeRows::InsertCheckedStatic;
	using Common::PropTreeRows::InsertCheckedEdit;
	using Common::PropTreeRows::InsertEdit;
	using Common::PropTreeRows::InsertEditText;
	using Common::PropTreeRows::InsertNumberEdit;
	using Common::PropTreeRows::InsertComboText;
	using Common::PropTreeRows::InsertComboIndex;
}

//{{AFX_INSERT_LOCATION}}

#endif//__OEPropTreePageHelpers_h__
