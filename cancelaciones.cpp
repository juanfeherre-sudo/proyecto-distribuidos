// Servicio de cancelaciones (VM2): canal asincrono.
// Recibe del gestor (PULL)      ELIMINAR <idSol> <evento> <fecha> <cliente>
// le pide a la persistencia liberar los asientos y publica el resultado (PUB):
//   <cliente> CANCELACION <evento> <fecha>: <resultado>
// El cliente que pidio la cancelacion esta suscrito y recibe la notificacion. El primer
// texto del mensaje es el nombre del cliente: con eso cada cliente se queda solo con la suya.

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

    // PUB hacia los clientes: notificacion de cada cancelacion terminada
    zmq::socket_t socketNotificacion(context, ZMQ_PUB);
    socketNotificacion.bind("tcp://*:" + cfg["NOTIFICACION_PUERTO"]);

    std::cout << "Cancelaciones escuchando en puerto " << cfg["CANCELACION_PUERTO"]
              << ", notificando en " << cfg["NOTIFICACION_PUERTO"]
              << ", conectado a persistencia en " << cfg["PERSISTENCIA_IP"] << ":"
              << cfg["PERSISTENCIA_PUERTO"] << std::endl;

    avisarListo();   // a systemd: ya hice el bind, estoy listo

    while (true) {
        avisarVivo();   // watchdog: cada vuelta, al menos una por segundo, systemd sabe que sigue bien
        zmq::message_t request;
        if (!socketGestor.recv(request, zmq::recv_flags::none)) continue;   // no llego nada en 1 s
        std::string msg(static_cast<char*>(request.data()), request.size());   // ej: "ELIMINAR a3f9-7 E5 2026-11-02 juan"
        std::cout << "[Cancelacion] Recibido: " << msg << std::endl;

        auto p = partir(msg);   // ELIMINAR <idSol> <evento> <fecha> <cliente>
        if (p.size() != 5) {
            std::cout << "[Cancelacion] Formato invalido, se descarta" << std::endl;
            continue;
        }

        // ELIMINAR lleva el idSol: si se reenvia, la persistencia no la aplica dos veces
        auto resp = persistencia.pedir(msg);
        std::string respuesta = resp ? *resp : "la persistencia no responde, la cancelacion no se aplico";
        std::cout << "[Cancelacion] Resultado: " << respuesta << std::endl;

        std::string notificacion = p[4] + " CANCELACION " + p[2] + " " + p[3] + ": " + respuesta;
        socketNotificacion.send(zmq::buffer(notificacion), zmq::send_flags::none);
        std::cout << "[Cancelacion] Notificado: " << notificacion << std::endl;
    }
}
