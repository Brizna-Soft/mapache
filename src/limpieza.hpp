#pragma once

// Limpieza segura: Papelera, temporales viejos, informes de errores y cachés de sombreadores.
// Solo se borran archivos que Windows y los programas vuelven a crear. Nada pide ser administrador.

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
    };

    // La carpeta de prueba solo vale si tiene dentro el archivo MAPACHE-PRUEBA.txt
    constexpr const wchar_t* MarcaDePrueba = L"MAPACHE-PRUEBA.txt";
    bool CarpetaDePruebaValida(const std::wstring& carpeta);

    // Arma los cuatro grupos. Con «carpetaPrueba», todo apunta a subcarpetas de ella (Papelera, Temp,
    // WER, D3DSCache, NVIDIA\DXCache, AMD\DxCache, AMD\DxcCache) y la Papelera de Windows no se toca.
    std::vector<Grupo> Preparar(const std::wstring& carpetaPrueba);

    void Calcular(Grupo& grupo, const std::atomic<bool>& cancelar);
    void Limpiar(Grupo& grupo, Informe& informe, const std::atomic<bool>& cancelar);
}
