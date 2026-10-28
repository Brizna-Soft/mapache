#pragma once

// Ajustes del programa en %LOCALAPPDATA%\Mapache\ajustes.json (nada en el registro de Windows).

#include <string>

struct Ajustes
{
    int x = 0, y = 0, ancho = 0, alto = 0;   // ancho 0: sin guardar
    bool maximizada = false;
    std::wstring ultimaCarpeta;

    static std::wstring Carpeta();
    void Cargar();
    void Guardar() const;
};
