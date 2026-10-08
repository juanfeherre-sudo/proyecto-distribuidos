
#include <zmq.hpp>
#include <iostream>
#include <sstream>
#include "persistencia.h"
#include "cancelaciones.h"
int main() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_REP);
    socket.bind("tcp://*:5555");

    std::cout << "Gestor de Transacciones escuchando en puerto 5555..." << std::endl;

    while (true) {
        zmq::message_t request;
        zmq::recv_result_t result = socket.recv(request, zmq::recv_flags::none);
        std::string msg(static_cast<char*>(request.data()), request.size());
	std::cout<<"solicitud recivida: "<<msg<<std::endl;
        std::string respuesta;

        if (msg == "CONSULTA") {
            respuesta = consultarEventos();
        } else if (msg.rfind("RESERVA", 0) == 0) {
            std::istringstream iss(msg);
            std::string cmd, idEvento, cliente;
            int numAsientos;
            iss >> cmd >> idEvento >> cliente >> numAsientos;
            respuesta = reservarAsientos(idEvento, cliente, numAsientos);
        } else if(msg.rfind("ELIMINAR",0)==0){
	    std::istringstream iss(msg);
	    std::string cmd, idEvento, cliente;
	    iss >> cmd >> idEvento;
   	    std::getline(iss, cliente);

	    if (!cliente.empty() && cliente[0] == ' ') cliente.erase(0,1);

	    std::cout << "[DEBUG gestor] cmd=" << cmd 
	              << ", idEvento=" << idEvento 
	              << ", cliente=" << cliente << std::endl;

	    respuesta = eliminarReserva(idEvento, cliente);
	}else{
            respuesta = "Comando no reconocido";
        }

        zmq::message_t reply(respuesta.size());
        memcpy(reply.data(), respuesta.c_str(), respuesta.size());
        zmq::send_result_t sent = socket.send(reply, zmq::send_flags::none);
    }
}
