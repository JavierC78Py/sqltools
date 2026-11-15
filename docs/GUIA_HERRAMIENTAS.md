# SQLTools Mac — Guía de herramientas (en español)

Esta guía explica **para qué sirve cada herramienta**, con su atajo y un ejemplo corto. Está pensada para descubrir cosas que quizá no usabas en la versión de Windows.

## 0. Cómo leer los atajos

SQLTools de Windows usa `Ctrl`; en el Mac se traduce así:

| En Windows | En el Mac | Símbolo |
|---|---|---|
| Ctrl | Command | ⌘ |
| Alt | Option (Opción) | ⌥ |
| Shift | Mayúsculas | ⇧ |
| Ctrl + **Q, M, H, W, Espacio** | **Control** (para no chocar con el sistema) | ⌃ |

- Un atajo como **⌘K, T** significa: pulsa ⌘K, suéltalas y luego pulsa **T** (es un «acordeón» de dos pasos).
- Las teclas **F1…F12** en un Mac suelen necesitar la tecla **Fn** (ej. Fn+F5), salvo que actives «Usar teclas F1, F2 como teclas estándar» en Ajustes del Sistema → Teclado.
- **Cualquier comando** está también en los menús, con su atajo a la derecha. Si no recuerdas un atajo, búscalo en el menú.
- Los comandos que aún no están hechos aparecen en gris.

---

## 1. Conexión y sesión (menú Session)

| Herramienta | Atajo | Para qué sirve |
|---|---|---|
| **Connect…** | ⌘D, C | Abre el administrador de conexiones (guardadas, con contraseña en el Llavero de macOS). |
| **Disconnect** | ⌘D, D | Cierra la sesión. |
| **Test Connection/Reconnect** | ⌘D, E | Comprueba que la sesión sigue viva y la reabre si se cayó. |
| **Commit / Rollback** | ⌘D, T / ⌘D, R | Confirma o deshace los cambios de la sesión. |
| **Read Only** | ⌘D, O | Modo «solo lectura»: la sesión rechaza cualquier escritura. **Muy útil en producción**: te protege de un UPDATE sin querer. |
| **Cancel** | ⌘D, A | Interrumpe la consulta que se está ejecutando (equivale a Ctrl+C). |
| **Enable Dbms Output** | — | Muestra en el panel *Output* lo que tu código escribe con `DBMS_OUTPUT.PUT_LINE`. |
| **Enable Session Statistics** | — | Tras cada ejecución, el panel *Statistics* muestra los contadores de la sesión (lecturas lógicas, parseos, etc.). Sirve para comparar dos versiones de una consulta. |
| **Refresh Object Name Cache** | — | Vuelve a leer la lista de objetos (para el explorador y el autocompletado) si cambió el esquema. |

**Carpeta del TNS:** en el administrador de conexiones, botón **Ajustes…**: ahí defines la carpeta donde está tu `tnsnames.ora` con las wallets del Mac.

---

## 2. Ejecutar código (menú Script)

| Herramienta | Atajo | Qué hace |
|---|---|---|
| **Execute** | F5 | Ejecuta la **sentencia donde está el cursor** (o la selección, si hay). Las sentencias se separan por `;` o por `/` en PL/SQL. |
| **Execute Current** | F10 / ⌘↵ | Igual que el anterior, pensado para usar con la mano en el teclado principal. |
| **Execute from Cursor position** | ⌘F5 | Ejecuta **desde el cursor hasta el final** del script. Útil para reanudar un script desde donde falló. |
| **Execute and Step** | ⌘F10 | Ejecuta la sentencia actual y **salta a la siguiente**. Permite recorrer un script sentencia a sentencia, como un depurador. |
| **Execute Current Alternative** | ⌥F10 | Ejecuta **el bloque de texto delimitado por líneas en blanco** que rodea al cursor (estilo TOAD). Útil si escribes sentencias separadas por una línea en blanco y sin `;`. |
| **Execute in SQL\*Plus** | ⇧⌘F5 | Abre la aplicación **Terminal** con `sqlplus` ejecutando el script (o la selección). La contraseña se escribe en Terminal; nunca pasa por este programa. Requiere tener `sqlplus` instalado (el Instant Client básico no lo trae). |
| **Enable Substitution Variables** | menú Script | Si está activa, las variables `&nombre` y `&&nombre` del script **piden su valor** antes de ejecutar, como en SQL\*Plus (`&&` se recuerda durante la sesión). Se pegan tal cual, sin comillas. Está apagada por defecto. |
| **Explain Plan** | F9 | Muestra el plan de ejecución (con `DBMS_XPLAN`) en el panel *Plan*, **sin ejecutar** la consulta. |
| **Halt Execution on Errors** | — | Si ejecutas varias sentencias y una falla, se detiene ahí (si no, continúa con las demás). |
| **Next Error / Previous Error** | F4 / ⇧F4 | Salta al error de compilación siguiente/anterior (al compilar paquetes, procedimientos, triggers). |
| **Quick Query** | F11 | **Consulta rápida**: pon el cursor sobre un nombre de tabla y pulsa F11; lanza `select * from esa_tabla` con un límite de filas (configurable). |
| **Quick Count** | ⌘F11 | Igual, pero hace `select count(*)`. |

