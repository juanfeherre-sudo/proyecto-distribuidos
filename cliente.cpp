// cliente.cpp  -  corre en la VM1 (capa de consumidores)
//
// Envia solicitudes al Gestor de Transacciones con REQ/REP: envia y espera la respuesta.
//
// Uso:
//   ./cliente                              menu interactivo
//   ./cliente <archivo> [solicitudesMin]   envia las operaciones del archivo, por defecto 20 por minuto,
//                                          y guarda el tiempo de cada una en metricas_<archivo>.csv
//
// Formato del archivo (una operacion por linea, igual que los mensajes del sistema):
//   CONSULTA 2026-11
//   RESERVA E5 2026-11-02 juan 3
//   ELIMINAR E5 juan
// Para varios clientes simultaneos se lanzan varios procesos, cada uno con su archivo.

#include <zmq.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include "config.h"

// Envia una solicitud, espera la respuesta y mide el tiempo en milisegundos.
std::string enviarSolicitud(zmq::socket_t& socket, const std::string& solicitud, double& ms) {
    auto inicio = std::chrono::steady_clock::now();

    zmq::message_t request(solicitud.size());
    memcpy(request.data(), solicitud.c_str(), solicitud.size());
    zmq::send_result_t sent = socket.send(request, zmq::send_flags::none);

    zmq::message_t reply;
    zmq::recv_result_t result = socket.recv(reply, zmq::recv_flags::none);
    std::string respuesta(static_cast<char*>(reply.data()), reply.size());

    ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - inicio).count();
    return respuesta;
}

// Modo archivo: genera la carga a la tasa indicada (solicitudes por minuto).
void modoArchivo(zmq::socket_t& socket, const std::string& ruta, int porMinuto) {
    std::ifstream archivo(ruta);
    if (!archivo) {
        std::cout << "No se encontro " << ruta << std::endl;
        return;
    }
    std::string nombre = ruta.substr(ruta.find_last_of('/') + 1);
    std::ofstream csv("metricas_" + nombre + ".csv");
    csv << "operacion,ms,resultado" << std::endl;
    auto pausa = std::chrono::milliseconds(60000 / porMinuto);   // 20 por minuto = una cada 3 s

    std::string linea;
    while (std::getline(archivo, linea)) {
        if (linea.empty() || linea[0] == '#') continue;
        auto siguiente = std::chrono::steady_clock::now() + pausa;

        double ms;
        std::string respuesta = enviarSolicitud(socket, linea, ms);
        std::string primeraLinea = respuesta.substr(0, respuesta.find('\n'));
        std::cout << linea << "  =>  " << primeraLinea << "  (" << (int)ms << " ms)" << std::endl;

        std::string operacion = linea.substr(0, linea.find(' '));
        std::string resultado = primeraLinea.substr(0, primeraLinea.find_first_of(":\n "));
        csv << operacion << "," << ms << "," << resultado << std::endl;

        std::this_thread::sleep_until(siguiente);
    }
    std::cout << "Terminado. Tiempos guardados en metricas_" << nombre << ".csv" << std::endl;
}

int main(int argc, char* argv[]) {
    auto cfg = leerConfig();
    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_REQ);
    socket.connect(direccion(cfg, "GESTOR"));
    std::cout << "Conectando al Gestor en " << direccion(cfg, "GESTOR") << std::endl;

    if (argc > 1) {
        modoArchivo(socket, argv[1], argc > 2 ? std::stoi(argv[2]) : 20);
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
        	std::string idEvento, cliente;
        	std::cout << "Ingrese ID del evento a cancelar: ";
        	std::cin >> idEvento;
        	std::cout << "Ingrese nombre del cliente: ";
        	std::cin >> cliente;
        	solicitud = "ELIMINAR " + idEvento + " " + cliente;
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
