// Cliente (VM1)
// ./cliente                    -> menu
// ./cliente archivo.txt 20     -> manda las operaciones del archivo, 20 por minuto
// formato del archivo: CONSULTA 2026-11 / RESERVA E5 2026-11-02 juan 3 / ELIMINAR E5 2026-11-02 juan

#include <zmq.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>

// manda una solicitud al gestor, espera la respuesta y mide cuanto se demoro (ms)
std::string enviarSolicitud(zmq::socket_t& socket, const std::string& solicitud, double& ms) {
    auto inicio = std::chrono::steady_clock::now();

    zmq::message_t request(solicitud.size());
    memcpy(request.data(), solicitud.c_str(), solicitud.size());
    zmq::send_result_t sent = socket.send(request, zmq::send_flags::none);

    zmq::message_t reply;
    zmq::recv_result_t result = socket.recv(reply, zmq::recv_flags::none);
    std::string respuesta(static_cast<char*>(reply.data()), reply.size());

    auto fin = std::chrono::steady_clock::now();
    ms = std::chrono::duration<double, std::milli>(fin - inicio).count();
    return respuesta;
}

// lee las operaciones de un archivo y las manda una por una (porMinuto = solicitudes por minuto)
void modoArchivo(zmq::socket_t& socket, const std::string& ruta, int porMinuto) {
    std::ifstream archivo(ruta);
    if (!archivo) {
        std::cout << "No se encontro " << ruta << std::endl;
        return;
    }
    std::ofstream csv("metricas_" + ruta + ".csv");
    csv << "operacion,ms,resultado" << std::endl;
    int espera = 60000 / porMinuto;   // 20 por minuto = una cada 3000 ms

    std::string linea;
    while (std::getline(archivo, linea)) {
        if (linea.empty() || linea[0] == '#') continue;

        double ms;
        std::string respuesta = enviarSolicitud(socket, linea, ms);
        std::string operacion = linea.substr(0, linea.find(' '));
        std::string resultado = "RECHAZO";
        if (respuesta.substr(0, 5) == "EXITO") resultado = "EXITO";
        if (respuesta.substr(0, 8) == "RECIBIDO") resultado = "RECIBIDO";

        std::cout << linea << "  =>  " << respuesta.substr(0, respuesta.find('\n'))
                  << "  (" << (int)ms << " ms)" << std::endl;
        csv << operacion << "," << ms << "," << resultado << std::endl;

        std::this_thread::sleep_for(std::chrono::milliseconds(espera));
    }
    std::cout << "Terminado. Tiempos guardados en metricas_" << ruta << ".csv" << std::endl;
}

int main(int argc, char* argv[]) {
    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_REQ);
    socket.connect("tcp://10.43.100.44:5555");
    
    if (argc > 1) {
        int porMinuto = 20;
        if (argc > 2) porMinuto = std::stoi(argv[2]);
        modoArchivo(socket, argv[1], porMinuto);
        return 0;
    }

    while (true){
    	//seleccion cliente
    	std::string accion;
    	std::cout<<"seleccione la accion a realizar :"<<std::endl;
    	std::cout<<"1. Consulta"<<std::endl;
    	std::cout<<"2. Reserva"<<std::endl;
    	std::cout<<"3. Eliminar"<<std::endl;
    	std::cout<<"0. Salir"<<std::endl;
    	std::cin>>accion;
    	std::string solicitud;

    	if (accion == "Consulta" || accion == "1"){
    		std::string mes;
    		std::cout<<"Ingrese el mes (AAAA-MM, ej. 2026-11) "<<std::endl;
    		std::cin>>mes;
    		solicitud = "CONSULTA " + mes;
   	}else if (accion == "Reserva" || accion == "2"){
    		std::string idEvento, fecha, cliente;
		int numAsientos;
		std::cout<<"Ingrese ID del evento (ej. E5) "<<std::endl;
		std::cin>>idEvento;
		std::cout<<"Ingrese la fecha (AAAA-MM-DD) "<<std::endl;
		std::cin>>fecha;
		std::cout<<"Ingrese nombre del cliente "<<std::endl;
		std::cin>> cliente;
		std::cout<<"ingrese numero de asientos "<<std::endl;
		std::cin>>numAsientos;
		solicitud = "RESERVA " + idEvento + " " + fecha + " " + cliente + " " + std::to_string(numAsientos);
	} else if (accion == "eliminar" || accion == "3") {
        	std::string idEvento, fecha, cliente;
        	std::cout << "Ingrese ID del evento a cancelar: ";
        	std::cin >> idEvento;
        	std::cout << "Ingrese la fecha (AAAA-MM-DD): ";
        	std::cin >> fecha;
        	std::cout << "Ingrese nombre del cliente: ";
        	std::cin >> cliente;
        	solicitud = "ELIMINAR " + idEvento + " " + fecha + " " + cliente;
	} else if (accion == "0") {
		break;
	}else{
		std::cout<<"accion no reconocida "<<std::endl;
		continue;
    	}

    	double ms;
    	std::string respuesta = enviarSolicitud(socket, solicitud, ms);
    	std::cout << "Respuesta: " << respuesta << "\n(" << (int)ms << " ms)" << std::endl;
    }
    return 0;
}