**Un resultado por consulta:** si ejecutas un script con varios `SELECT`, cada uno aparece en su propia pestaña dentro del panel de resultados.

**Variables de enlace (binds):** si escribes `:variable` en una sentencia, el panel **Binds** (botón `:1`) lista las variables y te deja poner su valor antes de ejecutar.

---

## 3. Paneles de resultados (parte inferior)

Se cambian con la barra de iconos o con el menú *View*:

- **Result** — la rejilla con las filas. Trae las filas por lotes (por defecto 10 000 por viaje de red), que es lo que lo hace rápido contra bases remotas.
- **Statistics** — contadores de la sesión (ver *Enable Session Statistics*).
- **Plan** — plan de ejecución (F9).
- **Output** — salida de `DBMS_OUTPUT` y errores de compilación.
- **History** — historial de todas las sentencias ejecutadas (con tiempo y filas). Recórrelo con **⌘D, S** (hacia atrás) y **⌘D, F** (hacia adelante): recupera sentencias anteriores en el editor.
- **Binds** — valores de las variables `:x`.

### Herramientas de la rejilla (menú Grid)

| Herramienta | Para qué sirve |
|---|---|
| **Copy Row** | Copia la(s) fila(s) seleccionada(s), separadas por tabulador (pega directo en Excel). |
| **Copy Column Header** | Copia el nombre de la columna actual. |
| **Copy All / Copy All Column Headers** | Copia todo el resultado (con o sin cabeceras). |
| **Export** | Guarda el resultado como CSV. |
| **Export and Shell Open** | Exporta a un archivo temporal y lo **abre con la aplicación por defecto** (Excel, Numbers…). |
| **Open in Editor** | Abre el resultado como texto (tabulado) en una pestaña nueva del editor. |
| **Format Options…** | Ajustes: filas por viaje (prefetch), máximo de caracteres por celda, texto para `NULL`, límite de Quick Query. |
| **Size Columns to Data / Headers** | Ajusta el ancho de columnas al contenido o al título. |
| **Rotate** | Gira la rejilla: cada columna pasa a ser una fila. Ideal para ver **un registro con muchas columnas**. |
| **Pin** | «Fija» el resultado actual para que no se sobrescriba con la siguiente ejecución (para comparar dos resultados). |
| **Clear Records** | Vacía la rejilla. |

---

## 4. Edición básica (menú Edit)

| Herramienta | Atajo | Qué hace |
|---|---|---|
| **Delete Line** | ⌘Y | Borra la línea actual entera. |
| **Duplicate Line or Selection** | ⌘K, Q | Duplica la línea (o la selección) justo debajo. |
| **Select Line / Select Word** | ⌘L / ⌃W | Selecciona la línea / la palabra. |
| **Delete Word Back / to End of Word** | ⌘⌫ / ⌘⌦ | Borra hasta el inicio / el final de la palabra. |
| **Copy Append / Cut Append** | ⇧⌘C / ⇧⌘X | **Añade** al portapapeles en vez de reemplazarlo: así juntas trozos de varios sitios y los pegas de una vez. |
| **Scroll Up / Down** | ⌘↑ / ⌘↓ | Desplaza la vista una línea **sin mover el cursor**. |
| **Scroll To Center** | — | Centra verticalmente la línea del cursor. |
| **Copy / Cut / Delete Bookmarked Lines** | ⌘K, C / X / D | Copia, corta o borra **todas las líneas marcadas** con marcador (ver sección 6). Útil para extraer líneas dispersas. |

---

## 5. Texto y formato (menús Text y Text/Format)

