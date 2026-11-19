// RMC (VM2): revisa disponibilidad con la persistencia y decide si la reserva se hace

#include "common.hpp"
#include <iostream>
#include <sstream>
#include <string>

// le manda una orden a la persistencia y espera la respuesta.
// Si la persistencia no contesta a tiempo se reintenta (Lazy Pirate); si se agotan
// los intentos devuelve vacio (nullopt) y el RMC responde RECHAZO en vez de quedarse colgado.
std::optional<std::string> pedirAPersistencia(ReqConReintento& persistencia, const std::string& orden,
                                              int intentos = 0) {
    std::cout << "[RMC]   -> Persistencia: " << orden << std::endl;
    auto respuesta = persistencia.pedir(orden, intentos);
    if (respuesta && orden.substr(0, 8) != "CONSULTA") {
        std::cout << "[RMC]   <- Persistencia: " << *respuesta << std::endl;
    }
    return respuesta;
}

// consulta: se le pide a persistencia y se devuelve la lista al gestor
std::string consultar(ReqConReintento& persistencia, const std::string& mes) {
    auto lista = pedirAPersistencia(persistencia, "CONSULTA " + mes);
    if (!lista) return "RECHAZO: la persistencia no responde";
    if (lista->empty()) return "RECHAZO: no hay eventos en " + mes;
    return "EXITO\n" + *lista;
}

// reserva: primero pregunta cuantos asientos hay libres, si alcanzan manda a reservar
std::string reservar(ReqConReintento& persistencia, const std::string& idEvento,
                     const std::string& fecha, const std::string& cliente, int numAsientos) {
    if (numAsientos <= 0) return "RECHAZO: numero de asientos invalido";

    // DISPONIBLES es una lectura: si se pierde la respuesta, preguntar otra vez no daña nada
    auto disp = pedirAPersistencia(persistencia, "DISPONIBLES " + idEvento + " " + fecha);
    if (!disp) return "RECHAZO: la persistencia no responde";
    int libres = std::stoi(*disp);
    if (libres < 0)
        return "RECHAZO: el evento " + idEvento + " no tiene ocurrencia el " + fecha;
    if (libres < numAsientos)
        return "RECHAZO: solo hay " + std::to_string(libres) + " asientos libres";

    // RESERVAR es una escritura: se manda UNA sola vez (intentos = 1). Si la persistencia
    // la aplico pero la respuesta se perdio, reenviarla reservaria dos veces.
    auto r = pedirAPersistencia(persistencia,
        "RESERVAR " + idEvento + " " + fecha + " " + cliente + " " + std::to_string(numAsientos), 1);
    if (!r) return "RECHAZO: la persistencia no responde";

    // si otro cliente reservo justo antes, persistencia responde SIN_CUPO
    if (r->rfind("OK", 0) == 0)
        return "EXITO: reserva " + r->substr(3) + " de " + std::to_string(numAsientos) +
               " asientos en " + idEvento + " el " + fecha;
    return "RECHAZO: " + *r;
}

int main() {
    auto cfg = leerConfig();
    zmq::context_t context(1);

    // Socket REP hacia los gestores. El RMC hace CONNECT a los dos (principal y respaldo),
    // por eso se pueden levantar varios RMC a la vez: ./rmc 1, ./rmc 2, ./rmc 3
    zmq::socket_t socketGestor(context, ZMQ_REP);
    socketGestor.connect(direccion(cfg["GESTOR_IP"], cfg["RMC_BACKEND_PUERTO"]));
    socketGestor.connect(direccion(cfg["GESTOR_IP"], cfg["RMC_BACKEND2_PUERTO"]));

    // REQ hacia la persistencia (VM3), con tiempo de espera y reintento
    ReqConReintento persistencia(context,
        {direccion(cfg["PERSISTENCIA_IP"], cfg["PERSISTENCIA_PUERTO"])},
        std::stoi(cfg["ESPERA_PERSISTENCIA_MS"]), std::stoi(cfg["INTENTOS_PERSISTENCIA"]), "RMC");

    std::cout << "RMC conectado a los gestores (" << cfg["RMC_BACKEND_PUERTO"] << " y "
              << cfg["RMC_BACKEND2_PUERTO"] << ") y a persistencia en "
              << cfg["PERSISTENCIA_IP"] << ":" << cfg["PERSISTENCIA_PUERTO"] << std::endl;

    socketGestor.set(zmq::sockopt::rcvtimeo, PULSO_MS);   // despertar cada segundo para el watchdog
    avisarListo();   // a systemd: ya estoy conectado y listo

    while (true) {
        avisarVivo();   // watchdog: cada vuelta, al menos una por segundo, systemd sabe que sigue bien
        zmq::message_t request;
        if (!socketGestor.recv(request, zmq::recv_flags::none)) continue;   // no llego nada en 1 s
        std::string msg(static_cast<char*>(request.data()), request.size());
        std::cout << "[RMC] Recibido: " << msg << std::endl;

        // Interpretar comando
        std::istringstream iss(msg);
        std::string comando;
        iss >> comando;
        std::string respuesta;

        try {
            if (comando == "CONSULTA") {
                std::string mes;
                iss >> mes;
                respuesta = consultar(persistencia, mes);
            } else if (comando == "RESERVA") {
                std::string idEvento, fecha, cliente;
                int numAsientos = 0;
                iss >> idEvento >> fecha >> cliente >> numAsientos;
                respuesta = reservar(persistencia, idEvento, fecha, cliente, numAsientos);
            } else if (comando == "MODIFICACION") {
                respuesta = "RECHAZO: la modificacion se implementa en la segunda entrega";
            } else {
                respuesta = "RECHAZO: comando no reconocido";
            }
        } catch (const std::exception&) {
            respuesta = "RECHAZO: formato invalido";
        }

        std::cout << "[RMC] Resultado: " << respuesta.substr(0, respuesta.find('\n')) << std::endl;
        socketGestor.send(zmq::buffer(respuesta), zmq::send_flags::none);
    }
}
