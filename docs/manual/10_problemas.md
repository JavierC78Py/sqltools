# 10. Solución de problemas

| Síntoma | Causa y solución |
|---|---|
| **ORA-12154: TNS: could not resolve the connect identifier** | Alias no encontrado o wallet con ruta equivocada. En **Connect… > Ajustes…** revise la carpeta TNS y use **Comprobar TNS…**; la ruta `my_wallet_directory` debe existir **en este Mac**. |
| **ORA-01017: invalid username/password** | Usuario o contraseña incorrectos. Reescriba la contraseña en la conexión (se guarda en el Llavero). |
| macOS dice que la aplicación «está dañada» o no deja abrirla | Ejecute `xattr -dr com.apple.quarantine "/Applications/SQLTools Mac.app"` y ábrala con clic derecho → Abrir. |
| Las teclas F5, F10, F11 no hacen nada | Pulse **Fn**+tecla, o active «Usar teclas F1, F2… como teclas estándar». **F11** además puede chocar con «Mostrar escritorio»: cambie ese atajo en Ajustes del Sistema → Teclado → Atajos. |
| El autocompletado no aparece | Por defecto es **manual**: pulse **⌃Espacio**. Para que sea automático, cámbielo en Settings > Autocompletado. |
| Las tildes salen mal (`Ã±`) | Codificación equivocada: **Text/Format > Encoding > ANSI / OEM / UTF-8** relee el archivo. |
| Al exportar a CSV Excel no muestra bien las tildes | Marque «Escribir marca BOM de UTF-8» en el diálogo de exportar. |
| Un cambio no se ve en otra sesión | No lo ha confirmado: **Session > Commit** (⌘D, T). |
| Una consulta larga no responde | **Session > Cancel** (⌘D, A) la interrumpe. |
| Quiero volver a los ajustes de fábrica | Cierre la aplicación y borre las preferencias (**Tools > Open Settings Folder** indica dónde). |

## Pedir ayuda
Envíe: **(1)** una captura o el texto del error, **(2)** qué estaba haciendo, **(3)** el diagnóstico: **Help > About… > Copiar diagnóstico**. **Nunca envíe contraseñas.**