### Mayúsculas y minúsculas
| Herramienta | Atajo | Qué hace |
|---|---|---|
| **Uppercase / Lowercase** | ⇧⌘↑ / ⇧⌘↓ | Pasa la selección (o la palabra) a MAYÚSCULAS / minúsculas. |
| **Capitalize** | ⌘K, A | Primera letra de cada palabra en mayúscula. |
| **Invert case** | ⌘K, I | Invierte mayúsculas y minúsculas. |
| **Normalize Keyword** | ⌘K, N | Pone en MAYÚSCULAS **solo las palabras reservadas** (`select`, `from`, `where`…) en la selección o todo el texto; no toca los textos entre comillas, los comentarios ni los nombres de tablas/columnas. |

### Sangría y comentarios
| Herramienta | Atajo | Qué hace |
|---|---|---|
| **Indent / Undent Selection** | ⌘I / ⌘U | Aumenta / reduce la sangría de las líneas seleccionadas. |
| **Comment Line / Uncomment Line** | ⌘> / ⌘< | Pone / quita `-- ` al principio de las líneas. |
| **Tabify (All) / (Leading)** | ⌘K, T / ⌘K, L | Convierte **espacios en tabuladores** (en todo el texto, o solo en la sangría inicial). |
| **Untabify** | ⌘K, U | Convierte tabuladores en espacios. |

### Limpieza de texto (Text/Format)
| Herramienta | Atajo | Qué hace |
|---|---|---|
| **Trim Leading / Trailing / Excessive Spaces** | ⌘K, O / R / P | Quita espacios al inicio / al final / reduce varios espacios seguidos a uno. |
| **Remove Blank Lines / Remove Excessive Blank Lines** | ⌘K, G / H | Quita todas las líneas en blanco / deja como máximo una seguida. |
| **Join Lines** | ⌘K, J | Une las líneas seleccionadas en una sola. |
| **Sort…** | ⌘K, S | Ordena las líneas seleccionadas (ascendente o descendente). |
| **Split Line…** | ⌘K, K | Parte las líneas largas en la columna que indiques (por el último espacio). |
| **Datetime Stamp** | ⌘T | Inserta fecha y hora actuales. |
| **Insert Number…** | ⌘K, M | Inserta una **secuencia numérica** (inicio, incremento, ancho con ceros) una por línea. Con selección en columna lo hace en la columna del bloque. |

### Archivos: codificación y saltos de línea

Un archivo de texto guarda las letras como bytes y hay varias «tablas» para hacerlo. Si abres un script y ves `Ã±` en vez de `ñ` (o símbolos raros), el archivo se leyó con la tabla equivocada. Hay dos familias de comandos, que no hay que confundir:

| Menú | Qué hace | Cuándo usarlo |
|---|---|---|
| **Text/Format > Encoding > UTF-8 / ANSI / OEM** | **Relee el archivo del disco** interpretándolo con esa tabla. | Cuando el texto se ve mal al abrirlo. Prueba ANSI o OEM hasta que se vea bien. (Pide confirmación si tienes cambios sin guardar.) |
| **Text/Format > Convert to encoding > UTF-8 / UTF-8 con BOM / ANSI / OEM / UTF-16 con BOM** | Cambia **cómo se guardará** el archivo la próxima vez. | Para convertir un script a otra codificación (por ejemplo, entregarlo en ANSI a quien usa Windows antiguo). |
| **Text/Format > File line ending style > Windows / Unix / Old Mac** | Cambia los saltos de línea al guardar: CRLF (Windows), LF (Mac/Linux actual), CR (Mac antiguo). | Cuando un script debe ser compatible con otro sistema. |

Significado de las tablas: **UTF-8** es la moderna y universal (la recomendada). **ANSI** es Windows-1252, la de Windows en español/inglés (incluye `€`, `“ ”`). **OEM** es CP850, la de la consola/MS-DOS de Windows en español. **UTF-16** es la de algunos archivos de Windows/Excel; se detecta sola por su marca (BOM). Los caracteres que no caben en la tabla elegida se guardan como `?`.

En el submenú **Others** hay más páginas de códigos: Latin-1, Latin-9 (con €), Europa central (Windows-1250), OEM de EE. UU. (CP437) y Mac Roman.

- **File > Properties… (⌃Q, F):** muestra ruta, tamaño, codificación, saltos de línea y número de líneas.
- **File > Reload (⌃Q, R):** vuelve a leer el archivo desde disco.
- **File > Show File Location (⌃Q, O):** lo muestra en el Finder. **Copy File Location:** copia la ruta.
- **Open file under cursor (⌘K, F):** si el cursor está sobre una ruta (por ejemplo `@scripts/a.sql`), abre ese archivo. Las rutas relativas se buscan junto al archivo actual.

