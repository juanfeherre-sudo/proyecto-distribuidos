// common.hpp
// Código que comparten TODOS los programas: leer la configuración,
// enviar/recibir texto por ZeroMQ, partir mensajes y escribir logs.
#ifndef COMMON_HPP
#define COMMON_HPP

#include <zmq.hpp>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Configuración: lee config.txt, que tiene líneas CLAVE=VALOR.
// Así las IPs y puertos no quedan "quemados" en el código: para pasar de
// probar en un solo computador (127.0.0.1) a las 3 VMs solo se edita el archivo.
// ---------------------------------------------------------------------------
inline std::map<std::string, std::string> leerConfig(const std::string& ruta = "config.txt") {
    std::map<std::string, std::string> cfg;
    std::ifstream archivo(ruta);
    if (!archivo) {
        std::cerr << "No se pudo abrir " << ruta << std::endl;
        std::exit(1);
    }
    std::string linea;
    while (std::getline(archivo, linea)) {
        if (linea.empty() || linea[0] == '#') continue;      // saltar vacías y comentarios
        auto pos = linea.find('=');
        if (pos == std::string::npos) continue;
        cfg[linea.substr(0, pos)] = linea.substr(pos + 1);   // "GESTOR_PUERTO=5555" -> cfg["GESTOR_PUERTO"]="5555"
    }
    return cfg;
}

// Arma una dirección de ZeroMQ: direccion("10.0.0.5", "6666") -> "tcp://10.0.0.5:6666"
inline std::string direccion(const std::string& ip, const std::string& puerto) {
    return "tcp://" + ip + ":" + puerto;
}

// ---------------------------------------------------------------------------
// Enviar y recibir texto. ZeroMQ trabaja con bytes; estas funciones convierten
// entre std::string y zmq::message_t para no repetir ese código en cada programa.
// ---------------------------------------------------------------------------
inline void enviar(zmq::socket_t& socket, const std::string& texto) {
    socket.send(zmq::buffer(texto), zmq::send_flags::none);
}

inline std::string recibir(zmq::socket_t& socket) {
    zmq::message_t mensaje;
    auto ok = socket.recv(mensaje, zmq::recv_flags::none);   // se bloquea hasta que llegue algo
    if (!ok) return "";
    return mensaje.to_string();
}

// Parte un mensaje por espacios: "RESERVA E5 2026-11-10 ana 3" -> {"RESERVA","E5","2026-11-10","ana","3"}
inline std::vector<std::string> partir(const std::string& texto) {
    std::istringstream iss(texto);
    std::vector<std::string> partes;
    std::string p;
    while (iss >> p) partes.push_back(p);
    return partes;
}

// Hora actual "HH:MM:SS" para que cada línea del log diga cuándo pasó.
inline std::string hora() {
    std::time_t t = std::time(nullptr);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&t), "%H:%M:%S");
    return oss.str();
}

// Escribe "[12:01:33][RMC] mensaje" en la consola.
inline void log(const std::string& quien, const std::string& texto) {
    std::cout << "[" << hora() << "][" << quien << "] " << texto << std::endl;
}

#endif
