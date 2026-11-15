# Menús y atajos — SQLTools Mac vs SQLTools (Windows)

Generado automáticamente desde la propia aplicación (`SQLToolsMac --dump-commands` + `tools/gen_commands_doc.py`). **No editar a mano.**

**Comandos de los menús de SQLTools: 235. Implementados: 226. Pendientes: 9.**

Reglas de traducción de atajos: Ctrl de Windows → **Cmd (⌘)**; Alt → **Opción (⌥)**. Los que chocan con el sistema (Cmd+Q salir, Cmd+H ocultar, Cmd+M minimizar, Cmd+W cerrar, Cmd+Espacio Spotlight) usan la tecla **Control (⌃)**. Los acordes (`Ctrl+K,Q`) se escriben igual: pulsar el primero y luego la segunda tecla. Un atajo solo está activo si el comando está implementado.

✅ = implementado · ⏳ = pendiente (aparece en el menú desactivado)

## File  (27/30)

| | Submenú | Comando | Windows | Mac | ID |
|---|---|---|---|---|---|
| ✅ |  | Close | Ctrl+F4 | ⌘F4 | `ID_FILE_CLOSE` |
| ✅ |  | Close All | Alt+Shift+F4 | ⌥⇧F4 | `ID_FILE_CLOSE_ALL` |
| ✅ |  | Copy File Location | — | — | `ID_FILE_COPY_LOCATION` |
| ✅ |  | Exit | Alt+F4 | ⌥F4 | `ID_APP_EXIT` |
| ✅ |  | Move / Rename | — | — | `ID_FILE_RENAME` |
| ✅ |  | New | Ctrl+N | ⌘N | `ID_FILE_NEW` |
| ✅ |  | New ... | Ctrl+Shift+N | ⇧⌘N | `ID_FILE_NEW_EXT` |
| ✅ |  | Open... | Ctrl+O | ⌘O | `ID_FILE_OPEN` |
| ✅ |  | Page Setup... | — | — | `ID_EDIT_PRINT_PAGE_SETUP` |
| ✅ |  | Print Preview | Ctrl+Shift+P | ⇧⌘P | `ID_FILE_PRINT_PREVIEW` |
| ✅ |  | Print Setup... | — | — | `ID_FILE_PRINT_SETUP` |
| ✅ |  | Print... | Ctrl+P | ⌘P | `ID_FILE_PRINT` |
| ✅ |  | Properties... | Ctrl+Q,F | ⌃Q, F | `ID_EDIT_FILE_SETTINGS` |
| ✅ |  | Reload | Ctrl+Q,R | ⌃Q, R | `ID_FILE_RELOAD` |
| ✅ |  | Save | Ctrl+S | ⌘S | `ID_FILE_SAVE` |
| ✅ |  | Save All | Alt+Shift+S | ⌥⇧S | `ID_FILE_SAVE_ALL` |
| ✅ |  | Save As... | Ctrl+Shift+S | ⇧⌘S | `ID_FILE_SAVE_AS` |
| ✅ |  | Send Mail | — | — | `ID_FILE_SEND_MAIL` |
| ✅ |  | Show File Location | Ctrl+Q,O | ⌃Q, O | `ID_FILE_SYNC_LOCATION` |
| ⏳ | Recent Files | Recent File | — | — | `ID_FILE_MRU_FILE1` |
| ⏳ | Recent Quick Snapshots | Recent Quick Snapshot | — | — | `ID_WORKSPACE_QUICK_MRU_FIRST` |
| ⏳ | Recent Workspaces | Recent Workspace | — | — | `ID_WORKSPACE_MRU_FILE1` |
| ✅ | Snapshot | Copy | Ctrl+Shift+W,C | ⇧⌘W, C | `ID_WORKSPACE_COPY` |
| ✅ | Snapshot | Open AS/QS... | Ctrl+Shift+W,T | ⇧⌘W, T | `ID_WORKSPACE_OPEN_AUTOSAVED` |
| ✅ | Snapshot | Paste | Ctrl+Shift+W,V | ⇧⌘W, V | `ID_WORKSPACE_PASTE` |
| ✅ | Snapshot | Quick Save | Ctrl+Shift+W,Q | ⇧⌘W, Q | `ID_WORKSPACE_SAVE_QUICK` |
| ✅ | Workspace | Close | Ctrl+Shift+W,L | ⇧⌘W, L | `ID_WORKSPACE_CLOSE` |
| ✅ | Workspace | Open... | Ctrl+Shift+W,O | ⇧⌘W, O | `ID_WORKSPACE_OPEN` |
| ✅ | Workspace | Save | Ctrl+Shift+W,S | ⇧⌘W, S | `ID_WORKSPACE_SAVE` |
| ✅ | Workspace | Save As... | Ctrl+Shift+W,A | ⇧⌘W, A | `ID_WORKSPACE_SAVE_AS` |

