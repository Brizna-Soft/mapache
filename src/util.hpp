#pragma once

// Ayudas comunes: textos de tamaños, números y fechas, rutas y tipos de archivo.

#include <wx/colour.h>
#include <wx/string.h>

#include <cstdint>
#include <string>

class wxWindow;

namespace util
{
    // Lee de Windows los separadores de números de la persona (coma o punto). Llamar al empezar.
    void PrepararFormato();

    wxString Tamano(uint64_t bytes);        // «1,23 GB»
    wxString Numero(uint64_t n);            // «1.234.567» (según Windows)
    wxString Porcentaje(double fraccion);   // «45,3 %»
    wxString Fecha(int64_t filetime);       // fecha corta, o vacío
    wxString Duracion(double segundos);     // «14 s», «2 min 5 s»
    wxString Cantidad(uint64_t n, const wchar_t* uno, const wchar_t* varios);   // «1 archivo», «2 archivos»

    // Ruta para mostrar en un aviso: si es muy larga, se acorta por el medio («C:\Users\…\Fotos\viaje»)
    wxString RutaCorta(const std::wstring& ruta, size_t maximo = 64);

    // Aviso con un solo botón «Aceptar» (wxMessageBox pondría «OK», en inglés)
    void Avisar(wxWindow* padre, const wxString& texto, const wxString& titulo, long icono, const wxString& detalle = wxString());

    // Rutas. Las rutas «normales» no llevan barra al final, salvo la raíz («C:\»).
    std::wstring RutaLarga(const std::wstring& ruta);   // con el prefijo \\?\ (rutas de más de 260 letras)
    std::wstring Unir(const std::wstring& carpeta, const std::wstring& nombre);
    std::wstring Normalizar(const std::wstring& ruta);
    bool EsRaiz(const std::wstring& ruta);
    bool MismaRuta(const std::wstring& a, const std::wstring& b);
    bool EstaDentro(const std::wstring& ruta, const std::wstring& carpeta);   // dentro, no igual
    std::wstring Padre(const std::wstring& ruta);
    std::wstring NombreDe(const std::wstring& ruta);
    std::wstring RaizDe(const std::wstring& ruta);      // «C:\» o «\\servidor\recurso\»
    bool IgualSinMayusculas(const std::wstring& a, const std::wstring& b);
    bool ExisteCarpeta(const std::wstring& ruta);
    bool Existe(const std::wstring& ruta);
    int64_t Ahora();                                    // hora actual como FILETIME

    // Tipos de archivo
    enum class Categoria : uint8_t { Videos, Imagenes, Musica, Documentos, Programas, Comprimidos, Otros };
    constexpr int NumCategorias = 7;

    // Extensión en minúsculas con el punto («.mp4»), o vacía si no tiene (o es muy larga)
    void ExtensionDe(const wchar_t* nombre, std::wstring& salida);
    Categoria CategoriaDeExtension(const std::wstring& extension);
    Categoria CategoriaDe(const wchar_t* nombre);
    const wchar_t* NombreCategoria(Categoria c);
    wxColour ColorCategoria(Categoria c);

    // Colores de la marca
    wxColour Verde();
    wxColour ColorPista();   // fondo de las barras (claro u oscuro según el tema)
}
