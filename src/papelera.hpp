#pragma once

// Enviar a la Papelera de forma segura (nunca se borra nada para siempre) y la lista de lo que no se toca.

#include <windows.h>
#include <guiddef.h>

#include <string>

namespace papelera
{
    // Ruta de una carpeta conocida de Windows (por ejemplo FOLDERID_Windows), o vacío
    std::wstring CarpetaConocida(REFGUID id);

    // Si la ruta es de Windows, de los programas o una carpeta que no conviene borrar entera,
    // devuelve el aviso para la persona. Si se puede enviar a la Papelera, devuelve vacío.
    std::wstring Motivo(const std::wstring& ruta);

    enum class Respuesta
    {
        Enviado,        // ya está en la Papelera
        NoSePudo,       // sigue en su lugar (en uso, sin permiso o no cabe en la Papelera)
    };

    // Envía a la Papelera con IFileOperation. Si Windows fuera a borrarlo para siempre (por ejemplo,
    // porque no cabe en la Papelera), se cancela y no se borra nada. Hay que llamarla desde la ventana.
    Respuesta Enviar(HWND ventana, const std::wstring& ruta, std::wstring& detalle);
}