## Edit  (21/21)

| | Submenú | Comando | Windows | Mac | ID |
|---|---|---|---|---|---|
| ✅ |  | Copy | Ctrl+C | Ctrl+Insert | ⌘C | ⌘Ins | `ID_EDIT_COPY` |
| ✅ |  | Copy Append | Ctrl+Shift+C | Ctrl+Shift+Insert | ⇧⌘C | ⇧⌘Ins | `ID_EDIT_COPY_N_APPEND` |
| ✅ |  | Copy Bookmarked Lines | Ctrl+K,C | ⌘K, C | `ID_EDIT_COPY_BOOKMARKED` |
| ✅ |  | Cut | Ctrl+X | Shift+Delete | ⌘X | ⇧⌦ | `ID_EDIT_CUT` |
| ✅ |  | Cut Append | Ctrl+Shift+X | Ctrl+Shift+Delete | ⇧⌘X | ⇧⌘⌦ | `ID_EDIT_CUT_N_APPEND` |
| ✅ |  | Cut Bookmarked Lines | Ctrl+K,X | ⌘K, X | `ID_EDIT_CUT_BOOKMARKED` |
| ✅ |  | Delete | — | — | `ID_EDIT_DELETE` |
| ✅ |  | Delete Bookmarked Lines | Ctrl+K,D | ⌘K, D | `ID_EDIT_DELETE_BOOKMARKED` |
| ✅ |  | Delete Line | Ctrl+Y | ⌘Y | `ID_EDIT_DELETE_LINE` |
| ✅ |  | Delete Word Back | Ctrl+Backspace | ⌘⌫ | `ID_EDIT_DELETE_WORD_TO_LEFT` |
| ✅ |  | Delete to End of Word | Ctrl+Delete | ⌘⌦ | `ID_EDIT_DELETE_WORD_TO_RIGHT` |
| ✅ |  | Duplicate Line or Selection | Ctrl+K,Q | ⌘K, Q | `ID_EDIT_DUP_LINE_OR_SELECTION` |
| ✅ |  | Paste | Ctrl+V | Shift+Insert | ⌘V | ⇧Ins | `ID_EDIT_PASTE` |
| ✅ |  | Redo | Ctrl+R | ⌘R | `ID_EDIT_REDO` |
| ✅ |  | Scroll Down | Ctrl+Down | ⌘↓ | `ID_EDIT_SCROLL_DOWN` |
| ✅ |  | Scroll To Center | Num5 | — | `ID_EDIT_SCROLL_CENTER` |
| ✅ |  | Scroll Up | Ctrl+Up | ⌘↑ | `ID_EDIT_SCROLL_UP` |
| ✅ |  | Select All | Ctrl+A | ⌘A | `ID_EDIT_SELECT_ALL` |
| ✅ |  | Select Line | Ctrl+L | ⌘L | `ID_EDIT_SELECT_LINE` |
| ✅ |  | Select Word | Ctrl+W | ⌃W | `ID_EDIT_SELECT_WORD` |
| ✅ |  | Undo | Alt+Backspace | Ctrl+Z | ⌥⌫ | ⌘Z | `ID_EDIT_UNDO` |

## View  (13/13)

