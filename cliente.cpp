#include <zmq.hpp>
#include <iostream>
#include <string>

int main() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_REQ);
    socket.connect("tcp://10.43.100.44:5555");
    while (true){
    	//seleccion cliente
    	std::string accion;
    	std::cout<<"seleccione la accion a realizar :"<<std::endl;
    	std::cout<<"1. Consulta"<<std::endl;
    	std::cout<<"2. Reserva"<<std::endl;
    	std::cout<<"3. Eliminar"<<std::endl;
    	std::cin>>accion;
    	if (accion == "Consulta" || accion == "1"){
    		// Enviar consulta
    		std::string consulta = "CONSULTA";
    		zmq::message_t request(consulta.size());
    		memcpy(request.data(), consulta.c_str(), consulta.size());
    		zmq::send_result_t sent1 = socket.send(request, zmq::send_flags::none);

    		zmq::message_t reply;
    		zmq::recv_result_t result1 = socket.recv(reply, zmq::recv_flags::none);
    		std::string respuesta(static_cast<char*>(reply.data()), reply.size());
    		std::cout << "Respuesta: " << respuesta << std::endl;



   	}else if (accion == "Reserva" || accion == "2"){
		// Enviar reserva
    		std::string idEvento, cliente;
		int numAsientos;
		std::cout<<"Ingrese ID del evento "<<std::endl;
		std::cin>>idEvento;
		std::cout<<"Ingrese nombre del cliente "<<std::endl;
		std::cin>> cliente;
		std::cout<<"ingrese numero de asientos "<<std::endl;
		std::cin>>numAsientos;
		std::string reserva = "RESERVA " + idEvento + " " + cliente + " " + std::to_string(numAsientos);
    		zmq::message_t request2(reserva.size());
    		memcpy(request2.data(), reserva.c_str(), reserva.size());
    		zmq::send_result_t sent2 = socket.send(request2, zmq::send_flags::none);

    		zmq::message_t reply2;
    		zmq::recv_result_t result2 = socket.recv(reply2, zmq::recv_flags::none);
    		std::string respuesta2(static_cast<char*>(reply2.data()), reply2.size());
    		std::cout << "Respuesta: " << respuesta2 << std::endl;
	} else if (accion == "eliminar" || accion == "3") {
        	// Pedir datos al usuario
        	std::string idEvento, cliente;
        	std::cout << "Ingrese ID del evento a cancelar: ";
        	std::cin >> idEvento;
        	std::cout << "Ingrese nombre del cliente: ";
        	std::cin >> cliente;

        	// Construir comando
        	std::string eliminar = "ELIMINAR " + idEvento + " " + cliente;
        	zmq::message_t request3(eliminar.size());
        	memcpy(request3.data(), eliminar.c_str(), eliminar.size());
        	zmq::send_result_t sent3 = socket.send(request3, zmq::send_flags::none);
	
        	zmq::message_t reply3;
        	zmq::recv_result_t result3 = socket.recv(reply3, zmq::recv_flags::none);
        	std::string respuesta(static_cast<char*>(reply3.data()), reply3.size());
        	std::cout << "Respuesta: " << respuesta << std::endl;
	}else{
		std::cout<<"accion no reconocida "<<std::endl;
    	}
    }
    return 0;
}