### Apariencia del editor (menú Text)
| Herramienta | Atajo | Qué hace |
|---|---|---|
| **Line Numbers** | ⌃Q, L | Muestra/oculta los números de línea. |
| **Syntax Gutter** | ⌃Q, Y | Muestra/oculta el margen con las flechas de plegado. |
| **Visible Spaces** | ⌃Q, I | Dibuja puntos en los espacios y flechas en los tabuladores (para detectar espacios sobrantes). |
| **Ruler** | ⌃Q, U | **Regla de columnas** sobre el editor, con marcas cada 5 y números cada 10; un trazo indica la columna del cursor. |
| **Indent Guide** | ⌃Q, G | **Guías verticales** de sangría (una por cada nivel de 4 espacios) para ver el anidamiento. |
| **Column Markers** | ⌃Q, M | Líneas punteadas en las columnas **80 y 120** (límites típicos de ancho de código). |
| **Expand Template** | ⌃Espacio | Expande una abreviatura (ver sección 8). |
| **Create Template** | menú Text | Convierte **el texto seleccionado en una plantilla propia**: te pide una abreviatura (y un nombre); luego la escribes, pulsas ⌃Espacio y se expande. Usa `$0` donde debe quedar el cursor. Aparece también en el panel *Snippets* y en *New…*. |

---

## 5b. Format SQL: dar formato automático al código

**⌘K, E** reescribe SQL y PL/SQL con un estilo uniforme **sin cambiar lo que hace**.
- Con texto seleccionado formatea la selección; **sin selección, la sentencia donde está el cursor**. Se puede deshacer con ⌘Z.
- **SQL (SELECT, INSERT, UPDATE, DELETE, MERGE, WITH):** una cláusula por línea, una columna por línea, `AND`/`OR` en línea propia, `JOIN` en líneas propias, subconsultas sangradas, espacios normalizados alrededor de los operadores. No toca los textos entre comillas, los comentarios ni los *hints* (`/*+ ... */`).
- **PL/SQL (bloques, procedimientos, funciones, paquetes, triggers):** re-sangra `BEGIN/END`, `IF/ELSIF/ELSE`, `LOOP`, `CASE`, `EXCEPTION`/`WHEN` y las declaraciones, y da formato a los `SELECT/INSERT/UPDATE/DELETE` que contienen.
- **CREATE TABLE/VIEW/INDEX y otro DDL** se dejan como están.

**Definir tu propia estructura:** menú **Tools > Settings… (⌃Q, P) > pestaña Format SQL**. A la izquierda están las reglas; a la derecha, **tu código** (pega el tuyo y edítalo) y el **resultado en vivo**.

| Regla | Qué controla |
|---|---|
| Espacios por nivel / tabuladores | El ancho de la sangría (1 a 8) o tabuladores. |
| Palabras reservadas / Funciones / Nombres de tablas y columnas | MAYÚSCULAS, minúsculas o como estén; cada grupo por separado. |
| Estilo base | **Estándar** (palabras a la izquierda) o **Riachuelo** (palabras alineadas por la derecha). |
| Columnas y condiciones (sangría) | A cuántas columnas de la palabra (SELECT, WHERE…) van las columnas y condiciones. |
| AND / OR (sangría desde WHERE) | `0` = AND justo bajo WHERE (como en muchos códigos de Oracle); mayor = sangrado. |
| JOIN (sangría desde FROM) | A cuántas columnas de FROM empiezan los JOIN. |
| SELECT solo en su línea | Con varias columnas: `SELECT` solo y las columnas debajo, o la primera columna en la misma línea. |
| Una columna por línea · AND/OR en línea propia · ON en línea propia | Cómo se parten las listas y condiciones. |
| Coma al principio · espacio tras la coma · espacios dentro de paréntesis | Detalles de puntuación. |
| PL/SQL: alinear declaraciones y `:=` · líneas en blanco antes de EXCEPTION y BEGIN | Estructura de los bloques PL/SQL. |

**Perfiles:** guarda tu configuración con un nombre (*Guardar como…*), cámbiala entre perfiles, y **exporta/importa** archivos `.sqlfmt` para dárselos a un compañero y que todo el equipo formatee igual.

**Deducir de un ejemplo:** pega código que **ya tenga el formato que te gusta** y el programa **deduce** la sangría, las mayúsculas, dónde van AND/OR y los JOIN, las columnas, las comas y las declaraciones alineadas, y rellena las reglas por ti. Luego ajustas lo que haga falta y lo guardas como perfil. Es la forma más rápida de definir «tu» estructura.