| | Submenú | Comando | Windows | Mac | ID |
|---|---|---|---|---|---|
| ✅ |  | Code Outline | Alt+4 | ⌥4 | `ID_VIEW_CODE_OUTLINE` |
| ✅ |  | Connection Toolbar | — | — | `ID_VIEW_CONNECT_TOOLBAR` |
| ✅ |  | File Panel | Alt+0 | ⌥0 | `ID_VIEW_FILE_PANEL` |
| ✅ |  | File Toolbar | — | — | `ID_VIEW_FILE_TOOLBAR` |
| ✅ |  | Find in Files Output | Alt+7 | ⌥7 | `ID_FILE_SHOW_GREP_OUTPUT` |
| ✅ |  | History | Alt+8 | ⌥8 | `ID_VIEW_HISTORY` |
| ✅ |  | Object List | Alt+3 | ⌥3 | `ID_SQL_DB_SOURCE` |
| ✅ |  | Object Viewer | Alt+1 | ⌥1 | `ID_SQL_OBJ_VIEWER` |
| ✅ |  | Open Files | Alt+9 | ⌥9 | `ID_VIEW_OPEN_FILES` |
| ✅ |  | Properties | Alt+2 | ⌥2 | `ID_VIEW_PROPERTIES` |
| ✅ |  | SQL Toolbar | — | — | `ID_VIEW_SQL_TOOLBAR` |
| ✅ |  | Snippets | Alt+5 | ⌥5 | `ID_VIEW_SNIPPETS` |
| ✅ |  | Status Bar | — | — | `ID_VIEW_STATUS_BAR` |

## Search  (44/44)

