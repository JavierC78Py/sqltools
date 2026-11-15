# SQLTools Mac — versión para Mac con chip **Intel**

> ## ⬇️ [Descargar SQLTools-Mac-x86_64.zip](https://github.com/JavierC78Py/sqltools/releases/download/v0.1.0-prueba/SQLTools-Mac-x86_64.zip)
> Es el programa listo para usar (Intel). Descárguelo, descomprímalo y siga los pasos de abajo.

Para Macs con procesador **Intel** (x86_64). En un Mac con chip Apple (M1…M4) funciona con Rosetta, pero use mejor la carpeta [`macM`](../macM).

¿Cómo saber qué chip tiene su Mac?  → menú  > **Acerca de este Mac** > *Chip* o *Procesador*.

## Para quien va a probarla
1. Descargue **`SQLTools-Mac-x86_64.zip`** de la sección **Releases** del repositorio y descomprímalo.
2. Arrastre **SQLTools Mac.app** a *Aplicaciones*.
3. La primera vez: clic derecho sobre la app → **Abrir** → **Abrir** (no está firmada por Apple). Si macOS dice que está dañada:
   `xattr -dr com.apple.quarantine "/Applications/SQLTools Mac.app"`
4. Configure su conexión como indica el `LEEME.md` incluido en el zip (carpeta TNS/wallet y contraseña en el Llavero).

## Para quien la compila
```bash
bash macIntel/construir.sh      # genera macIntel/paquete/SQLTools-Mac-x86_64.zip
```
Necesita Qt 6.8 y el Oracle Instant Client Intel (Basic + SDK) en `instantclient_19_16/` (ver el README principal). El zip **no se guarda en git**: se publica como *Release*.
