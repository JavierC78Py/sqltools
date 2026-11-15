# SQLTools Mac — guía rápida para probar

## Instalar
1. Descomprima `SQLTools-Mac-x86_64.zip` y arrastre **SQLTools Mac.app** a la carpeta *Aplicaciones*.
2. **La primera vez**: haga clic derecho (o Control+clic) sobre la app → **Abrir** → **Abrir**. macOS avisa porque la app no está firmada por un desarrollador registrado; es normal.
   Si macOS dice que «está dañada» o no deja abrirla, ejecute en Terminal:
   `xattr -dr com.apple.quarantine "/Applications/SQLTools Mac.app"`
3. Mac con chip **Apple (M1/M2/M3/M4)**: macOS pedirá instalar **Rosetta** la primera vez; acepte (esta versión es para procesadores Intel).

## Configurar su conexión (una sola vez)
1. Tenga a mano su **carpeta TNS**: la que contiene `tnsnames.ora`, con la ruta de cada wallet de este Mac en `(security=(my_wallet_directory=/ruta/a/la/wallet)…)`.
2. Abra la app → **Session > Connect…** (Cmd+D, C) → **Ajustes…** → elija esa carpeta → **Comprobar TNS…** para ver si algún alias tiene la wallet mal.
3. Cree su conexión (etiqueta, usuario, alias, contraseña) y pulse **Probar**, luego **Conectar**. La contraseña se guarda en el Llavero de macOS.
   (También puede usar **Importar…** con un `connections.xml` de SQLTools de Windows: no trae contraseñas.)

## Qué probar y cómo avisar de problemas
Use la app con su trabajo real: ejecutar sentencias y scripts, abrir paquetes desde el explorador, autocompletado, buscar/reemplazar, marcadores, plegado de código, historial…
Si algo falla o se ve mal envíe: **(1)** captura o texto del error, **(2)** qué estaba haciendo, **(3)** el diagnóstico: menú **Help > About… > Copiar diagnóstico** y péguelo en el mensaje. **No envíe contraseñas.**

La lista completa de menús y atajos está en la carpeta *Documentos*.
