// rmc.cpp  -  corre en la VM2 (capa de logica de negocio)
//
// Servicio de Reserva, Modificacion y Consulta. Aplica las reglas del negocio,
// pero nunca abre la BD: todo se lo pide a la Persistencia.
//
// Recibe del Gestor:
//   CONSULTA <mes>                               -> reenvia a Persistencia y devuelve la lista
//   RESERVA <idEvento> <fecha> <cliente> <n>     -> EXITO ... | RECHAZO ...
//   (MODIFICACION se implementa en la 2a entrega)

#include <zmq.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include "config.h"

// Envia una orden a la Persistencia y espera la respuesta (REQ: enviar -> recibir).
std::string pedirAPersistencia(zmq::socket_t& socketPersistencia, const std::string& orden) {
    std::cout << "[RMC]   -> Persistencia: " << orden << std::endl;
    socketPersistencia.send(zmq::buffer(orden), zmq::send_flags::none);

    zmq::message_t resp;
    auto res = socketPersistencia.recv(resp, zmq::recv_flags::none);
    std::string respuesta(static_cast<char*>(resp.data()), resp.size());
    std::cout << "[RMC]   <- Persistencia: "
              << (respuesta.find('\n') == std::string::npos ? respuesta : "(lista de eventos)") << std::endl;
    return respuesta;
}

// Consulta: enviar a persistencia, esperar y devolver el resultado al Gestor.
std::string consultar(zmq::socket_t& socketPersistencia, const std::string& mes) {
    std::string lista = pedirAPersistencia(socketPersistencia, "CONSULTA " + mes);
    if (lista.empty()) return "RECHAZO: no hay eventos en " + mes;
    return "EXITO\n" + lista;
}

// Reserva (pasos del enunciado):
//  1. Recibir la solicitud desde el Gestor.
//  2. Consultar a la Persistencia la disponibilidad en tiempo real.
//  3. Si el numero de asientos esta disponible, confirmar la reserva en la BD.
//  4. Responder Exito / Rechazo al Gestor.
std::string reservar(zmq::socket_t& socketPersistencia, const std::string& idEvento,
                     const std::string& fecha, const std::string& cliente, int numAsientos) {
    if (numAsientos <= 0) return "RECHAZO: numero de asientos invalido";

    int libres = std::stoi(pedirAPersistencia(socketPersistencia, "DISPONIBLES " + idEvento + " " + fecha));
    if (libres < 0)
        return "RECHAZO: el evento " + idEvento + " no tiene ocurrencia el " + fecha;
    if (libres < numAsientos)
        return "RECHAZO: solo hay " + std::to_string(libres) + " asientos libres";

    std::string r = pedirAPersistencia(socketPersistencia,
        "RESERVAR " + idEvento + " " + fecha + " " + cliente + " " + std::to_string(numAsientos));

    // Si otro cliente gano la carrera, la Persistencia responde SIN_CUPO
    if (r.rfind("OK", 0) == 0)
        return "EXITO: reserva " + r.substr(3) + " de " + std::to_string(numAsientos) +
               " asientos en " + idEvento + " el " + fecha;
    return "RECHAZO: " + r;
}

int main() {
    auto cfg = leerConfig();
    zmq::context_t context(1);

    // REP hacia el Gestor (el Gestor se conecta a este puerto)
    zmq::socket_t socketGestor(context, ZMQ_REP);
    socketGestor.bind("tcp://*:" + cfg["RMC_PUERTO"]);

    // REQ hacia la Persistencia (VM3)
    zmq::socket_t socketPersistencia(context, ZMQ_REQ);
    socketPersistencia.connect(direccion(cfg, "PERSISTENCIA"));

    std::cout << "[RMC] Escuchando al Gestor en puerto " << cfg["RMC_PUERTO"]
              << ", conectado a Persistencia en " << direccion(cfg, "PERSISTENCIA") << std::endl;

    while (true) {
        zmq::message_t request;
        auto res = socketGestor.recv(request, zmq::recv_flags::none);
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
                respuesta = consultar(socketPersistencia, mes);
            } else if (comando == "RESERVA") {
                std::string idEvento, fecha, cliente;
                int numAsientos = 0;
                iss >> idEvento >> fecha >> cliente >> numAsientos;
                respuesta = reservar(socketPersistencia, idEvento, fecha, cliente, numAsientos);
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
