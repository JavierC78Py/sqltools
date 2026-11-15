# SQLTools Mac — atajos de teclado

La referencia completa (los 235 comandos de los menús de SQLTools, su atajo de Windows, el equivalente en Mac y el estado de cada uno) está en
**[MENUS_Y_ATAJOS.md](MENUS_Y_ATAJOS.md)**, generada automáticamente desde la aplicación.

## Los más usados
| Atajo | Acción |
|---|---|
| Cmd+D, C | Conectar (gestor de conexiones) |
| F5 | Ejecutar el script completo o la selección |
| F10 o Cmd+Intro | Ejecutar la sentencia actual |
| Esc o Cmd+D, A | Cancelar |
| Cmd+D, T / Cmd+D, R | Commit / Rollback |
| Cmd+N / Cmd+O / Cmd+S / Cmd+Mayús+S | Nuevo, abrir, guardar, guardar como |
| Cmd+F / F3 / Mayús+F3 | Buscar, siguiente, anterior |
| Control+H | Reemplazar (Cmd+H oculta la aplicación en macOS) |
| Cmd+G | Ir a la línea |
| Cmd+> / Cmd+< | Comentar / descomentar líneas |
| Cmd+Mayús+↑ / Cmd+Mayús+↓ | MAYÚSCULAS / minúsculas |
| Cmd+K, Q | Duplicar línea o selección |
| Cmd+Y | Borrar línea |
| Control+Espacio | Plantillas y autocompletado |
| F4 / Mayús+F4 | Siguiente / anterior error de compilación |
| F12 | Buscar el objeto bajo el cursor (abre su código) |

## Diferencias deliberadas respecto a Windows
Ver el final de [MENUS_Y_ATAJOS.md](MENUS_Y_ATAJOS.md). En un MacBook las teclas F suelen necesitar **Fn**.

## Ejecución rápida y archivos (nuevo)
| Atajo | Acción |
|---|---|
| F11 | Quick Query (consulta rápida del objeto bajo el cursor) |
| Cmd+F11 | Quick Count (conteo rápido) |
| Cmd+F5 | Execute from Cursor (ejecuta desde el cursor hasta el final) |
| Cmd+F10 | Execute and Step (ejecuta y avanza a la siguiente sentencia) |
| (menú) File > Recent Files | Últimos 12 archivos |
| (menú) View > Code Outline | Panel de esquema del código |
| (menú) Search > Find In Files | Buscar texto en una carpeta |

Nota: las teclas F requieren Fn en el Mac, y F11 puede chocar con "Mostrar escritorio" de macOS.

## Edición de texto y DDL (nuevo)
| Atajo | Acción |
|---|---|
| Cmd+F12 / Cmd+Mayús+F12 | Load DDL script (código/DDL del objeto bajo el cursor) |
| Opt+F12 | Load DDL con DBMS_METADATA |
| Cmd+Retroceso / Cmd+Supr | Borrar palabra hacia atrás / adelante |
| Cmd+Mayús+X | Cut Append (corta y añade al portapapeles) |
| Cmd+↑ / Cmd+↓ | Scroll sin mover el cursor |
| Ctrl+K, T / L / U | Tabify (todo) / Tabify (solo sangría) / Untabify |
| Ctrl+K, N | Normalize Keyword (palabras reservadas en MAYÚSCULAS) |
| Ctrl+K, K | Split Line (partir líneas largas) |
| Ctrl+K, M | Insert Number (numerar líneas) |
| Ctrl+K, F | Abrir el archivo cuya ruta está bajo el cursor |
| Ctrl+Q, G / Ctrl+Q, M | Guías de sangría / marcas de columna (80 y 120) |
Extract Schema DDL…: menú Script (elige esquema y tipos de objeto).

Nota: en las tablas de esta página «Ctrl+K,x» es la notación de Windows; en el Mac se usa ⌘K, x (y Control solo para Q, M, H, W y Espacio). Guía completa en español: GUIA_HERRAMIENTAS.md.
Selección en columna: Mayús+Opción+flechas (Esc para salir).

## Workspaces, snapshots y paneles (nuevo)
| Atajo | Acción |
|---|---|
| ⇧⌘W, S / A / O / L | Workspace: guardar / guardar como / abrir / cerrar |
| ⇧⌘W, Q / T | Snapshot: guardar rápida / abrir (rápidas y automáticas) |
| ⇧⌘W, C / V | Snapshot: copiar / pegar (portapapeles) |
| ⇧⌘N | Nuevo desde plantilla |
| ⌥0 / ⌥1 / ⌥2 / ⌥3 / ⌥4 | Paneles: File Panel / Object Viewer / Properties / Object List / Code Outline |
| ⌥5 / ⌥7 / ⌥8 / ⌥9 | Paneles: Snippets / Find in Files Output / History / Open Files |

## Format SQL y últimos comandos (nuevo)
| Atajo | Acción |
|---|---|
| ⌘K, E | Format SQL (la selección o la sentencia del cursor) |
| ⌃Q, P | Settings (opciones de formato y del editor) |
| ⌥F10 | Execute Current Alternative (bloque entre líneas en blanco) |
| ⇧⌘F5 | Execute in SQL*Plus (Terminal) |
| ⌘F1 | SQL Short Reference |
| ⌘P | Imprimir |
| ⌥⇧P | Split (segunda vista del script) |
| ⇧⌘PgUp / ⇧⌘PgDn | Mover el divisor editor/resultados |