Ejemplos de lo que producen distintas reglas (estilo Estándar, sangría 2 / Riachuelo):
```sql
SELECT                              SELECT a.id,
  a.id,                                    b.nombre
  b.nombre                            FROM cab_pedido a
FROM cab_pedido a                     JOIN cliente b ON a.id_cliente = b.id
  JOIN cliente b ON ...              WHERE a.estado = 'A'
WHERE a.estado = 'A'                    AND b.activo = 1;
  AND b.activo = 1;
```
### Otras pestañas de Settings
- **Editor:** letra (solo tipos monoespaciados), tamaño y ancho del tabulador.
- **Colores:** el color, la negrita, la cursiva y el subrayado de cada elemento (palabras reservadas, funciones, números, textos, comentarios, operadores, variables `:x`, hints), **por separado para el tema claro y el oscuro**, con vista previa. Botones *Predeterminados* y *Aspecto SQLTools clásico* (palabras subrayadas, números en negrita…).
- **Ejecución e historial:** qué ejecuta F10 (sentencia delimitada por `;` y `/`, o el bloque entre líneas en blanco; ⌥F10 usa el otro); **commit automático**; qué hacer con los cambios sin confirmar al desconectar o cerrar (deshacer, confirmar o preguntar); Quick Query en minúsculas; y el historial: activarlo, máximo de sentencias, solo las sin error, sin repetidos y si se conserva al cerrar.
- **Autocompletado:** cuándo se abre la lista y **qué tipos de objeto se ofrecen** (tablas, vistas, sinónimos, paquetes, procedimientos, funciones, secuencias, tipos).
- **Exportación:** formato y opciones por destino (ver abajo).

### Exportar resultados: formatos
**Grid > Export** abre un diálogo con el formato y sus opciones; *Copy All* usa el formato del portapapeles y *Open in Editor* el del editor (se eligen en Settings > Exportación).
| Formato | Detalles |
|---|---|
| **CSV** | Separador (`,` `;` `|` tabulador u otro), comillas y escape configurables, entrecomillar siempre, texto para NULL, saltos CRLF, marca BOM (para que Excel lea tildes), prefijo anti-fórmula. |
| **TSV** | Separado por tabuladores. |
| **JSON** | Lista de objetos; números sin comillas, `null` real, legible o compacto. |
| **XML** | Elemento raíz y de fila configurables; nombres de columna en mayúsculas/minúsculas; `xsi:nil` para NULL. |
| **INSERT de SQL** | `INSERT INTO tabla (...) VALUES (...);` por fila; textos con comillas escapadas, números sin comillas, fechas con `TO_DATE` (formato editable) o literales ANSI. |
| **HTML** y **Texto alineado** | Tabla HTML; texto en columnas con los números a la derecha. |
El nombre de archivo sugerido sigue una plantilla con `{fecha}`, `{hora}` y `{tabla}`.

---

## 6. Selección en columna (rectangular)

Es de las herramientas más potentes y menos conocidas. Permite **editar varias líneas a la vez** en la misma columna.

**Cómo se activa**
- **⇧⌥↓ / ⇧⌥↑** (Mayúsculas + Opción + flecha): aparece una línea vertical que cruza varias líneas.
- **⇧⌥→ / ⇧⌥←**: la ensancha y se convierte en un bloque rectangular sombreado.
- O con el menú: **Text > Column Selection (⇧⌘F8)** o **Toggle Selection Mode (⌃Q, B)**. Si ya tenías texto seleccionado, ese texto pasa a ser el bloque.
- **Esc**, un clic con el ratón o cualquier flecha normal salen del modo.
- **Stream Selection (F8)** vuelve a la selección normal.

**Qué puedes hacer con el bloque**
| Acción | Resultado |
|---|---|
| Escribir | El texto se inserta **en todas las líneas** a la vez (y reemplaza el contenido del bloque si tiene ancho). |
| ⌫ / ⌦ | Borra un carácter en cada línea (o el bloque entero si tiene ancho). |
| ⌘C / ⌘X | Copia/corta el bloque, una línea por fila. |
| ⌘V | Pega: si el portapapeles tiene tantas líneas como el bloque, cada línea va a su fila; si tiene **una sola**, se repite en todas. |

**Ejemplos típicos en PL/SQL**
- Añadir `p_` delante de una lista de parámetros: baja el cursor con ⇧⌥↓ y escribe `p_`.
- Alinear los `:=` de una lista de asignaciones.
- Borrar una columna de números de línea pegados al texto.

