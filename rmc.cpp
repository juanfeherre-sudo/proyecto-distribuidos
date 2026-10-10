// rmc.cpp  —  corre en la VM2 (capa de lógica de negocio)
//
// Servicio de Reserva, Modificación y Consulta. Es "el cerebro": aplica las reglas
// de negocio, pero NUNCA toca la BD; todo lo que necesita se lo pide a la Persistencia.
//
// Recibe del Gestor (REP):
//   CONSULTA <mes>                                -> EXITO + lista   |  RECHAZO <motivo>
//   RESERVA <idEvento> <fecha> <cliente> <n>      -> EXITO reserva <id>  |  RECHAZO <motivo>
//   (MODIFICACION se implementa en la 2ª entrega)

#include "common.hpp"

// Manda una orden a la Persistencia y espera su respuesta (REQ: envío -> recibo, siempre en ese orden).
std::string pedirAPersistencia(zmq::socket_t& persistencia, const std::string& orden) {
    log("RMC", "  -> Persistencia: " + orden);
    enviar(persistencia, orden);
    std::string resp = recibir(persistencia);
    log("RMC", "  <- Persistencia: " + (resp.find('\n') == std::string::npos ? resp : "(lista de eventos)"));
    return resp;
}

// Consulta: solo reenvía la petición y devuelve lo que diga la Persistencia.
std::string consultar(zmq::socket_t& persistencia, const std::string& mes) {
    std::string lista = pedirAPersistencia(persistencia, "EVENTOS " + mes);
    if (lista.empty()) return "RECHAZO no hay eventos en " + mes;
    return "EXITO\n" + lista;
}

// Reserva: aquí está la regla de negocio.
//   1. Validar la solicitud.
//   2. Preguntar a la Persistencia cuántos asientos libres hay (disponibilidad en tiempo real).
//   3. Si alcanzan, ordenar registrar la reserva; si no, rechazar sin tocar la BD.
//   4. Responder Éxito o Rechazo al Gestor.
std::string reservar(zmq::socket_t& persistencia, const std::string& idEvento,
                     const std::string& fecha, const std::string& cliente, int n) {
    if (n <= 0) return "RECHAZO numero de asientos invalido";

    int libres = std::stoi(pedirAPersistencia(persistencia, "DISPONIBLES " + idEvento + " " + fecha));
    if (libres < 0) return "RECHAZO el evento " + idEvento + " no tiene ocurrencia el " + fecha;
    if (libres < n)  return "RECHAZO solo hay " + std::to_string(libres) + " asientos libres";

    std::string r = pedirAPersistencia(persistencia,
        "REGISTRAR " + idEvento + " " + fecha + " " + cliente + " " + std::to_string(n));

    // La Persistencia re-verifica el cupo: si otro cliente ganó la carrera, responde SIN_CUPO.
    if (r.rfind("OK", 0) == 0) return "EXITO reserva " + r.substr(3) + ": " + std::to_string(n) +
                                      " asientos en " + idEvento + " el " + fecha;
    return "RECHAZO " + r;
}

int main() {
    auto cfg = leerConfig();
    zmq::context_t contexto(1);

    // Socket hacia ARRIBA: el Gestor nos manda solicitudes y esperamos para responderle.
    zmq::socket_t gestor(contexto, zmq::socket_type::rep);
    gestor.bind("tcp://*:" + cfg["RMC_PUERTO"]);

    // Socket hacia ABAJO: nosotros le preguntamos a la Persistencia (en la VM3).
    zmq::socket_t persistencia(contexto, zmq::socket_type::req);
    persistencia.connect(direccion(cfg["PERSISTENCIA_IP"], cfg["PERSISTENCIA_PUERTO"]));

    log("RMC", "Escuchando al Gestor en el puerto " + cfg["RMC_PUERTO"] +
               ", conectado a Persistencia en " + direccion(cfg["PERSISTENCIA_IP"], cfg["PERSISTENCIA_PUERTO"]));

    while (true) {
        std::string solicitud = recibir(gestor);
        log("RMC", "<- Gestor: " + solicitud);
        auto p = partir(solicitud);
        std::string respuesta;

        try {
            if (p.size() == 2 && p[0] == "CONSULTA") {
                respuesta = consultar(persistencia, p[1]);
            } else if (p.size() == 5 && p[0] == "RESERVA") {
                respuesta = reservar(persistencia, p[1], p[2], p[3], std::stoi(p[4]));
            } else if (!p.empty() && p[0] == "MODIFICACION") {
                respuesta = "RECHAZO modificacion disponible en la segunda entrega";
            } else {
                respuesta = "RECHAZO solicitud mal formada";
            }
        } catch (const std::exception&) {
            respuesta = "RECHAZO formato invalido";
        }

        log("RMC", "-> Gestor: " + respuesta.substr(0, respuesta.find('\n')));
        enviar(gestor, respuesta);
    }
}
