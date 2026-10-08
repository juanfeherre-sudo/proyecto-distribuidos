// persistencia.h
#ifndef PERSISTENCIA_H
#define PERSISTENCIA_H

#include <string>

// Declaraciones de funciones
std::string consultarEventos();
std::string reservarAsientos(const std::string& idEvento, const std::string& cliente, int numAsientos);

#endif