| | Submenú | Comando | Windows | Mac | ID |
|---|---|---|---|---|---|
| ✅ |  | Find In Files... | Ctrl+Shift+F | ⇧⌘F | `ID_FILE_FIND_IN_FILE` |
| ✅ |  | Find Match | Ctrl+] | ⌘] | `ID_EDIT_FIND_MATCH` |
| ✅ |  | Find Match Select | Ctrl+Shift+] | ⇧⌘] | `ID_EDIT_FIND_MATCH_N_SELECT` |
| ✅ |  | Find next | F3 | F3 | `ID_EDIT_FIND_NEXT` |
| ✅ |  | Find previous | Shift+F3 | ⇧F3 | `ID_EDIT_FIND_PREVIOUS` |
| ✅ |  | Find selected | Ctrl+F3 | ⌘F3 | `ID_EDIT_FIND_SELECTED_NEXT` |
| ✅ |  | Find selected backward | Ctrl+Shift+F3 | ⇧⌘F3 | `ID_EDIT_FIND_SELECTED_PREVIOUS` |
| ✅ |  | Find... | Ctrl+F | ⌘F | `ID_EDIT_FIND` |
| ✅ |  | Go To... | Ctrl+G | ⌘G | `ID_EDIT_GOTO` |
| ✅ |  | Next Bookmark | F2 | F2 | `ID_EDIT_BKM_NEXT` |
| ✅ |  | Previous Bookmark | Shift+F2 | ⇧F2 | `ID_EDIT_BKM_PREV` |
| ✅ |  | Remove all Bookmarks | Ctrl+Shift+F2 | ⇧⌘F2 | `ID_EDIT_BKM_REMOVE_ALL` |
| ✅ |  | Replace... | Ctrl+H | ⌃H | `ID_EDIT_REPLACE` |
| ✅ |  | Toggle Bookmark | Ctrl+F2 | ⌘F2 | `ID_EDIT_BKM_TOGGLE` |
| ✅ | Get Random Bookmark | 0 | Ctrl+0 | ⌘0 | `ID_EDIT_BKM_GET_0` |
| ✅ | Get Random Bookmark | 1 | Ctrl+1 | ⌘1 | `ID_EDIT_BKM_GET_1` |
| ✅ | Get Random Bookmark | 2 | Ctrl+2 | ⌘2 | `ID_EDIT_BKM_GET_2` |
| ✅ | Get Random Bookmark | 3 | Ctrl+3 | ⌘3 | `ID_EDIT_BKM_GET_3` |
| ✅ | Get Random Bookmark | 4 | Ctrl+4 | ⌘4 | `ID_EDIT_BKM_GET_4` |
| ✅ | Get Random Bookmark | 5 | Ctrl+5 | ⌘5 | `ID_EDIT_BKM_GET_5` |
| ✅ | Get Random Bookmark | 6 | Ctrl+6 | ⌘6 | `ID_EDIT_BKM_GET_6` |
| ✅ | Get Random Bookmark | 7 | Ctrl+7 | ⌘7 | `ID_EDIT_BKM_GET_7` |
| ✅ | Get Random Bookmark | 8 | Ctrl+8 | ⌘8 | `ID_EDIT_BKM_GET_8` |
| ✅ | Get Random Bookmark | 9 | Ctrl+9 | ⌘9 | `ID_EDIT_BKM_GET_9` |
| ✅ | Outlining | Collapse All | Ctrl+M,A | ⌃M, A | `ID_EDIT_FOLD_COLLAPSE_ALL` |
| ✅ | Outlining | Collapse Current Region | Ctrl+M,S | ⌃M, S | `ID_EDIT_FOLD_COLLAPSE_CURRENT` |
| ✅ | Outlining | Collapse to Level 1 | Ctrl+M,1 | ⌃M, 1 | `ID_EDIT_FOLD_COLLAPSE_LEVEL1` |
| ✅ | Outlining | Collapse to Level 2 | Ctrl+M,2 | ⌃M, 2 | `ID_EDIT_FOLD_COLLAPSE_LEVEL2` |
| ✅ | Outlining | Collapse to Level 3 | Ctrl+M,3 | ⌃M, 3 | `ID_EDIT_FOLD_COLLAPSE_LEVEL3` |
| ✅ | Outlining | Expand All | Ctrl+M,X | ⌃M, X | `ID_EDIT_FOLD_EXPAND_ALL` |
| ✅ | Outlining | Expand Current Region | Ctrl+M,E | ⌃M, E | `ID_EDIT_FOLD_EXPAND_CURRENT` |
| ✅ | Outlining | Expand Current Subtree | Ctrl+M,J | ⌃M, J | `ID_EDIT_FOLD_EXPAND_SUBTREE` |
| ✅ | Outlining | Toggle All | Ctrl+M,L | ⌃M, L | `ID_EDIT_FOLD_TOGGLE_ALL` |
| ✅ | Outlining | Toggle Current Region | Ctrl+M,M | ⌃M, M | `ID_EDIT_FOLD_TOGGLE_CURRENT` |
| ✅ | Set Random Bookmark | 0 | Ctrl+Shift+0 | ⇧⌘0 | `ID_EDIT_BKM_SET_0` |
| ✅ | Set Random Bookmark | 1 | Ctrl+Shift+1 | ⇧⌘1 | `ID_EDIT_BKM_SET_1` |
| ✅ | Set Random Bookmark | 2 | Ctrl+Shift+2 | ⇧⌘2 | `ID_EDIT_BKM_SET_2` |
| ✅ | Set Random Bookmark | 3 | Ctrl+Shift+3 | ⇧⌘3 | `ID_EDIT_BKM_SET_3` |
| ✅ | Set Random Bookmark | 4 | Ctrl+Shift+4 | ⇧⌘4 | `ID_EDIT_BKM_SET_4` |
| ✅ | Set Random Bookmark | 5 | Ctrl+Shift+5 | ⇧⌘5 | `ID_EDIT_BKM_SET_5` |
| ✅ | Set Random Bookmark | 6 | Ctrl+Shift+6 | ⇧⌘6 | `ID_EDIT_BKM_SET_6` |
| ✅ | Set Random Bookmark | 7 | Ctrl+Shift+7 | ⇧⌘7 | `ID_EDIT_BKM_SET_7` |
| ✅ | Set Random Bookmark | 8 | Ctrl+Shift+8 | ⇧⌘8 | `ID_EDIT_BKM_SET_8` |
| ✅ | Set Random Bookmark | 9 | Ctrl+Shift+9 | ⇧⌘9 | `ID_EDIT_BKM_SET_9` |

## Text  (25/25)

