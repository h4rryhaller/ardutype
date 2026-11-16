#ifndef ADMIN_H
#define ADMIN_H

// Administración desde el PC por el puerto serie USB (herramienta
// ardutype_sync.py, que sincroniza con KeePassXC).
//
// Protocolo de líneas de texto a 115200 (en USB da igual la velocidad). Las
// respuestas empiezan por '@'; las demás líneas del puerto son mensajes de
// depuración y la herramienta las ignora. Los textos van en hexadecimal (UTF-8)
// y "-" es un campo vacío.
//
//   HOLA                    -> @ARDUTYPE 1 <SIN_FS|NUEVA|CERRADA|ABIERTA>
//   PENDIENTES              -> @PENDIENTES <n>
//   RECIBIR                 -> (confirmación en el dispositivo si hay alguno)
//                              @SERV <id> <uuid|-> <cat> <nombre> <usuario> <contraseña> <guion>
//                              @BORRADO <uuid> <nombre>
//                              @FIN
//   ENLAZAR <id> <uuid>     -> @OK   el PC ha creado la entrada de un servicio nuevo
//   CONFIRMADO              -> @OK   el PC ya ha guardado los cambios recibidos
//   ENVIAR                  -> @OK   empieza la lista completa desde KeePassXC
//   SERV <uuid> <cat> <nombre> <usuario> <contraseña> <guion>  -> @OK
//   FIN                     -> @RESUMEN <nuevos> <cambiados> <borrados>
//                              (confirmación en el dispositivo) @OK
//   ADIOS                   -> @OK
//   Cualquier fallo         -> @ERR <motivo>
//
// Notificaciones (notificacion.h), también con el Ardutype bloqueado:
//   AVISO <origen> <texto>                    -> @OK
//   PREGUNTA <id> <segundos> <origen> <texto> -> @OK, y al contestar o vencer
//                                                el plazo @RESPUESTA <id> SI|NO|TIEMPO
//   RETIRAR <id>                              -> @OK  (el PC ya no espera respuesta)
//   Sin respuesta si se cierra el puerto. «@ERR ocupado» si ya hay una
//   pregunta o se está sincronizando.
//
// Todo menos HOLA y las notificaciones exige el Ardutype desbloqueado, y abre en él la pantalla
// «PC conectado». Nada que lleve contraseñas sale ni entra sin confirmarlo en
// el dispositivo. ENVIAR se rechaza mientras haya cambios del dispositivo sin
// recoger, para que la lista de KeePassXC no los pise.
void adminTick();
void adminCancelar();  // el usuario sale de la pantalla del PC o se bloquea

#endif
