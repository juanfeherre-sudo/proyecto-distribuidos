// config.h
// Lee config.txt (lineas CLAVE=VALOR) para no dejar las IPs quemadas en el codigo.
// Asi el mismo codigo sirve en un solo computador (127.0.0.1) o en las 3 VMs:
// solo se cambia config.txt, no hay que recompilar.
#ifndef CONFIG_H
#define CONFIG_H

#include <fstream>
#include <iostream>
#include <map>
#include <string>

inline std::map<std::string, std::string> leerConfig(const std::string& ruta = "config.txt") {
    std::map<std::string, std::string> cfg;
    std::ifstream archivo(ruta);
    if (!archivo) {
        std::cerr << "No se encontro " << ruta << std::endl;
        exit(1);
    }
    std::string linea;
    while (std::getline(archivo, linea)) {
        if (linea.empty() || linea[0] == '#') continue;
        size_t pos = linea.find('=');
        if (pos == std::string::npos) continue;
        std::string valor = linea.substr(pos + 1);
        size_t fin = valor.find_first_of(" \t#\r");          // quitar espacios o comentarios al final
        if (fin != std::string::npos) valor = valor.substr(0, fin);
        cfg[linea.substr(0, pos)] = valor;
    }
    return cfg;
}

// "tcp://IP:PUERTO" a partir de dos claves del config, ej. direccion(cfg, "GESTOR")
inline std::string direccion(std::map<std::string, std::string>& cfg, const std::string& servicio) {
    return "tcp://" + cfg[servicio + "_IP"] + ":" + cfg[servicio + "_PUERTO"];
}

#endif
