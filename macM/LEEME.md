# SQLTools Mac — versión para Mac con chip **Apple Silicon** (M1/M2/M3/M4)

> ## ⬇️ Descargar: dos partes en esta carpeta
> GitHub no admite archivos de más de 100 MB, por eso el zip está dividido:
> [`SQLTools-Mac-arm64.zip.parte-aa`](SQLTools-Mac-arm64.zip.parte-aa) y [`SQLTools-Mac-arm64.zip.parte-ab`](SQLTools-Mac-arm64.zip.parte-ab) (botón *Download raw file* en cada una).
>
> Cuando tenga ambas en *Descargas*, **únalas** con este comando en Terminal:
>
> ```bash
> cd ~/Downloads && cat SQLTools-Mac-arm64.zip.parte-* > SQLTools-Mac-arm64.zip && shasum -a 256 SQLTools-Mac-arm64.zip
> ```
>
> El número que muestra debe ser igual al de [`SHA256.txt`](SHA256.txt). Después descomprima el zip y siga los pasos de abajo.

Versión nativa **arm64**: no necesita Rosetta.

## Para quien va a probarla
1. Descargue el zip ya unido (ver el recuadro de arriba) y descomprímalo.
2. Arrastre **SQLTools Mac.app** a *Aplicaciones*.
3. La primera vez: clic derecho sobre la app → **Abrir** → **Abrir**. Si macOS dice que está dañada:
   `xattr -dr com.apple.quarantine "/Applications/SQLTools Mac.app"`
4. Configure su conexión como indica el `LEEME.md` incluido en el zip.

## Para quien la compila
1. Descargue de Oracle el **Instant Client ARM64** (*Basic* y *SDK*, misma versión; probado con 23.26. El *Basic* viene en `.dmg` y el *SDK* en `.pkg`) desde
   <https://www.oracle.com/database/technologies/instant-client/macos-arm64-downloads.html> (hay que aceptar la licencia OTN).
2. Descomprima ambos en la raíz del proyecto, de modo que quede **`instantclient_arm64/`** con `libclntsh.dylib` y `sdk/include`.
3. Ejecute:
```bash
bash macM/construir.sh          # genera macM/paquete/SQLTools-Mac-arm64.zip
```
Qt 6.8 del instalador oficial ya es universal (Intel + Apple Silicon), así que se puede compilar desde un Mac Intel o desde uno M.

> Estado: compilada con Oracle Instant Client 23.26 ARM64 (ejecutable y librerías arm64, firma ad-hoc válida). Aún no se ha ejecutado en un Mac M real: el primer probador debe avisar de cualquier fallo.
