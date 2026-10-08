#include <zmq.hpp>
#include <iostream>
#include <sstream>
#include <string>

int main() {
    zmq::context_t context(1);

    // Socket REP para clientes (escucha en 5555)
    zmq::socket_t socketClientes(context, ZMQ_REP);
    socketClientes.bind("tcp://*:5555");
    std::cout << "Gestor escuchando clientes en puerto 5555..." << std::endl;

    // Socket REQ para persistencia (se conecta al 6666)
    zmq::socket_t socketPersistencia(context, ZMQ_REQ);
    socketPersistencia.connect("tcp://10.43.49.231:6666"); // IP de la VM de persistencia
    std::cout << "Gestor conectado a persistencia en puerto 6666..." << std::endl;

    while (true) {
        // Recibir solicitud del cliente
        zmq::message_t request;
        auto result = socketClientes.recv(request, zmq::recv_flags::none);
        std::string msg(static_cast<char*>(request.data()), request.size());
        std::cout << "[Gestor] Solicitud recibida: " << msg << std::endl;

        // Reenviar la solicitud a la persistencia
        zmq::message_t reqPersistencia(msg.size());
        memcpy(reqPersistencia.data(), msg.data(), msg.size());
        socketPersistencia.send(reqPersistencia, zmq::send_flags::none);

        // Recibir respuesta de la persistencia
        zmq::message_t respPersistencia;
        auto resultpersistencia = socketPersistencia.recv(respPersistencia, zmq::recv_flags::none);
        std::string respuesta(static_cast<char*>(respPersistencia.data()), respPersistencia.size());

        // Enviar respuesta al cliente
        zmq::message_t reply(respuesta.size());
        memcpy(reply.data(), respuesta.data(), respuesta.size());
        socketClientes.send(reply, zmq::send_flags::none);
    }
}
