#include <zmq.hpp>
#include <iostream>
#include <string>

int main() {
    zmq::context_t context(1);

    zmq::socket_t socketGestor(context, ZMQ_REP);
    socketGestor.bind("tcp://*:6666");

    zmq::socket_t socketPersistencia(context, ZMQ_REQ);
    socketPersistencia.connect("tcp://10.43.99.231:6666");

    while (true) {
        zmq::message_t request;
        auto res = socketGestor.recv(request, zmq::recv_flags::none);
        std::string msg(static_cast<char*>(request.data()), request.size());
        std::cout << "[RMC] Recibido: " << msg << std::endl;

        // Interpretar comando
        std::string comando;
        if (msg.find("CONSULTA") == 0) comando = "CONSULTA";
        else if (msg.find("RESERVA") == 0) comando = "RESERVA";
        else if (msg.find("ELIMINAR") == 0) comando = "ELIMINAR";
        else comando = "DESCONOCIDO";

        std::cout << "[RMC] Comando interpretado: " << comando << std::endl;

        // Reenviar a persistencia
        socketPersistencia.send(zmq::buffer(msg), zmq::send_flags::none);

        zmq::message_t respPersistencia;
        auto res2 = socketPersistencia.recv(respPersistencia, zmq::recv_flags::none);
        std::string respuesta(static_cast<char*>(respPersistencia.data()), respPersistencia.size());

        socketGestor.send(zmq::buffer(respuesta), zmq::send_flags::none);
    }
}

