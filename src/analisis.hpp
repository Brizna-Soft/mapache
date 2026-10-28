#pragma once

// Análisis de una unidad o carpeta: recorre todo con varios hilos y arma un árbol con los tamaños.

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "util.hpp"

struct Carpeta;

struct Archivo
{
    Carpeta* padre = nullptr;
    const wchar_t* nombre = nullptr;   // nullptr: se envió a la Papelera
    uint64_t tamano = 0;
    int64_t modificado = 0;            // FILETIME
};

struct Carpeta
{
    Carpeta* padre = nullptr;
    const wchar_t* nombre = nullptr;   // en la raíz: la ruta completa
    uint64_t tamano = 0;               // con todo lo de adentro
    uint64_t archivos = 0;             // con todo lo de adentro
    uint64_t carpetas = 0;             // subcarpetas, con todo lo de adentro
    uint64_t sinPermiso = 0;           // carpetas sin permiso, contando esta
    int64_t modificado = 0;            // lo más reciente de adentro
    uint32_t profundidad = 0;
    bool sinAcceso = false;            // no se pudo leer
    bool quitada = false;              // se envió a la Papelera
    std::vector<Carpeta*> subcarpetas; // de mayor a menor
    std::vector<Archivo> archivosPropios; // de mayor a menor
};

// Una carpeta o un archivo del resultado
struct Elemento
{
    Carpeta* carpeta = nullptr;
    Archivo* archivo = nullptr;

    explicit operator bool() const { return carpeta != nullptr || (archivo != nullptr && archivo->nombre != nullptr); }
    bool EsCarpeta() const { return carpeta != nullptr; }
    uint64_t Tamano() const { return carpeta ? carpeta->tamano : (archivo ? archivo->tamano : 0); }
    uint64_t Archivos() const { return carpeta ? carpeta->archivos : 1; }
    Carpeta* Padre() const { return carpeta ? carpeta->padre : (archivo ? archivo->padre : nullptr); }
    const wchar_t* Nombre() const
    {
        const wchar_t* n = carpeta ? carpeta->nombre : (archivo ? archivo->nombre : nullptr);
        return n ? n : L"";
    }
    int64_t Modificado() const { return carpeta ? carpeta->modificado : (archivo ? archivo->modificado : 0); }
    bool operator==(const Elemento& o) const { return carpeta == o.carpeta && archivo == o.archivo; }
};

// Memoria de un hilo: nombres y carpetas que duran lo que dure el resultado
class Almacen
{
public:
    const wchar_t* Copiar(const wchar_t* texto, size_t largo);
    Carpeta* NuevaCarpeta();
    std::deque<Carpeta>& Carpetas() { return carpetas; }

private:
    std::vector<std::unique_ptr<wchar_t[]>> bloques;
    wchar_t* libre = nullptr;
    size_t quedan = 0;
    std::deque<Carpeta> carpetas;
};

struct Tipo
{
    uint64_t tamano = 0;
    uint64_t archivos = 0;
};

struct Extension
{
    std::wstring nombre;   // «.mp4», o vacío si no tiene
    util::Categoria categoria = util::Categoria::Otros;
    uint64_t tamano = 0;
    uint64_t archivos = 0;
};

class Resultado
{
public:
    Carpeta* raiz = nullptr;
    std::wstring ruta;
    bool esUnidad = false;
    uint64_t libreDisco = 0, totalDisco = 0;
    double segundos = 0;
    uint64_t enLaNube = 0;   // archivos de OneDrive y similares que no están en el disco (cuentan 0)

    static constexpr size_t MaxGrandes = 200;
    std::vector<Archivo*> masGrandes;
    std::array<Tipo, util::NumCategorias> tipos{};
    std::vector<Extension> extensiones;

    std::wstring Ruta(const Carpeta* c) const;
    std::wstring Ruta(const Archivo* a) const;
    std::wstring Ruta(const Elemento& e) const;
    // Ruta desde la carpeta analizada («Videos\Viajes»); la de la raíz es la ruta completa
    std::wstring RutaRelativa(const Carpeta* c) const;

    // Quita del árbol algo que ya se envió a la Papelera y rehace los totales y las listas
    void Quitar(const Elemento& e);
    void CalcularListas();

    std::vector<std::unique_ptr<Almacen>> almacenes;
};

class Analizador
{
public:
    struct Avance
    {
        uint64_t archivos = 0, carpetas = 0, bytes = 0, sinPermiso = 0;
        std::wstring actual;
    };
    using AlTerminar = std::function<void(std::shared_ptr<Resultado>)>;

    ~Analizador();

    // Empieza en otro hilo. «alTerminar» se llama desde ese hilo (con nullptr si se canceló).
    void Comenzar(const std::wstring& ruta, AlTerminar alTerminar);
    void Cancelar();
    void Esperar();
    bool Ocupado() const { return ocupado; }
    // Solo para probar la ventana de progreso (opción oculta --lento): pausa en cada carpeta
    void PausaPorCarpeta(int milisegundos) { pausa = milisegundos; }
    Avance LeerAvance();

private:
    struct Pendiente
    {
        Carpeta* carpeta = nullptr;
        std::wstring ruta;
    };

    void Coordinar(std::wstring ruta, AlTerminar alTerminar);
    void Trabajar(Almacen& almacen);
    void Leer(Almacen& almacen, const Pendiente& p, std::vector<Pendiente>& nuevas);

    std::thread hilo;
    int pausa = 0;
    std::atomic<bool> cancelar{ false };
    std::atomic<bool> ocupado{ false };

    std::atomic<uint64_t> cuentaArchivos{ 0 }, cuentaCarpetas{ 0 }, cuentaBytes{ 0 }, cuentaSinPermiso{ 0 }, cuentaNube{ 0 };
    std::atomic<uint64_t> ultimoCambio{ 0 };
    std::mutex mutexActual;
    std::wstring actual;

    std::mutex mutexCola;
    std::condition_variable condicion;
    std::vector<Pendiente> cola;
    int trabajando = 0;
};
