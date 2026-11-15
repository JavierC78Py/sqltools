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

#ifndef __PropTreePageHelpers_h__
#define __PropTreePageHelpers_h__

#include "PropTree/PropTree.h"
#include "PropTree/PropTreeItemEdit.h"
#include "PropTree/PropTreeItemStatic.h"
#include "PropTree/PropTreeItemCombo.h"
#include "PropTree/PropTreeItemColor.h"
#include "Common/PropTreeItemFolder.h"
#include "Common/PropTreeItemComboOnSelect.h"

namespace Common {
namespace PropTreeRows {

	inline CPropTreeItem* InsertGroup (CPropTree& tree, LPCTSTR label, LPCTSTR info,
		BOOL expanded, CPropTreeItem* parent = NULL)
	{
		CPropTreeItem* p = tree.InsertItem(new CPropTreeItem(), parent);
		p->SetLabelText(label);
		p->SetInfoText(info);

		if (expanded)
			p->Expand();

		return p;
	}

	inline CPropTreeItemStatic* InsertCheckedStatic (CPropTree& tree, CPropTreeItem* parent,
		LPCTSTR label, LPCTSTR info, bool checked)
	{
		CPropTreeItemStatic* p = (CPropTreeItemStatic*)tree.InsertItem(new CPropTreeItemStatic(), parent);
		p->SetLabelText(label);
		p->SetInfoText(info);
		p->HasCheckBox(TRUE);
		p->Check(checked ? TRUE : FALSE);
		return p;
	}
	
	inline CPropTreeItemStatic* InsertRadio (CPropTree& tree, CPropTreeItem* parent,
		LPCTSTR label, LPCTSTR info, bool checked)
	{
		CPropTreeItemStatic* p = (CPropTreeItemStatic*)tree.InsertItem(new CPropTreeItemStatic(), parent);
		p->SetLabelText(label);
		p->SetInfoText(info);
		p->HasRadioButton(TRUE);

		if (checked)
			p->SelectRadio(); // this one on, the siblings already inserted off
		else
			p->Check(FALSE); // a fresh CPropTreeItem is unchecked, but say so anyway

		return p;
	}


	inline CPropTreeItemEdit* InsertCheckedEdit (CPropTree& tree, CPropTreeItem* parent,
		LPCTSTR label, LPCTSTR info, bool checked, int value)
	{
		CPropTreeItemEdit* p = (CPropTreeItemEdit*)tree.InsertItem(new CPropTreeItemEdit(), parent);
		p->SetLabelText(label);
		p->SetInfoText(info);
		p->HasCheckBox(TRUE);
		p->Check(checked ? TRUE : FALSE);
		p->SetValueFormat(CPropTreeItemEdit::ValueFormatInteger);
		p->SetItemValue(value);
		return p;
	}

	inline CPropTreeItemEdit* InsertEdit (CPropTree& tree, CPropTreeItem* parent,
		LPCTSTR label, LPCTSTR info, int value)
	{
		CPropTreeItemEdit* p = (CPropTreeItemEdit*)tree.InsertItem(new CPropTreeItemEdit(), parent);
		p->SetLabelText(label);
		p->SetInfoText(info);
		p->SetValueFormat(CPropTreeItemEdit::ValueFormatInteger);
		p->SetItemValue(value);
		return p;
	}

	inline CPropTreeItemEdit* InsertEditText (CPropTree& tree, CPropTreeItem* parent,
		LPCTSTR label, LPCTSTR info, LPCTSTR text, int textLimit = 0)
	{
		CPropTreeItemEdit* p = (CPropTreeItemEdit*)tree.InsertItem(new CPropTreeItemEdit(), parent);
		p->SetLabelText(label);
		p->SetInfoText(info);
		p->SetItemValue(text);          // selects ValueFormatText
		if (textLimit > 0)
			p->SetTextLimit(textLimit);
		return p;
	}

	inline CPropTreeItemEdit* InsertNumberEdit (CPropTree& tree, CPropTreeItem* parent,
		LPCTSTR label, LPCTSTR info, int value, int spinStep, int lower, int upper)
	{
		CPropTreeItemEdit* p = InsertEdit(tree, parent, label, info, value);
		p->SetSpinRange(lower, upper);
		p->SetSpinStep(spinStep);
		p->SetItemLimit(lower, upper);
		return p;
	}

	inline CPropTreeItemCombo* InsertComboText (CPropTree& tree, CPropTreeItem* parent,
		LPCTSTR label, LPCTSTR info, const LPCTSTR* items, int itemCount, LPCTSTR text,
		DWORD style = WS_CHILD | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL)
	{
		CPropTreeItemCombo* p = (CPropTreeItemCombo*)tree.InsertItem(new CPropTreeItemCombo(), parent);
		p->SetLabelText(label);
		p->SetInfoText(info);
		p->CreateComboBox(style);

		for (int i = 0; i < itemCount; i++)
			p->AddString(items[i]);

		p->SetItemValueText(text);
		return p;
	}

	inline CPropTreeItemCombo* InsertComboIndex (CPropTree& tree, CPropTreeItem* parent,
		LPCTSTR label, LPCTSTR info, const LPCTSTR* items, int itemCount, int selected,
		DWORD style = WS_CHILD | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_AUTOHSCROLL)
	{
		CPropTreeItemCombo* p = (CPropTreeItemCombo*)tree.InsertItem(new CPropTreeItemCombo(), parent);
		p->SetLabelText(label);
		p->SetInfoText(info);
		p->CreateComboBox(style);

		for (int i = 0; i < itemCount; i++)
			p->SetItemData(p->AddString(items[i]), (LPARAM)i);

		if (selected < 0 || selected >= itemCount)
			selected = 0;

		p->SetItemValue((LPARAM)selected);
		return p;
	}

	inline CPropTreeItemCombo* InsertComboIndexOnSelect (CPropTree& tree, CPropTreeItem* parent,
		LPCTSTR label, LPCTSTR info, const LPCTSTR* items, int itemCount, int selected,
		DWORD style = WS_CHILD | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_AUTOHSCROLL)
	{
		CPropTreeItemCombo* p = (CPropTreeItemCombo*)tree.InsertItem(
			new Common::CPropTreeItemComboOnSelect(), parent);
		p->SetLabelText(label);
		p->SetInfoText(info);
		p->CreateComboBox(style);

		for (int i = 0; i < itemCount; i++)
			p->SetItemData(p->AddString(items[i]), (LPARAM)i);

		if (selected < 0 || selected >= itemCount)
			selected = 0;

		p->SetItemValue((LPARAM)selected);
		return p;
	}

	inline CPropTreeItemColor* InsertColor (CPropTree& tree, CPropTreeItem* parent,
		LPCTSTR label, LPCTSTR info, COLORREF colour)
	{
		CPropTreeItemColor* p = (CPropTreeItemColor*)tree.InsertItem(new CPropTreeItemColor(), parent);
		p->SetLabelText(label);
		p->SetInfoText(info);
		p->SetItemValue(colour);
		return p;
	}

	inline CPropTreeItemFolder* InsertFolder (CPropTree& tree, CPropTreeItem* parent,
		LPCTSTR label, LPCTSTR info, LPCTSTR caption, LPCTSTR emptyText, LPCTSTR folder)
	{
		CPropTreeItemFolder* p = (CPropTreeItemFolder*)tree.InsertItem(
			new Common::CPropTreeItemFolder(caption, emptyText), parent);
		p->SetLabelText(label);
		p->SetInfoText(info);
		p->SetFolder(folder);
		return p;
	}

}}

//{{AFX_INSERT_LOCATION}}

#endif//__PropTreePageHelpers_h__
