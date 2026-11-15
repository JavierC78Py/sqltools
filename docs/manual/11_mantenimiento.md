# Apéndice C. Cómo se mantiene este manual (para quien lo edite)

## Estructura
```
docs/manual/
  00_inicio.md … 11_mantenimiento.md   capítulos (texto en Markdown, uno por tema)
  img/                                 capturas (se GENERAN; no editar a mano)
  salida/                              HTML y PDF construidos (se GENERAN)
docs/GUIA_HERRAMIENTAS.md              Apéndice A: todas las herramientas (se incluye al construir)
docs/ATAJOS.md                         Apéndice B: atajos
docs/MENUS_Y_ATAJOS.md                 lista completa generada del programa
tools/build_manual.sh                  regenera capturas + documentos + HTML + PDF
tools/build_manual_html.py + manual.css  diseño del HTML/PDF (portada, índice lateral, estilos); el PDF necesita Chrome, Brave o Edge instalado
```

## Flujo de trabajo
1. **Editar** el capítulo que corresponda (son archivos de texto: se ven los cambios con `git diff`).
2. **Regenerar todo** con un solo comando:
   ```bash
   tools/build_manual.sh
   ```
   Esto (a) vuelve a tomar **todas las capturas** con la versión actual del programa y datos ficticios, (b) regenera `MENUS_Y_ATAJOS.md`, y (c) construye `docs/manual/salida/SQLTools-Mac-Manual.html` y `.pdf`.
3. Revisar las capturas nuevas y publicar el HTML/PDF junto a la aplicación.

## Reglas para que siga siendo fácil de mantener
- **Una idea, un lugar.** Si una explicación ya está en la guía (Apéndice A), enlácela en vez de copiarla.
- **Nunca pegar capturas a mano.** Si falta una pantalla, se añade a `ui/ManualShots.cpp` (una línea) y se vuelve a generar.
- **Datos ficticios siempre.** El generador usa un dominio de preferencias de prueba y no toca sus ajustes ni sus conexiones.
- **Cuando se añada un ajuste nuevo**: (1) añadirlo a la tabla del capítulo 9, (2) regenerar.
- **Cuando se añada un comando nuevo**: aparece solo en `MENUS_Y_ATAJOS.md`; describirlo en la guía (Apéndice A).
