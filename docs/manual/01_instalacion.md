# 1. Instalación y primer arranque

## Instalar
1. Descomprima `SQLTools-Mac-….zip` y arrastre **SQLTools Mac.app** a la carpeta *Aplicaciones*. La aplicación ya trae todo lo que necesita (incluido el cliente Oracle): **no hay que instalar nada más**.
2. **La primera vez**, haga clic derecho (o Control+clic) sobre la aplicación → **Abrir** → **Abrir**. macOS avisa porque la aplicación no está firmada por un desarrollador registrado; es normal.
   Si macOS dice que «está dañada» o no la deja abrir, ejecute una vez en Terminal:
   ```bash
   xattr -dr com.apple.quarantine "/Applications/SQLTools Mac.app"
   ```
3. En un Mac con chip **Apple (M1/M2/M3/M4)** macOS le pedirá instalar **Rosetta** la primera vez si la versión es para Intel; acepte. Existe una versión nativa para chip Apple.

## Qué verá al abrir
La ventana principal (capítulo 3) con un script vacío, el panel de objetos a la izquierda y los resultados abajo. Al cerrar y volver a abrir, se recuperan los archivos que tenía abiertos.

## Claro u oscuro
Por defecto la aplicación usa el tema **claro** (los iconos originales se diseñaron sobre gris claro). Cambie a oscuro o al del sistema en **View > Appearance**.
