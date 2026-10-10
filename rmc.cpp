// RMC (VM2): revisa disponibilidad con la persistencia y decide si la reserva se hace

#include <zmq.hpp>
#include <iostream>
#include <sstream>
#include <string>

// le manda una orden a la persistencia y espera la respuesta
std::string pedirAPersistencia(zmq::socket_t& socketPersistencia, const std::string& orden) {
    std::cout << "[RMC]   -> Persistencia: " << orden << std::endl;
    socketPersistencia.send(zmq::buffer(orden), zmq::send_flags::none);

    zmq::message_t resp;
    auto res = socketPersistencia.recv(resp, zmq::recv_flags::none);
    std::string respuesta(static_cast<char*>(resp.data()), resp.size());
    if (orden.substr(0, 8) != "CONSULTA") {
        std::cout << "[RMC]   <- Persistencia: " << respuesta << std::endl;
    }
    return respuesta;
}

// consulta: se le pide a persistencia y se devuelve la lista al gestor
std::string consultar(zmq::socket_t& socketPersistencia, const std::string& mes) {
    std::string lista = pedirAPersistencia(socketPersistencia, "CONSULTA " + mes);
    if (lista.empty()) return "RECHAZO: no hay eventos en " + mes;
    return "EXITO\n" + lista;
}

// reserva: primero pregunta cuantos asientos hay libres, si alcanzan manda a reservar
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

    // si otro cliente reservo justo antes, persistencia responde SIN_CUPO
    if (r.rfind("OK", 0) == 0)
        return "EXITO: reserva " + r.substr(3) + " de " + std::to_string(numAsientos) +
               " asientos en " + idEvento + " el " + fecha;
    return "RECHAZO: " + r;
}

int main() {
    zmq::context_t context(1);

    // Socket REP para el gestor
    zmq::socket_t socketGestor(context, ZMQ_REP);
    socketGestor.bind("tcp://*:5556");

    // Socket REQ para la persistencia (VM3)
    zmq::socket_t socketPersistencia(context, ZMQ_REQ);
    socketPersistencia.connect("tcp://10.43.99.231:6666"); // IP de la VM de persistencia

    std::cout << "RMC escuchando en puerto 5556, conectado a persistencia..." << std::endl;

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