**Herramientas de columna** (el bloque debe tener ancho, salvo *Insert*):
| Herramienta | Atajo | Qué hace |
|---|---|---|
| **Insert/Fill Columns…** | ⌘K, B | Pide un texto; **insertarlo** en la columna de cada línea, o **rellenar** el ancho del bloque repitiéndolo (ej. una línea de `-----`). |
| **Column Left Justify** | ⌘K, Y | Alinea a la **izquierda** el contenido de cada fila del bloque. |
| **Column Center Justify** | ⌘K, V | Lo **centra**. |
| **Column Right Justify** | ⌘K, W | Alinea a la **derecha** (ideal para una columna de números). |
| **Insert Number…** | ⌘K, M | Numera cada línea del bloque. |

> Límite actual: la columna se cuenta en caracteres; una línea con tabuladores puede verse algo desalineada. Convierte con *Untabify* antes si hace falta.

---

## 7. Buscar, marcadores y plegado (menú Search)

### Buscar
| Herramienta | Atajo | Qué hace |
|---|---|---|
| **Find… / Replace…** | ⌘F / ⌃H | Buscar y reemplazar (mayúsculas, palabra completa, expresión regular). |
| **Find next / previous** | F3 / ⇧F3 | Siguiente / anterior coincidencia. |
| **Find selected / backward** | ⌘F3 / ⇧⌘F3 | Busca el **texto seleccionado** (o la palabra bajo el cursor) sin abrir el diálogo. |
| **Find Match / Match Select** | ⌘] / ⇧⌘] | Salta al **paréntesis, corchete o BEGIN/END pareja**; con *Select* selecciona todo lo que hay entre ambos. |
| **Go To…** | ⌘G | Ir a un número de línea. |
| **Find In Files…** | ⇧⌘F | Busca texto **dentro de todos los archivos de una carpeta** (máscaras, subcarpetas, mayúsculas, regex). Doble clic en un resultado abre el archivo en esa línea. |

### Marcadores
| Herramienta | Atajo | Qué hace |
|---|---|---|
| **Toggle Bookmark** | ⌘F2 | Pone/quita un marcador (punto azul) en la línea. |
| **Next / Previous Bookmark** | F2 / ⇧F2 | Salta entre marcadores. |
| **Remove all Bookmarks** | ⇧⌘F2 | Los quita todos. |
| **Set Random Bookmark 0–9** | ⇧⌘0 … ⇧⌘9 | Marcadores **numerados**: tu «memoria» de 10 posiciones. |
| **Get Random Bookmark 0–9** | ⌘0 … ⌘9 | Salta al marcador numerado. |

### Plegado (Outlining): ocultar bloques de código
Se usa mucho en paquetes largos. Los atajos empiezan con **⌃M**:
| Herramienta | Atajo | Qué hace |
|---|---|---|
| **Toggle Current Region** | ⌃M, M | Pliega/despliega el bloque donde está el cursor. |
| **Collapse / Expand Current Region** | ⌃M, S / ⌃M, E | Solo pliega / solo despliega. |
| **Expand Current Subtree** | ⌃M, J | Despliega el bloque y todo lo que contiene. |
| **Collapse All / Expand All / Toggle All** | ⌃M, A / X / L | Pliega, despliega o invierte **todo**. |
| **Collapse to Level 1 / 2 / 3** | ⌃M, 1 / 2 / 3 | Deja visibles solo N niveles de anidamiento: el nivel 1 muestra solo la estructura exterior (por ejemplo, solo los procedimientos de un paquete). |

---

## 8. Autocompletado y plantillas

- **Por defecto el autocompletado es manual**: pulsa **⌃Espacio** (Control+Espacio) donde quieras sugerencias. Si hay una sola coincidencia, se inserta directamente.
- Puedes cambiarlo en **View > Autocompletado**: *Solo con el atajo*, *Automático después del punto* (`alias.`) o *Siempre automático*.
- Entiende **alias** (`select e.` → columnas de la tabla de `e`), **CTE** (`with …`), subconsultas, esquemas (`esquema.` → tablas) y paquetes (`pkg.` → sus procedimientos).
- **Plantillas (snippets):** escribe una abreviatura y pulsa **⌃Espacio** (o Tab al aceptarla) para expandir un bloque de código típico. Las plantillas aparecen en la lista marcadas con `▸`.

---

## 9. Código y objetos de la base de datos