| | Submenú | Comando | Windows | Mac | ID |
|---|---|---|---|---|---|
| ✅ |  | Capitalize | Ctrl+K,A | ⌘K, A | `ID_EDIT_CAPITALIZE` |
| ✅ |  | Column Markers | Ctrl+Q,M | ⌃Q, M | `ID_EDIT_VIEW_COLUMN_MARKERS` |
| ✅ |  | Column Selection | Ctrl+Shift+F8 | ⇧⌘F8 | `ID_EDIT_COLUMN_SEL` |
| ✅ |  | Comment Line | Ctrl+> | ⌘> | `ID_EDIT_COMMENT` |
| ✅ |  | Create Template | — | — | `ID_EDIT_CREATE_TEMPLATE` |
| ✅ |  | Datetime Stamp | Ctrl+T | ⌘T | `ID_EDIT_DATETIME_STAMP` |
| ✅ |  | Expand Template | Ctrl+Space | ⌃Space | `ID_EDIT_EXPAND_TEMPLATE` |
| ✅ |  | Indent Guide | Ctrl+Q,G | ⌃Q, G | `ID_EDIT_VIEW_INDENT_GUIDE` |
| ✅ |  | Indent Selection | Ctrl+I | ⌘I | `ID_EDIT_INDENT` |
| ✅ |  | Invert case | Ctrl+K,I | ⌘K, I | `ID_EDIT_INVERT_CASE` |
| ✅ |  | Line Numbers | Ctrl+Q,L | ⌃Q, L | `ID_EDIT_VIEW_LINE_NUMBERS` |
| ✅ |  | Lowercase | Ctrl+Shift+Down | ⇧⌘↓ | `ID_EDIT_LOWER` |
| ✅ |  | Normalize Keyword | Ctrl+K,N | ⌘K, N | `ID_EDIT_NORMALIZE_TEXT` |
| ✅ |  | Open file under cursor | Ctrl+K,F | ⌘K, F | `ID_EDIT_OPENFILEUNDERCURSOR` |
| ✅ |  | Ruler | Ctrl+Q,U | ⌃Q, U | `ID_EDIT_VIEW_RULER` |
| ✅ |  | Stream Selection | F8 | F8 | `ID_EDIT_STREAM_SEL` |
| ✅ |  | Syntax Gutter | Ctrl+Q,Y | ⌃Q, Y | `ID_EDIT_VIEW_SYNTAX_GUTTER` |
| ✅ |  | Tabify Selection (All) | Ctrl+K,T | ⌘K, T | `ID_EDIT_TABIFY` |
| ✅ |  | Tabify Selection (Leading) | Ctrl+K,L | ⌘K, L | `ID_EDIT_TABIFY_LEADING` |
| ✅ |  | Toggle Selection Mode | Ctrl+Q,B | ⌃Q, B | `ID_EDIT_TOGGLES_SEL` |
| ✅ |  | Uncomment Line | Ctrl+< | ⌘< | `ID_EDIT_UNCOMMENT` |
| ✅ |  | Undent Selection | Ctrl+U | ⌘U | `ID_EDIT_UNDENT` |
| ✅ |  | Untabify Selection | Ctrl+K,U | ⌘K, U | `ID_EDIT_UNTABIFY` |
| ✅ |  | Uppercase | Ctrl+Shift+Up | ⇧⌘↑ | `ID_EDIT_UPPER` |
| ✅ |  | Visible Spaces | Ctrl+Q,I | ⌃Q, I | `ID_EDIT_VIEW_WHITE_SPACE` |

## Text/Format  (25/27)

