# SQLTools Windows vs. SQLTools Mac — verificación de igualdad (11/11/2026)

**Resultado (actualizado tras exportación, ejecución/historial, colores y perfiles de formato): ≈ 84 % de igualdad global** (rango razonable 80–86 %; antes 78 %). Para el trabajo diario de un desarrollador PL/SQL/APEX (escribir, ejecutar, ver resultados, buscar objetos) la igualdad práctica es mayor (~90 %); lo que falta son sobre todo las pantallas de configuración y los formatos de exportación.

## Lo medido (datos, no opinión)
| Medición | Resultado |
|---|---|
| Comandos del menú original implementados | 226 de 235 (96 %); los 9 restantes son marcadores internos del menú |
| Atajos del original con atajo asignado y funcionando | 162 de 162 (100 %), traducidos a Mac (Ctrl→⌘, Alt→⌥) |
| Menús / barras de herramientas | 12 de 12 / 5 de 5, generados desde los recursos del programa original |
| Iconos de barras | 75 de 75, los originales |
| Capa Oracle (OCI) | código original portado; tipos de datos 25/25, binds 12/12, formato de fechas validado contra TO_CHAR en tu base real |
| Pruebas automáticas | 146 del programa + 9 suites sueltas, todo en verde |
| Opciones de configuración del original | ≈115 (11 páginas de opciones); en la versión Mac hay ≈55 (≈48 %) |
| Diálogos del original | 55 plantillas; en la versión Mac ≈20 diálogos propios |

## Estimación ponderada (el peso y los porcentajes de cada área son mi criterio)
| Área | Peso | Igualdad | Por qué |
|---|---|---|---|
| Menús, comandos y atajos | 25 % | 96 % | todo implementado; algunas funciones son versiones propias (Format SQL, Object Viewer) |
| Barras e iconos | 5 % | 100 % | originales |
| Núcleo Oracle (conexión, ejecución, tipos, binds, fechas, prefetch) | 25 % | 90 % | mismo código portado y verificado; falta probar más escenarios (wallets distintos, LOBs enormes) |
| Editor (colores, plegado, marcadores, columna, autocompletado) | 15 % | 88 % | ya hay colores por tipo de sintaxis (claro/oscuro), fuente y filtros de autocompletado; falta selección en columna con ratón |
| Rejilla y resultados | 10 % | 85 % | ya exporta CSV/TSV/JSON/XML/INSERT/HTML/texto con opciones; falta formato por tipo de columna y la «página 2» de opciones de rejilla |
| Explorador / Object Viewer | 5 % | 50 % | el original tiene árbol con muchas opciones (comentarios, ordenar, infotips) |
| Configuración (páginas de opciones) | 10 % | 50 % | ≈55 de ≈115 opciones: Format SQL, editor, colores, ejecución, historial, autocompletado y exportación; faltan opciones del Object Viewer, DDL y rejilla avanzada |
| Format SQL | 5 % | 70 % | formateador propio con perfiles y «deducir de un ejemplo»; no es el analizador original (~20 000 líneas) |
| **Total ponderado** | 100 % | **≈ 84 %** | 24.0 + 5.0 + 22.5 + 13.2 + 8.5 + 2.5 + 5.0 + 3.5 |

## Lo que NO se ha podido verificar
- No se ha comparado lado a lado con la versión de Windows ejecutándose sobre la misma base: las cifras de comandos y pruebas son medidas; la «igualdad de comportamiento» de cada pantalla es estimada.
- Sin verificar contra Oracle: Load DDL / Extract Schema DDL, columnas del Object Viewer, Table Transformation Helper.
- Execute in SQL*Plus: depende de tener `sqlplus` instalado.

## Cómo subir el porcentaje (en orden de rendimiento)
1. Opciones avanzadas de la rejilla (formato por tipo de columna, ancho, LOBs) y del Object Viewer: +3 %
2. Selección en columna con ratón y más ajustes del editor: +1 %
3. Verificación lado a lado con Windows (lista de 30 casos): afina la estimación, no suma.
4. Probar y corregir lo no verificado contra Oracle (DDL, Object Viewer, Table Transformer): protege el porcentaje actual.