| Herramienta | Atajo | Qué hace |
|---|---|---|
| **Show in Object Viewer** | F12 | Con el cursor sobre un nombre de objeto (tabla, vista, paquete…), abre el **Object Viewer** con sus propiedades. (Para abrir el código fuente en una pestaña use **Load DDL**, ⌘F12.) |
| **Load DDL script** | ⌘F12 | Carga el DDL del objeto bajo el cursor (código de `ALL_SOURCE` para PL/SQL; `DBMS_METADATA` para tablas, vistas, etc.). |
| **Load DDL con Dbms_Metadata** | ⌥F12 | Igual, pero **siempre** usando `DBMS_METADATA` (incluye atributos como `AUTHID`, etc.). |
| **Extract Schema DDL…** | menú Tools | Genera en una sola pestaña el DDL de **todo un esquema** (tablas, vistas, secuencias, tipos, paquetes, procedimientos, funciones, triggers, sinónimos; índices opcionales). Sirve para respaldar o replicar un esquema. |
| **Object List** (panel) | ⌥3 | Explorador de objetos del esquema, con filtro; doble clic abre el código. **Clic derecho** sobre un objeto abre un menú propio de cada tipo (Query, Load DDL, Drop, Truncate, Compile, Enable/Disable, Create Transformation Script…); admite selección múltiple. Las acciones que modifican la base piden confirmación. |
| **Code Outline** (panel) | ⌥4 | **Esquema del archivo actual**: lista de paquetes, procedimientos y funciones con salto a la línea (doble clic). Se actualiza sola mientras escribes. |
| **File Panel** (panel) | ⌥0 | **Árbol de carpetas** del disco, en la carpeta del archivo actual; doble clic abre el archivo. |
| **Object Viewer** (panel) | ⌥1 / **F12** | Con el cursor sobre el nombre de un objeto, **F12** abre el panel a la derecha con un árbol de sus propiedades: en una tabla, las columnas (tipo, valor por defecto y comentario) y las carpetas **Constraints, Indexes, Triggers, References, Referenced By, Dependent Objects y Granted Privileges**. En paquetes y procedimientos, los subprogramas y parámetros. |
| **Properties** (panel) | ⌥2 | Propiedades del script actual (archivo, tamaño, codificación, saltos de línea, líneas, cursor, selección) y de la sesión. |
| **Snippets** (panel) | ⌥5 | Lista de las plantillas de código; **doble clic** inserta una en el cursor. |
| **Find in Files Output** | ⌥7 | Vuelve a mostrar la ventana de *Buscar en archivos* con el último resultado. |
| **History** (panel) | ⌥8 | Las sentencias ejecutadas, la más reciente primero (⚠ las que dieron error); **doble clic** la inserta en el editor. Pasa el ratón para ver el texto completo, filas y tiempo. |
| **Open Files** (panel) | ⌥9 | Lista de las pestañas abiertas (● = con cambios); clic para ir a una. |

Los paneles aparecen como pestañas en los laterales (los de la izquierda junto a *Objetos*, los de la derecha junto a *Code Outline*) y también se pueden arrastrar para separarlos. Se activan desde el menú **View**.

> Nota: las herramientas de DDL solo **leen** el diccionario de Oracle; no modifican nada. (Están implementadas pero todavía por confirmar contra tu base real.)

---

## 10. Archivos, pestañas, workspaces y snapshots (menú File)

| Herramienta | Atajo | Qué hace |
|---|---|---|
| **New / Open… / Save / Save As… / Save All** | ⌘N / ⌘O / ⌘S / ⇧⌘S / ⌥⇧S | Lo habitual. |
| **New…** | ⇧⌘N | **Nuevo desde plantilla**: eliges «Script vacío», procedimiento, función, paquete, trigger, bloque anónimo, CREATE TABLE… y se abre ya escrito, con el cursor donde hay que completar. |
| **Close / Close All** | ⌘F4 / ⌥⇧F4 | Cierra la pestaña / todas (preguntando si hay cambios). |
| **Recent Files** | menú File | Los últimos 12 archivos abiertos o guardados. |
| **Reload** | ⌃Q, R | Relee el archivo desde disco. |
| **Move / Rename** | menú File | Mueve o renombra **el archivo en disco** y la pestaña lo sigue. |
| **Send Mail** | menú File | Abre tu programa de correo con el texto (la selección o todo) en el cuerpo; se recorta a unos 1800 caracteres, que es el límite de estos enlaces. |

### Workspaces: guardar «mi sesión de trabajo»

Un **workspace** es el conjunto de pestañas abiertas (archivos y también scripts sin guardar, con su texto) más la pestaña activa y la posición del cursor. Sirve para retomar un trabajo exactamente donde lo dejaste, o para tener «el paquete de pestañas del proyecto X».