| | Submenú | Comando | Windows | Mac | ID |
|---|---|---|---|---|---|
| ✅ |  | Column Center Justify | Ctrl+K,V | ⌘K, V | `ID_EDIT_COLUMN_CENTER_JUSTIFY` |
| ✅ |  | Column Left Justify | Ctrl+K,Y | ⌘K, Y | `ID_EDIT_COLUMN_LEFT_JUSTIFY` |
| ✅ |  | Column Right Justify | Ctrl+K,W | ⌘K, W | `ID_EDIT_COLUMN_RIGT_JUSTIFY` |
| ✅ |  | Format SQL | Ctrl+K,E | ⌘K, E | `ID_EDIT_FORMAT_SQL` |
| ✅ |  | Insert Number... | Ctrl+K,M | ⌘K, M | `ID_EDIT_COLUMN_INSERT_NUMBER` |
| ✅ |  | Insert/Fill Columns... | Ctrl+K,B | ⌘K, B | `ID_EDIT_COLUMN_INSERT_FILL` |
| ✅ |  | Join Lines | Ctrl+K,J | ⌘K, J | `ID_EDIT_FORMAT_JOIN_LINES` |
| ✅ |  | Remove Blank Lines | Ctrl+K,G | ⌘K, G | `ID_EDIT_FORMAT_REMOVE_BLANK` |
| ✅ |  | Remove Excessive Blank Lines | Ctrl+K,H | ⌘K, H | `ID_EDIT_FORMAT_REMOVE_EXCESSIVE_BLANK` |
| ✅ |  | Sort... | Ctrl+K,S | ⌘K, S | `ID_EDIT_SORT` |
| ✅ |  | Split Line... | Ctrl+K,K | ⌘K, K | `ID_EDIT_FORMAT_SPLIT_LINES` |
| ✅ |  | Trim Excessive Spaces | Ctrl+K,P | ⌘K, P | `ID_EDIT_FORMAT_TRIM_EXCESSIVE_SPACES` |
| ✅ |  | Trim Leading Spaces | Ctrl+K,O | ⌘K, O | `ID_EDIT_FORMAT_TRIM_LEADING_SPACES` |
| ✅ |  | Trim Trailing Spaces | Ctrl+K,R | ⌘K, R | `ID_EDIT_FORMAT_TRIM_TRAILING_SPACES` |
| ✅ | Convert to encoding | ANSI | — | — | `ID_FILE_ENCODING_ANSI` |
| ✅ | Convert to encoding | OEM | — | — | `ID_FILE_ENCODING_OEM` |
| ✅ | Convert to encoding | UTF-16 with BOM | — | — | `ID_FILE_ENCODING_UTF16_BOM` |
| ✅ | Convert to encoding | UTF-8 | — | — | `ID_FILE_ENCODING_UTF8` |
| ✅ | Convert to encoding | UTF-8 with BOM | — | — | `ID_FILE_ENCODING_UTF8_BOM` |
| ⏳ | Convert to encoding > Others | Codepage #1 | — | — | `ID_FILE_ENCODING_CODEPAGE_FIRST` |
| ✅ | Encoding | ANSI | — | — | `ID_EDIT_ENCODING_ANSI` |
| ✅ | Encoding | OEM | — | — | `ID_EDIT_ENCODING_OEM` |
| ✅ | Encoding | UTF-8 | — | — | `ID_EDIT_ENCODING_UTF8` |
| ⏳ | Encoding > Others | Codepage #1 | — | — | `ID_EDIT_ENCODING_CODEPAGE_FIRST` |
| ✅ | File line ending style | Old Mac | — | — | `ID_EDIT_FILE_FORMAT_MAC` |
| ✅ | File line ending style | Unix | — | — | `ID_EDIT_FILE_FORMAT_UNIX` |
| ✅ | File line ending style | Windows | — | — | `ID_EDIT_FILE_FORMAT_WINOWS` |

## Grid  (10/10)

| | Submenú | Comando | Windows | Mac | ID |
|---|---|---|---|---|---|
| ✅ |  | Copy All | — | — | `ID_GRID_COPY_ALL` |
| ✅ |  | Copy All Column Headers | — | — | `ID_GRID_COPY_HEADERS` |
| ✅ |  | Copy Column Header | — | — | `ID_GRID_COPY_COL_HEADER` |
| ✅ |  | Copy Row | — | — | `ID_GRID_COPY_ROW` |
| ✅ |  | Export | — | — | `ID_GRID_OUTPUT_EXPORT` |
| ✅ |  | Export and Shell Open | — | — | `ID_GRID_OUTPUT_EXPORT_AND_OPEN` |
| ✅ |  | Format Options... | — | — | `ID_GRID_OUTPUT_OPTIONS` |
| ✅ |  | Open in Editor | — | — | `ID_GRID_OUTPUT_OPEN` |
| ✅ |  | Size Columns to Data | — | — | `ID_OCIGRID_DATA_FIT` |
| ✅ |  | Size Columns to Headers | — | — | `ID_OCIGRID_COLS_TO_HEADERS` |

## Session  (10/10)

