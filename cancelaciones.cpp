// cancelaciones.cpp  -  corre en la VM2 (capa de logica de negocio)
//
// Servicio de Cancelaciones y Notificaciones (canal asincrono).
// El Gestor le empuja (PUSH) las cancelaciones y sigue atendiendo sin esperar.
// Este servicio ordena a la Persistencia liberar los asientos.
//
// En la 1a entrega no se pide la cancelacion; el proceso existe porque es parte de la
// arquitectura base (Figura 1). La notificacion al cliente (PUB/SUB) va en la 2a entrega.

#include <zmq.hpp>
#include <iostream>
#include <string>
#include "config.h"

int main() {
    auto cfg = leerConfig();
    zmq::context_t context(1);

    // PULL: recibe lo que el Gestor envia con PUSH
    zmq::socket_t socketGestor(context, ZMQ_PULL);
    socketGestor.bind("tcp://*:" + cfg["CANCELACION_PUERTO"]);

    // REQ hacia la Persistencia para liberar asientos
    zmq::socket_t socketPersistencia(context, ZMQ_REQ);
    socketPersistencia.connect(direccion(cfg, "PERSISTENCIA"));

    std::cout << "[Cancelacion] Esperando cancelaciones en puerto " << cfg["CANCELACION_PUERTO"] << "..." << std::endl;

    while (true) {
        zmq::message_t request;
        auto res = socketGestor.recv(request, zmq::recv_flags::none);
        std::string msg(static_cast<char*>(request.data()), request.size());   // "ELIMINAR E5 juan"
        std::cout << "[Cancelacion] Recibido: " << msg << std::endl;

        socketPersistencia.send(zmq::buffer(msg), zmq::send_flags::none);
        zmq::message_t resp;
        auto res2 = socketPersistencia.recv(resp, zmq::recv_flags::none);
        std::string respuesta(static_cast<char*>(resp.data()), resp.size());

        std::cout << "[Cancelacion] Resultado: " << respuesta
                  << "  (notificacion al cliente: 2a entrega)" << std::endl;
    }
}