| Herramienta | Atajo | Qué hace |
|---|---|---|
| **Workspace > Save / Save As…** | ⇧⌘W, S / ⇧⌘W, A | Guarda el conjunto de pestañas en un archivo `.sqlws`. |
| **Workspace > Open…** | ⇧⌘W, O | Cierra las pestañas actuales (preguntando por los cambios) y abre las del workspace. |
| **Workspace > Close** | ⇧⌘W, L | Cierra todas las pestañas y el workspace. |
| **Recent Workspaces** | menú File | Los últimos 8 workspaces. |

### Snapshots: «fotos» rápidas

Un **snapshot** es lo mismo, pero **sin ponerle nombre ni elegir archivo**; sirve de red de seguridad.

| Herramienta | Atajo | Qué hace |
|---|---|---|
| **Snapshot > Quick Save** | ⇧⌘W, Q | Guarda una foto de tus pestañas ahora mismo (se conservan las últimas 20). |
| **Snapshot > Open AS/QS…** | ⇧⌘W, T | Lista las fotos **rápidas (QS)** y las **automáticas (AS)**. Las automáticas se guardan solas **cada vez que cierras el programa** (las últimas 10), así que si cerraste sin guardar un borrador, aquí lo recuperas. Se abren **sin cerrar** tus pestañas actuales. |
| **Recent Quick Snapshots** | menú File | Las últimas 8 fotos rápidas. |
| **Snapshot > Copy / Paste** | ⇧⌘W, C / V | **Copia** la foto al portapapeles como texto y la **pega** en otra copia del programa (o en otro Mac) para pasarte tu sesión sin archivos. |

Al cerrar la aplicación también se recuerdan los archivos abiertos y se restauran al volver a abrirla.

---

## 10b. Ayuda, impresión y ventanas

| Herramienta | Atajo | Qué hace |
|---|---|---|
| **SQL Short Reference** | ⌘F1 | Referencia rápida de SQL y PL/SQL de Oracle (SELECT, JOIN, MERGE, fechas, cadenas, analíticas, CONNECT BY, excepciones, cursores, diccionario…). Si el cursor está sobre una palabra como `merge` o `nvl`, **abre directamente ese tema**; tiene buscador. |
| **Help Topics** | menú Help | Esta guía de herramientas, dentro del programa. |
| **Print… / Print Preview / Print Setup / Page Setup** | ⌘P y menú File | Imprime el script con el diálogo de macOS (también a PDF), con vista previa y configuración de página. |
| **Window > Split** | ⌥⇧P | Muestra una **segunda vista del mismo script** debajo: edites donde edites, cambia en las dos. Sirve para mirar la declaración mientras escribes el cuerpo de un paquete largo. |
| **Move splitter up / down** | ⇧⌘PgUp / ⇧⌘PgDn | Mueve el divisor entre el editor y los resultados. |
| **Table Transformation Helper** | menú Tools | Genera (**sin ejecutar nada**) el script para **reconstruir una tabla**: bloquear, renombrar la original como copia de respaldo, quitar disparadores/restricciones/índices, crear la tabla nueva, copiar los datos y restablecer índices, restricciones (incluidas las de otras tablas que apuntan a ella), disparadores, comentarios y permisos. Lo editas (por ejemplo, para cambiar una columna) y lo ejecutas **paso a paso**. |

---

## 11. Apariencia y ajustes

- **View > Appearance:** *Light* (claro, por defecto), *Dark* (oscuro) o *Match macOS* (sigue el sistema).
- **View > Autocompletado:** cuándo se abre la lista de sugerencias.
- **View > Code Outline / Object List:** paneles laterales.
- **Tools > Settings… (⌃Q, P):** opciones de Format SQL (con vista previa) y del editor (letra, tabulador, autocompletado).
- **Tools > Open Settings Folder:** abre la carpeta de configuración de la aplicación.
- Las preferencias (vista, tema, autocompletado, recientes, ajustes de rejilla…) se guardan solas.

---

## 12. Lo que todavía no está

Casi todo el menú original está hecho (226 de 235 comandos; los demás son marcadores internos del menú que no hacen nada por sí solos). Lo que queda fuera:
- **Selección en columna con ratón** (solo con teclado: ⇧⌥+flechas).
- **Format SQL** es un formateador propio, no el analizador original de SQLTools: en PL/SQL muy complejo puede dar un resultado algo distinto al de Windows; el DDL (CREATE TABLE…) no se reformatea.
- La versión nativa para **chips Apple Silicon** (M1/M2/M3…).

La lista completa y actualizada, comando por comando, está en `docs/MENUS_Y_ATAJOS.md`; el resumen de atajos en `docs/ATAJOS.md`.
