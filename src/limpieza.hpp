#pragma once

// Limpieza segura: Papelera, temporales viejos, informes de errores y cachés de sombreadores.
// Solo se borran archivos que Windows y los programas vuelven a crear. Nada pide ser administrador.
//
// Cada carpeta y cada archivo se abre sin seguir uniones ni enlaces, y antes de borrar se comprueba
// con el archivo ya abierto que de verdad está dentro de la carpeta que se limpia. Lo que no cuadra
// se salta («omitido»).

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace limpieza
{
    enum class Tipo { Papelera, Temporales, Informes, Sombreadores };

    struct Lugar
    {
        std::wstring ruta;
        bool soloViejos = false;   // solo archivos con más de 24 horas
        std::wstring permitida;    // ruta final de la carpeta dentro de la cual tiene que estar
    };

    struct Grupo
    {
        Tipo tipo = Tipo::Papelera;
        std::vector<Lugar> lugares;
        bool papeleraReal = false;   // la Papelera de Windows (si no, una carpeta de prueba)
        bool disponible = true;
        std::wstring aviso;          // por qué no está disponible
        bool calculado = false;
        uint64_t bytes = 0;
        uint64_t archivos = 0;
    };

    struct Informe
    {
        uint64_t liberado = 0;
        uint64_t borrados = 0;
        uint64_t enUso = 0;
        uint64_t fallidos = 0;
        uint64_t omitidos = 0;   // no estaba donde debía (por ejemplo, la cambiaron por una unión)
    };

    // Aviso cuando la carpeta temporal no está dentro de %LOCALAPPDATA%\Temp en un disco local
    constexpr const wchar_t* AvisoTemporalRaro = L"Tu carpeta temporal está en un lugar poco común; Mapache no la toca.";

#ifdef MAPACHE_PRUEBAS
    // Solo en compilaciones de desarrollo: la carpeta de prueba tiene que tener este archivo
    constexpr const wchar_t* MarcaDePrueba = L"MAPACHE-PRUEBA.txt";
    bool CarpetaDePruebaValida(const std::wstring& carpeta);
    // Pausa al empezar cada lugar al limpiar, para poder cambiar carpetas en medio y probar que no se siguen
    void PausaDePrueba(int milisegundos);
#endif

    // Arma los cuatro grupos. Con «carpetaPrueba» (solo en compilaciones de desarrollo), todo apunta a
    // subcarpetas de ella (Papelera, Temp, WER, D3DSCache, NVIDIA\DXCache, AMD\DxCache, AMD\DxcCache)
    // y la Papelera de Windows no se toca.
    std::vector<Grupo> Preparar(const std::wstring& carpetaPrueba);

    void Calcular(Grupo& grupo, const std::atomic<bool>& cancelar);
    void Limpiar(Grupo& grupo, Informe& informe, const std::atomic<bool>& cancelar);
}