| | Submenú | Comando | Windows | Mac | ID |
|---|---|---|---|---|---|
| ✅ |  | Cancel | Ctrl+D,A | ⌘D, A | `ID_SQL_CANCEL` |
| ✅ |  | Commit | Ctrl+D,T | ⌘D, T | `ID_SQL_COMMIT` |
| ✅ |  | Connect... | Ctrl+D,C | ⌘D, C | `ID_SQL_CONNECT` |
| ✅ |  | Disconnect | Ctrl+D,D | ⌘D, D | `ID_SQL_DISCONNECT` |
| ✅ |  | Enable Dbms Output | — | — | `ID_SQL_DBMS_OUTPUT` |
| ✅ |  | Enable Session Statistics | — | — | `ID_SQL_SESSION_STATISTICS` |
| ✅ |  | Read Only | Ctrl+D,O | ⌘D, O | `ID_SQL_READ_ONLY` |
| ✅ |  | Refresh Object Name Cache | — | — | `ID_SQL_REFRESH_OBJECT_CACHE` |
| ✅ |  | Rollback | Ctrl+D,R | ⌘D, R | `ID_SQL_ROLLBACK` |
| ✅ |  | Test Connection/Reconnect | Ctrl+D,E | ⌘D, E | `ID_SQL_RECONNECT` |

## Script  (18/18)

| | Submenú | Comando | Windows | Mac | ID |
|---|---|---|---|---|---|
| ✅ |  | Enable Substitution Variables | — | — | `ID_SQL_SUBSTITUTION_VARIABLES` |
| ✅ |  | Execute | F5 | F5 | `ID_SQL_EXECUTE` |
| ✅ |  | Execute Current | F10 | Ctrl+Enter | F10 | ⌘↵ | `ID_SQL_EXECUTE_CURRENT` |
| ✅ |  | Execute Current Alternative | Alt+F10 | ⌥F10 | `ID_SQL_EXECUTE_CURRENT_ALT` |
| ✅ |  | Execute and Step | Ctrl+F10 | ⌘F10 | `ID_SQL_EXECUTE_CURRENT_AND_STEP` |
| ✅ |  | Execute from Cursor position | Ctrl+F5 | ⌘F5 | `ID_SQL_EXECUTE_FROM_CURSOR` |
| ✅ |  | Execute in SQL*Plus | Ctrl+Shift+F5 | ⇧⌘F5 | `ID_SQL_EXECUTE_IN_SQLPLUS` |
| ✅ |  | Explain Plan | F9 | F9 | `ID_SQL_EXPLAIN_PLAN` |
| ✅ |  | Find Object | F12 | F12 | `ID_SQL_DESCRIBE` |
| ✅ |  | Get history and step back | Ctrl+D,S | ⌘D, S | `ID_SQL_HISTORY_GET_AND_STEPBACK` |
| ✅ |  | Halt Execution on Errors | — | — | `ID_SQL_HALT_ON_ERROR` |
| ✅ |  | Load DDL script | Ctrl+F12 | Ctrl+Shift+F12 | ⌘F12 | ⇧⌘F12 | `ID_SQL_LOAD` |
| ✅ |  | Load DDL script with Dbms_Metadata | Alt+F12 | ⌥F12 | `ID_SQL_LOAD_WITH_DBMS_METADATA` |
| ✅ |  | Next Error | F4 | F4 | `ID_SQL_NEXT_ERROR` |
| ✅ |  | Previous Error | Shift+F4 | ⇧F4 | `ID_SQL_PREV_ERROR` |
| ✅ |  | Quick Count | Ctrl+F11 | ⌘F11 | `ID_SQL_QUICK_COUNT` |
| ✅ |  | Quick Query | F11 | F11 | `ID_SQL_QUICK_QUERY` |
| ✅ |  | Step forward and get history | Ctrl+D,F | ⌘D, F | `ID_SQL_HISTORY_STEPFORWARD_AND_GET` |

## Tools  (4/4)

| | Submenú | Comando | Windows | Mac | ID |
|---|---|---|---|---|---|
| ✅ |  | Extract Schema DDL... | — | — | `ID_SQL_EXTRACT_SCHEMA` |
| ✅ |  | Open Settings Folder | — | — | `ID_APP_SETTINGS_FOLDER` |
| ✅ |  | Settings... | Ctrl+Q,P | ⌃Q, P | `ID_APP_SETTINGS` |
| ✅ |  | Table Transformation Helper... | — | — | `ID_SQL_TABLE_TRANSFORMER` |

## Window  (3/3)

