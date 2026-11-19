// Servicio de cancelaciones (VM2)
// recibe las cancelaciones del gestor y le pide a persistencia liberar los asientos
// la notificacion al cliente queda para la segunda entrega

#include "common.hpp"
#include <iostream>
#include <string>

int main() {
    auto cfg = leerConfig();
    zmq::context_t context(1);

    // Socket PULL, recibe lo que manda el gestor
    zmq::socket_t socketGestor(context, ZMQ_PULL);
    socketGestor.set(zmq::sockopt::rcvtimeo, PULSO_MS);   // despertar cada segundo para el watchdog
    socketGestor.bind("tcp://*:" + cfg["CANCELACION_PUERTO"]);

    // REQ hacia la persistencia, con tiempo de espera y reintento (Lazy Pirate).
    // Reintentar ELIMINAR es seguro: si ya se aplico, la segunda vez no encuentra
    // la reserva y no libera nada mas.
    ReqConReintento persistencia(context,
        {direccion(cfg["PERSISTENCIA_IP"], cfg["PERSISTENCIA_PUERTO"])},
        std::stoi(cfg["ESPERA_PERSISTENCIA_MS"]), std::stoi(cfg["INTENTOS_PERSISTENCIA"]), "Cancelacion");

    std::cout << "Cancelaciones escuchando en puerto " << cfg["CANCELACION_PUERTO"]
              << ", conectado a persistencia en " << cfg["PERSISTENCIA_IP"] << ":"
              << cfg["PERSISTENCIA_PUERTO"] << std::endl;

    avisarListo();   // a systemd: ya hice el bind, estoy listo

    while (true) {
        avisarVivo();   // watchdog: cada vuelta, al menos una por segundo, systemd sabe que sigue bien
        zmq::message_t request;
        if (!socketGestor.recv(request, zmq::recv_flags::none)) continue;   // no llego nada en 1 s
        std::string msg(static_cast<char*>(request.data()), request.size());   // ej: "ELIMINAR E5 2026-11-02 juan"
        std::cout << "[Cancelacion] Recibido: " << msg << std::endl;

        auto resp = persistencia.pedir(msg);
        std::string respuesta = resp ? *resp : "la persistencia no responde, la cancelacion no se aplico";

        std::cout << "[Cancelacion] Resultado: " << respuesta
                  << "  (notificacion al cliente: 2a entrega)" << std::endl;
    }
}
