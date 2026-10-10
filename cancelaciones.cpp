// Servicio de cancelaciones (VM2)
// recibe las cancelaciones del gestor y le pide a persistencia liberar los asientos
// la notificacion al cliente queda para la segunda entrega

#include <zmq.hpp>
#include <iostream>
#include <string>

int main() {
    zmq::context_t context(1);

    // Socket PULL, recibe lo que manda el gestor
    zmq::socket_t socketGestor(context, ZMQ_PULL);
    socketGestor.bind("tcp://*:5557");

    // Socket REQ para la persistencia
    zmq::socket_t socketPersistencia(context, ZMQ_REQ);
    socketPersistencia.connect("tcp://10.43.99.231:6666"); // IP de la VM de persistencia

    std::cout << "Cancelaciones escuchando en puerto 5557..." << std::endl;

    while (true) {
        zmq::message_t request;
        auto res = socketGestor.recv(request, zmq::recv_flags::none);
        std::string msg(static_cast<char*>(request.data()), request.size());   // ej: "ELIMINAR E5 2026-11-02 juan"
        std::cout << "[Cancelacion] Recibido: " << msg << std::endl;

        socketPersistencia.send(zmq::buffer(msg), zmq::send_flags::none);
        zmq::message_t resp;
        auto res2 = socketPersistencia.recv(resp, zmq::recv_flags::none);
        std::string respuesta(static_cast<char*>(resp.data()), resp.size());

        std::cout << "[Cancelacion] Resultado: " << respuesta
                  << "  (notificacion al cliente: 2a entrega)" << std::endl;
    }
}