| | Submenú | Comando | Windows | Mac | ID |
|---|---|---|---|---|---|
| ✅ |  | Move splitter down | Shift+Ctrl+PgDown | ⇧⌘⇟ | `ID_CUSTOM_SPLITTER_DOWN` |
| ✅ |  | Move splitter up | Shift+Ctrl+PgUp | ⇧⌘⇞ | `ID_CUSTOM_SPLITTER_UP` |
| ✅ |  | Split | Shift+Alt+P | ⌥⇧P | `ID_WINDOW_SPLIT` |

## Help  (4/4)

| | Submenú | Comando | Windows | Mac | ID |
|---|---|---|---|---|---|
| ✅ |  | About... | Ctrl+Q,A | ⌃Q, A | `ID_APP_ABOUT` |
| ✅ |  | Help Topics | — | — | `ID_HELP_FINDER` |
| ✅ |  | SQL Short Reference | Ctrl+F1 | ⌘F1 | `ID_SQL_HELP` |
| ✅ |  | SQLTools on the Web | — | — | `ID_SQLTOOLS_ON_THE_WEB` |

## (solo barra)  (22/26)

| | Submenú | Comando | Windows | Mac | ID |
|---|---|---|---|---|---|
| ✅ |  |  | — | — | `ID_BOOKLET_PIN` |
| ✅ |  |  | — | — | `ID_BOOKLET_UNPIN` |
| ⏳ |  |  | — | — | `ID_GRIDPOPUP_HTML` |
| ⏳ |  |  | — | — | `ID_GRIDPOPUP_TEXT` |
| ⏳ |  |  | — | — | `ID_GRIDPOPUP_WORDWRAP` |
| ✅ |  |  | — | — | `ID_GRID_CLEAR_RECORDS` |
| ✅ |  |  | — | — | `ID_GRID_DATA_FIT` |
| ✅ |  |  | — | — | `ID_GRID_EXPORT` |
| ✅ |  |  | — | — | `ID_GRID_HEADER_FIT` |
| ✅ |  |  | — | — | `ID_GRID_MOVE_DOWN` |
| ✅ |  |  | — | — | `ID_GRID_MOVE_END` |
| ✅ |  |  | — | — | `ID_GRID_MOVE_HOME` |
| ✅ |  |  | — | — | `ID_GRID_MOVE_PGDOWN` |
| ✅ |  |  | — | — | `ID_GRID_MOVE_PGUP` |
| ✅ |  |  | — | — | `ID_GRID_MOVE_UP` |
| ✅ |  |  | — | — | `ID_GRID_OPEN_WITH_EXCEL` |
| ✅ |  |  | — | — | `ID_GRID_OPEN_WITH_IE` |
| ✅ |  |  | — | — | `ID_GRID_ROTATE` |
| ✅ |  |  | — | — | `ID_GRID_SETTINGS` |
| ✅ |  | Bind Window | — | — | `ID_BOOKLET_BINDS` |
| ⏳ |  | Connection | — | — | `ID_SQL_CONN_DESCRIPTION` |
| ✅ |  | Explain Plan Window | — | — | `ID_BOOKLET_SQL_PLAN` |
| ✅ |  | History Window | — | — | `ID_BOOKLET_HISTORY` |
| ✅ |  | Output Window | — | — | `ID_BOOKLET_OUTPUT` |
| ✅ |  | Results Window | — | — | `ID_BOOKLET_RESULT` |
| ✅ |  | Statistics Window | — | — | `ID_BOOKLET_STATISTICS` |

## Diferencias deliberadas respecto a Windows

- **Cancelar**: además de `Cmd+D, A`, se puede usar **Esc**.
- **Expand Template / autocompletado**: **Control+Espacio** (Cmd+Espacio es Spotlight).
- **Exit / About / Settings**: usan el menú de la aplicación de macOS (Cmd+Q para salir).
- **Cmd+K** ya no abre las conexiones: es el prefijo de los acordes de edición `Cmd+K, x` (duplicar línea, etc.). Las conexiones se abren con **Cmd+D, C**.
- **F5** ejecuta el script completo o la selección (como Windows); **F10** o **Cmd+Intro** ejecuta la sentencia actual.
- En un MacBook las teclas F suelen requerir **Fn** (o activar «Usar teclas F1, F2, etc. como teclas de función estándar»).
